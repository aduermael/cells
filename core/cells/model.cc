#include "core/cells/model.h"

#include <cstdlib>
#include <stdexcept>

namespace cells {

// ============================================================================
// CellValue
// ============================================================================

CellValue::CellValue()
    : raw(), type(CellValueType::kString), error(CellError::kNone) {}

CellValue::CellValue(double number)
    : raw(std::to_string(number)), type(CellValueType::kNumber), error(CellError::kNone) {
    // Remove trailing zeros for cleaner representation
    size_t dot = raw.find('.');
    if (dot != std::string::npos) {
        size_t last = raw.find_last_not_of('0');
        if (last != std::string::npos && last > dot) {
            raw = raw.substr(0, last + 1);
        }
        // Remove trailing dot if all decimals were zeros
        if (raw.back() == '.') {
            raw.pop_back();
        }
    }
}

CellValue::CellValue(const std::string& str, CellValueType type)
    : raw(str), type(type), error(CellError::kNone) {}

CellValue::CellValue(bool boolean)
    : raw(boolean ? "true" : "false"), type(CellValueType::kBoolean), error(CellError::kNone) {}

CellValue::CellValue(CellError err)
    : raw(ErrorToString(err)), type(CellValueType::kError), error(err) {}

double CellValue::AsNumber() const {
    if (type != CellValueType::kNumber) {
        return 0.0;
    }
    return std::strtod(raw.c_str(), nullptr);
}

bool CellValue::AsBoolean() const {
    if (type != CellValueType::kBoolean) {
        return false;
    }
    return raw == "true";
}

const std::string& CellValue::AsString() const {
    return raw;
}

// ============================================================================
// Cell
// ============================================================================

Cell::Cell() : id(), col_id(), row_id(), value(), formula() {}

Cell::Cell(const ID& id) : id(id), col_id(), row_id(), value(), formula() {}

Cell::Cell(const ID& id, const ID& col, const ID& row)
    : id(id), col_id(col), row_id(row), value(), formula() {}

bool Cell::IsFormula() const {
    return value.type == CellValueType::kFormula || !formula.empty();
}

bool Cell::HasError() const {
    return value.error != CellError::kNone;
}

// ============================================================================
// Axis
// ============================================================================

Axis::Axis()
    : id(), is_column(true), prev_id(), next_id(),
      gap_before(0), gap_after(0), name(), size(kDefaultColumnWidth) {}

Axis::Axis(const ID& id, bool is_column)
    : id(id), is_column(is_column), prev_id(), next_id(),
      gap_before(0), gap_after(0), name(),
      size(is_column ? kDefaultColumnWidth : kDefaultRowHeight) {}

bool Axis::IsHead() const {
    return IsNullID(prev_id);
}

bool Axis::IsTail() const {
    return IsNullID(next_id);
}

// ============================================================================
// Sheet
// ============================================================================

Sheet::Sheet() : id(), name("Sheet1") {}

Sheet::Sheet(const ID& id, const std::string& name) : id(id), name(name) {}

Cell* Sheet::GetCell(const ID& cell_id) {
    auto it = cells.find(cell_id);
    return (it != cells.end()) ? it->second.get() : nullptr;
}

Cell* Sheet::GetCellAt(const ID& col_id, const ID& row_id) {
    auto key = MakeCellKey(col_id, row_id);
    auto it = cell_index.find(key);
    if (it == cell_index.end()) {
        return nullptr;
    }
    return GetCell(it->second);
}

void Sheet::AddCell(std::unique_ptr<Cell> cell) {
    if (!cell) return;

    const ID& cell_id = cell->id;
    const ID& col_id = cell->col_id;
    const ID& row_id = cell->row_id;

    // Update secondary index
    auto key = MakeCellKey(col_id, row_id);
    cell_index[key] = cell_id;

    // Store cell
    cells[cell_id] = std::move(cell);
}

Axis* Sheet::GetColumn(const ID& col_id) {
    auto it = columns.find(col_id);
    return (it != columns.end()) ? it->second.get() : nullptr;
}

Axis* Sheet::GetRow(const ID& row_id) {
    auto it = rows.find(row_id);
    return (it != rows.end()) ? it->second.get() : nullptr;
}

void Sheet::AddColumn(std::unique_ptr<Axis> col) {
    if (!col) return;

    col->is_column = true;
    const ID& col_id = col->id;

    // Track first/last for linked list
    if (col->IsHead()) {
        first_col = col_id;
    }
    if (col->IsTail()) {
        last_col = col_id;
    }

    columns[col_id] = std::move(col);
}

void Sheet::AddRow(std::unique_ptr<Axis> row) {
    if (!row) return;

    row->is_column = false;
    const ID& row_id = row->id;

    // Track first/last for linked list
    if (row->IsHead()) {
        first_row = row_id;
    }
    if (row->IsTail()) {
        last_row = row_id;
    }

    rows[row_id] = std::move(row);
}

std::string Sheet::MakeCellKey(const ID& col_id, const ID& row_id) {
    // Simple composite key: col_id + ":" + row_id
    return col_id + ":" + row_id;
}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook() : id(), name("Untitled") {}

Workbook::Workbook(const ID& id, const std::string& name) : id(id), name(name) {}

Sheet* Workbook::GetSheet(const ID& sheet_id) {
    auto it = sheet_index.find(sheet_id);
    return (it != sheet_index.end()) ? it->second : nullptr;
}

Sheet* Workbook::GetSheetByIndex(size_t index) {
    if (index >= sheets.size()) {
        return nullptr;
    }
    return sheets[index].get();
}

void Workbook::AddSheet(std::unique_ptr<Sheet> sheet) {
    if (!sheet) return;

    const ID& sheet_id = sheet->id;
    Sheet* raw_ptr = sheet.get();

    sheets.push_back(std::move(sheet));
    sheet_index[sheet_id] = raw_ptr;
}

}  // namespace cells
