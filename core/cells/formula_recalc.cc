#include "core/cells/formula_recalc.h"

#include <cstdlib>

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
            // Try to parse as number first (using strtod to avoid exceptions in WASM)
            if (!value.raw.empty()) {
                char* endPtr = nullptr;  // NOLINT(misc-const-correctness)
                const double numVal = std::strtod(value.raw.c_str(), &endPtr);
                // Check if entire string was consumed (successful number parse)
                if (endPtr != nullptr && *endPtr == '\0') {
                    return EvalResult::Number(numVal);
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
    ctx.workbook = sheet->getWorkbook();
    ctx.namedRanges = ctx.workbook ? ctx.workbook->getNamedRanges() : nullptr;
    ctx.currentCellId = cell->id;
    ctx.evaluatingCells = &evaluatingCells;
    ctx.recursionDepth = 0;

    // Evaluate the formula
    EvalResult result = evaluate(formula->ast, ctx);

    // Handle array results (spill functions like UNIQUE, SORT, FILTER, etc.)
    if (result.isArray()) {
        processSpill(sheet, cell, result);
        // processSpill sets the cell value from the first array element
        // or sets #SPILL! error if blocked
        formula->dirty = false;
        // Return the first element as the "visible" result for this cell
        if (result.getArrayRows() > 0 && result.getArrayCols() > 0) {
            return result.getArrayAt(0, 0);
        }
        return EvalResult::Empty();
    }

    // Clear any existing spill range (formula result is no longer an array)
    sheet->clearSpillRange(cell->id);

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
// Cross-Sheet Recalculation
// =============================================================================

void recalculateCrossSheet(Workbook* workbook, Sheet* changedSheet,
                           const std::vector<ID>& changedCells) {
    if (!workbook || !changedSheet || changedCells.empty()) {
        return;
    }

    // Collect all cross-sheet dependents grouped by target sheet
    // This avoids recalculating the same sheet multiple times
    std::unordered_map<ID, std::vector<ID>, IDHash> depsBySheet;
    std::unordered_set<ID> addedFormulas;  // Avoid duplicates

    // Check direct cell dependencies
    for (const ID& cellId : changedCells) {
        auto crossDeps = workbook->getCrossSheetDependents(cellId);
        for (const auto& dep : crossDeps) {
            // Only process deps on OTHER sheets (not the sheet where the change occurred)
            if (dep.formulaSheetId != changedSheet->id) {
                if (addedFormulas.insert(dep.formulaCellId).second) {
                    depsBySheet[dep.formulaSheetId].push_back(dep.formulaCellId);
                }
            }
        }
    }

    // Check range dependencies - formulas that reference ranges containing the changed cells
    for (const ID& cellId : changedCells) {
        const Cell* cell = changedSheet->getCell(cellId);
        if (!cell) {
            continue;
        }

        auto rangeDeps =
            workbook->getCrossSheetRangeDependents(changedSheet->id, cell->colId, cell->rowId);
        for (const auto& dep : rangeDeps) {
            // Only process deps on OTHER sheets (not the sheet where the change occurred)
            if (dep.formulaSheetId != changedSheet->id) {
                if (addedFormulas.insert(dep.formulaCellId).second) {
                    depsBySheet[dep.formulaSheetId].push_back(dep.formulaCellId);
                }
            }
        }
    }

    // Recalculate each affected sheet
    for (auto& [sheetId, formulaCells] : depsBySheet) {
        Sheet* targetSheet = workbook->getSheetById(sheetId);
        if (!targetSheet) {
            continue;
        }

        // Mark formula cells as dirty and recalculate
        for (const ID& formulaCellId : formulaCells) {
            const Cell* cell = targetSheet->getCell(formulaCellId);
            if (cell) {
                Formula* formula = cell->getFormula();
                if (formula) {
                    formula->dirty = true;
                }
            }
        }

        // Recalculate the target sheet with the cross-sheet dependent cells
        recalculate(targetSheet, formulaCells);

        // Recursively handle cross-sheet deps from this sheet
        // This handles chains like Sheet3!A1 -> Sheet2!B1 -> Sheet1!C1
        recalculateCrossSheet(workbook, targetSheet, formulaCells);
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

    for (const ID& cellId : sheet->getCellIds()) {
        const Cell* cell = sheet->getCell(cellId);
        if (!cell)
            continue;
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
    for (const ID& cellId : sheet->getCellIds()) {
        const Cell* cell = sheet->getCell(cellId);
        if (!cell)
            continue;
        const Formula* formula = cell->getFormula();
        if (formula && formula->dirty) {
            dirtyCells.push_back(cellId);
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

// =============================================================================
// Spill Range Management
// =============================================================================

std::vector<std::pair<ID, ID>> calculateSpillRange(Sheet* sheet, Cell* masterCell, size_t rows,
                                                   size_t cols) {
    std::vector<std::pair<ID, ID>> positions;

    if (!sheet || !masterCell || rows == 0 || cols == 0) {
        return positions;
    }

    // Get master cell's position
    const Axis* masterCol = sheet->getColumn(masterCell->colId);
    const Axis* masterRow = sheet->getRow(masterCell->rowId);
    if (!masterCol || !masterRow) {
        return positions;
    }

    const uint32_t startColPos = masterCol->position;
    const uint32_t startRowPos = masterRow->position;

    // Reserve space for all positions except master (at position 0,0 of the array)
    positions.reserve(rows * cols - 1);

    // Collect positions in row-major order (matches array layout)
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            // Skip the master cell position (top-left of spill range)
            if (r == 0 && c == 0) {
                continue;
            }

            const uint32_t targetColPos = startColPos + static_cast<uint32_t>(c);
            const uint32_t targetRowPos = startRowPos + static_cast<uint32_t>(r);

            // Get or create column at target position
            const Axis* const col = sheet->getOrCreateColumnByPosition(targetColPos);
            if (!col) {
                // Failed to create column - abort
                positions.clear();
                return positions;
            }

            // Get or create row at target position
            const Axis* const row = sheet->getOrCreateRowByPosition(targetRowPos);
            if (!row) {
                // Failed to create row - abort
                positions.clear();
                return positions;
            }

            positions.emplace_back(col->id, row->id);
        }
    }

    return positions;
}

bool checkSpillBlocked(Sheet* sheet, const ID& masterCellId,
                       const std::vector<std::pair<ID, ID>>& spillPositions) {
    if (!sheet) {
        return true;  // No sheet = blocked
    }

    for (const auto& [colId, rowId] : spillPositions) {
        // Check if this position is already spilled from a DIFFERENT master
        const ID existingMaster = sheet->getSpillMaster(colId, rowId);
        if (!existingMaster.isNull() && existingMaster != masterCellId) {
            // Position is spilled from another formula - blocked
            return true;
        }

        // Check if there's a cell at this position
        const Cell* cell = sheet->getCellAt(colId, rowId);
        if (cell) {
            // Cell exists - check if it has content that would block spill
            // A cell blocks if:
            // 1. It has its own formula (not just cached result)
            // 2. It has a non-empty value that wasn't set by a spill

            // If cell has its own formula, it blocks
            if (cell->isFormula()) {
                return true;
            }

            // If cell has a non-empty value, it blocks
            // (Empty cells and cells with empty strings don't block)
            if (cell->value.type != CellValueType::FORMULA_EMPTY) {
                if (cell->value.type == CellValueType::STRING ||
                    cell->value.type == CellValueType::FORMULA_STRING) {
                    // Empty strings don't block
                    if (!cell->value.raw.empty()) {
                        return true;
                    }
                } else if (cell->value.type != CellValueType::FORMULA &&
                           cell->value.error == CellError::NONE) {
                    // Non-formula types with actual values block
                    // Check if it's an actual value (not just default)
                    if (cell->value.type == CellValueType::NUMBER ||
                        cell->value.type == CellValueType::BOOLEAN ||
                        cell->value.type == CellValueType::DATE ||
                        cell->value.type == CellValueType::DATE_TIME ||
                        cell->value.type == CellValueType::FORMULA_NUMBER ||
                        cell->value.type == CellValueType::FORMULA_BOOLEAN) {
                        return true;
                    }
                }
            }
        }
    }

    return false;  // Not blocked
}

void processSpill(Sheet* sheet, Cell* masterCell, const EvalResult& result) {
    if (!sheet || !masterCell) {
        return;
    }

    // If result is not an array, clear any existing spill and return
    if (!result.isArray()) {
        sheet->clearSpillRange(masterCell->id);
        return;
    }

    const size_t arrayRows = result.getArrayRows();
    const size_t arrayCols = result.getArrayCols();

    // Empty array - no spill needed
    if (arrayRows == 0 || arrayCols == 0) {
        sheet->clearSpillRange(masterCell->id);
        return;
    }

    // Single cell result (1x1) - no spill, just store the value
    if (arrayRows == 1 && arrayCols == 1) {
        sheet->clearSpillRange(masterCell->id);
        // The master cell's value will be set by the caller from arrayValue[0][0]
        return;
    }

    // Check spill size limit to prevent performance issues
    const size_t totalCells = arrayRows * arrayCols;
    if (totalCells > MAX_SPILL_CELLS) {
        // Too large - set master to #SPILL! error
        sheet->clearSpillRange(masterCell->id);
        masterCell->value = CellValue(CellError::SPILL);
        masterCell->value.type = CellValueType::FORMULA_ERROR;
        return;
    }

    // Calculate spill positions (excludes master cell)
    const std::vector<std::pair<ID, ID>> spillPositions =
        calculateSpillRange(sheet, masterCell, arrayRows, arrayCols);

    // Check if spill is blocked
    if (checkSpillBlocked(sheet, masterCell->id, spillPositions)) {
        // Blocked - set master to #SPILL! error and clear any existing spill
        sheet->clearSpillRange(masterCell->id);
        masterCell->value = CellValue(CellError::SPILL);
        masterCell->value.type = CellValueType::FORMULA_ERROR;
        return;
    }

    // Build the list of spilled values (excludes master cell value at [0][0])
    std::vector<CellValue> spilledValues;
    spilledValues.reserve(spillPositions.size());

    const auto& array = result.getArray();
    for (size_t r = 0; r < arrayRows; ++r) {
        for (size_t c = 0; c < arrayCols; ++c) {
            // Skip master cell position
            if (r == 0 && c == 0) {
                continue;
            }

            // Convert EvalResult to CellValue
            const EvalResult& elem = array[r][c];
            CellValue val;
            if (elem.isError()) {
                val = CellValue(elem.getError());
                val.type = CellValueType::FORMULA_ERROR;
            } else if (elem.isNumber()) {
                val = CellValue(elem.getNumber());
                val.type = CellValueType::FORMULA_NUMBER;
            } else if (elem.isString()) {
                val = CellValue(elem.getString());
                val.type = CellValueType::FORMULA_STRING;
            } else if (elem.isBoolean()) {
                val = CellValue(elem.getBoolean());
                val.type = CellValueType::FORMULA_BOOLEAN;
            } else {
                // Empty or other type
                val = CellValue("");
                val.type = CellValueType::FORMULA_EMPTY;
            }
            spilledValues.push_back(std::move(val));
        }
    }

    // Register the spill range
    sheet->registerSpillRange(masterCell->id, spillPositions, spilledValues);

    // Set the master cell's value from the first element of the array
    const EvalResult& firstElem = array[0][0];
    if (firstElem.isError()) {
        masterCell->value = CellValue(firstElem.getError());
        masterCell->value.type = CellValueType::FORMULA_ERROR;
    } else if (firstElem.isNumber()) {
        masterCell->value = CellValue(firstElem.getNumber());
        masterCell->value.type = CellValueType::FORMULA_NUMBER;
    } else if (firstElem.isString()) {
        masterCell->value = CellValue(firstElem.getString());
        masterCell->value.type = CellValueType::FORMULA_STRING;
    } else if (firstElem.isBoolean()) {
        masterCell->value = CellValue(firstElem.getBoolean());
        masterCell->value.type = CellValueType::FORMULA_BOOLEAN;
    } else {
        masterCell->value = CellValue("");
        masterCell->value.type = CellValueType::FORMULA_EMPTY;
    }
}

void clearSpillForMaster(Sheet* sheet, const ID& masterCellId) {
    if (!sheet) {
        return;
    }
    sheet->clearSpillRange(masterCellId);
}

}  // namespace cells
