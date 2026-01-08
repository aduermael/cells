// =============================================================================
// Formula Recalculation Engine
// =============================================================================
//
// Manages formula recalculation in response to cell value changes.
// Uses the dependency graph to determine recalculation order.
//
// Key responsibilities:
// - Evaluate single cells or cascade from changed cells
// - Determine recalculation order via topological sort
// - Handle circular references with #CIRCULAR! errors
// - Track and recalculate volatile cells (NOW, RAND, etc.)
//
// Recalculation strategy:
// 1. Mark changed cells as dirty
// 2. Propagate dirty flags to dependents
// 3. Topologically sort dirty cells
// 4. Evaluate in dependency order
//
// Note: This is a read-only evaluation layer. It computes values from
// CRDT-synced input data but does not modify the CRDT state.
//
// Dependencies: types.h
// Used by: bindings.cc (triggering recalc after edits)
//
// =============================================================================

#ifndef CELLS_FORMULA_RECALC_H_
#define CELLS_FORMULA_RECALC_H_

#include <vector>

#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Sheet;
struct Cell;
struct EvalResult;

// =============================================================================
// Recalculation Engine
// =============================================================================
// Provides APIs for evaluating formulas and managing recalculation cascades.
// This is a read-only layer that evaluates formulas but does not modify the
// CRDT state - results are computed on-demand from synced input data.
// =============================================================================

// Evaluate a single cell's formula and store the result
// Returns the evaluation result
// If the cell has no formula, returns its current value as EvalResult
// If the cell has a formula, evaluates it and stores the result in cell->value
EvalResult evaluateCell(Sheet* sheet, Cell* cell);

// Evaluate a single cell by ID
// Convenience overload that looks up the cell first
EvalResult evaluateCell(Sheet* sheet, const ID& cellId);

// Recalculate cells in response to changes
// changedCells: cells whose values have changed (triggers dependent recalc)
// Uses the dependency graph to determine recalculation order
// Handles circular references by marking cells with #CIRCULAR! error
void recalculate(Sheet* sheet, const std::vector<ID>& changedCells);

// Recalculate all volatile cells and their dependents
// Call this when any edit occurs to ensure NOW(), TODAY(), etc. are updated
void recalculateVolatile(Sheet* sheet);

// Mark a cell as dirty (needs recalculation)
// Also marks all dependent cells as dirty
void markDirty(Sheet* sheet, const ID& cellId);

// Check if any cells need recalculation
bool hasDirtyCells(Sheet* sheet);

// Get all cells that need recalculation, in proper order
std::vector<ID> getDirtyCells(Sheet* sheet);

}  // namespace cells

#endif  // CELLS_FORMULA_RECALC_H_
