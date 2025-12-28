#include "core/cells/formula_recalc.h"

#include <string>
#include <unordered_set>
#include <vector>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_eval.h"
#include "core/cells/model.h"

namespace cells {

// =============================================================================
// Dependency Lookup Helpers
// =============================================================================

// Get all formula cells that depend on a given cell (including range dependencies)
// Uses optimized O(1) reverse index + O(log n) R-tree queries
static std::vector<ID> getDependentsWithRanges(Sheet* sheet, const ID& cellId) {
    const DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) {
        return {};
    }

    // Get the cell's position for R-tree query
    const Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        // Cell doesn't exist - just use direct deps lookup
        return depGraph->getDependents(cellId);
    }

    const Axis* col = sheet->getColumn(cell->colId);
    const Axis* row = sheet->getRow(cell->rowId);
    if (!col || !row) {
        // Can't determine position - fall back to direct deps only
        return depGraph->getDependents(cellId);
    }

    // Use optimized combined lookup:
    // - O(1) reverse index for direct cell references
    // - O(log n) R-tree query for range references
    return depGraph->getDependentsForCell(cellId, static_cast<int32_t>(col->position),
                                          static_cast<int32_t>(row->position));
}

// =============================================================================
// Single-Cell Evaluation
// =============================================================================

// Helper to convert a CellValue to an EvalResult
static EvalResult cellValueToEvalResult(const CellValue& value) {
    switch (value.type) {
        case CellValueType::NUMBER:
        case CellValueType::FORMULA_NUMBER:
            return EvalResult::Number(std::strtod(value.raw.c_str(), nullptr));
        case CellValueType::STRING:
        case CellValueType::FORMULA_STRING:
            if (value.raw.empty()) {
                return EvalResult::Empty();
            }
            return EvalResult::String(value.asString());
        case CellValueType::BOOLEAN:
        case CellValueType::FORMULA_BOOLEAN:
            return EvalResult::Boolean(value.raw == "true");
        case CellValueType::ERROR:
        case CellValueType::FORMULA_ERROR:
            return EvalResult::Error(value.error);
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
            return EvalResult::Number(std::strtod(value.raw.c_str(), nullptr));
        case CellValueType::FORMULA_EMPTY:
            return EvalResult::Empty();
        case CellValueType::FORMULA:
            // Unevaluated formula - parse raw value
            if (value.error != CellError::NONE) {
                return EvalResult::Error(value.error);
            }
            // Try to parse as number first
            if (!value.raw.empty()) {
                try {
                    size_t pos = 0;
                    const double numVal = std::stod(value.raw, &pos);
                    if (pos == value.raw.size()) {
                        return EvalResult::Number(numVal);
                    }
                } catch (...) {
                    // Not a number, fall through to return as string
                    (void)0;  // Suppress bugprone-empty-catch
                }
                return EvalResult::String(value.raw);
            }
            return EvalResult::Empty();
    }
    return EvalResult::Empty();
}

EvalResult evaluateCell(Sheet* sheet, Cell* cell) {
    if (!sheet || !cell) {
        return EvalResult::Error(CellError::VALUE);
    }

    // If cell doesn't have a formula, return its current value
    Formula* formula = cell->getFormula();
    if (!formula || !formula->ast) {
        return cellValueToEvalResult(cell->value);
    }

    // If formula is not dirty, return the cached value
    // This prevents re-evaluation of volatile functions like RAND()
    if (!formula->dirty) {
        return cellValueToEvalResult(cell->value);
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

    // Store the result in the cell's value using FORMULA_* result types
    // This preserves the formula nature while indicating the computed result type.
    // Using specific types (FORMULA_NUMBER, etc.) allows code to know both
    // "it's a formula" AND "what type the result is" without additional fields.
    if (result.isError()) {
        cell->value = CellValue(result.getError());
        cell->value.type = CellValueType::FORMULA_ERROR;
    } else if (result.isNumber()) {
        cell->value = CellValue(result.getNumber());
        cell->value.type = CellValueType::FORMULA_NUMBER;
    } else if (result.isString()) {
        cell->value = CellValue(result.getString());
        cell->value.type = CellValueType::FORMULA_STRING;
    } else if (result.isBoolean()) {
        cell->value = CellValue(result.getBoolean());
        cell->value.type = CellValueType::FORMULA_BOOLEAN;
    } else if (result.isEmpty()) {
        cell->value = CellValue("");
        cell->value.type = CellValueType::FORMULA_EMPTY;
    } else {
        // Range or other type - shouldn't happen for cell result
        cell->value = CellValue(CellError::VALUE);
        cell->value.type = CellValueType::FORMULA_ERROR;
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

    // Mark all cells in the recalculation set as dirty
    // This ensures they will be re-evaluated even if they were previously clean
    for (const ID& cellId : recalcOrder) {
        const Cell* cell = sheet->getCell(cellId);
        if (cell) {
            Formula* formula = cell->getFormula();
            if (formula) {
                formula->dirty = true;
            }
        }
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
