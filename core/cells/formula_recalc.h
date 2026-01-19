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

#include <utility>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Sheet;
struct Workbook;
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

// Recalculate cells in response to changes (workbook-level, sheet-agnostic)
// changedCells: cells whose values have changed (triggers dependent recalc)
// Uses the global dependency graph to determine recalculation order
// Handles circular references by marking cells with #CIRCULAR! error
// This recalculates ALL dependents regardless of which sheet they're on.
void recalculate(Workbook* workbook, const std::vector<ID>& changedCells);

// Legacy sheet-based recalculate (delegates to workbook-level version)
// Kept for backward compatibility with existing callers
void recalculate(Sheet* sheet, const std::vector<ID>& changedCells);

// Recalculate cross-sheet dependents for changed cells
// DEPRECATED: With workbook-level recalculate(), this is no longer needed.
// The global dependency graph handles all dependencies regardless of sheet.
// Kept temporarily for backward compatibility - will be removed.
void recalculateCrossSheet(Workbook* workbook, Sheet* changedSheet,
                           const std::vector<ID>& changedCells);

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

// =============================================================================
// Spill Range Management
// =============================================================================
// Functions for handling dynamic array formula "spill" behavior where a single
// formula produces multiple values that populate neighboring cells automatically.
// =============================================================================

// Maximum number of cells that can be spilled by a single formula
// This prevents runaway spill operations that could impact performance
// Matches Excel's practical limit of ~1 million cells
constexpr size_t MAX_SPILL_CELLS = 1000000;

// Calculate the positions where an array result would spill
// Returns vector of (colId, rowId) pairs for each spilled position (excludes master)
// Creates new columns/rows if needed to accommodate the spill
// If successful, positions are returned in row-major order
std::vector<std::pair<ID, ID>> calculateSpillRange(Sheet* sheet, Cell* masterCell, size_t rows,
                                                   size_t cols);

// Check if a spill range would be blocked by existing data
// A position is "blocked" if:
// - Cell exists with a non-empty value (has content)
// - Cell has its own formula (not just a cached result)
// - Cell is spilled from a DIFFERENT master (not this one)
// Returns true if blocked, false if spill can proceed
bool checkSpillBlocked(Sheet* sheet, const ID& masterCellId,
                       const std::vector<std::pair<ID, ID>>& spillPositions);

// Process spill for a cell after evaluation
// If the result is an ARRAY type, calculates spill range and populates values
// If blocked, sets master cell to #SPILL! error
// If not an array, clears any existing spill for this cell
void processSpill(Sheet* sheet, Cell* masterCell, const EvalResult& result);

// Clear spill range for a master cell (call when master is deleted or formula changes)
void clearSpillForMaster(Sheet* sheet, const ID& masterCellId);

}  // namespace cells

#endif  // CELLS_FORMULA_RECALC_H_
