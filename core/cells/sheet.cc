// =============================================================================
// Sheet Implementation
// =============================================================================
//
// Implementation of the Sheet class, which represents a 2D grid of cells in a
// spreadsheet workbook. Provides cell and axis (column/row) management, formula
// handling, and coordinate conversion utilities.
//
// Key responsibilities:
// - Manage cell storage and lookup by ID or (colId, rowId) pair
// - Manage axis (column/row) storage and lookup by ID or position
// - Handle cell formula lifecycle and dependency graph integration
// - Convert between column positions and names (A, B, ..., Z, AA, ...)
// - Support axis operations: insert, delete, move, resize
//
// Dependencies: model.h, dependency_graph.h, formula_parser.h, formula_serializer.h
// Used by: crdt.cc (applies operations), bindings.cc (WASM API), persistence modules
//
// =============================================================================

#include <utility>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/range.h"
#include "core/cells/range_index.h"
#include "core/cells/style_registry.h"

namespace cells {

// Create a position resolver for a Sheet
// Returns (col, row) position for a cell ID, or (-1, -1) if not found
namespace {
PositionResolver makePositionResolver(Sheet* sheet) {
    return [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
        if (!sheet) {
            return {-1, -1};
        }

        const Cell* cell = sheet->getCell(cellId);
        if (!cell) {
            // Maybe it's a column or row ID, not a cell ID
            // Check columns first
            const Axis* col = sheet->getColumn(cellId);
            if (col) {
                return {static_cast<int32_t>(col->position), -1};
            }
            // Check rows
            const Axis* row = sheet->getRow(cellId);
            if (row) {
                return {-1, static_cast<int32_t>(row->position)};
            }
            return {-1, -1};
        }

        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (!col || !row) {
            return {-1, -1};
        }

        return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
    };
}
}  // namespace

// ============================================================================
// Sheet
// ============================================================================

Sheet::Sheet() : id(), name("Sheet1") {}

Sheet::Sheet(const ID& id, std::string name) : id(id), name(std::move(name)) {}

Sheet::~Sheet() = default;

Cell* Sheet::getCell(const ID& cellId) {
    // Delegate to workbook for actual cell storage
    // The cell belongs to this sheet if it exists and its column is in this sheet
    if (!_workbook) {
        return nullptr;
    }
    const Cell* cell = _workbook->getCell(cellId);
    if (!cell) {
        return nullptr;
    }
    // Verify the cell belongs to this sheet by checking its column
    if (columns.find(cell->colId) == columns.end()) {
        return nullptr;
    }
    return _workbook->getCell(cellId);
}

std::vector<ID> Sheet::getCellIds() const {
    std::vector<ID> ids;
    ids.reserve(_cellIndex.size());
    for (const auto& [key, cellId] : _cellIndex) {
        ids.push_back(cellId);
    }
    return ids;
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
    if (!cell || !_workbook) {
        return;
    }

    const ID& cellId = cell->id;
    const ID& colId = cell->colId;
    const ID& rowId = cell->rowId;

    // Update position index (colId:rowId -> cellId)
    auto key = makeCellKey(colId, rowId);
    _cellIndex[key] = cellId;

    // Store cell at workbook level (takes ownership)
    _workbook->addCell(std::move(cell));
}

void Sheet::reserveCells(size_t count) {
    _cellIndex.reserve(count);
}

void Sheet::removeCellFromIndex(const ID& cellId) {
    // Get the cell to find its position key
    if (!_workbook) {
        return;
    }
    const Cell* cell = _workbook->getCell(cellId);
    if (!cell) {
        return;
    }

    auto key = makeCellKey(cell->colId, cell->rowId);
    _cellIndex.erase(key);
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
    col->sheetId = id;  // Set the sheet ID for reverse lookup
    columns[col->id] = std::move(col);
}

void Sheet::addRow(std::unique_ptr<Axis> row) {
    if (!row) {
        return;
    }

    row->isColumn = false;
    row->sheetId = id;  // Set the sheet ID for reverse lookup
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

    // Create new column with sheet ID
    auto col = std::make_unique<Axis>(generate_id(), id, true);
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

    // Create new row with sheet ID
    auto row = std::make_unique<Axis>(generate_id(), id, false);
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

    // Rebuild R-tree with updated positions (positions are now stale)
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
    }

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

    // Rebuild R-tree with updated positions (positions are now stale)
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
    }

    return true;
}

Axis* Sheet::insertColumnAt(uint32_t position) {
    // Shift all columns at position or greater to the right
    for (auto& [colId, axis] : columns) {
        if (axis->position >= position) {
            axis->position++;
        }
    }

    // Create new column at the specified position with sheet ID
    auto col = std::make_unique<Axis>(generate_id(), id, true);
    col->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = col.get();
    addColumn(std::move(col));

    // Rebuild R-tree with updated positions
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
    }

    return rawPtr;
}

Axis* Sheet::insertRowAt(uint32_t position) {
    // Shift all rows at position or greater down
    for (auto& [rowId, axis] : rows) {
        if (axis->position >= position) {
            axis->position++;
        }
    }

    // Create new row at the specified position with sheet ID
    auto row = std::make_unique<Axis>(generate_id(), id, false);
    row->position = position;
    // NOLINTNEXTLINE(misc-const-correctness) - returned as non-const
    Axis* const rawPtr = row.get();
    addRow(std::move(row));

    // Rebuild R-tree with updated positions
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
    }

    return rawPtr;
}

bool Sheet::deleteColumn(const ID& colId) {
    auto it = columns.find(colId);
    if (it == columns.end()) {
        return false;
    }

    const uint32_t deletedPosition = it->second->position;

    // Delete all cells in this column
    // Collect keys and cell IDs to delete (can't modify _cellIndex while iterating)
    std::vector<std::pair<std::string, ID>> toDelete;
    for (const auto& [key, cellId] : _cellIndex) {
        const Cell* cell = _workbook ? _workbook->getCell(cellId) : nullptr;
        if (cell && cell->colId == colId) {
            toDelete.emplace_back(key, cellId);
        }
    }
    for (const auto& [key, cellId] : toDelete) {
        // Clear formula dependencies before removing
        clearCellFormula(cellId);
        // Remove from position index and workbook storage
        _cellIndex.erase(key);
        if (_workbook) {
            _workbook->removeCell(cellId);
        }
    }

    // Remove the column
    columns.erase(it);

    // Shift columns to the right of the deleted one left by 1
    for (auto& [id, axis] : columns) {
        if (axis->position > deletedPosition) {
            axis->position--;
        }
    }

    // Rebuild R-tree with updated positions
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
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
    // Collect keys and cell IDs to delete (can't modify _cellIndex while iterating)
    std::vector<std::pair<std::string, ID>> toDelete;
    for (const auto& [key, cellId] : _cellIndex) {
        const Cell* cell = _workbook ? _workbook->getCell(cellId) : nullptr;
        if (cell && cell->rowId == rowId) {
            toDelete.emplace_back(key, cellId);
        }
    }
    for (const auto& [key, cellId] : toDelete) {
        // Clear formula dependencies before removing
        clearCellFormula(cellId);
        // Remove from position index and workbook storage
        _cellIndex.erase(key);
        if (_workbook) {
            _workbook->removeCell(cellId);
        }
    }

    // Remove the row
    rows.erase(it);

    // Shift rows below the deleted one up by 1
    for (auto& [id, axis] : rows) {
        if (axis->position > deletedPosition) {
            axis->position--;
        }
    }

    // Rebuild R-tree with updated positions
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->rebuildRTree(makePositionResolver(this));
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

    // Create the formula - AST is the only storage, no text field
    auto* formula = new Formula();
    formula->ast = ast;  // Transfer ownership
    formula->dirty = true;
    cell->setFormula(formula);

    // Add to dependency graph (AST should already be resolved)
    // Use position resolver to populate R-tree for range queries
    DependencyGraph* depGraph = getDependencyGraph();
    if (ast != nullptr && depGraph != nullptr) {
        depGraph->addFormula(cellId, ast, makePositionResolver(this));

        // Track volatile functions
        if (formula->hasVolatile()) {
            depGraph->markVolatile(cellId);
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

    // Create the formula - AST is the only storage
    auto* formula = new Formula();
    formula->ast = ast.release();
    formula->dirty = true;
    cell->setFormula(formula);

    // Note: Not adding to dependency graph since refs aren't resolved
    // Caller should resolve and update deps separately if needed

    return {!parser.hasErrors(), parser.hasErrors() ? "Formula has syntax errors" : ""};
}

std::string Sheet::getCellFormulaText(const ID& cellId) const {
    // Get cell from workbook (getCell validates it belongs to this sheet)
    // Note: const_cast needed because getCell is non-const
    const Cell* cell = const_cast<Sheet*>(this)->getCell(cellId);

    if (cell == nullptr || !cell->isFormula()) {
        return "";
    }

    const Formula* formula = cell->getFormula();
    if (formula == nullptr || formula->ast == nullptr) {
        return "";
    }

    // Generate UUID-format text from AST
    return FormulaSerializer::serialize(formula->ast);
}

void Sheet::clearCellFormula(const ID& cellId) {
    Cell* cell = getCell(cellId);
    if (cell == nullptr) {
        return;
    }

    // Remove from dependency graph (uses workbook's global graph)
    DependencyGraph* depGraph = getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->removeFormula(cellId);
        depGraph->unmarkVolatile(cellId);
    }

    // Clear the formula
    cell->clearFormula();
}

DependencyGraph* Sheet::getDependencyGraph() {
    return _workbook ? _workbook->getDependencyGraph() : nullptr;
}

const DependencyGraph* Sheet::getDependencyGraph() const {
    return _workbook ? _workbook->getDependencyGraph() : nullptr;
}

// ============================================================================
// Spill Range Management (delegates to Workbook)
// ============================================================================

SpillInfo* Sheet::getSpillInfo(const ID& masterCellId) {
    return _workbook ? _workbook->getSpillInfo(masterCellId) : nullptr;
}

const SpillInfo* Sheet::getSpillInfo(const ID& masterCellId) const {
    return _workbook ? _workbook->getSpillInfo(masterCellId) : nullptr;
}

ID Sheet::getSpillMaster(const ID& colId, const ID& rowId) const {
    return _workbook ? _workbook->getSpillMaster(colId, rowId) : ID();
}

bool Sheet::isSpilledPosition(const ID& colId, const ID& rowId) const {
    return _workbook ? _workbook->isSpilledPosition(colId, rowId) : false;
}

const CellValue* Sheet::getSpilledValue(const ID& colId, const ID& rowId) const {
    return _workbook ? _workbook->getSpilledValue(colId, rowId) : nullptr;
}

void Sheet::registerSpillRange(const ID& masterCellId,
                               const std::vector<std::pair<ID, ID>>& positions,
                               const std::vector<CellValue>& values) {
    if (_workbook) {
        _workbook->registerSpillRange(masterCellId, positions, values);
    }
}

void Sheet::clearSpillRange(const ID& masterCellId) {
    if (_workbook) {
        _workbook->clearSpillRange(masterCellId);
    }
}

void Sheet::clearAllSpillRanges() {
    if (_workbook) {
        _workbook->clearAllSpillRanges();
    }
}

// ============================================================================
// Shared Formula Tracking (delegates to Workbook)
// ============================================================================

SharedFormulaInfo* Sheet::getSharedFormulaInfo(const ID& masterCellId) {
    return _workbook ? _workbook->getSharedFormulaInfo(masterCellId) : nullptr;
}

const SharedFormulaInfo* Sheet::getSharedFormulaInfo(const ID& masterCellId) const {
    return _workbook ? _workbook->getSharedFormulaInfo(masterCellId) : nullptr;
}

ID Sheet::getSharedFormulaMaster(const ID& subscriberId) const {
    return _workbook ? _workbook->getSharedFormulaMaster(subscriberId) : ID();
}

Formula* Sheet::getEffectiveFormula(Cell* cell) {
    return _workbook ? _workbook->getEffectiveFormula(cell) : nullptr;
}

const Formula* Sheet::getEffectiveFormula(const Cell* cell) const {
    return _workbook ? _workbook->getEffectiveFormula(cell) : nullptr;
}

bool Sheet::isInSharedFormulaGroup(const ID& cellId) const {
    return _workbook ? _workbook->isInSharedFormulaGroup(cellId) : false;
}

void Sheet::registerSharedFormulaGroup(const ID& masterCellId,
                                       const std::vector<ID>& subscriberIds) {
    if (_workbook) {
        _workbook->registerSharedFormulaGroup(masterCellId, subscriberIds);
    }
}

void Sheet::addSharedFormulaSubscriber(const ID& masterCellId, const ID& subscriberId) {
    if (_workbook) {
        _workbook->addSharedFormulaSubscriber(masterCellId, subscriberId);
    }
}

void Sheet::removeSharedFormulaSubscriber(const ID& subscriberId) {
    if (_workbook) {
        _workbook->removeSharedFormulaSubscriber(subscriberId);
    }
}

void Sheet::clearSharedFormulaGroup(const ID& masterCellId) {
    if (_workbook) {
        _workbook->clearSharedFormulaGroup(masterCellId);
    }
}

void Sheet::clearAllSharedFormulaGroups() {
    if (_workbook) {
        _workbook->clearAllSharedFormulaGroups();
    }
}

// ============================================================================
// Unified Range System
// ============================================================================

Range* Sheet::getRange(const ID& rangeId) {
    auto it = _ranges.find(rangeId);
    return (it != _ranges.end()) ? it->second.get() : nullptr;
}

const Range* Sheet::getRange(const ID& rangeId) const {
    auto it = _ranges.find(rangeId);
    return (it != _ranges.end()) ? it->second.get() : nullptr;
}

Range* Sheet::addRange(std::unique_ptr<Range> range) {
    if (!range || range->id.isNull()) {
        return nullptr;
    }

    // Check for duplicate ID
    if (_ranges.find(range->id) != _ranges.end()) {
        return nullptr;
    }

    Range* rangePtr = range.get();
    _ranges[range->id] = std::move(range);

    // Update spatial index
    updateRangeIndex(rangePtr);

    return rangePtr;
}

bool Sheet::removeRange(const ID& rangeId) {
    auto it = _ranges.find(rangeId);
    if (it == _ranges.end()) {
        return false;
    }

    // Remove from spatial index
    if (_rangeIndex) {
        _rangeIndex->removeById(rangeId);
    }

    // Release style reference if any
    const ID styleId = getRangeStyleId(rangeId);
    if (!styleId.isNull() && _workbook != nullptr) {
        StyleRegistry* registry = _workbook->getStyleRegistry();
        if (registry != nullptr) {
            registry->release(styleId);
        }
    }

    // Remove style association if any
    _rangeStyles.erase(rangeId);

    _ranges.erase(it);
    return true;
}

std::vector<Range*> Sheet::getRangesAt(uint32_t colPos, uint32_t rowPos) const {
    if (!_rangeIndex) {
        return {};
    }
    return _rangeIndex->queryAt(colPos, rowPos);
}

std::vector<Range*> Sheet::getRangesAt(uint32_t colPos, uint32_t rowPos,
                                       RangeFlags flagMask) const {
    if (!_rangeIndex) {
        return {};
    }
    return _rangeIndex->queryAt(colPos, rowPos, flagMask);
}

void Sheet::updateRangeIndex(Range* range) {
    if (!range) {
        return;
    }

    // Create index if it doesn't exist yet
    if (!_rangeIndex) {
        _rangeIndex = std::make_unique<RangeIndex>();
    }

    // Resolve corner UUIDs to positions
    const Axis* startCol = getColumn(range->startColId);
    const Axis* startRow = getRow(range->startRowId);
    const Axis* endCol = getColumn(range->endColId);
    const Axis* endRow = getRow(range->endRowId);

    // If any corner is missing, remove from index (range is temporarily invalid)
    if (!startCol || !startRow || !endCol || !endRow) {
        _rangeIndex->removeById(range->id);
        return;
    }

    // Normalize positions (ensure start <= end)
    const uint32_t minCol =
        startCol->position <= endCol->position ? startCol->position : endCol->position;
    const uint32_t maxCol =
        startCol->position <= endCol->position ? endCol->position : startCol->position;
    const uint32_t minRow =
        startRow->position <= endRow->position ? startRow->position : endRow->position;
    const uint32_t maxRow =
        startRow->position <= endRow->position ? endRow->position : startRow->position;

    // Check if already indexed (update bounds) or new (insert)
    const RangePositionBounds* existingBounds = _rangeIndex->getBounds(range->id);
    if (existingBounds) {
        _rangeIndex->updateBounds(range, minCol, minRow, maxCol, maxRow);
    } else {
        _rangeIndex->insert(range, minCol, minRow, maxCol, maxRow);
    }
}

void Sheet::clearAllRanges() {
    // Release all style references before clearing
    if (_workbook != nullptr) {
        StyleRegistry* registry = _workbook->getStyleRegistry();
        if (registry != nullptr) {
            for (const auto& [rangeId, styleId] : _rangeStyles) {
                if (!styleId.isNull()) {
                    registry->release(styleId);
                }
            }
        }
    }

    _ranges.clear();
    _rangeStyles.clear();
    if (_rangeIndex) {
        _rangeIndex->clear();
    }
}

// ============================================================================
// Range Style Mapping
// ============================================================================

ID Sheet::getRangeStyleId(const ID& rangeId) const {
    auto it = _rangeStyles.find(rangeId);
    if (it != _rangeStyles.end()) {
        return it->second;
    }
    return {};  // Return null ID if no style association
}

void Sheet::setRangeStyleId(const ID& rangeId, const ID& styleId) {
    // Get the range to update its flags
    Range* range = getRange(rangeId);
    if (!range) {
        return;  // Range doesn't exist
    }

    // Get style registry for reference counting
    StyleRegistry* registry = nullptr;
    if (_workbook != nullptr) {
        registry = _workbook->getStyleRegistry();
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
