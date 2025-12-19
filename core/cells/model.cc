#include "core/cells/model.h"

#include <cstdlib>
#include <cstring>

#include <utility>

namespace cells {

// ============================================================================
// CellValue
// ============================================================================

CellValue::CellValue() : raw(), type(CellValueType::STRING), error(CellError::NONE) {}

CellValue::CellValue(double number)
    : raw(std::to_string(number)), type(CellValueType::NUMBER), error(CellError::NONE) {
    // Remove trailing zeros for cleaner representation
    // e.g., "30.000000" -> "30", "3.140000" -> "3.14"
    size_t const dot = raw.find('.');
    if (dot != std::string::npos) {
        size_t const last = raw.find_last_not_of('0');
        if (last != std::string::npos && last >= dot) {
            raw = raw.substr(0, last + 1);
        }
        // Remove trailing dot if all decimals were zeros
        if (!raw.empty() && raw.back() == '.') {
            raw.pop_back();
        }
    }
}

CellValue::CellValue(std::string str)
    : raw(std::move(str)), type(CellValueType::STRING), error(CellError::NONE) {}

CellValue::CellValue(const char* str)
    : raw(str ? str : ""), type(CellValueType::STRING), error(CellError::NONE) {}

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
// Formula
// ============================================================================

Formula::Formula() : text(nullptr), ast(nullptr), dirty(true) {}

Formula::Formula(const char* text) : text(nullptr), ast(nullptr), dirty(true) {
    if (text) {
        size_t const len = std::strlen(text);
        this->text = new char[len + 1];
        std::memcpy(this->text, text, len + 1);
    }
}

Formula::~Formula() {
    delete[] text;
    // Note: ast cleanup will be handled when ASTNode is implemented
}

Formula::Formula(Formula&& other) noexcept : text(other.text), ast(other.ast), dirty(other.dirty) {
    other.text = nullptr;
    other.ast = nullptr;
}

Formula& Formula::operator=(Formula&& other) noexcept {
    if (this != &other) {
        delete[] text;
        // Note: ast cleanup will be handled when ASTNode is implemented
        text = other.text;
        ast = other.ast;
        dirty = other.dirty;
        other.text = nullptr;
        other.ast = nullptr;
    }
    return *this;
}

// ============================================================================
// Cell
// ============================================================================

Cell::Cell() : id(), colId(), rowId(), value(), formula(nullptr) {}

Cell::Cell(const ID& id) : id(id), colId(), rowId(), value(), formula(nullptr) {}

Cell::Cell(const ID& id, const ID& col, const ID& row)
    : id(id), colId(col), rowId(row), value(), formula(nullptr) {}

Cell::~Cell() {
    delete formula;
}

Cell::Cell(Cell&& other) noexcept
    : id(other.id),
      colId(other.colId),
      rowId(other.rowId),
      value(std::move(other.value)),
      formula(other.formula) {
    other.formula = nullptr;
}

Cell& Cell::operator=(Cell&& other) noexcept {
    if (this != &other) {
        delete formula;
        id = other.id;
        colId = other.colId;
        rowId = other.rowId;
        value = std::move(other.value);
        formula = other.formula;
        other.formula = nullptr;
    }
    return *this;
}

bool Cell::isFormula() const {
    return formula != nullptr;
}

bool Cell::hasError() const {
    return value.error != CellError::NONE;
}

void Cell::setFormula(Formula* f) {
    delete formula;
    formula = f;
    if (f) {
        value.type = CellValueType::FORMULA;
    }
}

void Cell::clearFormula() {
    delete formula;
    formula = nullptr;
}

// ============================================================================
// Axis
// ============================================================================

Axis::Axis() : name(), id(), position(0), size(DEFAULT_COLUMN_WIDTH), isColumn(true) {}

Axis::Axis(const ID& id, bool isColumn)
    : name(),
      id(id),
      position(0),
      size(isColumn ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT),
      isColumn(isColumn) {}

// ============================================================================
// Sheet
// ============================================================================

Sheet::Sheet() : id(), name("Sheet1") {}

Sheet::Sheet(const ID& id, std::string name) : id(id), name(std::move(name)) {}

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
    if (!cell) {
        return;
    }

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
    if (!col) {
        return;
    }

    col->isColumn = true;
    columns[col->id] = std::move(col);
}

void Sheet::addRow(std::unique_ptr<Axis> row) {
    if (!row) {
        return;
    }

    row->isColumn = false;
    rows[row->id] = std::move(row);
}

std::string Sheet::makeCellKey(const ID& colId, const ID& rowId) {
    // Simple composite key: colId + ":" + rowId
    return colId.toString() + ":" + rowId.toString();
}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook() : id(), name("Untitled") {}

Workbook::Workbook(const ID& id, std::string name) : id(id), name(std::move(name)) {}

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
    if (!sheet) {
        return;
    }

    const ID& sheetId = sheet->id;
    Sheet* rawPtr = sheet.get();

    sheets.push_back(std::move(sheet));
    _sheetIndex[sheetId] = rawPtr;
}

}  // namespace cells
