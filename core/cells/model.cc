#include "core/cells/model.h"

#include <cstdlib>
#include <stdexcept>

namespace cells {

// ============================================================================
// CellValue
// ============================================================================

CellValue::CellValue()
    : raw(), type(CellValueType::STRING), error(CellError::NONE) {}

CellValue::CellValue(double number)
    : raw(std::to_string(number)), type(CellValueType::NUMBER), error(CellError::NONE) {
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
    : raw(str), type(type), error(CellError::NONE) {}

CellValue::CellValue(bool boolean)
    : raw(boolean ? "true" : "false"), type(CellValueType::BOOLEAN), error(CellError::NONE) {}

CellValue::CellValue(CellError err)
    : raw(errorToString(err)), type(CellValueType::ERROR), error(err) {}

double CellValue::asNumber() const {
    if (type != CellValueType::NUMBER) {
        return 0.0;
    }
    return std::strtod(raw.c_str(), nullptr);
}

bool CellValue::asBoolean() const {
    if (type != CellValueType::BOOLEAN) {
        return false;
    }
    return raw == "true";
}

const std::string& CellValue::asString() const {
    return raw;
}

// ============================================================================
// Cell
// ============================================================================

Cell::Cell() : id(), colId(), rowId(), value(), formula() {}

Cell::Cell(const ID& id) : id(id), colId(), rowId(), value(), formula() {}

Cell::Cell(const ID& id, const ID& col, const ID& row)
    : id(id), colId(col), rowId(row), value(), formula() {}

bool Cell::isFormula() const {
    return value.type == CellValueType::FORMULA || !formula.empty();
}

bool Cell::hasError() const {
    return value.error != CellError::NONE;
}

// ============================================================================
// Axis
// ============================================================================

Axis::Axis()
    : id(), isColumn(true), prevId(), nextId(),
      gapBefore(0), gapAfter(0), name(), size(DEFAULT_COLUMN_WIDTH) {}

Axis::Axis(const ID& id, bool isColumn)
    : id(id), isColumn(isColumn), prevId(), nextId(),
      gapBefore(0), gapAfter(0), name(),
      size(isColumn ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT) {}

bool Axis::isHead() const {
    return prevId.isNull();
}

bool Axis::isTail() const {
    return nextId.isNull();
}

// ============================================================================
// Sheet
// ============================================================================

Sheet::Sheet() : id(), name("Sheet1") {}

Sheet::Sheet(const ID& id, const std::string& name) : id(id), name(name) {}

Cell* Sheet::getCell(const ID& cellId) {
    auto it = cells.find(cellId);
    return (it != cells.end()) ? it->second.get() : nullptr;
}

Cell* Sheet::getCellAt(const ID& colId, const ID& rowId) {
    auto key = makeCellKey(colId, rowId);
    auto it = _cellIndex.find(key);
    if (it == _cellIndex.end()) {
        return nullptr;
    }
    return getCell(it->second);
}

void Sheet::addCell(std::unique_ptr<Cell> cell) {
    if (!cell) return;

    const ID& cellId = cell->id;
    const ID& colId = cell->colId;
    const ID& rowId = cell->rowId;

    // Update secondary index
    auto key = makeCellKey(colId, rowId);
    _cellIndex[key] = cellId;

    // Store cell
    cells[cellId] = std::move(cell);
}

Axis* Sheet::getColumn(const ID& colId) {
    auto it = columns.find(colId);
    return (it != columns.end()) ? it->second.get() : nullptr;
}

Axis* Sheet::getRow(const ID& rowId) {
    auto it = rows.find(rowId);
    return (it != rows.end()) ? it->second.get() : nullptr;
}

void Sheet::addColumn(std::unique_ptr<Axis> col) {
    if (!col) return;

    col->isColumn = true;
    const ID& colId = col->id;

    // Track first/last for linked list
    if (col->isHead()) {
        firstCol = colId;
    }
    if (col->isTail()) {
        lastCol = colId;
    }

    columns[colId] = std::move(col);
}

void Sheet::addRow(std::unique_ptr<Axis> row) {
    if (!row) return;

    row->isColumn = false;
    const ID& rowId = row->id;

    // Track first/last for linked list
    if (row->isHead()) {
        firstRow = rowId;
    }
    if (row->isTail()) {
        lastRow = rowId;
    }

    rows[rowId] = std::move(row);
}

std::string Sheet::makeCellKey(const ID& colId, const ID& rowId) {
    // Simple composite key: colId + ":" + rowId
    return colId.toString() + ":" + rowId.toString();
}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook() : id(), name("Untitled") {}

Workbook::Workbook(const ID& id, const std::string& name) : id(id), name(name) {}

Sheet* Workbook::getSheet(const ID& sheetId) {
    auto it = _sheetIndex.find(sheetId);
    return (it != _sheetIndex.end()) ? it->second : nullptr;
}

Sheet* Workbook::getSheetByIndex(size_t index) {
    if (index >= sheets.size()) {
        return nullptr;
    }
    return sheets[index].get();
}

void Workbook::addSheet(std::unique_ptr<Sheet> sheet) {
    if (!sheet) return;

    const ID& sheetId = sheet->id;
    Sheet* rawPtr = sheet.get();

    sheets.push_back(std::move(sheet));
    _sheetIndex[sheetId] = rawPtr;
}

}  // namespace cells
