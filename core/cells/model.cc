#include "core/cells/model.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
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

Cell::Cell() : id(), colId(), rowId(), value(), formula(nullptr), sharedFormulaRef(nullptr) {}

Cell::Cell(const ID& id)
    : id(id), colId(), rowId(), value(), formula(nullptr), sharedFormulaRef(nullptr) {}

Cell::Cell(const ID& id, const ID& col, const ID& row)
    : id(id), colId(col), rowId(row), value(), formula(nullptr), sharedFormulaRef(nullptr) {}

Cell::~Cell() {
    // Only delete formula if we own it (not a shared formula subscriber)
    if (formula != nullptr && sharedFormulaRef == nullptr) {
        delete formula;
    }
}

Cell::Cell(Cell&& other) noexcept
    : id(other.id),
      colId(other.colId),
      rowId(other.rowId),
      value(std::move(other.value)),
      formula(other.formula),
      sharedFormulaRef(other.sharedFormulaRef),
      _isSharedFormulaMaster(other._isSharedFormulaMaster) {
    other.formula = nullptr;
    other.sharedFormulaRef = nullptr;
    other._isSharedFormulaMaster = false;
}

Cell& Cell::operator=(Cell&& other) noexcept {
    if (this != &other) {
        // Only delete formula if we own it
        if (formula != nullptr && sharedFormulaRef == nullptr) {
            delete formula;
        }
        id = other.id;
        colId = other.colId;
        rowId = other.rowId;
        value = std::move(other.value);
        formula = other.formula;
        sharedFormulaRef = other.sharedFormulaRef;
        _isSharedFormulaMaster = other._isSharedFormulaMaster;
        other.formula = nullptr;
        other.sharedFormulaRef = nullptr;
        other._isSharedFormulaMaster = false;
    }
    return *this;
}

bool Cell::isFormula() const {
    return formula != nullptr || sharedFormulaRef != nullptr;
}

bool Cell::isSharedFormula() const {
    return sharedFormulaRef != nullptr;
}

bool Cell::isSharedFormulaMaster() const {
    return _isSharedFormulaMaster;
}

bool Cell::hasError() const {
    return value.error != CellError::NONE;
}

Formula* Cell::getFormula() const {
    if (sharedFormulaRef != nullptr) {
        return sharedFormulaRef->formula;
    }
    return formula;
}

void Cell::setFormula(Formula* f) {
    // Clear any existing formula or shared ref
    clearFormula();
    formula = f;
    if (f != nullptr) {
        value.type = CellValueType::FORMULA;
    }
}

void Cell::setSharedFormulaRef(Cell* master) {
    // Clear any existing formula or shared ref
    clearFormula();
    sharedFormulaRef = master;
    if (master != nullptr) {
        value.type = CellValueType::FORMULA;
        master->_isSharedFormulaMaster = true;
    }
}

void Cell::clearFormula() {
    // Only delete formula if we own it (not a shared formula subscriber)
    if (formula != nullptr && sharedFormulaRef == nullptr) {
        delete formula;
    }
    formula = nullptr;
    sharedFormulaRef = nullptr;
    // Note: _isSharedFormulaMaster is managed by SharedFormulaGroup
}

// ============================================================================
// SharedFormulaGroup
// ============================================================================

void SharedFormulaGroup::addSubscriber(Cell* cell) {
    if (cell == nullptr || cell == master) {
        return;
    }

    // Set cell's shared formula reference to master
    cell->setSharedFormulaRef(master);
    subscribers.push_back(cell);
}

void SharedFormulaGroup::removeSubscriber(Cell* cell) {
    if (cell == nullptr) {
        return;
    }

    // Find and remove from subscribers
    auto it = std::find(subscribers.begin(), subscribers.end(), cell);
    if (it != subscribers.end()) {
        subscribers.erase(it);
        cell->sharedFormulaRef = nullptr;
    }

    // Update master's flag if no more subscribers
    if (master != nullptr && subscribers.empty()) {
        master->_isSharedFormulaMaster = false;
    }
}

Cell* SharedFormulaGroup::promoteMaster() {
    if (subscribers.empty()) {
        // No subscribers, group becomes empty
        if (master != nullptr) {
            master->_isSharedFormulaMaster = false;
        }
        master = nullptr;
        return nullptr;
    }

    // Sort subscribers alphabetically by UUID to get deterministic new master
    // Use a temporary vector with (id_string, index) pairs to avoid pointer sorting
    std::vector<std::pair<std::string, size_t>> sortedIndices;
    sortedIndices.reserve(subscribers.size());
    for (size_t i = 0; i < subscribers.size(); ++i) {
        sortedIndices.emplace_back(subscribers[i]->id.toString(), i);
    }
    std::sort(sortedIndices.begin(), sortedIndices.end());

    // Rebuild subscribers in sorted order
    std::vector<Cell*> sortedSubscribers;
    sortedSubscribers.reserve(subscribers.size());
    for (const auto& [idStr, idx] : sortedIndices) {
        sortedSubscribers.push_back(subscribers[idx]);
    }
    subscribers = std::move(sortedSubscribers);

    // New master is first subscriber alphabetically
    Cell* newMaster = subscribers.front();
    subscribers.erase(subscribers.begin());

    // Clone formula from old master to new master
    if (master != nullptr && master->formula != nullptr) {
        newMaster->formula = new Formula(master->formula->text);
        newMaster->sharedFormulaRef = nullptr;
    }

    // Update all remaining subscribers to point to new master
    for (Cell* sub : subscribers) {
        sub->sharedFormulaRef = newMaster;
    }

    // Update master flags
    if (master != nullptr) {
        master->_isSharedFormulaMaster = false;
    }
    newMaster->_isSharedFormulaMaster = !subscribers.empty();

    master = newMaster;
    return newMaster;
}

std::vector<Cell*> SharedFormulaGroup::getAllCells() const {
    std::vector<Cell*> cells;
    cells.reserve(1 + subscribers.size());

    if (master != nullptr) {
        cells.push_back(master);
    }
    cells.insert(cells.end(), subscribers.begin(), subscribers.end());

    return cells;
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

void Sheet::reserveCells(size_t count) {
    cells.reserve(count);
    _cellIndex.reserve(count);
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
