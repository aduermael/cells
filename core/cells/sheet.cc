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

#include <algorithm>
#include <utility>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

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

    // Rebuild R-tree with updated positions (positions are now stale)
    _depGraph->rebuildRTree(makePositionResolver(this));

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
    _depGraph->rebuildRTree(makePositionResolver(this));

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

    // Rebuild R-tree with updated positions
    _depGraph->rebuildRTree(makePositionResolver(this));

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

    // Rebuild R-tree with updated positions
    _depGraph->rebuildRTree(makePositionResolver(this));

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

    // Rebuild R-tree with updated positions
    _depGraph->rebuildRTree(makePositionResolver(this));

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

    // Rebuild R-tree with updated positions
    _depGraph->rebuildRTree(makePositionResolver(this));

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
    if (ast != nullptr) {
        _depGraph->addFormula(cellId, ast, makePositionResolver(this));

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
    const Cell* cell = nullptr;
    auto it = cells.find(cellId);
    if (it != cells.end()) {
        cell = it->second.get();
    }

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

    // Remove from dependency graph
    _depGraph->removeFormula(cellId);
    _depGraph->unmarkVolatile(cellId);

    // Clear the formula
    cell->clearFormula();
}

// ============================================================================
// Spill Range Management
// ============================================================================

SpillInfo* Sheet::getSpillInfo(const ID& masterCellId) {
    auto it = _spillMasters.find(masterCellId);
    return (it != _spillMasters.end()) ? &it->second : nullptr;
}

const SpillInfo* Sheet::getSpillInfo(const ID& masterCellId) const {
    auto it = _spillMasters.find(masterCellId);
    return (it != _spillMasters.end()) ? &it->second : nullptr;
}

ID Sheet::getSpillMaster(const ID& colId, const ID& rowId) const {
    auto key = makeCellKey(colId, rowId);
    auto it = _spilledFrom.find(key);
    return (it != _spilledFrom.end()) ? it->second : ID();
}

bool Sheet::isSpilledPosition(const ID& colId, const ID& rowId) const {
    auto key = makeCellKey(colId, rowId);
    return _spilledFrom.find(key) != _spilledFrom.end();
}

const CellValue* Sheet::getSpilledValue(const ID& colId, const ID& rowId) const {
    auto key = makeCellKey(colId, rowId);
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

void Sheet::registerSpillRange(const ID& masterCellId,
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
        auto key = makeCellKey(colId, rowId);
        _spilledFrom[key] = masterCellId;
    }
}

void Sheet::clearSpillRange(const ID& masterCellId) {
    auto it = _spillMasters.find(masterCellId);
    if (it == _spillMasters.end()) {
        return;
    }

    // Remove all reverse lookups for this master's spilled positions
    for (const auto& [colId, rowId] : it->second.spilledPositions) {
        auto key = makeCellKey(colId, rowId);
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

void Sheet::clearAllSpillRanges() {
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
// Shared Formula Tracking
// ============================================================================

SharedFormulaInfo* Sheet::getSharedFormulaInfo(const ID& masterCellId) {
    auto it = _sharedFormulaMasters.find(masterCellId);
    return (it != _sharedFormulaMasters.end()) ? &it->second : nullptr;
}

const SharedFormulaInfo* Sheet::getSharedFormulaInfo(const ID& masterCellId) const {
    auto it = _sharedFormulaMasters.find(masterCellId);
    return (it != _sharedFormulaMasters.end()) ? &it->second : nullptr;
}

ID Sheet::getSharedFormulaMaster(const ID& subscriberId) const {
    auto it = _sharedFormulaFrom.find(subscriberId);
    return (it != _sharedFormulaFrom.end()) ? it->second : ID();
}

Formula* Sheet::getEffectiveFormula(Cell* cell) {
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

const Formula* Sheet::getEffectiveFormula(const Cell* cell) const {
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
            // Use const_cast since getCell is non-const but we return const Formula*
            auto* ncThis = const_cast<Sheet*>(this);
            const Cell* master = ncThis->getCell(masterId);
            if (master != nullptr) {
                return master->formula;
            }
        }
    }

    return nullptr;
}

bool Sheet::isInSharedFormulaGroup(const ID& cellId) const {
    // Check if it's a master
    if (_sharedFormulaMasters.find(cellId) != _sharedFormulaMasters.end()) {
        return true;
    }
    // Check if it's a subscriber
    return _sharedFormulaFrom.find(cellId) != _sharedFormulaFrom.end();
}

void Sheet::registerSharedFormulaGroup(const ID& masterCellId,
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

void Sheet::addSharedFormulaSubscriber(const ID& masterCellId, const ID& subscriberId) {
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

void Sheet::removeSharedFormulaSubscriber(const ID& subscriberId) {
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

void Sheet::clearSharedFormulaGroup(const ID& masterCellId) {
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

void Sheet::clearAllSharedFormulaGroups() {
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

// ============================================================================
// Merged Cells
// ============================================================================

const MergeRange* Sheet::getMergeRange(const ID& colId, const ID& rowId) const {
    auto key = makeCellKey(colId, rowId);
    auto it = _mergeIndex.find(key);
    if (it == _mergeIndex.end()) {
        return nullptr;
    }
    return &_mergeRanges[it->second];
}

bool Sheet::isMergeAnchor(const ID& colId, const ID& rowId) const {
    const MergeRange* range = getMergeRange(colId, rowId);
    if (range == nullptr) {
        return false;
    }
    return range->anchorColId == colId && range->anchorRowId == rowId;
}

bool Sheet::isMergedCell(const ID& colId, const ID& rowId) const {
    const MergeRange* range = getMergeRange(colId, rowId);
    if (range == nullptr) {
        return false;
    }
    // It's a merged (non-anchor) cell if we're in the range but not the anchor
    return !(range->anchorColId == colId && range->anchorRowId == rowId);
}

void Sheet::addMergeRange(const ID& anchorColId, const ID& anchorRowId, uint16_t colSpan,
                          uint16_t rowSpan) {
    // Validate span (must be at least 1 in each direction, and span more than 1 cell total)
    if (colSpan < 1 || rowSpan < 1 || (colSpan == 1 && rowSpan == 1)) {
        return;
    }

    // Get anchor column/row positions
    const Axis* anchorCol = getColumn(anchorColId);
    const Axis* anchorRow = getRow(anchorRowId);
    if (anchorCol == nullptr || anchorRow == nullptr) {
        return;
    }

    // Create the merge range
    MergeRange range(anchorColId, anchorRowId, colSpan, rowSpan);
    const size_t rangeIndex = _mergeRanges.size();
    _mergeRanges.push_back(range);

    // Build index entries for all cells in the merged region
    const uint32_t startColPos = anchorCol->position;
    const uint32_t startRowPos = anchorRow->position;

    for (uint32_t c = 0; c < colSpan; ++c) {
        const Axis* col = getColumnByPosition(startColPos + c);
        if (col == nullptr) {
            continue;
        }
        for (uint32_t r = 0; r < rowSpan; ++r) {
            const Axis* row = getRowByPosition(startRowPos + r);
            if (row == nullptr) {
                continue;
            }
            auto key = makeCellKey(col->id, row->id);
            _mergeIndex[key] = rangeIndex;
        }
    }
}

void Sheet::removeMergeRange(const ID& anchorColId, const ID& anchorRowId) {
    // Find the merge range by its anchor
    auto anchorKey = makeCellKey(anchorColId, anchorRowId);
    auto it = _mergeIndex.find(anchorKey);
    if (it == _mergeIndex.end()) {
        return;
    }

    const size_t indexToRemove = it->second;
    const MergeRange& range = _mergeRanges[indexToRemove];

    // Only allow removing by the actual anchor
    if (range.anchorColId != anchorColId || range.anchorRowId != anchorRowId) {
        return;
    }

    // Get anchor positions
    const Axis* anchorCol = getColumn(anchorColId);
    const Axis* anchorRow = getRow(anchorRowId);
    if (anchorCol == nullptr || anchorRow == nullptr) {
        return;
    }

    const uint32_t startColPos = anchorCol->position;
    const uint32_t startRowPos = anchorRow->position;

    // Remove all index entries for this merge range
    for (uint32_t c = 0; c < range.colSpan; ++c) {
        const Axis* col = getColumnByPosition(startColPos + c);
        if (col == nullptr) {
            continue;
        }
        for (uint32_t r = 0; r < range.rowSpan; ++r) {
            const Axis* row = getRowByPosition(startRowPos + r);
            if (row == nullptr) {
                continue;
            }
            auto key = makeCellKey(col->id, row->id);
            _mergeIndex.erase(key);
        }
    }

    // Remove from the vector (swap with last and pop for efficiency)
    if (indexToRemove != _mergeRanges.size() - 1) {
        // Swap with last element
        _mergeRanges[indexToRemove] = std::move(_mergeRanges.back());

        // Update all index entries that pointed to the moved element
        const MergeRange& movedRange = _mergeRanges[indexToRemove];
        const Axis* movedAnchorCol = getColumn(movedRange.anchorColId);
        const Axis* movedAnchorRow = getRow(movedRange.anchorRowId);
        if (movedAnchorCol != nullptr && movedAnchorRow != nullptr) {
            const uint32_t movedColPos = movedAnchorCol->position;
            const uint32_t movedRowPos = movedAnchorRow->position;

            for (uint32_t c = 0; c < movedRange.colSpan; ++c) {
                const Axis* col = getColumnByPosition(movedColPos + c);
                if (col == nullptr) {
                    continue;
                }
                for (uint32_t r = 0; r < movedRange.rowSpan; ++r) {
                    const Axis* row = getRowByPosition(movedRowPos + r);
                    if (row == nullptr) {
                        continue;
                    }
                    auto key = makeCellKey(col->id, row->id);
                    _mergeIndex[key] = indexToRemove;
                }
            }
        }
    }

    _mergeRanges.pop_back();
}

void Sheet::clearAllMergeRanges() {
    _mergeRanges.clear();
    _mergeIndex.clear();
}

}  // namespace cells
