#include "core/cells/model.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <utility>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/named_ranges.h"

namespace cells {

// Local helper to check if an AST contains volatile functions
// (Avoids circular dependency with formula_resolver.h)
namespace {
bool containsVolatileFunctionImpl(const ASTNode* ast) {
    if (ast == nullptr) {
        return false;
    }

    switch (ast->type) {
        case ASTNodeType::FUNCTION_CALL: {
            const auto* func = static_cast<const FunctionCallNode*>(ast);
            if (func->isVolatile || FunctionCallNode::isVolatileFunction(func->name)) {
                return true;
            }
            for (const auto& arg : func->args) {
                if (containsVolatileFunctionImpl(arg.get())) {
                    return true;
                }
            }
            return false;
        }
        case ASTNodeType::BINARY_OP: {
            const auto* binOp = static_cast<const BinaryOpNode*>(ast);
            return containsVolatileFunctionImpl(binOp->left.get()) ||
                   containsVolatileFunctionImpl(binOp->right.get());
        }
        case ASTNodeType::UNARY_OP: {
            const auto* unOp = static_cast<const UnaryOpNode*>(ast);
            return containsVolatileFunctionImpl(unOp->operand.get());
        }
        case ASTNodeType::ERROR_NODE: {
            const auto* errNode = static_cast<const ErrorNode*>(ast);
            for (const auto& child : errNode->partialChildren) {
                if (containsVolatileFunctionImpl(child.get())) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}
}  // namespace

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
    delete ast;
}

Formula::Formula(Formula&& other) noexcept : text(other.text), ast(other.ast), dirty(other.dirty) {
    other.text = nullptr;
    other.ast = nullptr;
}

Formula& Formula::operator=(Formula&& other) noexcept {
    if (this != &other) {
        delete[] text;
        delete ast;
        text = other.text;
        ast = other.ast;
        dirty = other.dirty;
        other.text = nullptr;
        other.ast = nullptr;
    }
    return *this;
}

bool Formula::parse() {
    // Delete any existing AST
    delete ast;
    ast = nullptr;

    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    FormulaParser parser(text);
    std::unique_ptr<ASTNode> parsed = parser.parse();
    if (parsed) {
        ast = parsed.release();
        return true;
    }
    return false;
}

bool Formula::isValid() const {
    if (ast == nullptr) {
        return false;
    }
    return !ast->hasError();
}

bool Formula::hasVolatile() const {
    if (ast == nullptr) {
        return false;
    }
    return containsVolatileFunctionImpl(ast);
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

Sheet::Sheet() : id(), name("Sheet1"), _depGraph(std::make_unique<DependencyGraph>()) {}

Sheet::Sheet(const ID& id, std::string name)
    : id(id), name(std::move(name)), _depGraph(std::make_unique<DependencyGraph>()) {}

Sheet::~Sheet() = default;

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

Cell* Sheet::getOrCreateCellAt(const ID& colId, const ID& rowId) {
    // Check if cell already exists
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Cell* const existing = getCellAt(colId, rowId);
    if (existing != nullptr) {
        return existing;
    }

    // Create new cell
    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Cell* const rawPtr = cell.get();
    addCell(std::move(cell));
    return rawPtr;
}

Axis* Sheet::getColumnByPosition(uint32_t position) {
    for (auto& [id, col] : columns) {
        if (col->position == position) {
            return col.get();
        }
    }
    return nullptr;
}

Axis* Sheet::getRowByPosition(uint32_t position) {
    for (auto& [id, row] : rows) {
        if (row->position == position) {
            return row.get();
        }
    }
    return nullptr;
}

Axis* Sheet::getColumnByName(const std::string& name) {
    // Convert name to position, then look up by position
    const int32_t position = columnNameToPosition(name);
    if (position < 0) {
        return nullptr;
    }
    return getColumnByPosition(static_cast<uint32_t>(position));
}

Axis* Sheet::getOrCreateColumnByPosition(uint32_t position) {
    // Check if column already exists
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const existing = getColumnByPosition(position);
    if (existing != nullptr) {
        return existing;
    }

    // Create new column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = col.get();
    addColumn(std::move(col));
    return rawPtr;
}

Axis* Sheet::getOrCreateRowByPosition(uint32_t position) {
    // Check if row already exists
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const existing = getRowByPosition(position);
    if (existing != nullptr) {
        return existing;
    }

    // Create new row
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = row.get();
    addRow(std::move(row));
    return rawPtr;
}

std::string Sheet::positionToColumnName(uint32_t position) {
    // Convert 0-indexed position to column name (A, B, ..., Z, AA, AB, ...)
    // 0 -> A, 25 -> Z, 26 -> AA, 27 -> AB, ...
    std::string result;
    uint32_t pos = position;
    do {
        result = static_cast<char>('A' + (pos % 26)) + result;
        pos = pos / 26;
        if (pos > 0) {
            pos--;  // Adjust for base-26 without zero digit
        } else {
            break;
        }
    } while (pos >= 0);
    return result;
}

int32_t Sheet::columnNameToPosition(const std::string& name) {
    // Convert column name (A, B, ..., Z, AA, AB, ...) to 0-indexed position
    // A -> 0, Z -> 25, AA -> 26, AB -> 27, ...
    if (name.empty()) {
        return -1;
    }

    int32_t position = 0;
    for (const char c : name) {
        // Convert to uppercase if lowercase
        char upper = c;
        if (c >= 'a' && c <= 'z') {
            upper = static_cast<char>(c - 'a' + 'A');
        }

        // Validate it's a letter
        if (upper < 'A' || upper > 'Z') {
            return -1;
        }

        position = position * 26 + (upper - 'A' + 1);
    }
    return position - 1;  // Convert to 0-indexed
}

std::string Sheet::makeCellKey(const ID& colId, const ID& rowId) {
    // Simple composite key: colId + ":" + rowId
    return colId.toString() + ":" + rowId.toString();
}

bool Sheet::moveColumn(const ID& colId, uint32_t newPosition) {
    // Find the column to move
    auto it = columns.find(colId);
    if (it == columns.end()) {
        return false;
    }

    Axis* col = it->second.get();
    const uint32_t oldPosition = col->position;

    if (oldPosition == newPosition) {
        return true;  // No-op
    }

    // Shift other columns
    if (newPosition < oldPosition) {
        // Moving left: shift columns in [newPosition, oldPosition) right by 1
        for (auto& [id, axis] : columns) {
            if (axis->position >= newPosition && axis->position < oldPosition) {
                axis->position++;
            }
        }
    } else {
        // Moving right: shift columns in (oldPosition, newPosition] left by 1
        for (auto& [id, axis] : columns) {
            if (axis->position > oldPosition && axis->position <= newPosition) {
                axis->position--;
            }
        }
    }

    // Set the new position
    col->position = newPosition;
    return true;
}

bool Sheet::moveRow(const ID& rowId, uint32_t newPosition) {
    // Find the row to move
    auto it = rows.find(rowId);
    if (it == rows.end()) {
        return false;
    }

    Axis* row = it->second.get();
    const uint32_t oldPosition = row->position;

    if (oldPosition == newPosition) {
        return true;  // No-op
    }

    // Shift other rows
    if (newPosition < oldPosition) {
        // Moving up: shift rows in [newPosition, oldPosition) down by 1
        for (auto& [id, axis] : rows) {
            if (axis->position >= newPosition && axis->position < oldPosition) {
                axis->position++;
            }
        }
    } else {
        // Moving down: shift rows in (oldPosition, newPosition] up by 1
        for (auto& [id, axis] : rows) {
            if (axis->position > oldPosition && axis->position <= newPosition) {
                axis->position--;
            }
        }
    }

    // Set the new position
    row->position = newPosition;
    return true;
}

Axis* Sheet::insertColumnAt(uint32_t position) {
    // Shift all columns at position or greater to the right
    for (auto& [id, axis] : columns) {
        if (axis->position >= position) {
            axis->position++;
        }
    }

    // Create new column at the specified position
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = col.get();
    addColumn(std::move(col));
    return rawPtr;
}

Axis* Sheet::insertRowAt(uint32_t position) {
    // Shift all rows at position or greater down
    for (auto& [id, axis] : rows) {
        if (axis->position >= position) {
            axis->position++;
        }
    }

    // Create new row at the specified position
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = row.get();
    addRow(std::move(row));
    return rawPtr;
}

bool Sheet::deleteColumn(const ID& colId) {
    auto it = columns.find(colId);
    if (it == columns.end()) {
        return false;
    }

    const uint32_t deletedPosition = it->second->position;

    // Delete all cells in this column
    std::vector<ID> cellsToDelete;
    for (const auto& [cellId, cell] : cells) {
        if (cell->colId == colId) {
            cellsToDelete.push_back(cellId);
        }
    }
    for (const ID& cellId : cellsToDelete) {
        // Clear formula dependencies before removing
        clearCellFormula(cellId);
        // Remove from cell index
        const Cell* const cell = getCell(cellId);
        if (cell != nullptr) {
            _cellIndex.erase(makeCellKey(cell->colId, cell->rowId));
        }
        cells.erase(cellId);
    }

    // Remove the column
    columns.erase(it);

    // Shift columns to the right of the deleted one left by 1
    for (auto& [id, axis] : columns) {
        if (axis->position > deletedPosition) {
            axis->position--;
        }
    }

    return true;
}

bool Sheet::deleteRow(const ID& rowId) {
    auto it = rows.find(rowId);
    if (it == rows.end()) {
        return false;
    }

    const uint32_t deletedPosition = it->second->position;

    // Delete all cells in this row
    std::vector<ID> cellsToDelete;
    for (const auto& [cellId, cell] : cells) {
        if (cell->rowId == rowId) {
            cellsToDelete.push_back(cellId);
        }
    }
    for (const ID& cellId : cellsToDelete) {
        // Clear formula dependencies before removing
        clearCellFormula(cellId);
        // Remove from cell index
        const Cell* const cell = getCell(cellId);
        if (cell != nullptr) {
            _cellIndex.erase(makeCellKey(cell->colId, cell->rowId));
        }
        cells.erase(cellId);
    }

    // Remove the row
    rows.erase(it);

    // Shift rows below the deleted one up by 1
    for (auto& [id, axis] : rows) {
        if (axis->position > deletedPosition) {
            axis->position--;
        }
    }

    return true;
}

FormulaResult Sheet::setCellFormula(const ID& cellId, const std::string& /* formulaText */,
                                    ASTNode* ast) {
    // Get the cell
    Cell* cell = getCell(cellId);
    if (cell == nullptr) {
        return {false, "Cell not found"};
    }

    // Clear existing formula and dependencies
    clearCellFormula(cellId);

    // Serialize the AST to UUID format for storage
    // This is the key change for move stability: we store UUID refs, not A1 refs
    std::string uuidFormula;
    if (ast != nullptr) {
        uuidFormula = FormulaSerializer::serialize(ast);
    }

    // Create the formula with UUID-format text (not the original A1 input)
    auto* formula = new Formula(uuidFormula.c_str());
    formula->ast = ast;  // Transfer ownership
    formula->dirty = true;
    cell->setFormula(formula);

    // Add to dependency graph (AST should already be resolved)
    if (ast != nullptr) {
        _depGraph->addFormula(cellId, ast);

        // Track volatile functions
        if (formula->hasVolatile()) {
            _depGraph->markVolatile(cellId);
        }
    }

    return {true, ""};
}

FormulaResult Sheet::setCellFormulaUnresolved(const ID& cellId, const std::string& formulaText) {
    // Get the cell
    Cell* cell = getCell(cellId);
    if (cell == nullptr) {
        return {false, "Cell not found"};
    }

    // Clear existing formula and dependencies
    clearCellFormula(cellId);

    // Validate formula starts with '='
    if (formulaText.empty() || formulaText[0] != '=') {
        return {false, "Formula must start with '='"};
    }

    // Parse the formula (but don't resolve references)
    FormulaParser parser(formulaText);
    std::unique_ptr<ASTNode> ast = parser.parse();

    // Create the formula
    auto* formula = new Formula(formulaText.c_str());
    formula->ast = ast.release();
    formula->dirty = true;
    cell->setFormula(formula);

    // Note: Not adding to dependency graph since refs aren't resolved
    // Caller should resolve and update deps separately if needed

    return {!parser.hasErrors(), parser.hasErrors() ? "Formula has syntax errors" : ""};
}

std::string Sheet::getCellFormulaText(const ID& cellId) const {
    const Cell* cell = nullptr;
    auto it = cells.find(cellId);
    if (it != cells.end()) {
        cell = it->second.get();
    }

    if (cell == nullptr || !cell->isFormula()) {
        return "";
    }

    const Formula* formula = cell->getFormula();
    if (formula == nullptr || formula->text == nullptr) {
        return "";
    }

    return formula->text;
}

void Sheet::clearCellFormula(const ID& cellId) {
    Cell* cell = getCell(cellId);
    if (cell == nullptr) {
        return;
    }

    // Remove from dependency graph
    _depGraph->removeFormula(cellId);
    _depGraph->unmarkVolatile(cellId);

    // Clear the formula
    cell->clearFormula();
}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook()
    : id(),
      name("Untitled"),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()) {}

Workbook::Workbook(const ID& id, std::string name)
    : id(id),
      name(std::move(name)),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()) {}

Workbook::~Workbook() = default;

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

bool Workbook::removeSheet(const ID& sheetId) {
    // Find the sheet
    auto it =
        std::find_if(sheets.begin(), sheets.end(),
                     [&sheetId](const std::unique_ptr<Sheet>& s) { return s->id == sheetId; });
    if (it == sheets.end()) {
        return false;
    }

    // Remove from index
    _sheetIndex.erase(sheetId);

    // Remove from vector
    sheets.erase(it);

    return true;
}

OpLog* Workbook::getOpLog() {
    return _oplog.get();
}

const OpLog* Workbook::getOpLog() const {
    return _oplog.get();
}

void Workbook::setNodeId(const ID& nodeId) {
    _nodeId = nodeId;
}

const ID& Workbook::getNodeId() const {
    return _nodeId;
}

HLC Workbook::getCurrentHLC() const {
    // If no node ID set, return zero HLC
    if (_nodeId.isNull()) {
        return {};
    }

    // Get the latest HLC from the OpLog or last generated
    const HLC oplog_hlc = _oplog->getCurrentHLC();
    const HLC base = (_lastHLC > oplog_hlc) ? _lastHLC : oplog_hlc;

    // Generate new HLC
    _lastHLC = generate_hlc(base, _nodeId);
    return _lastHLC;
}

// ============================================================================
// Workbook - Collaboration Mode
// ============================================================================

CollabMode Workbook::getCollabMode() const {
    return _collabMode;
}

void Workbook::setCollabMode(CollabMode mode) {
    _collabMode = mode;
}

bool Workbook::isCollaborating() const {
    return _collabMode == CollabMode::COLLABORATING;
}

void Workbook::startCollaboration() {
    if (_collabMode == CollabMode::COLLABORATING) {
        // Already collaborating, nothing to do
        return;
    }

    // Switch to collaboration mode
    _collabMode = CollabMode::COLLABORATING;

    // Note: OpLog bootstrap (generating operations for existing state) is done
    // by the WASM binding layer which calls bootstrapOpLog() from crdt.h
    // This avoids circular dependency between model and crdt.
}

Sheet* Workbook::getSheetByName(const std::string& sheetName) {
    for (auto& sheet : sheets) {
        if (sheet->name == sheetName) {
            return sheet.get();
        }
    }
    return nullptr;
}

const Sheet* Workbook::getSheetByName(const std::string& sheetName) const {
    for (const auto& sheet : sheets) {
        if (sheet->name == sheetName) {
            return sheet.get();
        }
    }
    return nullptr;
}

}  // namespace cells
