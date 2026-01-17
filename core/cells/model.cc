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
// Shared formula master/subscriber relationships are managed at Sheet level
// via SharedFormulaInfo and the _sharedFormulaMasters/_sharedFormulaFrom maps.
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

#include "core/cells/formula_ast.h"
#include "core/cells/id.h"
#include "core/cells/named_ranges.h"
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

Axis::Axis() : name(), id(), sheetId(), position(0), size(DEFAULT_COLUMN_WIDTH), isColumn(true) {}

Axis::Axis(const ID& id, bool isColumn)
    : name(),
      id(id),
      sheetId(),
      position(0),
      size(isColumn ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT),
      isColumn(isColumn) {}

Axis::Axis(const ID& id, const ID& sheetId, bool isColumn)
    : name(),
      id(id),
      sheetId(sheetId),
      position(0),
      size(isColumn ? DEFAULT_COLUMN_WIDTH : DEFAULT_ROW_HEIGHT),
      isColumn(isColumn) {}

// ============================================================================
// Workbook
// ============================================================================

Workbook::Workbook()
    : id(),
      name("Untitled"),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()),
      _styleRegistry(std::make_unique<StyleRegistry>()) {}

Workbook::Workbook(const ID& id, std::string name)
    : id(id),
      name(std::move(name)),
      _oplog(std::make_unique<OpLog>()),
      _namedRanges(std::make_unique<NamedRangeRegistry>()),
      _nodeId(generate_id()),
      _styleRegistry(std::make_unique<StyleRegistry>()) {}

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

    Cell* rawPtr = cell.get();
    _cells[cell->id] = std::move(cell);
    return rawPtr;
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

    // Find the sheet by looking up the column's sheetId
    for (auto& sheet : sheets) {
        Axis* col = sheet->getColumn(cell->colId);
        if (col) {
            return {cell, sheet.get()};
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

    // Find the sheet by looking up the column's sheetId
    for (const auto& sheet : sheets) {
        const Axis* col = sheet->getColumn(cell->colId);
        if (col) {
            return {cell, sheet.get()};
        }
    }
    return {cell, nullptr};  // Cell exists but sheet not found (shouldn't happen)
}

Sheet* Workbook::findAxisSheet(const ID& axisId) {
    for (auto& sheet : sheets) {
        // Check columns
        if (sheet->getColumn(axisId)) {
            return sheet.get();
        }
        // Check rows
        if (sheet->getRow(axisId)) {
            return sheet.get();
        }
    }
    return nullptr;
}

const Sheet* Workbook::findAxisSheet(const ID& axisId) const {
    for (const auto& sheet : sheets) {
        // Check columns
        if (sheet->getColumn(axisId)) {
            return sheet.get();
        }
        // Check rows
        if (sheet->getRow(axisId)) {
            return sheet.get();
        }
    }
    return nullptr;
}

bool Workbook::registerCustomFormat(const ID& formatId, const std::string& formatCode) {
    auto [it, inserted] = _customFormats.try_emplace(formatId, formatCode);
    return inserted;
}

bool Workbook::hasCustomFormat(const ID& formatId) const {
    return _customFormats.find(formatId) != _customFormats.end();
}

std::string Workbook::getCustomFormatCode(const ID& formatId) const {
    auto it = _customFormats.find(formatId);
    if (it != _customFormats.end()) {
        return it->second;
    }
    return "";
}

const std::unordered_map<ID, std::string, IDHash>& Workbook::getCustomFormats() const {
    return _customFormats;
}

bool Workbook::registerStyle(const ID& styleId, const CellStyle& style) {
    // Direct registration with specific ID (for CRDT replay)
    return _styleRegistry->registerStyleDirect(styleId, style);
}

ID Workbook::findOrRegisterStyle(const CellStyle& style, bool* wasCreated) {
    // Content-addressed registration with deduplication
    return _styleRegistry->registerStyle(style, ID(), wasCreated);
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
// Cell format storage
// =============================================================================

ID Workbook::getCellFormatId(const ID& cellId) const {
    auto it = _cellFormats.find(cellId);
    if (it != _cellFormats.end()) {
        return it->second;
    }
    return {};  // null ID
}

ID Workbook::setCellFormatId(const ID& cellId, const ID& formatId) {
    // If clearing the format (null ID), remove from map
    if (formatId.isNull()) {
        auto it = _cellFormats.find(cellId);
        if (it != _cellFormats.end()) {
            ID oldId = it->second;
            _cellFormats.erase(it);
            return oldId;
        }
        return {};  // No previous format
    }

    // Check if exists first, get old value before overwriting
    auto existing = _cellFormats.find(cellId);
    if (existing != _cellFormats.end()) {
        ID oldId = existing->second;
        existing->second = formatId;
        return oldId;
    }
    _cellFormats[cellId] = formatId;
    return {};  // No previous format
}

bool Workbook::clearCellFormat(const ID& cellId) {
    return _cellFormats.erase(cellId) > 0;
}

// =============================================================================
// Cell style storage
// =============================================================================

ID Workbook::getCellStyleId(const ID& cellId) const {
    auto it = _cellStyles.find(cellId);
    if (it != _cellStyles.end()) {
        return it->second;
    }
    return {};  // null ID
}

ID Workbook::setCellStyleId(const ID& cellId, const ID& styleId) {
    // If clearing the style (null ID), remove from map
    if (styleId.isNull()) {
        auto it = _cellStyles.find(cellId);
        if (it != _cellStyles.end()) {
            ID oldId = it->second;
            _cellStyles.erase(it);
            return oldId;
        }
        return {};  // No previous style
    }

    // Check if exists first, get old value before overwriting
    auto existing = _cellStyles.find(cellId);
    if (existing != _cellStyles.end()) {
        ID oldId = existing->second;
        existing->second = styleId;
        return oldId;
    }
    _cellStyles[cellId] = styleId;
    return {};  // No previous style
}

bool Workbook::clearCellStyle(const ID& cellId) {
    return _cellStyles.erase(cellId) > 0;
}

// =============================================================================
// Cross-sheet dependency tracking
// =============================================================================

void Workbook::addCrossSheetDep(const ID& sourceCellId, const ID& formulaSheetId,
                                const ID& formulaCellId) {
    // Add to forward index (source -> formula)
    const CrossSheetDep dep{formulaSheetId, formulaCellId};
    _crossSheetDeps[sourceCellId].push_back(dep);

    // Add to reverse index (formula -> source) for cleanup
    _crossSheetDepReverse[formulaCellId].push_back(sourceCellId);
}

void Workbook::addCrossSheetRangeDep(const ID& sourceSheetId, const ID& startColId,
                                     const ID& startRowId, const ID& endColId, const ID& endRowId,
                                     const ID& formulaSheetId, const ID& formulaCellId) {
    const CrossSheetRangeDep dep{sourceSheetId, startColId,     startRowId,   endColId,
                                 endRowId,      formulaSheetId, formulaCellId};
    _crossSheetRangeDeps[formulaCellId].push_back(dep);
}

void Workbook::removeCrossSheetDeps(const ID& formulaCellId) {
    // Look up what source cells this formula depends on
    auto revIt = _crossSheetDepReverse.find(formulaCellId);
    if (revIt != _crossSheetDepReverse.end()) {
        // Remove from forward index for each source cell
        for (const ID& sourceCellId : revIt->second) {
            auto fwdIt = _crossSheetDeps.find(sourceCellId);
            if (fwdIt != _crossSheetDeps.end()) {
                auto& deps = fwdIt->second;
                deps.erase(std::remove_if(deps.begin(), deps.end(),
                                          [&formulaCellId](const CrossSheetDep& d) {
                                              return d.formulaCellId == formulaCellId;
                                          }),
                           deps.end());
                // Clean up empty vectors
                if (deps.empty()) {
                    _crossSheetDeps.erase(fwdIt);
                }
            }
        }

        // Remove from reverse index
        _crossSheetDepReverse.erase(revIt);
    }

    // Also remove range dependencies for this formula cell
    _crossSheetRangeDeps.erase(formulaCellId);
}

std::vector<Workbook::CrossSheetDep> Workbook::getCrossSheetDependents(
    const ID& sourceCellId) const {
    auto it = _crossSheetDeps.find(sourceCellId);
    if (it != _crossSheetDeps.end()) {
        return it->second;
    }
    return {};
}

std::vector<Workbook::CrossSheetDep> Workbook::getCrossSheetRangeDependents(
    const ID& changedSheetId, const ID& changedColId, const ID& changedRowId) const {
    std::vector<CrossSheetDep> result;

    // Get the sheet to look up positions
    // const_cast is safe here because getColumn/getRow are read-only lookups
    Sheet* changedSheet = const_cast<Workbook*>(this)->getSheetById(changedSheetId);
    if (!changedSheet) {
        return result;
    }

    // Get the position of the changed cell
    const Axis* changedCol = changedSheet->getColumn(changedColId);
    const Axis* changedRow = changedSheet->getRow(changedRowId);
    if (!changedCol || !changedRow) {
        return result;
    }
    const uint32_t changedColPos = changedCol->position;
    const uint32_t changedRowPos = changedRow->position;

    // Check all range dependencies
    for (const auto& [formulaCellId, rangeDeps] : _crossSheetRangeDeps) {
        for (const auto& dep : rangeDeps) {
            // Only check ranges on the same sheet where the change occurred
            if (dep.sourceSheetId != changedSheetId) {
                continue;
            }

            // Get the target sheet to look up range positions
            // const_cast is safe here because getColumn/getRow are read-only lookups
            Sheet* targetSheet = const_cast<Workbook*>(this)->getSheetById(dep.sourceSheetId);
            if (!targetSheet) {
                continue;
            }

            // Get range corner positions
            const Axis* startCol = targetSheet->getColumn(dep.startColId);
            const Axis* startRow = targetSheet->getRow(dep.startRowId);
            const Axis* endCol = targetSheet->getColumn(dep.endColId);
            const Axis* endRow = targetSheet->getRow(dep.endRowId);

            if (!startCol || !startRow || !endCol || !endRow) {
                continue;
            }

            const uint32_t minCol = std::min(startCol->position, endCol->position);
            const uint32_t maxCol = std::max(startCol->position, endCol->position);
            const uint32_t minRow = std::min(startRow->position, endRow->position);
            const uint32_t maxRow = std::max(startRow->position, endRow->position);

            // Check if the changed cell is within this range
            if (changedColPos >= minCol && changedColPos <= maxCol && changedRowPos >= minRow &&
                changedRowPos <= maxRow) {
                result.push_back({dep.formulaSheetId, dep.formulaCellId});
            }
        }
    }

    return result;
}

}  // namespace cells
