#include "core/cells/formula_recalc.h"

#include <unordered_set>
#include <vector>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_eval.h"
#include "core/cells/model.h"

namespace cells {

// =============================================================================
// Range Membership Helpers
// =============================================================================

// Check if a cell is within the range defined by two corner cells
static bool isCellInRange(Sheet* sheet, const ID& cellId, const ID& rangeStartCellId,
                          const ID& rangeEndCellId) {
    // Get the cell's column and row
    const Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        return false;
    }

    const Axis* cellCol = sheet->getColumn(cell->colId);
    const Axis* cellRow = sheet->getRow(cell->rowId);
    if (!cellCol || !cellRow) {
        return false;
    }

    // Get the range start cell's column and row
    const Cell* startCell = sheet->getCell(rangeStartCellId);
    const Cell* endCell = sheet->getCell(rangeEndCellId);
    if (!startCell || !endCell) {
        return false;
    }

    const Axis* startCol = sheet->getColumn(startCell->colId);
    const Axis* startRow = sheet->getRow(startCell->rowId);
    const Axis* endCol = sheet->getColumn(endCell->colId);
    const Axis* endRow = sheet->getRow(endCell->rowId);

    if (!startCol || !startRow || !endCol || !endRow) {
        return false;
    }

    // Get positions and check bounds
    const uint32_t minCol = std::min(startCol->position, endCol->position);
    const uint32_t maxCol = std::max(startCol->position, endCol->position);
    const uint32_t minRow = std::min(startRow->position, endRow->position);
    const uint32_t maxRow = std::max(startRow->position, endRow->position);

    return cellCol->position >= minCol && cellCol->position <= maxCol &&
           cellRow->position >= minRow && cellRow->position <= maxRow;
}

// Get all formula cells that depend on a given cell (including range dependencies)
static std::vector<ID> getDependentsWithRanges(Sheet* sheet, const ID& cellId) {
    const DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) {
        return {};
    }

    std::vector<ID> dependents;

    // Iterate through all formula cells and check their dependencies
    for (const auto& [formulaCellId, cellPtr] : sheet->cells) {
        if (!cellPtr->isFormula()) {
            continue;
        }

        auto deps = depGraph->getDependencies(formulaCellId);
        for (const auto& ref : deps) {
            bool matches = false;
            switch (ref.type) {
                case DependencyRef::Type::CELL:
                    matches = (ref.cellId == cellId);
                    break;
                case DependencyRef::Type::RANGE:
                    // Check if cellId is within the range
                    matches = (ref.startCellId == cellId || ref.endCellId == cellId ||
                               isCellInRange(sheet, cellId, ref.startCellId, ref.endCellId));
                    break;
                case DependencyRef::Type::COLUMN:
                case DependencyRef::Type::ROW:
                case DependencyRef::Type::COLUMN_RANGE:
                case DependencyRef::Type::ROW_RANGE:
                    // Column/row refs would need additional handling
                    // For now, skip these
                    break;
            }
            if (matches) {
                dependents.push_back(formulaCellId);
                break;  // Only add once per formula cell
            }
        }
    }

    return dependents;
}

// =============================================================================
// Single-Cell Evaluation
// =============================================================================

EvalResult evaluateCell(Sheet* sheet, Cell* cell) {
    if (!sheet || !cell) {
        return EvalResult::Error(CellError::VALUE);
    }

    // If cell doesn't have a formula, return its current value
    Formula* formula = cell->getFormula();
    if (!formula || !formula->ast) {
        // Convert cell value to EvalResult
        switch (cell->value.type) {
            case CellValueType::NUMBER:
                return EvalResult::Number(cell->value.asNumber());
            case CellValueType::STRING:
                if (cell->value.raw.empty()) {
                    return EvalResult::Empty();
                }
                return EvalResult::String(cell->value.asString());
            case CellValueType::BOOLEAN:
                return EvalResult::Boolean(cell->value.asBoolean());
            case CellValueType::ERROR:
                return EvalResult::Error(cell->value.error);
            case CellValueType::DATE:
            case CellValueType::DATE_TIME:
                return EvalResult::Number(cell->value.asNumber());
            case CellValueType::FORMULA:
                // Formula type but no AST means unparsed or error
                if (cell->value.error != CellError::NONE) {
                    return EvalResult::Error(cell->value.error);
                }
                return EvalResult::Empty();
        }
        return EvalResult::Empty();
    }

    // Set up evaluation context
    std::unordered_set<ID> evaluatingCells;
    evaluatingCells.insert(cell->id);  // Mark current cell as being evaluated

    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = nullptr;  // TODO: Add workbook parameter for cross-sheet refs
    ctx.currentCellId = cell->id;
    ctx.evaluatingCells = &evaluatingCells;
    ctx.recursionDepth = 0;

    // Evaluate the formula
    EvalResult result = evaluate(formula->ast, ctx);

    // Store the result in the cell's value
    if (result.isError()) {
        cell->value = CellValue(result.getError());
    } else if (result.isNumber()) {
        cell->value = CellValue(result.getNumber());
    } else if (result.isString()) {
        cell->value = CellValue(result.getString());
    } else if (result.isBoolean()) {
        cell->value = CellValue(result.getBoolean());
    } else if (result.isEmpty()) {
        cell->value = CellValue("");
    } else {
        // Range or other type - shouldn't happen for cell result
        cell->value = CellValue(CellError::VALUE);
        result = EvalResult::Error(CellError::VALUE);
    }

    // Mark formula as clean
    formula->dirty = false;

    return result;
}

EvalResult evaluateCell(Sheet* sheet, const ID& cellId) {
    if (!sheet) {
        return EvalResult::Error(CellError::VALUE);
    }

    Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        // Non-existent cell is treated as empty/0
        return EvalResult::Number(0.0);
    }

    return evaluateCell(sheet, cell);
}

// =============================================================================
// Batch Recalculation
// =============================================================================

void recalculate(Sheet* sheet, const std::vector<ID>& changedCells) {
    if (!sheet || changedCells.empty()) {
        return;
    }

    const DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) {
        return;
    }

    // Build the full set of cells to recalculate
    // Start with changed cells and expand to include all dependents (with range checking)
    std::unordered_set<ID> toRecalc(changedCells.begin(), changedCells.end());
    std::vector<ID> queue = changedCells;

    while (!queue.empty()) {
        const ID cellId = queue.back();
        queue.pop_back();

        auto dependents = getDependentsWithRanges(sheet, cellId);
        for (const ID& dep : dependents) {
            if (toRecalc.insert(dep).second) {
                queue.push_back(dep);
            }
        }
    }

    // Get recalculation order (topological sort of dependents)
    const std::vector<ID> toRecalcVec(toRecalc.begin(), toRecalc.end());
    bool hasCycle = false;
    const std::vector<ID> recalcOrder = depGraph->getRecalcOrder(toRecalcVec, &hasCycle);

    if (hasCycle) {
        // When there's a cycle, getRecalcOrder returns empty
        // We need to find and mark all cells involved in circular references
        // Use cycle detection for each cell that may be in the cycle
        std::unordered_set<ID> cellsInCycle;

        // Check each cell for cycles
        for (const ID& cellId : toRecalc) {
            auto cycle = depGraph->detectCycle(cellId);
            if (!cycle.empty()) {
                for (const ID& cycleCell : cycle) {
                    cellsInCycle.insert(cycleCell);
                }
            }
        }

        // Mark cells in cycles with error
        for (const ID& cellId : cellsInCycle) {
            Cell* cell = sheet->getCell(cellId);
            if (cell) {
                cell->value = CellValue(CellError::CIRCULAR);
                Formula* formula = cell->getFormula();
                if (formula) {
                    formula->dirty = false;
                }
            }
        }
        return;
    }

    // Evaluate each cell in dependency order
    for (const ID& cellId : recalcOrder) {
        Cell* cell = sheet->getCell(cellId);
        if (cell && cell->isFormula()) {
            evaluateCell(sheet, cell);
        }
    }
}

// =============================================================================
// Volatile Cell Recalculation
// =============================================================================

void recalculateVolatile(Sheet* sheet) {
    if (!sheet) {
        return;
    }

    const DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) {
        return;
    }

    // Get all volatile cells
    const std::vector<ID> volatileCells = depGraph->getVolatileCells();
    if (volatileCells.empty()) {
        return;
    }

    // Mark volatile cells as dirty
    for (const ID& cellId : volatileCells) {
        const Cell* cell = sheet->getCell(cellId);
        if (cell) {
            Formula* formula = cell->getFormula();
            if (formula) {
                formula->dirty = true;
            }
        }
    }

    // Recalculate volatile cells and their dependents
    recalculate(sheet, volatileCells);
}

// =============================================================================
// Dirty Cell Management
// =============================================================================

void markDirty(Sheet* sheet, const ID& cellId) {
    if (!sheet) {
        return;
    }

    // Transitively mark all cells that depend on this cell (directly or indirectly)
    // Uses range-aware dependency checking
    std::unordered_set<ID> marked;
    std::vector<ID> queue = getDependentsWithRanges(sheet, cellId);

    while (!queue.empty()) {
        const ID depId = queue.back();
        queue.pop_back();

        if (marked.count(depId) > 0) {
            continue;  // Already processed
        }
        marked.insert(depId);

        const Cell* cell = sheet->getCell(depId);
        if (cell) {
            Formula* formula = cell->getFormula();
            if (formula && !formula->dirty) {
                formula->dirty = true;

                // Also mark cells that depend on this one
                auto transitiveDeps = getDependentsWithRanges(sheet, depId);
                for (const ID& transDep : transitiveDeps) {
                    if (marked.count(transDep) == 0) {
                        queue.push_back(transDep);
                    }
                }
            }
        }
    }
}

bool hasDirtyCells(Sheet* sheet) {
    if (!sheet) {
        return false;
    }

    for (const auto& [id, cell] : sheet->cells) {
        const Formula* formula = cell->getFormula();
        if (formula && formula->dirty) {
            return true;
        }
    }
    return false;
}

std::vector<ID> getDirtyCells(Sheet* sheet) {
    std::vector<ID> dirtyCells;
    if (!sheet) {
        return dirtyCells;
    }

    const DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) {
        return dirtyCells;
    }

    // Collect all dirty cells
    for (const auto& [id, cell] : sheet->cells) {
        const Formula* formula = cell->getFormula();
        if (formula && formula->dirty) {
            dirtyCells.push_back(id);
        }
    }

    // Return them in recalculation order
    if (!dirtyCells.empty()) {
        bool hasCycle = false;
        std::vector<ID> ordered = depGraph->getRecalcOrder(dirtyCells, &hasCycle);
        if (!hasCycle) {
            return ordered;
        }
    }

    return dirtyCells;
}

}  // namespace cells
