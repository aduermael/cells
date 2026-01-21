// =============================================================================
// Model Implementation
// =============================================================================
//
// Implementation of the core data model types: CellValue, Formula, Cell, Axis,
// and Workbook. Sheet implementation is in sheet.cc.
//
// Key responsibilities:
// - CellValue: Type-safe value storage with number/string/boolean/error conversions
// - Formula: AST ownership and volatile function detection
// - Cell: Value/formula cell management with shared formula support
// - Axis: Column/row metadata (position, size)
// - Workbook: Top-level container with sheets, OpLog, and collaboration mode
//
// Shared formula master/subscriber relationships are managed at Workbook level
// via SharedFormulaInfo and the _sharedFormulaMasters/_sharedFormulaFrom maps.
// Sheet methods delegate to Workbook for convenience.
//
// Dependencies: types.h, operation.h, oplog.h, formula_ast.h
// Used by: crdt.cc (applies operations), bindings.cc (WASM API), sheet.cc
//
// =============================================================================

#include "core/cells/model.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <utility>

#include "core/cells/dependency_graph.h"
#include "core/cells/format_registry.h"
#include "core/cells/formula_ast.h"
#include "core/cells/id.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/cells/style_registry.h"

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
    // Allow reading numbers from NUMBER and formula number types
    if (type != CellValueType::NUMBER && type != CellValueType::FORMULA_NUMBER) {
        return 0.0;
    }
    return std::strtod(raw.c_str(), nullptr);
}

bool CellValue::asBoolean() const {
    // Allow reading booleans from BOOLEAN and formula boolean types
    if (type != CellValueType::BOOLEAN && type != CellValueType::FORMULA_BOOLEAN) {
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

Formula::Formula() = default;

Formula::~Formula() {
    delete ast;
}

Formula::Formula(Formula&& other) noexcept : ast(other.ast), dirty(other.dirty) {
    other.ast = nullptr;
}

Formula& Formula::operator=(Formula&& other) noexcept {
    if (this != &other) {
        delete ast;
        ast = other.ast;
        dirty = other.dirty;
        other.ast = nullptr;
    }
    return *this;
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

Cell::Cell() : id(), colId(), rowId(), value(), formula(nullptr) {}

Cell::Cell(const ID& id) : id(id), colId(), rowId(), value(), formula(nullptr) {}

Cell::Cell(const ID& id, const ID& col, const ID& row)
    : id(id), colId(col), rowId(row), value(), formula(nullptr) {}

Cell::~Cell() {
    // Delete formula if we own it (subscribers don't have formulas, so formula is always owned)
    // Note: shared formula subscribers have formula = nullptr, so this is safe
    delete formula;
}

Cell::Cell(Cell&& other) noexcept
    : id(other.id),
      colId(other.colId),
      rowId(other.rowId),
      value(std::move(other.value)),
      formula(other.formula),
      _flags(other._flags) {
    other.formula = nullptr;
    other._flags = 0;
}

Cell& Cell::operator=(Cell&& other) noexcept {
    if (this != &other) {
        // Delete our formula (subscribers don't have formulas)
        delete formula;
        id = other.id;
        colId = other.colId;
        rowId = other.rowId;
        value = std::move(other.value);
        formula = other.formula;
        _flags = other._flags;
        other.formula = nullptr;
        other._flags = 0;
    }
    return *this;
}

bool Cell::isFormula() const {
    return formula != nullptr || hasFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
}

bool Cell::isSharedFormula() const {
    return hasFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
}

bool Cell::isSharedFormulaMaster() const {
    return hasFlag(CellFlags::SHARED_FORMULA_MASTER);
}

bool Cell::hasError() const {
    return value.error != CellError::NONE;
}

Formula* Cell::getFormula() const {
    // Returns own formula only. For shared formula subscribers, returns nullptr.
    // Use Sheet::getEffectiveFormula(cell) to get the effective formula.
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

void Cell::setSharedFormulaSubscriber(bool isSubscriber) {
    if (isSubscriber) {
        // Clear any existing formula
        clearFormula();
        value.type = CellValueType::FORMULA;
        setFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
    } else {
        clearFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
    }
}

void Cell::clearFormula() {
    // Delete formula if we have one (subscribers don't have formulas)
    delete formula;
    formula = nullptr;
    clearFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
    // Note: SHARED_FORMULA_MASTER flag is managed by Sheet-level methods
}

bool Cell::hasFlag(CellFlags flag) const {
    return (static_cast<uint8_t>(_flags) & static_cast<uint8_t>(flag)) != 0;
}

void Cell::setFlag(CellFlags flag) {
    _flags |= static_cast<uint8_t>(flag);
}

void Cell::clearFlag(CellFlags flag) {
    _flags &= ~static_cast<uint8_t>(flag);
}

// ============================================================================
// Axis
// ============================================================================

Axis::Axis()
    : name(),
      id(),
      sheetId(),
      position(0),
      size(DEFAULT_COLUMN_WIDTH),
      _flags(AxisFlags::IS_COLUMN) {}

Axis::Axis(const ID& id, bool isCol)
    : name(),
      id(id),
      sheetId(),
      position(0),
      size(isCol ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT),
      _flags(isCol ? AxisFlags::IS_COLUMN : AxisFlags::NONE) {}

Axis::Axis(const ID& id, const ID& sheetId, bool isCol)
    : name(),
      id(id),
      sheetId(sheetId),
      position(0),
      size(isCol ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT),
      _flags(isCol ? AxisFlags::IS_COLUMN : AxisFlags::NONE) {}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook()
    : id(),
      name("Untitled"),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()),
      _formatRegistry(std::make_unique<FormatRegistry>()),
      _styleRegistry(std::make_unique<StyleRegistry>()),
      _depGraph(std::make_unique<DependencyGraph>()) {}

Workbook::Workbook(const ID& id, std::string name)
    : id(id),
      name(std::move(name)),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()),
      _formatRegistry(std::make_unique<FormatRegistry>()),
      _styleRegistry(std::make_unique<StyleRegistry>()),
      _depGraph(std::make_unique<DependencyGraph>()) {}

Workbook::~Workbook() = default;

Sheet* Workbook::getSheet(const ID& sheetId) {
    auto it = _sheetIndex.find(sheetId);
    return (it != _sheetIndex.end()) ? it->second : nullptr;
}

const Sheet* Workbook::getSheet(const ID& sheetId) const {
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

    // Set parent workbook reference
    rawPtr->setWorkbook(this);

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

Sheet* Workbook::getSheetById(const ID& sheetId) {
    for (auto& sheet : sheets) {
        if (sheet->id == sheetId) {
            return sheet.get();
        }
    }
    return nullptr;
}

const Sheet* Workbook::getSheetById(const ID& sheetId) const {
    for (const auto& sheet : sheets) {
        if (sheet->id == sheetId) {
            return sheet.get();
        }
    }
    return nullptr;
}

// =============================================================================
// Workbook-level cell storage
// =============================================================================

Cell* Workbook::getCell(const ID& cellId) {
    auto it = _cells.find(cellId);
    return (it != _cells.end()) ? it->second.get() : nullptr;
}

const Cell* Workbook::getCell(const ID& cellId) const {
    auto it = _cells.find(cellId);
    return (it != _cells.end()) ? it->second.get() : nullptr;
}

Cell* Workbook::addCell(std::unique_ptr<Cell> cell) {
    if (!cell || cell->id.isNull()) {
        return nullptr;
    }

    // Check if cell with this ID already exists
    if (_cells.find(cell->id) != _cells.end()) {
        return nullptr;
    }

    const Cell* rawPtr = cell.get();
    _cells[cell->id] = std::move(cell);
    return _cells[rawPtr->id].get();
}

std::unique_ptr<Cell> Workbook::removeCell(const ID& cellId) {
    auto it = _cells.find(cellId);
    if (it == _cells.end()) {
        return nullptr;
    }

    std::unique_ptr<Cell> cell = std::move(it->second);
    _cells.erase(it);
    return cell;
}

// =============================================================================
// Cross-sheet cell lookup
// =============================================================================

Workbook::CellLookupResult Workbook::findCell(const ID& cellId) {
    // Look up cell directly from workbook storage
    Cell* cell = getCell(cellId);
    if (!cell) {
        return {nullptr, nullptr};
    }

    // Find the sheet via the column's sheetId (more reliable than checking _columnIds)
    const Axis* col = getColumn(cell->colId);
    if (col) {
        Sheet* sheet = getSheetById(col->sheetId);
        if (sheet) {
            return {cell, sheet};
        }
    }
    return {cell, nullptr};  // Cell exists but sheet not found (shouldn't happen)
}

std::pair<const Cell*, const Sheet*> Workbook::findCell(const ID& cellId) const {
    // Look up cell directly from workbook storage
    const Cell* cell = getCell(cellId);
    if (!cell) {
        return {nullptr, nullptr};
    }

    // Find the sheet via the column's sheetId (more reliable than checking _columnIds)
    const Axis* col = getColumn(cell->colId);
    if (col) {
        const Sheet* sheet = getSheetById(col->sheetId);
        if (sheet) {
            return {cell, sheet};
        }
    }
    return {cell, nullptr};  // Cell exists but sheet not found (shouldn't happen)
}

Sheet* Workbook::findAxisSheet(const ID& axisId) {
    // Try columns first
    const Axis* col = getColumn(axisId);
    if (col) {
        return getSheetById(col->sheetId);
    }
    // Try rows
    const Axis* row = getRow(axisId);
    if (row) {
        return getSheetById(row->sheetId);
    }
    return nullptr;
}

const Sheet* Workbook::findAxisSheet(const ID& axisId) const {
    // Try columns first
    const Axis* col = getColumn(axisId);
    if (col) {
        return getSheetById(col->sheetId);
    }
    // Try rows
    const Axis* row = getRow(axisId);
    if (row) {
        return getSheetById(row->sheetId);
    }
    return nullptr;
}

// =============================================================================
// Workbook-level axis storage
// =============================================================================

Axis* Workbook::getColumn(const ID& colId) {
    auto it = _columns.find(colId);
    return (it != _columns.end()) ? it->second.get() : nullptr;
}

const Axis* Workbook::getColumn(const ID& colId) const {
    auto it = _columns.find(colId);
    return (it != _columns.end()) ? it->second.get() : nullptr;
}

Axis* Workbook::getRow(const ID& rowId) {
    auto it = _rows.find(rowId);
    return (it != _rows.end()) ? it->second.get() : nullptr;
}

const Axis* Workbook::getRow(const ID& rowId) const {
    auto it = _rows.find(rowId);
    return (it != _rows.end()) ? it->second.get() : nullptr;
}

Axis* Workbook::addColumn(std::unique_ptr<Axis> col) {
    if (!col || col->id.isNull()) {
        return nullptr;
    }

    // Check if column with this ID already exists
    if (_columns.find(col->id) != _columns.end()) {
        return nullptr;
    }

    const Axis* rawPtr = col.get();
    _columns[col->id] = std::move(col);
    return _columns[rawPtr->id].get();
}

Axis* Workbook::addRow(std::unique_ptr<Axis> row) {
    if (!row || row->id.isNull()) {
        return nullptr;
    }

    // Check if row with this ID already exists
    if (_rows.find(row->id) != _rows.end()) {
        return nullptr;
    }

    const Axis* rawPtr = row.get();
    _rows[row->id] = std::move(row);
    return _rows[rawPtr->id].get();
}

std::unique_ptr<Axis> Workbook::removeColumn(const ID& colId) {
    auto it = _columns.find(colId);
    if (it == _columns.end()) {
        return nullptr;
    }

    std::unique_ptr<Axis> col = std::move(it->second);
    _columns.erase(it);
    return col;
}

std::unique_ptr<Axis> Workbook::removeRow(const ID& rowId) {
    auto it = _rows.find(rowId);
    if (it == _rows.end()) {
        return nullptr;
    }

    std::unique_ptr<Axis> row = std::move(it->second);
    _rows.erase(it);
    return row;
}

bool Workbook::registerCustomFormat(const ID& formatId, const std::string& formatCode) {
    return _formatRegistry->registerFormat(formatId, formatCode);
}

ID Workbook::findFormatByCode(const std::string& formatCode) const {
    // Lookup only, no registration - use for deduplication before FORMAT_DEFINE
    return _formatRegistry->findFormatByCode(formatCode);
}

bool Workbook::hasCustomFormat(const ID& formatId) const {
    return _formatRegistry->hasFormat(formatId);
}

std::string Workbook::getCustomFormatCode(const ID& formatId) const {
    return _formatRegistry->getFormatCode(formatId);
}

const std::unordered_map<ID, std::string, IDHash>& Workbook::getCustomFormats() const {
    return _formatRegistry->getFormats();
}

FormatRegistry* Workbook::getFormatRegistry() {
    return _formatRegistry.get();
}

const FormatRegistry* Workbook::getFormatRegistry() const {
    return _formatRegistry.get();
}

bool Workbook::registerStyle(const ID& styleId, const CellStyle& style) {
    // Direct registration with specific ID (for CRDT replay)
    return _styleRegistry->registerStyleDirect(styleId, style);
}

ID Workbook::findOrRegisterStyle(const CellStyle& style, bool* wasCreated) {
    // Content-addressed registration with deduplication
    // DEPRECATED: Use findStyleByContent + STYLE_DEFINE operation instead
    return _styleRegistry->registerStyle(style, ID(), wasCreated);
}

ID Workbook::findStyleByContent(const CellStyle& style) const {
    // Lookup only, no registration - use for deduplication before STYLE_DEFINE
    return _styleRegistry->findStyleByContent(style);
}

bool Workbook::hasStyle(const ID& styleId) const {
    return _styleRegistry->hasStyle(styleId);
}

const CellStyle* Workbook::getStyle(const ID& styleId) const {
    return _styleRegistry->getStyle(styleId);
}

const std::unordered_map<ID, CellStyle, IDHash>& Workbook::getStyles() const {
    return _styleRegistry->getStyles();
}

StyleRegistry* Workbook::getStyleRegistry() {
    return _styleRegistry.get();
}

const StyleRegistry* Workbook::getStyleRegistry() const {
    return _styleRegistry.get();
}

// =============================================================================
// Workbook-level dependency graph
// =============================================================================

DependencyGraph* Workbook::getDependencyGraph() {
    return _depGraph.get();
}

const DependencyGraph* Workbook::getDependencyGraph() const {
    return _depGraph.get();
}

// =============================================================================
// Entity format storage (unified: cells, axes, etc.)
// =============================================================================

ID Workbook::getFormatId(const ID& entityId) const {
    auto it = _formats.find(entityId);
    if (it != _formats.end()) {
        return it->second;
    }
    return {};  // null ID
}

ID Workbook::setFormatId(const ID& entityId, const ID& formatId) {
    // Get old format ID for reference counting
    ID oldFormatId;
    auto existing = _formats.find(entityId);
    if (existing != _formats.end()) {
        oldFormatId = existing->second;
    }

    // If clearing the format (null ID), remove from map
    if (formatId.isNull()) {
        if (existing != _formats.end()) {
            _formats.erase(existing);
        }
    } else {
        // Set format association
        _formats[entityId] = formatId;
    }

    // Update reference counts
    FormatRegistry* registry = getFormatRegistry();
    if (registry != nullptr) {
        // Release old format reference (if any)
        if (!oldFormatId.isNull()) {
            registry->release(oldFormatId);
        }
        // Add reference to new format (if not null)
        if (!formatId.isNull()) {
            registry->addRef(formatId);
        }
    }

    return oldFormatId;
}

bool Workbook::clearFormat(const ID& entityId) {
    auto it = _formats.find(entityId);
    if (it == _formats.end()) {
        return false;
    }

    // Release reference before erasing
    const ID formatId = it->second;
    _formats.erase(it);

    FormatRegistry* registry = getFormatRegistry();
    if (registry != nullptr && !formatId.isNull()) {
        registry->release(formatId);
    }

    return true;
}

// =============================================================================
// Entity style storage (unified: cells, axes, etc.)
// =============================================================================

ID Workbook::getStyleId(const ID& entityId) const {
    auto it = _styles.find(entityId);
    if (it != _styles.end()) {
        return it->second;
    }
    return {};  // null ID
}

ID Workbook::setStyleId(const ID& entityId, const ID& styleId) {
    // Get old style ID for reference counting
    ID oldStyleId;
    auto existing = _styles.find(entityId);
    if (existing != _styles.end()) {
        oldStyleId = existing->second;
    }

    // If clearing the style (null ID), remove from map
    if (styleId.isNull()) {
        if (existing != _styles.end()) {
            _styles.erase(existing);
        }
    } else {
        // Set style association
        _styles[entityId] = styleId;
    }

    // Update reference counts
    StyleRegistry* registry = getStyleRegistry();
    if (registry != nullptr) {
        // Release old style reference (if any)
        if (!oldStyleId.isNull()) {
            registry->release(oldStyleId);
        }
        // Add reference to new style (if not null)
        if (!styleId.isNull()) {
            registry->addRef(styleId);
        }
    }

    return oldStyleId;
}

bool Workbook::clearStyle(const ID& entityId) {
    auto it = _styles.find(entityId);
    if (it == _styles.end()) {
        return false;
    }

    // Release reference before erasing
    const ID styleId = it->second;
    _styles.erase(it);

    StyleRegistry* registry = getStyleRegistry();
    if (registry != nullptr && !styleId.isNull()) {
        registry->release(styleId);
    }

    return true;
}

// =============================================================================
// Workbook-level shared formula tracking
// =============================================================================

SharedFormulaInfo* Workbook::getSharedFormulaInfo(const ID& masterCellId) {
    auto it = _sharedFormulaMasters.find(masterCellId);
    return (it != _sharedFormulaMasters.end()) ? &it->second : nullptr;
}

const SharedFormulaInfo* Workbook::getSharedFormulaInfo(const ID& masterCellId) const {
    auto it = _sharedFormulaMasters.find(masterCellId);
    return (it != _sharedFormulaMasters.end()) ? &it->second : nullptr;
}

ID Workbook::getSharedFormulaMaster(const ID& subscriberId) const {
    auto it = _sharedFormulaFrom.find(subscriberId);
    return (it != _sharedFormulaFrom.end()) ? it->second : ID();
}

Formula* Workbook::getEffectiveFormula(Cell* cell) {
    if (cell == nullptr) {
        return nullptr;
    }

    // If cell has its own formula, return it
    if (cell->formula != nullptr) {
        return cell->formula;
    }

    // If cell is a shared formula subscriber, look up the master
    if (cell->hasFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER)) {
        const ID masterId = getSharedFormulaMaster(cell->id);
        if (!masterId.isNull()) {
            // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
            Cell* const master = getCell(masterId);
            if (master != nullptr) {
                return master->formula;
            }
        }
    }

    return nullptr;
}

const Formula* Workbook::getEffectiveFormula(const Cell* cell) const {
    if (cell == nullptr) {
        return nullptr;
    }

    // If cell has its own formula, return it
    if (cell->formula != nullptr) {
        return cell->formula;
    }

    // If cell is a shared formula subscriber, look up the master
    if (cell->hasFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER)) {
        const ID masterId = getSharedFormulaMaster(cell->id);
        if (!masterId.isNull()) {
            const Cell* master = getCell(masterId);
            if (master != nullptr) {
                return master->formula;
            }
        }
    }

    return nullptr;
}

bool Workbook::isInSharedFormulaGroup(const ID& cellId) const {
    // Check if it's a master
    if (_sharedFormulaMasters.find(cellId) != _sharedFormulaMasters.end()) {
        return true;
    }
    // Check if it's a subscriber
    return _sharedFormulaFrom.find(cellId) != _sharedFormulaFrom.end();
}

void Workbook::registerSharedFormulaGroup(const ID& masterCellId,
                                          const std::vector<ID>& subscriberIds) {
    // Clear any existing group for this master
    clearSharedFormulaGroup(masterCellId);

    // Create new shared formula info
    SharedFormulaInfo info(masterCellId);
    info.subscribers = subscriberIds;

    // Register in sharedFormulaMasters
    _sharedFormulaMasters[masterCellId] = std::move(info);

    // Build reverse lookup for each subscriber
    for (const ID& subId : subscriberIds) {
        _sharedFormulaFrom[subId] = masterCellId;
    }

    // Set the master flag on the master cell
    Cell* master = getCell(masterCellId);
    if (master != nullptr) {
        master->setFlag(CellFlags::SHARED_FORMULA_MASTER);
    }

    // Set subscriber flags on subscriber cells
    for (const ID& subId : subscriberIds) {
        Cell* sub = getCell(subId);
        if (sub != nullptr) {
            sub->setFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
        }
    }
}

void Workbook::addSharedFormulaSubscriber(const ID& masterCellId, const ID& subscriberId) {
    auto it = _sharedFormulaMasters.find(masterCellId);
    if (it == _sharedFormulaMasters.end()) {
        // No existing group - create one with just this subscriber
        SharedFormulaInfo info(masterCellId);
        info.subscribers.push_back(subscriberId);
        _sharedFormulaMasters[masterCellId] = std::move(info);

        // Set master flag
        Cell* master = getCell(masterCellId);
        if (master != nullptr) {
            master->setFlag(CellFlags::SHARED_FORMULA_MASTER);
        }
    } else {
        // Add to existing group
        it->second.subscribers.push_back(subscriberId);
    }

    // Add reverse lookup
    _sharedFormulaFrom[subscriberId] = masterCellId;

    // Set subscriber flag
    Cell* sub = getCell(subscriberId);
    if (sub != nullptr) {
        sub->setFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
    }
}

void Workbook::removeSharedFormulaSubscriber(const ID& subscriberId) {
    auto fromIt = _sharedFormulaFrom.find(subscriberId);
    if (fromIt == _sharedFormulaFrom.end()) {
        return;  // Not a subscriber
    }

    const ID masterId = fromIt->second;
    _sharedFormulaFrom.erase(fromIt);

    // Clear subscriber flag
    Cell* sub = getCell(subscriberId);
    if (sub != nullptr) {
        sub->clearFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
    }

    // Remove from master's subscriber list
    auto masterIt = _sharedFormulaMasters.find(masterId);
    if (masterIt != _sharedFormulaMasters.end()) {
        auto& subs = masterIt->second.subscribers;
        subs.erase(std::remove(subs.begin(), subs.end(), subscriberId), subs.end());

        // If no more subscribers, remove the group entirely
        if (subs.empty()) {
            Cell* master = getCell(masterId);
            if (master != nullptr) {
                master->clearFlag(CellFlags::SHARED_FORMULA_MASTER);
            }
            _sharedFormulaMasters.erase(masterIt);
        }
    }
}

void Workbook::clearSharedFormulaGroup(const ID& masterCellId) {
    auto it = _sharedFormulaMasters.find(masterCellId);
    if (it == _sharedFormulaMasters.end()) {
        return;
    }

    // Clear subscriber flags and reverse lookups
    for (const ID& subId : it->second.subscribers) {
        _sharedFormulaFrom.erase(subId);
        Cell* sub = getCell(subId);
        if (sub != nullptr) {
            sub->clearFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
        }
    }

    // Clear master flag
    Cell* master = getCell(masterCellId);
    if (master != nullptr) {
        master->clearFlag(CellFlags::SHARED_FORMULA_MASTER);
    }

    // Remove the master entry
    _sharedFormulaMasters.erase(it);
}

void Workbook::clearAllSharedFormulaGroups() {
    // Clear all flags before clearing the maps
    for (const auto& [masterId, info] : _sharedFormulaMasters) {
        Cell* master = getCell(masterId);
        if (master != nullptr) {
            master->clearFlag(CellFlags::SHARED_FORMULA_MASTER);
        }
        for (const ID& subId : info.subscribers) {
            Cell* sub = getCell(subId);
            if (sub != nullptr) {
                sub->clearFlag(CellFlags::SHARED_FORMULA_SUBSCRIBER);
            }
        }
    }

    _sharedFormulaMasters.clear();
    _sharedFormulaFrom.clear();
}

// =============================================================================
// Workbook-level spill range tracking
// =============================================================================

std::string Workbook::makePositionKey(const ID& colId, const ID& rowId) {
    // Simple composite key: colId + ":" + rowId
    return colId.toString() + ":" + rowId.toString();
}

SpillInfo* Workbook::getSpillInfo(const ID& masterCellId) {
    auto it = _spillMasters.find(masterCellId);
    return (it != _spillMasters.end()) ? &it->second : nullptr;
}

const SpillInfo* Workbook::getSpillInfo(const ID& masterCellId) const {
    auto it = _spillMasters.find(masterCellId);
    return (it != _spillMasters.end()) ? &it->second : nullptr;
}

ID Workbook::getSpillMaster(const ID& colId, const ID& rowId) const {
    auto key = makePositionKey(colId, rowId);
    auto it = _spilledFrom.find(key);
    return (it != _spilledFrom.end()) ? it->second : ID();
}

bool Workbook::isSpilledPosition(const ID& colId, const ID& rowId) const {
    auto key = makePositionKey(colId, rowId);
    return _spilledFrom.find(key) != _spilledFrom.end();
}

const CellValue* Workbook::getSpilledValue(const ID& colId, const ID& rowId) const {
    auto key = makePositionKey(colId, rowId);
    auto it = _spilledFrom.find(key);
    if (it == _spilledFrom.end()) {
        return nullptr;
    }

    const ID& masterId = it->second;
    const SpillInfo* info = getSpillInfo(masterId);
    if (info == nullptr) {
        return nullptr;
    }

    // Find the index in spilledPositions
    for (size_t i = 0; i < info->spilledPositions.size(); ++i) {
        const auto& [pColId, pRowId] = info->spilledPositions[i];
        if (pColId == colId && pRowId == rowId) {
            return (i < info->spilledValues.size()) ? &info->spilledValues[i] : nullptr;
        }
    }

    return nullptr;
}

void Workbook::registerSpillRange(const ID& masterCellId,
                                  const std::vector<std::pair<ID, ID>>& positions,
                                  const std::vector<CellValue>& values) {
    // Clear any existing spill for this master first
    clearSpillRange(masterCellId);

    // Create new spill info
    SpillInfo info(masterCellId);
    info.spilledPositions = positions;
    info.spilledValues = values;

    // Register in spillMasters
    _spillMasters[masterCellId] = std::move(info);

    // Set SPILL_MASTER flag on the master cell for O(1) lookup
    Cell* master = getCell(masterCellId);
    if (master != nullptr) {
        master->setFlag(CellFlags::SPILL_MASTER);
    }

    // Build reverse lookup for each spilled position
    // Note: SPILLED_FROM flag is not set on cells because spilled positions
    // are virtual (no actual Cell object) - we track them in the _spilledFrom map only
    for (const auto& [colId, rowId] : positions) {
        auto key = makePositionKey(colId, rowId);
        _spilledFrom[key] = masterCellId;
    }
}

void Workbook::clearSpillRange(const ID& masterCellId) {
    auto it = _spillMasters.find(masterCellId);
    if (it == _spillMasters.end()) {
        return;
    }

    // Remove all reverse lookups for this master's spilled positions
    for (const auto& [colId, rowId] : it->second.spilledPositions) {
        auto key = makePositionKey(colId, rowId);
        _spilledFrom.erase(key);
    }

    // Clear SPILL_MASTER flag on the master cell
    Cell* master = getCell(masterCellId);
    if (master != nullptr) {
        master->clearFlag(CellFlags::SPILL_MASTER);
    }

    // Remove the master entry
    _spillMasters.erase(it);
}

void Workbook::clearAllSpillRanges() {
    // Clear SPILL_MASTER flags on all master cells before clearing the maps
    for (const auto& [masterId, info] : _spillMasters) {
        Cell* master = getCell(masterId);
        if (master != nullptr) {
            master->clearFlag(CellFlags::SPILL_MASTER);
        }
    }

    _spillMasters.clear();
    _spilledFrom.clear();
}

// ============================================================================
// Workbook-Level Range Storage
// ============================================================================

Range* Workbook::getRange(const ID& rangeId) {
    auto it = _ranges.find(rangeId);
    return (it != _ranges.end()) ? it->second.get() : nullptr;
}

const Range* Workbook::getRange(const ID& rangeId) const {
    auto it = _ranges.find(rangeId);
    return (it != _ranges.end()) ? it->second.get() : nullptr;
}

Range* Workbook::addRange(std::unique_ptr<Range> range) {
    if (!range || range->id.isNull()) {
        return nullptr;
    }

    // Check for duplicate ID
    if (_ranges.find(range->id) != _ranges.end()) {
        return nullptr;
    }

    const Range* rawPtr = range.get();
    _ranges[range->id] = std::move(range);

    // Add to global range ID set
    _rangeIds.insert(rawPtr->id);

    return _ranges[rawPtr->id].get();
}

std::unique_ptr<Range> Workbook::removeRange(const ID& rangeId) {
    auto it = _ranges.find(rangeId);
    if (it == _ranges.end()) {
        return nullptr;
    }

    std::unique_ptr<Range> removed = std::move(it->second);
    _ranges.erase(it);

    // Remove from global range ID set
    _rangeIds.erase(rangeId);

    // Also remove any style association
    _rangeStyles.erase(rangeId);

    return removed;
}

std::vector<ID> Workbook::getRangeIdsForSheet(const ID& sheetId) const {
    std::vector<ID> result;
    for (const ID& rangeId : _rangeIds) {
        const Range* range = getRange(rangeId);
        if (range == nullptr) {
            continue;
        }
        // Check if this range belongs to the specified sheet by looking up its start column's
        // sheetId
        const Axis* startCol = getColumn(range->startColId);
        if (startCol != nullptr && startCol->sheetId == sheetId) {
            result.push_back(rangeId);
        }
    }
    return result;
}

// ============================================================================
// Workbook-Level Range Style Storage
// ============================================================================

ID Workbook::getRangeStyleId(const ID& rangeId) const {
    auto it = _rangeStyles.find(rangeId);
    if (it != _rangeStyles.end()) {
        return it->second;
    }
    return {};  // Return null ID if no style association
}

void Workbook::setRangeStyleId(const ID& rangeId, const ID& styleId) {
    // Get the range to update its flags
    Range* range = getRange(rangeId);
    if (!range) {
        return;  // Range doesn't exist
    }

    // Get the old style ID (if any) for reference counting
    const ID oldStyleId = getRangeStyleId(rangeId);

    if (styleId.isNull()) {
        // Remove style association
        _rangeStyles.erase(rangeId);
        range->flags = range->flags & ~RangeFlags::STYLE;
    } else {
        // Set style association
        _rangeStyles[rangeId] = styleId;
        range->flags = range->flags | RangeFlags::STYLE;
    }

    // Update reference counts
    StyleRegistry* registry = getStyleRegistry();
    if (registry != nullptr) {
        // Release old style reference (if any)
        if (!oldStyleId.isNull()) {
            registry->release(oldStyleId);
        }
        // Add reference to new style (if not null)
        if (!styleId.isNull()) {
            registry->addRef(styleId);
        }
    }
}

}  // namespace cells
