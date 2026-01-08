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

}  // namespace cells
