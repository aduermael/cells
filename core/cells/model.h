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
    CellError error;         // Error state (kNone if no error)

    CellValue();
    CellValue(double number);
    CellValue(const std::string& str, CellValueType type = CellValueType::kString);
    CellValue(bool boolean);
    CellValue(CellError error);

    // Value accessors (parse from raw string)
    double AsNumber() const;
    bool AsBoolean() const;
    const std::string& AsString() const;
};

// Cell - fundamental unit of data
struct Cell {
    ID id;                   // Unique identifier (8-char base62)
    ID col_id;               // Column axis ID
    ID row_id;               // Row axis ID
    CellValue value;         // Cell value and type
    std::string formula;     // Formula text (empty if not a formula)

    Cell();
    explicit Cell(const ID& id);
    Cell(const ID& id, const ID& col, const ID& row);

    bool IsFormula() const;
    bool HasError() const;
};

// Axis - represents a column or row
struct Axis {
    ID id;                   // Unique identifier (8-char base62)
    bool is_column;          // true = column (x), false = row (y)

    // Doubly-linked list structure
    ID prev_id;              // Previous axis ID (empty = head)
    ID next_id;              // Next axis ID (empty = tail)
    uint32_t gap_before;     // Empty positions between prev and this
    uint32_t gap_after;      // Empty positions between this and next

    // Properties
    std::string name;        // Custom name (empty = compute from position)
    uint32_t size;           // Width (column) or height (row) in pixels

    Axis();
    explicit Axis(const ID& id, bool is_column = true);

    bool IsHead() const;     // No prev
    bool IsTail() const;     // No next
};

// Sheet - 2D grid containing cells
struct Sheet {
    ID id;                   // Unique identifier
    std::string name;        // Sheet name

    // Axis storage (maps ID -> Axis)
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> columns;
    std::unordered_map<ID, std::unique_ptr<Axis>, IDHash> rows;

    // Head/tail of linked lists (for traversal)
    ID first_col;
    ID last_col;
    ID first_row;
    ID last_row;

    // Cell storage (maps ID -> Cell)
    std::unordered_map<ID, std::unique_ptr<Cell>, IDHash> cells;

    // Secondary index: (col_id, row_id) -> cell_id
    std::unordered_map<std::string, ID> cell_index;

    Sheet();
    explicit Sheet(const ID& id, const std::string& name = "Sheet1");

    // Cell operations
    Cell* GetCell(const ID& cell_id);
    Cell* GetCellAt(const ID& col_id, const ID& row_id);
    void AddCell(std::unique_ptr<Cell> cell);

    // Axis operations
    Axis* GetColumn(const ID& col_id);
    Axis* GetRow(const ID& row_id);
    void AddColumn(std::unique_ptr<Axis> col);
    void AddRow(std::unique_ptr<Axis> row);

    // Count accessors
    size_t ColumnCount() const { return columns.size(); }
    size_t RowCount() const { return rows.size(); }
    size_t CellCount() const { return cells.size(); }

private:
    // Build composite key for cell index
    static std::string MakeCellKey(const ID& col_id, const ID& row_id);
};

// Workbook - top-level container
struct Workbook {
    ID id;                   // Document ID
    std::string name;        // Document name

    // Sheets (order matters for tab display)
    std::vector<std::unique_ptr<Sheet>> sheets;

    // Sheet lookup by ID
    std::unordered_map<ID, Sheet*, IDHash> sheet_index;

    Workbook();
    explicit Workbook(const ID& id, const std::string& name = "Untitled");

    // Sheet operations
    Sheet* GetSheet(const ID& sheet_id);
    Sheet* GetSheetByIndex(size_t index);
    void AddSheet(std::unique_ptr<Sheet> sheet);

    size_t SheetCount() const { return sheets.size(); }
};

}  // namespace cells

#endif  // CELLS_MODEL_H_
