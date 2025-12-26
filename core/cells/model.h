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
struct SharedFormulaGroup;
struct OpLog;

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

// Formula - contains source text and parsed AST
// Owned by Cell, cleaned up in Cell destructor
struct Formula {
    char* text;           // Formula source with UUID refs (e.g., "=$cK7mXp2Q$rFp3nW9x+10"), owned
    struct ASTNode* ast;  // Parsed AST, null if not parsed, owned
    bool dirty;           // Needs recalculation?

    Formula();
    explicit Formula(const char* text);
    ~Formula();

    // Non-copyable (owns resources)
    Formula(const Formula&) = delete;
    Formula& operator=(const Formula&) = delete;

    // Movable
    Formula(Formula&& other) noexcept;
    Formula& operator=(Formula&& other) noexcept;

    // Parse the formula text into an AST
    // Returns true if parsing succeeded (AST will be set)
    // Note: Success doesn't mean the formula is valid - check isValid() for that
    bool parse();

    // Check if the AST is valid (no ErrorNodes)
    // Returns false if AST is null or contains errors
    [[nodiscard]] bool isValid() const;

    // Check if the formula contains volatile functions (NOW, RAND, TODAY, etc.)
    // Returns false if AST is null
    [[nodiscard]] bool hasVolatile() const;

    // Get the formula text (for display)
    [[nodiscard]] const char* getText() const { return text; }
};

// Cell - fundamental unit of data
// Either a direct value OR a formula with cached result
struct Cell {
    ID id;             // Unique identifier (8-char base62)
    ID colId;          // Column axis ID
    ID rowId;          // Row axis ID
    CellValue value;   // Direct value OR cached formula result
    Formula* formula;  // null = value cell, non-null = formula cell (owned)

    // Shared formula support: if non-null, this cell uses master's formula
    // (does not own the formula - master owns it)
    Cell* sharedFormulaRef;

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

    // Returns true if cell uses another cell's formula
    [[nodiscard]] bool isSharedFormula() const;

    // Returns true if cell is a shared formula master (has subscribers)
    [[nodiscard]] bool isSharedFormulaMaster() const;

    [[nodiscard]] bool hasError() const;

    // Get the effective formula (own formula or master's formula)
    [[nodiscard]] Formula* getFormula() const;

    // Set cell to a formula (takes ownership)
    void setFormula(Formula* f);

    // Set cell to use another cell's formula (shared formula subscriber)
    void setSharedFormulaRef(Cell* master);

    // Clear formula, making this a value cell
    void clearFormula();

private:
    // Track if this cell is a shared formula master (has subscribers)
    bool _isSharedFormulaMaster = false;
    friend struct SharedFormulaGroup;
};

// SharedFormulaGroup - manages shared formula master/subscriber relationships
// Master cell owns the formula, subscribers reference it
struct SharedFormulaGroup {
    Cell* master;                    // First alphabetically, owns the formula
    std::vector<Cell*> subscribers;  // Cells using =@master

    SharedFormulaGroup() : master(nullptr) {}
    explicit SharedFormulaGroup(Cell* master) : master(master) {}

    // Add a subscriber to this group
    void addSubscriber(Cell* cell);

    // Remove a subscriber from this group
    void removeSubscriber(Cell* cell);

    // Promote next subscriber to master (when master is deleted)
    // Returns new master, or nullptr if no subscribers remain
    Cell* promoteMaster();

    // Get all cells in the group (master + subscribers)
    [[nodiscard]] std::vector<Cell*> getAllCells() const;
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

private:
    // Secondary index: (colId, rowId) -> cellId
    std::unordered_map<std::string, ID> _cellIndex;

    // Dependency graph for tracking formula dependencies
    std::unique_ptr<DependencyGraph> _depGraph;

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
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
