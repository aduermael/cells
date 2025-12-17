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

// Cell value - stores the raw value as a string (for simplicity)
// The type field indicates how to interpret it
struct CellValue {
    std::string raw;         // Raw string representation
    CellValueType type;      // How to interpret the value
    CellError error;         // Error state (NONE if no error)

    CellValue();
    CellValue(double number);
    CellValue(const std::string& str, CellValueType type = CellValueType::STRING);
    CellValue(bool boolean);
    CellValue(CellError error);

    // Value accessors (parse from raw string)
    double asNumber() const;
    bool asBoolean() const;
    const std::string& asString() const;
};

// Formula - contains source text and optional compiled bytecode
// Owned by Cell, cleaned up in Cell destructor
struct Formula {
    char* text;              // Formula source (e.g., "=A1+B1"), owned
    uint8_t* bytecode;       // Compiled Luau bytecode, null if not compiled, owned
    size_t bytecodeLen;      // Bytecode length in bytes
    bool dirty;              // Needs recalculation?

    Formula();
    explicit Formula(const char* text);
    ~Formula();

    // Non-copyable (owns resources)
    Formula(const Formula&) = delete;
    Formula& operator=(const Formula&) = delete;

    // Movable
    Formula(Formula&& other);
    Formula& operator=(Formula&& other);
};

// Cell - fundamental unit of data
// Either a direct value OR a formula with cached result
struct Cell {
    ID id;                   // Unique identifier (8-char base62)
    ID colId;                // Column axis ID
    ID rowId;                // Row axis ID
    CellValue value;         // Direct value OR cached formula result
    Formula* formula;        // null = value cell, non-null = formula cell (owned)

    Cell();
    explicit Cell(const ID& id);
    Cell(const ID& id, const ID& col, const ID& row);
    ~Cell();

    // Non-copyable (owns formula)
    Cell(const Cell&) = delete;
    Cell& operator=(const Cell&) = delete;

    // Movable
    Cell(Cell&& other);
    Cell& operator=(Cell&& other);

    bool isFormula() const;
    bool hasError() const;

    // Set cell to a formula (takes ownership)
    void setFormula(Formula* f);

    // Clear formula, making this a value cell
    void clearFormula();
};

// Axis - represents a column or row
struct Axis {
    ID id;                   // Unique identifier (8-char base62)
    bool isColumn;           // true = column (x), false = row (y)

    // Doubly-linked list structure
    ID prevId;               // Previous axis ID (null = head)
    ID nextId;               // Next axis ID (null = tail)
    uint32_t gapBefore;      // Empty positions between prev and this
    uint32_t gapAfter;       // Empty positions between this and next

    // Properties
    std::string name;        // Custom name (empty = compute from position)
    uint32_t size;           // Width (column) or height (row) in pixels

    Axis();
    explicit Axis(const ID& id, bool isColumn = true);

    bool isHead() const;     // No prev
    bool isTail() const;     // No next
};

// Sheet - 2D grid containing cells
struct Sheet {
    ID id;                   // Unique identifier
    std::string name;        // Sheet name

    // Axis storage (maps ID -> Axis)
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> columns;
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> rows;

    // Head/tail of linked lists (for traversal)
    ID firstCol;
    ID lastCol;
    ID firstRow;
    ID lastRow;

    // Cell storage (maps ID -> Cell)
    std::unordered_map<ID, std::unique_ptr<Cell>, IDHash> cells;

    Sheet();
    explicit Sheet(const ID& id, const std::string& name = "Sheet1");

    // Cell operations
    Cell* getCell(const ID& cellId);
    Cell* getCellAt(const ID& colId, const ID& rowId);
    void addCell(std::unique_ptr<Cell> cell);

    // Axis operations
    Axis* getColumn(const ID& colId);
    Axis* getRow(const ID& rowId);
    void addColumn(std::unique_ptr<Axis> col);
    void addRow(std::unique_ptr<Axis> row);

    // Count accessors
    size_t columnCount() const { return columns.size(); }
    size_t rowCount() const { return rows.size(); }
    size_t cellCount() const { return cells.size(); }

private:
    // Secondary index: (colId, rowId) -> cellId
    std::unordered_map<std::string, ID> _cellIndex;

    // Build composite key for cell index
    static std::string makeCellKey(const ID& colId, const ID& rowId);
};

// Workbook - top-level container
struct Workbook {
    ID id;                   // Document ID
    std::string name;        // Document name

    // Sheets (order matters for tab display)
    std::vector<std::unique_ptr<Sheet>> sheets;

    Workbook();
    explicit Workbook(const ID& id, const std::string& name = "Untitled");

    // Sheet operations
    Sheet* getSheet(const ID& sheetId);
    Sheet* getSheetByIndex(size_t index);
    void addSheet(std::unique_ptr<Sheet> sheet);

    size_t sheetCount() const { return sheets.size(); }

private:
    // Sheet lookup by ID
    std::unordered_map<ID, Sheet*, IDHash> _sheetIndex;
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
