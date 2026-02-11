// =============================================================================
// Workbook Data Model
// =============================================================================
//
// Core data structures representing a spreadsheet workbook: Workbook, Sheet,
// Cell, Axis (column/row), and supporting types like CellValue and Formula.
//
// Key responsibilities:
// - Define the in-memory representation of spreadsheet data
// - Provide entity access by UUID (primary) or position (derived)
// - Manage cell-axis relationships and formula ownership
// - Track collaboration mode and custom format definitions
//
// Architecture notes:
// - UUIDs are the source of truth; positions are derived
// - When CollabMode is COLLABORATING, all mutations MUST go through CRDT operations
// - Direct mutations are only valid for file loading or applying peer operations
// - Formula AST is owned by Cell; text is serialized on-demand
//
// Storage architecture (workbook-level entities):
// - Cells: Owned by Workbook::_cells; Sheets keep position index (_cellIndex)
// - Ranges: Owned by Workbook::_ranges; Sheets keep ID set and R-tree index
// - Columns/Rows: Owned by Workbook::_columns/_rows; Sheets keep ID sets and position indexes
// - Dependency Graph: Single global graph in Workbook::_depGraph
// - Shared Formulas: Workbook::_sharedFormulaMasters, _sharedFormulaFrom
// - Spill Regions: Workbook::_spillMasters, _spilledFrom
//
// Dependencies: types.h, operation.h, oplog.h
// Used by: crdt.cc, formula_eval.cc, bindings.cc, all persistence modules
//
// =============================================================================

#ifndef CELLS_MODEL_H_
#define CELLS_MODEL_H_

#include <cstdint>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/cells/axis_index.h"
#include "core/cells/format_buffer.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"
#include "core/cells/style_buffer.h"
#include "core/cells/style_types.h"
#include "core/cells/theme.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Axis;
struct Cell;
struct Sheet;
struct Workbook;
struct SharedFormulaInfo;
struct OpLog;
struct SpillInfo;
struct Range;
class RangeIndex;
class SpillIndex;
enum class RangeFlags : uint8_t;

// Collaboration mode for the workbook
// Determines how edits are tracked and synchronized
enum class CollabMode : std::uint8_t {
    OFFLINE,       // No collaboration - edits bypass OpLog, direct mutation
    COLLABORATING  // Active collaboration - edits tracked in OpLog, broadcast to peers
};

// =============================================================================
// IMPORTANT: CRDT Model Modification Contract
// =============================================================================
//
// When CollabMode is COLLABORATING, all model modifications MUST be performed
// through CRDT operations (see crdt.cc). Direct model mutations (e.g., calling
// addCell(), addColumn(), addRow() directly) will NOT sync to other peers.
//
// The only exceptions are:
// 1. File loading (initial state, no peers yet)
// 2. Applying operations from peers (the operation itself handles the mutation)
//
// To create/modify entities, use the unified operation helpers in crdt.h:
// - makeCellSetOp() / makeCellDeleteOp() - create/modify/delete cells
// - makeColSetOp() / makeColDeleteOp() - create/modify/delete columns
// - makeRowSetOp() / makeRowDeleteOp() - create/modify/delete rows
// - makeSheetSetOp() / makeSheetDeleteOp() - create/modify/delete sheets
// - makeRangeSetOp() / makeRangeDeleteOp() - create/modify/delete ranges
//
// Then apply with applyOperation() to both mutate the model AND add to OpLog.
// =============================================================================

// Cell Style Types are defined in style_types.h (included above)
// This provides TextAlign, VerticalAlign, BorderStyle, BorderEdge, CellBorder, CellStyle

// =============================================================================
// Cell Value Types
// =============================================================================

// Cell value - stores the raw value as a string (for simplicity)
// The type field indicates how to interpret it
struct CellValue {
    std::string raw;     // Raw string representation
    CellValueType type;  // How to interpret the value
    CellError error;     // Error state (NONE if no error)

    CellValue();
    explicit CellValue(double number);
    explicit CellValue(std::string str);
    explicit CellValue(const char* str);  // Needed to prevent const char* -> bool conversion
    explicit CellValue(bool boolean);
    explicit CellValue(CellError error);

    [[nodiscard]] double asNumber() const;
    [[nodiscard]] bool asBoolean() const;
    [[nodiscard]] const std::string& asString() const;
};

// Formula - contains parsed AST (source of truth)
// Text is generated on-demand from AST via FormulaSerializer
// Owned by Cell, cleaned up in Cell destructor
struct Formula {
    struct ASTNode* ast{nullptr};  // Parsed AST, null if not parsed, owned
    bool dirty{true};              // Needs recalculation?

    Formula();
    ~Formula();

    // Non-copyable (owns resources)
    Formula(const Formula&) = delete;
    Formula& operator=(const Formula&) = delete;

    // Movable
    Formula(Formula&& other) noexcept;
    Formula& operator=(Formula&& other) noexcept;

    // Check if the AST is valid (no ErrorNodes)
    // Returns false if AST is null or contains errors
    [[nodiscard]] bool isValid() const;

    // Check if the formula contains volatile functions (NOW, RAND, TODAY, etc.)
    // Returns false if AST is null
    [[nodiscard]] bool hasVolatile() const;
};

// Cell flags for runtime state tracking (not persisted)
// Combines shared formula, spill state, and format/style presence in a single byte
enum class CellFlags : uint8_t {
    NONE = 0,
    SHARED_FORMULA_MASTER = 1 << 0,      // bit 0: This cell is a shared formula master
    SHARED_FORMULA_SUBSCRIBER = 1 << 1,  // bit 1: This cell subscribes to another's formula
    SPILL_MASTER = 1 << 2,               // bit 2: This cell is a spill range master
    SPILLED_FROM = 1 << 3,               // bit 3: This position has spilled data
    HAS_FORMAT = 1 << 4,                 // bit 4: This cell has a custom format in workbook map
    HAS_STYLE = 1 << 5,                  // bit 5: This cell has a custom style in workbook map
    // bits 6-7: reserved for future use
};

// Bitwise operators for CellFlags
inline CellFlags operator|(CellFlags a, CellFlags b) {
    return static_cast<CellFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CellFlags operator&(CellFlags a, CellFlags b) {
    return static_cast<CellFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline CellFlags operator~(CellFlags a) {
    return static_cast<CellFlags>(~static_cast<uint8_t>(a));
}
inline CellFlags& operator|=(CellFlags& a, CellFlags b) {
    a = a | b;
    return a;
}
inline CellFlags& operator&=(CellFlags& a, CellFlags b) {
    a = a & b;
    return a;
}

// Cell - fundamental unit of data
// Either a direct value OR a formula with cached result
// Note: formatId is stored at the Workbook level (see Workbook::_formats) and
// styles use content-addressed StyleBuffer (see Workbook::_entityStyles).
struct Cell {
    ID id;             // Unique identifier (8-char base62)
    ID colId;          // Column axis ID
    ID rowId;          // Row axis ID
    CellValue value;   // Direct value OR cached formula result
    Formula* formula;  // null = value cell, non-null = formula cell (owned)

    Cell();
    explicit Cell(const ID& id);
    Cell(const ID& id, const ID& col, const ID& row);
    ~Cell();

    // Non-copyable (owns formula)
    Cell(const Cell&) = delete;
    Cell& operator=(const Cell&) = delete;

    // Movable
    Cell(Cell&& other) noexcept;
    Cell& operator=(Cell&& other) noexcept;

    // Returns true if cell has a formula (own or shared)
    [[nodiscard]] bool isFormula() const;

    // Returns true if cell uses another cell's formula (subscriber)
    // NOTE: Use Sheet::getSharedFormulaMaster() to get the actual master cell ID
    [[nodiscard]] bool isSharedFormula() const;

    // Returns true if cell is a shared formula master (has subscribers)
    [[nodiscard]] bool isSharedFormulaMaster() const;

    // Mark cell as a shared formula subscriber (sets flag only)
    // The actual master relationship is tracked at Sheet level
    void setSharedFormulaSubscriber(bool isSubscriber = true);

    [[nodiscard]] bool hasError() const;

    // Get this cell's own formula (does NOT follow shared formula reference)
    // For shared formula subscribers, this returns nullptr.
    // Use Sheet::getEffectiveFormula(cell) to get the effective formula for any cell.
    [[nodiscard]] Formula* getFormula() const;

    // Set cell to a formula (takes ownership)
    void setFormula(Formula* f);

    // Clear formula, making this a value cell
    void clearFormula();

    // ========================================================================
    // Flag helpers (for runtime state)
    // ========================================================================

    // Check if a flag is set
    [[nodiscard]] bool hasFlag(CellFlags flag) const;

    // Set a flag
    void setFlag(CellFlags flag);

    // Clear a flag
    void clearFlag(CellFlags flag);

    // Get all flags (for debugging/testing)
    [[nodiscard]] uint8_t getFlags() const { return _flags; }

    // ========================================================================
    // Format/Style flag helpers (for workbook-level storage optimization)
    // ========================================================================

    // Check if cell has a custom format in workbook map
    [[nodiscard]] bool hasFormat() const { return hasFlag(CellFlags::HAS_FORMAT); }

    // Mark cell as having a custom format
    void markHasFormat() { setFlag(CellFlags::HAS_FORMAT); }

    // Clear the has-format flag
    void clearHasFormat() { clearFlag(CellFlags::HAS_FORMAT); }

    // Check if cell has a custom style in workbook map
    [[nodiscard]] bool hasStyle() const { return hasFlag(CellFlags::HAS_STYLE); }

    // Mark cell as having a custom style
    void markHasStyle() { setFlag(CellFlags::HAS_STYLE); }

    // Clear the has-style flag
    void clearHasStyle() { clearFlag(CellFlags::HAS_STYLE); }

private:
    // Runtime flags (not persisted) - combines multiple bool fields
    uint8_t _flags = 0;
};

// =============================================================================
// Spill Range Management (Runtime-Only)
// =============================================================================
//
// Tracks dynamic array formula "spill" behavior where a single formula
// produces multiple values that populate neighboring cells automatically.
// This data is runtime-only (not persisted) - recomputed on recalculation.
//

// SpillInfo - tracks a spill range from a master cell
// The master cell contains the array formula; spilled positions get computed values
struct SpillInfo {
    ID masterCellId;  // Cell containing the array formula

    // Spilled positions as (colId, rowId) pairs
    // Does NOT include the master cell position
    // Order: row-major (left-to-right, top-to-bottom)
    std::vector<std::pair<ID, ID>> spilledPositions;

    // Cached spilled values (parallel to spilledPositions)
    // These are the computed values from the array result
    std::vector<CellValue> spilledValues;

    SpillInfo() = default;
    explicit SpillInfo(const ID& master) : masterCellId(master) {}

    // Clear all spilled data
    void clear() {
        spilledPositions.clear();
        spilledValues.clear();
    }

    // Get the number of spilled cells (excluding master)
    [[nodiscard]] size_t spillCount() const { return spilledPositions.size(); }
};

// SharedFormulaInfo - tracks a shared formula group at the Sheet level
// The master cell owns the formula; subscribers reference it
// This is the Sheet-level tracking structure (runtime-only, not persisted)
struct SharedFormulaInfo {
    ID masterCellId;              // Cell that owns the formula
    std::vector<ID> subscribers;  // Cell IDs using master's formula

    SharedFormulaInfo() = default;
    explicit SharedFormulaInfo(const ID& master) : masterCellId(master) {}

    // Get total cells in group (master + subscribers)
    [[nodiscard]] size_t size() const { return 1 + subscribers.size(); }
};

// Axis flags - combines type, visibility, style, and format presence in a single byte
enum class AxisFlags : uint8_t {
    NONE = 0,
    IS_COLUMN = 1 << 0,   // bit 0: true = column (x), false = row (y)
    HIDDEN = 1 << 1,      // bit 1: Whether axis is hidden
    HAS_STYLE = 1 << 2,   // bit 2: This axis has a style in workbook._styles map
    HAS_FORMAT = 1 << 3,  // bit 3: This axis has a format in workbook._formats map
    SIZE_SET = 1 << 4,    // bit 4: Size was explicitly set (vs using local default)
    // bits 5-7: reserved for future use
};

// Bitwise operators for AxisFlags
inline AxisFlags operator|(AxisFlags a, AxisFlags b) {
    return static_cast<AxisFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline AxisFlags operator&(AxisFlags a, AxisFlags b) {
    return static_cast<AxisFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline AxisFlags operator~(AxisFlags a) {
    return static_cast<AxisFlags>(~static_cast<uint8_t>(a));
}
inline AxisFlags& operator|=(AxisFlags& a, AxisFlags b) {
    a = a | b;
    return a;
}
inline AxisFlags& operator&=(AxisFlags& a, AxisFlags b) {
    a = a & b;
    return a;
}

// Axis - represents a column or row
struct Axis {
    std::string name;        // Custom name (empty = compute from position)
    ID id;                   // Unique identifier (8-char base62)
    ID sheetId;              // ID of the sheet this axis belongs to
    uint32_t position;       // Visual position (0-indexed)
    uint32_t size;           // Width (column) or height (row) in pixels
    double sizeOriginal{0};  // Original size in Excel units (char-widths for cols, points for rows)
    AxisFlags _flags;        // Combined flags: IS_COLUMN, HIDDEN, HAS_STYLE, HAS_FORMAT
    // NOTE: Axis styles/formats stored in Workbook::_styles/_formats maps via axis ID

    Axis();
    explicit Axis(const ID& id, bool isColumn = true);
    Axis(const ID& id, const ID& sheetId, bool isColumn);

    // Get the sheet ID this axis belongs to
    [[nodiscard]] const ID& getSheetId() const { return sheetId; }

    // Flag accessors
    [[nodiscard]] bool isColumn() const {
        return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(AxisFlags::IS_COLUMN)) != 0;
    }
    [[nodiscard]] bool hidden() const {
        return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(AxisFlags::HIDDEN)) != 0;
    }
    void setHidden(bool hide) {
        if (hide) {
            _flags = _flags | AxisFlags::HIDDEN;
        } else {
            _flags = _flags & ~AxisFlags::HIDDEN;
        }
    }
    [[nodiscard]] bool hasStyle() const {
        return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(AxisFlags::HAS_STYLE)) != 0;
    }
    void setHasStyle(bool has) {
        if (has) {
            _flags = _flags | AxisFlags::HAS_STYLE;
        } else {
            _flags = _flags & ~AxisFlags::HAS_STYLE;
        }
    }
    [[nodiscard]] bool hasFormat() const {
        return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(AxisFlags::HAS_FORMAT)) != 0;
    }
    void setHasFormat(bool has) {
        if (has) {
            _flags = _flags | AxisFlags::HAS_FORMAT;
        } else {
            _flags = _flags & ~AxisFlags::HAS_FORMAT;
        }
    }
    [[nodiscard]] bool sizeSet() const {
        return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(AxisFlags::SIZE_SET)) != 0;
    }
    void setSizeSet(bool set) {
        if (set) {
            _flags = _flags | AxisFlags::SIZE_SET;
        } else {
            _flags = _flags & ~AxisFlags::SIZE_SET;
        }
    }

    // Internal: set axis type (used by Sheet::addColumn/addRow for safety)
    void setIsColumn(bool isCol) {
        if (isCol) {
            _flags = _flags | AxisFlags::IS_COLUMN;
        } else {
            _flags = _flags & ~AxisFlags::IS_COLUMN;
        }
    }
};

// Forward declarations for formula management
class DependencyGraph;
class NamedRangeRegistry;
struct Workbook;

// Result of formula operations
struct FormulaResult {
    bool success{false};
    std::string errorMessage;
};

// Sheet - 2D grid containing cells
struct Sheet {
    ID id;             // Unique identifier
    std::string name;  // Sheet name

    // Sheet view properties
    bool showGridLines{true};  // Show grid lines (default: true)
    uint16_t zoomScale{100};   // Zoom level percentage (10-400, default: 100)
    uint16_t freezeCol{0};     // Number of frozen columns (0 = none, 1 = column A frozen, etc.)
    uint16_t freezeRow{0};     // Number of frozen rows (0 = none, 1 = row 1 frozen, etc.)

    Sheet();
    explicit Sheet(const ID& id, std::string name = "Sheet1");
    ~Sheet();

    // Cell operations
    Cell* getCell(const ID& cellId);
    Cell* getCellAt(const ID& colId, const ID& rowId);
    Cell* getCellAtPosition(uint32_t colPos,
                            uint32_t rowPos);  // Lookup only, returns nullptr if not exists
    Cell* getOrCreateCellAt(const ID& colId, const ID& rowId);  // Auto-creates if needed
    void addCell(std::unique_ptr<Cell> cell);
    void reserveCells(size_t count);             // Pre-allocate capacity for bulk imports
    void removeCellFromIndex(const ID& cellId);  // Remove cell from position index (for CRDT ops)

    // Axis operations
    Axis* getColumn(const ID& colId);
    [[nodiscard]] const Axis* getColumn(const ID& colId) const;
    Axis* getRow(const ID& rowId);
    [[nodiscard]] const Axis* getRow(const ID& rowId) const;
    Axis* getColumnByPosition(uint32_t position);
    Axis* getRowByPosition(uint32_t position);
    Axis* getColumnByName(const std::string& name);        // A, B, ..., Z, AA, AB, ...
    Axis* getOrCreateColumnByPosition(uint32_t position);  // Auto-creates if needed
    Axis* getOrCreateRowByPosition(uint32_t position);     // Auto-creates if needed
    void addColumn(std::unique_ptr<Axis> col);
    void addRow(std::unique_ptr<Axis> row);
    void removeColumnFromIndex(
        const ID& colId);                      // Remove column from sheet tracking (for CRDT ops)
    void removeRowFromIndex(const ID& rowId);  // Remove row from sheet tracking (for CRDT ops)

    // Update position index when a column/row position changes (for CRDT ops)
    // These update _columnIndex/_rowIndex without shifting other axes
    void updateColumnPositionIndex(const ID& colId, uint32_t oldPos, uint32_t newPos);
    void updateRowPositionIndex(const ID& rowId, uint32_t oldPos, uint32_t newPos);

    // Count accessors
    [[nodiscard]] size_t columnCount() const { return _columnIds.size(); }
    [[nodiscard]] size_t rowCount() const { return _rowIds.size(); }
    [[nodiscard]] size_t cellCount() const { return _cellIndex.size(); }

    // Get all column/row IDs belonging to this sheet (for iteration)
    [[nodiscard]] const std::unordered_set<ID, IDHash>& getColumnIds() const { return _columnIds; }
    [[nodiscard]] const std::unordered_set<ID, IDHash>& getRowIds() const { return _rowIds; }

    // Get all cell IDs in this sheet (for iteration)
    // Returns cell IDs from the position index
    [[nodiscard]] std::vector<ID> getCellIds() const;

    // Axis movement operations (for move stability testing)
    // Moves column to new position, shifting other columns as needed
    // Returns false if colId not found
    bool moveColumn(const ID& colId, uint32_t newPosition);

    // Moves row to new position, shifting other rows as needed
    // Returns false if rowId not found
    bool moveRow(const ID& rowId, uint32_t newPosition);

    // Insert a new column at the given position, shifting existing columns right
    // Returns the new column
    Axis* insertColumnAt(uint32_t position);

    // Insert a new row at the given position, shifting existing rows down
    // Returns the new row
    Axis* insertRowAt(uint32_t position);

    // Delete a column by ID, shifting other columns left
    // Returns false if colId not found
    bool deleteColumn(const ID& colId);

    // Delete a row by ID, shifting other rows up
    // Returns false if rowId not found
    bool deleteRow(const ID& rowId);

    // Column name utilities (A, B, ..., Z, AA, AB, ...)
    static std::string positionToColumnName(uint32_t position);  // 0 -> "A", 25 -> "Z", 26 -> "AA"
    static int32_t columnNameToPosition(
        const std::string& name);  // "A" -> 0, "Z" -> 25, "AA" -> 26, "" -> -1

    // ========================================================================
    // Parent workbook access
    // ========================================================================

    // Get the parent workbook (nullptr if sheet not yet added to a workbook)
    [[nodiscard]] Workbook* getWorkbook() { return _workbook; }
    [[nodiscard]] const Workbook* getWorkbook() const { return _workbook; }

    // Set the parent workbook (called by Workbook::addSheet)
    void setWorkbook(Workbook* wb) { _workbook = wb; }

    // ========================================================================
    // Formula management
    // ========================================================================

    // Set a formula on a cell with a pre-resolved AST
    // formulaText: the original formula text (stored for display)
    // ast: the parsed and resolved AST (ownership transferred to the cell)
    // Updates dependency graph based on the AST
    // Returns success/error status
    FormulaResult setCellFormula(const ID& cellId, const std::string& formulaText, ASTNode* ast);

    // Set a formula on a cell (parses but does NOT resolve references)
    // For use when references will be resolved later or resolution is not needed
    // Returns success/error status
    FormulaResult setCellFormulaUnresolved(const ID& cellId, const std::string& formulaText);

    // Get the raw formula text (UUID format for resolved formulas)
    // Returns empty string if cell has no formula
    // Note: For A1 display to users, use FormulaDisplayConverter from formula_display.h
    [[nodiscard]] std::string getCellFormulaText(const ID& cellId) const;

    // Clear a cell's formula (removes from dependency graph)
    void clearCellFormula(const ID& cellId);

    // Get the dependency graph (delegates to workbook's global graph)
    // Returns nullptr if sheet has no workbook set
    [[nodiscard]] DependencyGraph* getDependencyGraph();
    [[nodiscard]] const DependencyGraph* getDependencyGraph() const;

    // ========================================================================
    // Spill Range Management (Runtime-Only)
    // All these methods delegate to Workbook-level storage for convenience.
    // ========================================================================

    // Get spill info for a master cell (returns nullptr if not a spill master)
    // Delegates to Workbook::getSpillInfo()
    [[nodiscard]] SpillInfo* getSpillInfo(const ID& masterCellId);
    [[nodiscard]] const SpillInfo* getSpillInfo(const ID& masterCellId) const;

    // Get the master cell ID if this position is spilled into (returns null ID if not spilled)
    // Delegates to Workbook::getSpillMaster()
    [[nodiscard]] ID getSpillMaster(const ID& colId, const ID& rowId) const;

    // Check if a position is part of any spill range (spilled position only, not master)
    // Delegates to Workbook::isSpilledPosition()
    [[nodiscard]] bool isSpilledPosition(const ID& colId, const ID& rowId) const;

    // Get spilled value at a position (returns nullptr if not a spilled position)
    // Delegates to Workbook::getSpilledValue()
    [[nodiscard]] const CellValue* getSpilledValue(const ID& colId, const ID& rowId) const;

    // Register a new spill range (clears any existing spill for this master first)
    // Delegates to Workbook::registerSpillRange()
    void registerSpillRange(const ID& masterCellId, const std::vector<std::pair<ID, ID>>& positions,
                            const std::vector<CellValue>& values);

    // Clear the spill range for a master cell
    // Delegates to Workbook::clearSpillRange()
    void clearSpillRange(const ID& masterCellId);

    // Clear all spill data (called during full recalculation)
    // Delegates to Workbook::clearAllSpillRanges()
    void clearAllSpillRanges();

    // ========================================================================
    // Shared Formula Tracking (Runtime-Only)
    // All these methods delegate to Workbook-level storage for convenience.
    // ========================================================================

    // Get shared formula info for a master cell (returns nullptr if not a master)
    // Delegates to Workbook::getSharedFormulaInfo()
    [[nodiscard]] SharedFormulaInfo* getSharedFormulaInfo(const ID& masterCellId);
    [[nodiscard]] const SharedFormulaInfo* getSharedFormulaInfo(const ID& masterCellId) const;

    // Get the master cell ID if this cell is a shared formula subscriber (returns null ID if not)
    // Delegates to Workbook::getSharedFormulaMaster()
    [[nodiscard]] ID getSharedFormulaMaster(const ID& subscriberId) const;

    // Get the effective formula for a cell (follows shared formula reference if needed)
    // Delegates to Workbook::getEffectiveFormula()
    [[nodiscard]] Formula* getEffectiveFormula(Cell* cell);
    [[nodiscard]] const Formula* getEffectiveFormula(const Cell* cell) const;

    // Check if a cell is part of any shared formula group (master or subscriber)
    // Delegates to Workbook::isInSharedFormulaGroup()
    [[nodiscard]] bool isInSharedFormulaGroup(const ID& cellId) const;

    // Register a new shared formula group (masterId owns the formula, subscribers reference it)
    // Delegates to Workbook::registerSharedFormulaGroup()
    void registerSharedFormulaGroup(const ID& masterCellId, const std::vector<ID>& subscriberIds);

    // Add a subscriber to an existing shared formula group
    // Delegates to Workbook::addSharedFormulaSubscriber()
    void addSharedFormulaSubscriber(const ID& masterCellId, const ID& subscriberId);

    // Remove a subscriber from a shared formula group
    // Delegates to Workbook::removeSharedFormulaSubscriber()
    void removeSharedFormulaSubscriber(const ID& subscriberId);

    // Clear the shared formula group for a master (removes master and all subscribers)
    // Delegates to Workbook::clearSharedFormulaGroup()
    void clearSharedFormulaGroup(const ID& masterCellId);

    // Clear all shared formula tracking data
    // Delegates to Workbook::clearAllSharedFormulaGroups()
    void clearAllSharedFormulaGroups();

    // ========================================================================
    // Unified Range System
    // All range data is stored at Workbook level. Sheet maintains a set of
    // range IDs that belong to this sheet, plus a spatial index (R-tree).
    // ========================================================================

    // Get a range by ID (delegates to Workbook, returns nullptr if not found or not in this sheet)
    [[nodiscard]] Range* getRange(const ID& rangeId);
    [[nodiscard]] const Range* getRange(const ID& rangeId) const;

    // Add a new range (adds to Workbook storage and registers with this sheet)
    // Returns the range pointer, or nullptr if a range with this ID already exists
    Range* addRange(std::unique_ptr<Range> range);

    // Remove a range by ID (removes from Workbook storage and this sheet's set)
    // Returns true if the range was found and removed
    bool removeRange(const ID& rangeId);

    // Get all ranges containing a cell position
    // Requires column/row positions to be resolved first
    [[nodiscard]] std::vector<Range*> getRangesAt(uint32_t colPos, uint32_t rowPos) const;

    // Get all ranges containing a cell position with specific flag(s)
    [[nodiscard]] std::vector<Range*> getRangesAt(uint32_t colPos, uint32_t rowPos,
                                                  RangeFlags flagMask) const;

    // Get the range index for direct spatial queries
    [[nodiscard]] RangeIndex* getRangeIndex() { return _rangeIndex.get(); }
    [[nodiscard]] const RangeIndex* getRangeIndex() const { return _rangeIndex.get(); }

    // Get all range IDs belonging to this sheet (delegates to Workbook)
    // Returns a vector because it's computed by filtering all workbook ranges
    [[nodiscard]] std::vector<ID> getRangeIds() const;

    // Update the spatial index for a range (call after column/row positions change)
    // Resolves column/row UUIDs to positions and updates the R-tree entry
    void updateRangeIndex(Range* range);

    // Clear all ranges belonging to this sheet
    void clearAllRanges();

    // ========================================================================
    // Spill Range Spatial Index (for fast viewport queries)
    // ========================================================================

    // Get the spill index for direct spatial queries
    [[nodiscard]] SpillIndex* getSpillIndex() { return _spillIndex.get(); }
    [[nodiscard]] const SpillIndex* getSpillIndex() const { return _spillIndex.get(); }

    // ========================================================================
    // Axis Spatial Index (for fast pixel-to-axis conversion)
    // ========================================================================

    // Get the column axis index for viewport queries
    [[nodiscard]] AxisIndex& getColumnAxisIndex() { return _columnAxisIndex; }
    [[nodiscard]] const AxisIndex& getColumnAxisIndex() const { return _columnAxisIndex; }

    // Get the row axis index for viewport queries
    [[nodiscard]] AxisIndex& getRowAxisIndex() { return _rowAxisIndex; }
    [[nodiscard]] const AxisIndex& getRowAxisIndex() const { return _rowAxisIndex; }

    // Update the spill index with a new or changed spill extent
    // Called by Workbook when a spill range is registered
    void updateSpillIndex(const ID& masterCellId, uint32_t startCol, uint32_t startRow,
                          uint32_t endCol, uint32_t endRow);

    // Remove a spill extent from the index
    // Called by Workbook when a spill range is cleared
    void removeFromSpillIndex(const ID& masterCellId);

    // Clear all spill extents from the index
    void clearSpillIndex();

    // ========================================================================
    // Range style storage (content-addressed StyleBuffer, delegates to Workbook)
    // ========================================================================

    // Set the style on a range using content-addressed StyleBuffer
    void setRangeStyle(const ID& rangeId, const StyleBuffer& style);

    // Set the style directly on a range (move semantics)
    void setRangeStyle(const ID& rangeId, StyleBuffer&& style);

    // Clear the style from a range
    void clearRangeStyle(const ID& rangeId);

    // Get the style from a range (returns nullptr if no style)
    [[nodiscard]] const StyleBuffer* getRangeStyle(const ID& rangeId) const;

    // Get mutable style from a range (returns nullptr if no style)
    [[nodiscard]] StyleBuffer* getRangeStyle(const ID& rangeId);

    // ========================================================================
    // Range format storage (content-addressed FormatBuffer, delegates to Workbook)
    // ========================================================================

    // Set the format on a range using content-addressed FormatBuffer
    void setRangeFormat(const ID& rangeId, const FormatBuffer& format);

    // Set the format directly on a range (move semantics)
    void setRangeFormat(const ID& rangeId, FormatBuffer&& format);

    // Clear the format from a range
    void clearRangeFormat(const ID& rangeId);

    // Get the format from a range (returns nullptr if no format)
    [[nodiscard]] const FormatBuffer* getRangeFormat(const ID& rangeId) const;

    // Get mutable format from a range (returns nullptr if no format)
    [[nodiscard]] FormatBuffer* getRangeFormat(const ID& rangeId);

private:
    // Parent workbook (set by Workbook::addSheet)
    Workbook* _workbook{nullptr};

    // ========================================================================
    // Axis ID sets and position indexes
    // Axis objects are stored at Workbook level. Sheet tracks which axis IDs
    // belong to it and maintains position indexes for fast position lookups.
    // ========================================================================

    // Sets of column/row IDs belonging to this sheet
    // Axis objects themselves are stored in Workbook::_columns/_rows
    std::unordered_set<ID, IDHash> _columnIds;
    std::unordered_set<ID, IDHash> _rowIds;

    // Position-to-ID indexes for fast position-based lookups
    // Using std::map (sorted) to enable O(k) tree position computation
    // where k = number of axes with smaller positions (often 0 for appends)
    std::map<uint32_t, ID> _columnIndex;  // position -> colId (sorted by position)
    std::map<uint32_t, ID> _rowIndex;     // position -> rowId (sorted by position)

    // Secondary index: (colId, rowId) -> cellId
    std::unordered_map<std::string, ID> _cellIndex;

    // ========================================================================
    // Axis Spatial Index (OSTree for O(log n) pixel-to-axis conversion)
    // These indexes maintain axis positions and sizes for viewport queries.
    // They are maintained incrementally as axes are added/removed/resized.
    // ========================================================================

    // Column axis index for pixel-to-column conversion
    AxisIndex _columnAxisIndex;

    // Row axis index for pixel-to-row conversion
    AxisIndex _rowAxisIndex;

    // ========================================================================
    // Unified Range System
    // Range objects are stored at Workbook level. Sheet maintains only a
    // spatial index (R-tree) for fast viewport queries. Range ownership and
    // tracking is fully at Workbook level.
    // ========================================================================

    // Spatial index for range queries (stores Range pointers from Workbook)
    // Positions are sheet-local, so R-tree must remain per-sheet
    std::unique_ptr<RangeIndex> _rangeIndex;

    // ========================================================================
    // Spill Range Spatial Index
    // Spill data is stored at Workbook level. Sheet maintains a spatial index
    // (R-tree) for fast viewport queries to find spill extents.
    // ========================================================================

    // Spatial index for spill extent queries (stores master cell IDs)
    // Indexed by bounding box of master + all spilled positions
    std::unique_ptr<SpillIndex> _spillIndex;

    // Build composite key for cell index
    static std::string makeCellKey(const ID& colId, const ID& rowId);
};

// Workbook - top-level container
struct Workbook {
    ID id;             // Document ID
    std::string name;  // Document name

    // Sheets (order matters for tab display)
    std::vector<std::unique_ptr<Sheet>> sheets;

    Workbook();
    explicit Workbook(const ID& id, std::string name = "Untitled");
    ~Workbook();

    // Sheet operations
    Sheet* getSheet(const ID& sheetId);
    [[nodiscard]] const Sheet* getSheet(const ID& sheetId) const;
    Sheet* getSheetByIndex(size_t index);
    void addSheet(std::unique_ptr<Sheet> sheet);
    bool removeSheet(const ID& sheetId);  // Returns true if sheet was removed

    [[nodiscard]] size_t sheetCount() const { return sheets.size(); }

    // Operation log for CRDT collaboration
    OpLog* getOpLog();
    [[nodiscard]] const OpLog* getOpLog() const;

    // Node ID for HLC generation (local peer identity)
    void setNodeId(const ID& nodeId);
    [[nodiscard]] const ID& getNodeId() const;

    // Current HLC for generating new operations
    [[nodiscard]] HLC getCurrentHLC() const;

    // ========================================================================
    // Collaboration mode
    // ========================================================================

    // Get current collaboration mode
    [[nodiscard]] CollabMode getCollabMode() const;

    // Set collaboration mode
    // Switching to COLLABORATING may trigger OpLog bootstrap (see startCollaboration)
    void setCollabMode(CollabMode mode);

    // Check if in collaboration mode
    [[nodiscard]] bool isCollaborating() const;

    // Start collaboration - switches mode and bootstraps OpLog if needed
    // Call this when user clicks "Share" or joins a room
    void startCollaboration();

    // ========================================================================
    // Theme
    // ========================================================================

    // Get the workbook theme (returns nullptr if no theme is set)
    [[nodiscard]] const Theme* getTheme() const { return _theme.get(); }
    [[nodiscard]] Theme* getTheme() { return _theme.get(); }

    // Set the workbook theme (takes ownership)
    void setTheme(std::unique_ptr<Theme> theme) { _theme = std::move(theme); }

    // Check if workbook has a theme
    [[nodiscard]] bool hasTheme() const { return _theme != nullptr; }

    // ========================================================================
    // Named ranges
    // ========================================================================

    // Get the named range registry for this workbook
    [[nodiscard]] NamedRangeRegistry* getNamedRanges() { return _namedRanges.get(); }
    [[nodiscard]] const NamedRangeRegistry* getNamedRanges() const { return _namedRanges.get(); }

    // Get sheet by name (for cross-sheet references)
    [[nodiscard]] Sheet* getSheetByName(const std::string& name);
    [[nodiscard]] const Sheet* getSheetByName(const std::string& name) const;

    // Get sheet by ID (for cross-sheet references with UUID storage)
    [[nodiscard]] Sheet* getSheetById(const ID& sheetId);
    [[nodiscard]] const Sheet* getSheetById(const ID& sheetId) const;

    // ========================================================================
    // Workbook-level dependency graph
    // ========================================================================

    // Get the global dependency graph (shared by all sheets)
    // All formula dependencies are tracked in this single graph
    [[nodiscard]] DependencyGraph* getDependencyGraph();
    [[nodiscard]] const DependencyGraph* getDependencyGraph() const;

    // ========================================================================
    // Workbook-level cell storage
    // ========================================================================

    // Get a cell by ID from workbook-level storage (O(1) lookup)
    // Returns nullptr if cell not found
    [[nodiscard]] Cell* getCell(const ID& cellId);
    [[nodiscard]] const Cell* getCell(const ID& cellId) const;

    // Add a cell to workbook-level storage (takes ownership)
    // The cell must have a valid ID set
    // Returns the cell pointer, or nullptr if cell is null or ID already exists
    Cell* addCell(std::unique_ptr<Cell> cell);

    // Remove a cell from workbook-level storage
    // Returns the removed cell (ownership transferred to caller), or nullptr if not found
    std::unique_ptr<Cell> removeCell(const ID& cellId);

    // ========================================================================
    // Cross-sheet cell lookup
    // ========================================================================

    // Find a cell by ID across all sheets. Returns {cell, sheet} or {nullptr, nullptr} if not
    // found. Used for evaluating formulas that reference cells by UUID without explicit sheet
    // prefix.
    struct CellLookupResult {
        Cell* cell;
        Sheet* sheet;
    };
    [[nodiscard]] CellLookupResult findCell(const ID& cellId);
    [[nodiscard]] std::pair<const Cell*, const Sheet*> findCell(const ID& cellId) const;

    // Find a column/row axis by ID. Returns the sheet it belongs to.
    // Uses the axis's sheetId field for O(1) lookup.
    // Returns nullptr if not found.
    [[nodiscard]] Sheet* findAxisSheet(const ID& axisId);
    [[nodiscard]] const Sheet* findAxisSheet(const ID& axisId) const;

    // ========================================================================
    // Workbook-level axis storage
    // ========================================================================

    // Get a column by ID from workbook-level storage (O(1) lookup)
    // Returns nullptr if column not found
    [[nodiscard]] Axis* getColumn(const ID& colId);
    [[nodiscard]] const Axis* getColumn(const ID& colId) const;

    // Get a row by ID from workbook-level storage (O(1) lookup)
    // Returns nullptr if row not found
    [[nodiscard]] Axis* getRow(const ID& rowId);
    [[nodiscard]] const Axis* getRow(const ID& rowId) const;

    // Add a column to workbook-level storage (takes ownership)
    // The column must have a valid ID set
    // Returns the column pointer, or nullptr if column is null or ID already exists
    Axis* addColumn(std::unique_ptr<Axis> col);

    // Add a row to workbook-level storage (takes ownership)
    // The row must have a valid ID set
    // Returns the row pointer, or nullptr if row is null or ID already exists
    Axis* addRow(std::unique_ptr<Axis> row);

    // Remove a column from workbook-level storage
    // Returns the removed column (ownership transferred to caller), or nullptr if not found
    std::unique_ptr<Axis> removeColumn(const ID& colId);

    // Remove a row from workbook-level storage
    // Returns the removed row (ownership transferred to caller), or nullptr if not found
    std::unique_ptr<Axis> removeRow(const ID& rowId);

    // ========================================================================
    // Entity style storage (content-addressed StyleBuffer)
    // ========================================================================

    // Get the style for an entity (cell, axis, etc.)
    // Returns nullptr if no style is set
    [[nodiscard]] const StyleBuffer* getEntityStyle(const ID& entityId) const;

    // Set a content-addressed style for an entity
    // Pass empty StyleBuffer or use clearEntityStyle() to clear
    void setEntityStyle(const ID& entityId, const StyleBuffer& style);

    // Clear the content-addressed style for an entity
    // Returns true if the entity had a style
    bool clearEntityStyle(const ID& entityId);

    // Check if entity has a content-addressed style
    [[nodiscard]] bool hasEntityStyle(const ID& entityId) const;

    // Get all entity styles (for serialization)
    [[nodiscard]] const std::unordered_map<ID, StyleBuffer, IDHash>& getEntityStyles() const {
        return _entityStyles;
    }

    // ========================================================================
    // Entity format storage (content-addressed FormatBuffer)
    // ========================================================================

    // Get the format for an entity (cell, axis, range, etc.)
    // Returns nullptr if no format is set
    [[nodiscard]] const FormatBuffer* getEntityFormat(const ID& entityId) const;

    // Get mutable format for an entity (returns nullptr if no format)
    [[nodiscard]] FormatBuffer* getEntityFormat(const ID& entityId);

    // Set a content-addressed format for an entity
    // Pass empty FormatBuffer or use clearEntityFormat() to clear
    void setEntityFormat(const ID& entityId, const FormatBuffer& format);

    // Set a content-addressed format with move semantics
    void setEntityFormat(const ID& entityId, FormatBuffer&& format);

    // Clear the content-addressed format for an entity
    // Returns true if the entity had a format
    bool clearEntityFormat(const ID& entityId);

    // Check if entity has a content-addressed format
    [[nodiscard]] bool hasEntityFormat(const ID& entityId) const;

    // Get all entity formats (for serialization)
    [[nodiscard]] const std::unordered_map<ID, FormatBuffer, IDHash>& getEntityFormats() const {
        return _entityFormats;
    }

    // ========================================================================
    // Workbook-level shared formula tracking (runtime-only)
    // ========================================================================

    // Get shared formula info for a master cell (returns nullptr if not a master)
    [[nodiscard]] SharedFormulaInfo* getSharedFormulaInfo(const ID& masterCellId);
    [[nodiscard]] const SharedFormulaInfo* getSharedFormulaInfo(const ID& masterCellId) const;

    // Get the master cell ID if this cell is a shared formula subscriber (returns null ID if not)
    [[nodiscard]] ID getSharedFormulaMaster(const ID& subscriberId) const;

    // Get the effective formula for a cell (follows shared formula reference if needed)
    // For regular cells, returns cell->formula
    // For shared formula subscribers, returns the master cell's formula
    // Returns nullptr if cell has no formula
    [[nodiscard]] Formula* getEffectiveFormula(Cell* cell);
    [[nodiscard]] const Formula* getEffectiveFormula(const Cell* cell) const;

    // Check if a cell is part of any shared formula group (master or subscriber)
    [[nodiscard]] bool isInSharedFormulaGroup(const ID& cellId) const;

    // Register a new shared formula group (masterId owns the formula, subscribers reference it)
    void registerSharedFormulaGroup(const ID& masterCellId, const std::vector<ID>& subscriberIds);

    // Add a subscriber to an existing shared formula group
    void addSharedFormulaSubscriber(const ID& masterCellId, const ID& subscriberId);

    // Remove a subscriber from a shared formula group
    // If no subscribers remain, removes the group entirely
    void removeSharedFormulaSubscriber(const ID& subscriberId);

    // Clear the shared formula group for a master (removes master and all subscribers)
    void clearSharedFormulaGroup(const ID& masterCellId);

    // Clear all shared formula tracking data
    void clearAllSharedFormulaGroups();

    // ========================================================================
    // Workbook-level spill range tracking (runtime-only)
    // ========================================================================

    // Get spill info for a master cell (returns nullptr if not a spill master)
    [[nodiscard]] SpillInfo* getSpillInfo(const ID& masterCellId);
    [[nodiscard]] const SpillInfo* getSpillInfo(const ID& masterCellId) const;

    // Get the master cell ID if this position is spilled into (returns null ID if not spilled)
    // colId and rowId identify the position
    [[nodiscard]] ID getSpillMaster(const ID& colId, const ID& rowId) const;

    // Check if a position is part of any spill range (spilled position only, not master)
    [[nodiscard]] bool isSpilledPosition(const ID& colId, const ID& rowId) const;

    // Get spilled value at a position (returns nullptr if not a spilled position)
    [[nodiscard]] const CellValue* getSpilledValue(const ID& colId, const ID& rowId) const;

    // Register a new spill range (clears any existing spill for this master first)
    void registerSpillRange(const ID& masterCellId, const std::vector<std::pair<ID, ID>>& positions,
                            const std::vector<CellValue>& values);

    // Clear the spill range for a master cell
    void clearSpillRange(const ID& masterCellId);

    // Clear all spill data (called during full recalculation)
    void clearAllSpillRanges();

    // ========================================================================
    // Workbook-level range storage
    // ========================================================================

    // Get a range by ID from workbook-level storage (O(1) lookup)
    // Returns nullptr if range not found
    [[nodiscard]] Range* getRange(const ID& rangeId);
    [[nodiscard]] const Range* getRange(const ID& rangeId) const;

    // Add a range to workbook-level storage (takes ownership)
    // The range must have a valid ID set
    // Returns the range pointer, or nullptr if range is null or ID already exists
    Range* addRange(std::unique_ptr<Range> range);

    // Remove a range from workbook-level storage
    // Returns the removed range (ownership transferred to caller), or nullptr if not found
    std::unique_ptr<Range> removeRange(const ID& rangeId);

    // Get all range IDs in the workbook (global set)
    [[nodiscard]] const std::unordered_set<ID, IDHash>& getRangeIds() const { return _rangeIds; }

    // Get all range IDs belonging to a specific sheet
    // Iterates ranges and checks axis sheetId - O(n) where n is total ranges
    [[nodiscard]] std::vector<ID> getRangeIdsForSheet(const ID& sheetId) const;

    // ========================================================================
    // Range style storage (content-addressed StyleBuffer)
    // ========================================================================

    // Set the style on a range using content-addressed StyleBuffer
    void setRangeStyle(const ID& rangeId, const StyleBuffer& style);

    // Set the style directly on a range (move semantics)
    void setRangeStyle(const ID& rangeId, StyleBuffer&& style);

    // Clear the style from a range
    void clearRangeStyle(const ID& rangeId);

    // Get the style from a range (returns nullptr if no style)
    [[nodiscard]] const StyleBuffer* getRangeStyle(const ID& rangeId) const;

    // Get mutable style from a range (returns nullptr if no style)
    [[nodiscard]] StyleBuffer* getRangeStyle(const ID& rangeId);

    // ========================================================================
    // Range format storage (content-addressed FormatBuffer)
    // ========================================================================

    // Set the format on a range using content-addressed FormatBuffer
    void setRangeFormat(const ID& rangeId, const FormatBuffer& format);

    // Set the format directly on a range (move semantics)
    void setRangeFormat(const ID& rangeId, FormatBuffer&& format);

    // Clear the format from a range
    void clearRangeFormat(const ID& rangeId);

    // Get the format from a range (returns nullptr if no format)
    [[nodiscard]] const FormatBuffer* getRangeFormat(const ID& rangeId) const;

    // Get mutable format from a range (returns nullptr if no format)
    [[nodiscard]] FormatBuffer* getRangeFormat(const ID& rangeId);

private:
    // Sheet lookup by ID
    std::unordered_map<ID, Sheet*, IDHash> _sheetIndex;

    // Workbook theme (color scheme + font scheme from Excel)
    std::unique_ptr<Theme> _theme;

    // Operation log for CRDT synchronization
    std::unique_ptr<OpLog> _oplog;

    // Named range registry (workbook-owned for both workbook and sheet scopes)
    std::unique_ptr<NamedRangeRegistry> _namedRanges;

    // Local node ID for HLC generation
    ID _nodeId;

    // Last HLC used for generating operations
    mutable HLC _lastHLC;

    // Collaboration mode (default: OFFLINE)
    CollabMode _collabMode{CollabMode::OFFLINE};

    // ========================================================================
    // Entity format/style storage (content-addressed)
    // ========================================================================

    // Entity ID -> content-addressed StyleBuffer mapping (cells, axes, etc.)
    // Content-addressed: the style data IS its identity
    std::unordered_map<ID, StyleBuffer, IDHash> _entityStyles;

    // Entity ID -> content-addressed FormatBuffer mapping (cells, axes, ranges, etc.)
    // Content-addressed: the format data IS its identity (no separate format ID needed)
    std::unordered_map<ID, FormatBuffer, IDHash> _entityFormats;

    // ========================================================================
    // Workbook-level dependency graph (global, shared by all sheets)
    // ========================================================================

    // Global dependency graph for tracking formula dependencies across all sheets
    // This replaces per-sheet _depGraph - all deps in one unified graph
    std::unique_ptr<DependencyGraph> _depGraph;

    // ========================================================================
    // Workbook-level cell storage (primary storage, sheets keep secondary index)
    // ========================================================================

    // Primary cell storage: cell ID -> Cell
    // Cells are owned by Workbook; Sheets maintain lightweight ID sets and position indexes
    std::unordered_map<ID, std::unique_ptr<Cell>, IDHash> _cells;

    // ========================================================================
    // Workbook-level axis storage (primary storage, sheets keep position indexes)
    // ========================================================================

    // Primary column storage: column ID -> Axis
    // Columns are owned by Workbook; Sheets maintain ID sets and position indexes
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> _columns;

    // Primary row storage: row ID -> Axis
    // Rows are owned by Workbook; Sheets maintain ID sets and position indexes
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> _rows;

    // ========================================================================
    // Workbook-level shared formula tracking (runtime-only)
    // ========================================================================

    // Maps master cell ID → shared formula info (subscriber list)
    std::unordered_map<ID, SharedFormulaInfo, IDHash> _sharedFormulaMasters;

    // Reverse lookup: subscriber cell ID → master cell ID
    std::unordered_map<ID, ID, IDHash> _sharedFormulaFrom;

    // ========================================================================
    // Workbook-level spill range tracking (runtime-only)
    // ========================================================================

    // Maps master cell ID → spill info (positions and values)
    std::unordered_map<ID, SpillInfo, IDHash> _spillMasters;

    // Reverse lookup: (colId, rowId) composite key → master cell ID
    // Only contains spilled positions, not the master position itself
    std::unordered_map<std::string, ID> _spilledFrom;

    // Helper for building composite position keys
    static std::string makePositionKey(const ID& colId, const ID& rowId);

    // Set up callback to mark dependent formulas dirty when named ranges are removed
    void setupNamedRangeRemovalCallback();

    // ========================================================================
    // Workbook-level range storage (primary storage)
    // ========================================================================

    // Primary range storage: range ID -> Range
    // Ranges are owned by Workbook; Sheets maintain spatial indexes for viewport queries
    std::unordered_map<ID, std::unique_ptr<Range>, IDHash> _ranges;

    // Global set of all range IDs for fast iteration
    std::unordered_set<ID, IDHash> _rangeIds;
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
