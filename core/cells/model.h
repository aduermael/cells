#ifndef CELLS_MODEL_H_
#define CELLS_MODEL_H_

#include <cstdint>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Axis;
struct Cell;
struct Sheet;
struct Workbook;
struct SharedFormulaGroup;

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

    // Cell operations
    Cell* getCell(const ID& cellId);
    Cell* getCellAt(const ID& colId, const ID& rowId);
    void addCell(std::unique_ptr<Cell> cell);
    void reserveCells(size_t count);  // Pre-allocate capacity for bulk imports

    // Axis operations
    Axis* getColumn(const ID& colId);
    Axis* getRow(const ID& rowId);
    void addColumn(std::unique_ptr<Axis> col);
    void addRow(std::unique_ptr<Axis> row);

    // Count accessors
    [[nodiscard]] size_t columnCount() const { return columns.size(); }
    [[nodiscard]] size_t rowCount() const { return rows.size(); }
    [[nodiscard]] size_t cellCount() const { return cells.size(); }

private:
    // Secondary index: (colId, rowId) -> cellId
    std::unordered_map<std::string, ID> _cellIndex;

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

    // Sheet operations
    Sheet* getSheet(const ID& sheetId);
    Sheet* getSheetByIndex(size_t index);
    void addSheet(std::unique_ptr<Sheet> sheet);

    [[nodiscard]] size_t sheetCount() const { return sheets.size(); }

private:
    // Sheet lookup by ID
    std::unordered_map<ID, Sheet*, IDHash> _sheetIndex;
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
