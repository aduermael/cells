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
// - UUIDs are the source of truth; positions are derived via Order Statistic Tree
// - When CollabMode is COLLABORATING, all mutations MUST go through CRDT operations
// - Direct mutations are only valid for file loading or applying peer operations
// - Formula AST is owned by Cell; text is serialized on-demand
//
// Dependencies: types.h, operation.h, oplog.h
// Used by: crdt.cc, formula_eval.cc, bindings.cc, all persistence modules
//
// =============================================================================

#ifndef CELLS_MODEL_H_
#define CELLS_MODEL_H_

#include <cstdint>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/operation.h"
#include "core/cells/oplog.h"
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
// To create/modify entities, use the operation helpers in crdt.h:
// - makeCellSetValueOp() - create or modify a cell
// - makeDimInsertAxisOp() - create a column or row
// - makeDimDeleteAxisOp() - delete a column or row
// - makeDimSetAxisSizeOp() - resize a column or row
// - makeDimMoveAxisOp() - move a column or row
//
// Then apply with applyOperation() to both mutate the model AND add to OpLog.
// =============================================================================

// =============================================================================
// Cell Style Types
// =============================================================================

// Horizontal text alignment within cell
enum class TextAlign : std::uint8_t { LEFT = 0, CENTER = 1, RIGHT = 2, JUSTIFY = 3 };

// Vertical text alignment within cell
enum class VerticalAlign : std::uint8_t { TOP = 0, MIDDLE = 1, BOTTOM = 2 };

// Cell style properties for formatting
// Each property is optional - empty string or 0 means "use default"
// Colors use CSS hex format: "#RRGGBB" or "" for transparent/default
struct CellStyle {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string bgColor;     // Background color (hex, e.g. "#FF0000")
    std::string textColor;   // Text color (hex, e.g. "#000000")
    std::string fontFamily;  // Font name (e.g. "Arial"), empty = system default
    uint8_t fontSize{0};     // Font size in points, 0 = default (11pt)
    TextAlign hAlign{TextAlign::LEFT};
    VerticalAlign vAlign{VerticalAlign::BOTTOM};

    CellStyle() = default;

    // Check if style has any non-default values
    [[nodiscard]] bool isEmpty() const {
        return !bold && !italic && !underline && bgColor.empty() && textColor.empty() &&
               fontFamily.empty() && fontSize == 0 && hAlign == TextAlign::LEFT &&
               vAlign == VerticalAlign::BOTTOM;
    }

    // Equality comparison
    bool operator==(const CellStyle& other) const {
        return bold == other.bold && italic == other.italic && underline == other.underline &&
               bgColor == other.bgColor && textColor == other.textColor &&
               fontFamily == other.fontFamily && fontSize == other.fontSize &&
               hAlign == other.hAlign && vAlign == other.vAlign;
    }

    bool operator!=(const CellStyle& other) const { return !(*this == other); }
};

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
// Combines shared formula and spill state in a single byte
enum class CellFlags : uint8_t {
    NONE = 0,
    SHARED_FORMULA_MASTER = 1 << 0,      // bit 0: This cell is a shared formula master
    SHARED_FORMULA_SUBSCRIBER = 1 << 1,  // bit 1: This cell subscribes to another's formula
    SPILL_MASTER = 1 << 2,               // bit 2: This cell is a spill range master
    SPILLED_FROM = 1 << 3,               // bit 3: This position has spilled data
    // bits 4-7: reserved for future use
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
struct Cell {
    ID id;             // Unique identifier (8-char base62)
    ID colId;          // Column axis ID
    ID rowId;          // Row axis ID
    CellValue value;   // Direct value OR cached formula result
    Formula* formula;  // null = value cell, non-null = formula cell (owned)
    ID formatId;       // Number format ID (null = use default/General format)
    ID styleId;        // Cell style ID (null = use default style)

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

// Axis - represents a column or row
struct Axis {
    std::string name;   // Custom name (empty = compute from position)
    ID id;              // Unique identifier (8-char base62)
    uint32_t position;  // Visual position (0-indexed)
    uint32_t size;      // Width (column) or height (row) in pixels
    bool isColumn;      // true = column (x), false = row (y)

    Axis();
    explicit Axis(const ID& id, bool isColumn = true);
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

    // Axis storage (maps ID -> Axis)
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> columns;
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> rows;

    // Cell storage (maps ID -> Cell)
    std::unordered_map<ID, std::unique_ptr<Cell>, IDHash> cells;

    Sheet();
    explicit Sheet(const ID& id, std::string name = "Sheet1");
    ~Sheet();

    // Cell operations
    Cell* getCell(const ID& cellId);
    Cell* getCellAt(const ID& colId, const ID& rowId);
    Cell* getOrCreateCellAt(const ID& colId, const ID& rowId);  // Auto-creates if needed
    void addCell(std::unique_ptr<Cell> cell);
    void reserveCells(size_t count);  // Pre-allocate capacity for bulk imports

    // Axis operations
    Axis* getColumn(const ID& colId);
    Axis* getRow(const ID& rowId);
    Axis* getColumnByPosition(uint32_t position);
    Axis* getRowByPosition(uint32_t position);
    Axis* getColumnByName(const std::string& name);        // A, B, ..., Z, AA, AB, ...
    Axis* getOrCreateColumnByPosition(uint32_t position);  // Auto-creates if needed
    Axis* getOrCreateRowByPosition(uint32_t position);     // Auto-creates if needed
    void addColumn(std::unique_ptr<Axis> col);
    void addRow(std::unique_ptr<Axis> row);

    // Count accessors
    [[nodiscard]] size_t columnCount() const { return columns.size(); }
    [[nodiscard]] size_t rowCount() const { return rows.size(); }
    [[nodiscard]] size_t cellCount() const { return cells.size(); }

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

    // Get the dependency graph for this sheet
    [[nodiscard]] DependencyGraph* getDependencyGraph() { return _depGraph.get(); }
    [[nodiscard]] const DependencyGraph* getDependencyGraph() const { return _depGraph.get(); }

    // ========================================================================
    // Spill Range Management (Runtime-Only)
    // ========================================================================

    // Get spill info for a master cell (returns nullptr if not a spill master)
    [[nodiscard]] SpillInfo* getSpillInfo(const ID& masterCellId);
    [[nodiscard]] const SpillInfo* getSpillInfo(const ID& masterCellId) const;

    // Get the master cell ID if this position is spilled into (returns null ID if not spilled)
    [[nodiscard]] ID getSpillMaster(const ID& colId, const ID& rowId) const;

    // Check if a position is part of any spill range (including master position)
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
    // Shared Formula Tracking (Runtime-Only)
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

private:
    // Parent workbook (set by Workbook::addSheet)
    Workbook* _workbook{nullptr};

    // Secondary index: (colId, rowId) -> cellId
    std::unordered_map<std::string, ID> _cellIndex;

    // Dependency graph for tracking formula dependencies
    std::unique_ptr<DependencyGraph> _depGraph;

    // ========================================================================
    // Spill range tracking (runtime-only, not persisted)
    // ========================================================================

    // Maps master cell ID → spill info (positions and values)
    std::unordered_map<ID, SpillInfo, IDHash> _spillMasters;

    // Reverse lookup: (colId, rowId) composite key → master cell ID
    // Only contains spilled positions, not the master position itself
    std::unordered_map<std::string, ID> _spilledFrom;

    // ========================================================================
    // Shared formula tracking (runtime-only, not persisted)
    // ========================================================================

    // Maps master cell ID → shared formula info (subscriber list)
    std::unordered_map<ID, SharedFormulaInfo, IDHash> _sharedFormulaMasters;

    // Reverse lookup: subscriber cell ID → master cell ID
    std::unordered_map<ID, ID, IDHash> _sharedFormulaFrom;

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
    // Named ranges
    // ========================================================================

    // Get the named range registry for this workbook
    [[nodiscard]] NamedRangeRegistry* getNamedRanges() { return _namedRanges.get(); }
    [[nodiscard]] const NamedRangeRegistry* getNamedRanges() const { return _namedRanges.get(); }

    // Get sheet by name (for cross-sheet references)
    [[nodiscard]] Sheet* getSheetByName(const std::string& name);
    [[nodiscard]] const Sheet* getSheetByName(const std::string& name) const;

    // ========================================================================
    // Custom formats (CRDT-synced)
    // ========================================================================

    // Register a custom format definition (called by CRDT when applying FORMAT_DEFINE)
    // Returns true if the format was newly added, false if it already existed
    bool registerCustomFormat(const ID& formatId, const std::string& formatCode);

    // Check if a custom format is defined
    [[nodiscard]] bool hasCustomFormat(const ID& formatId) const;

    // Get a custom format's code (returns empty string if not found)
    [[nodiscard]] std::string getCustomFormatCode(const ID& formatId) const;

    // Get all custom formats (for bootstrapOpLog and sync)
    [[nodiscard]] const std::unordered_map<ID, std::string, IDHash>& getCustomFormats() const;

    // ========================================================================
    // Cell styles (CRDT-synced)
    // ========================================================================

    // Register a style definition (called by CRDT when applying STYLE_DEFINE)
    // Returns true if the style was newly added, false if it already existed
    bool registerStyle(const ID& styleId, const CellStyle& style);

    // Check if a style is defined
    [[nodiscard]] bool hasStyle(const ID& styleId) const;

    // Get a style by ID (returns nullptr if not found)
    [[nodiscard]] const CellStyle* getStyle(const ID& styleId) const;

    // Get all styles (for bootstrapOpLog and sync)
    [[nodiscard]] const std::unordered_map<ID, CellStyle, IDHash>& getStyles() const;

private:
    // Sheet lookup by ID
    std::unordered_map<ID, Sheet*, IDHash> _sheetIndex;

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

    // Custom format definitions (format ID -> format code)
    // Synced via FORMAT_DEFINE operations
    std::unordered_map<ID, std::string, IDHash> _customFormats;

    // Cell style definitions (style ID -> CellStyle)
    // Synced via STYLE_DEFINE operations
    std::unordered_map<ID, CellStyle, IDHash> _styles;
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
