// =============================================================================
// Array/Spill Functions
// =============================================================================
//
// Dynamic array functions that return multi-cell results (spill arrays).
// Includes UNIQUE, SORT, FILTER, SEQUENCE, TRANSPOSE, RANDARRAY.
//
// These functions return EvalResult::ARRAY which triggers spill behavior
// when the result is written to the sheet.
//
// Functions:
// - UNIQUE: Return unique values from a range
// - SORT: Sort a range of data
// - FILTER: Filter data based on criteria
// - SEQUENCE: Generate a sequence of numbers
// - TRANSPOSE: Transpose rows and columns
// - RANDARRAY: Generate random number array
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_ARRAY_H_
#define CELLS_FUNCTIONS_FN_ARRAY_H_

#include "core/cells/formula_eval.h"

namespace cells {

// UNIQUE(array, [by_col], [exactly_once]) - Returns unique values from a range
// array: The range to extract unique values from
// by_col: FALSE (default) = compare rows, TRUE = compare columns
// exactly_once: FALSE (default) = all unique, TRUE = only values appearing exactly once
EvalResult fn_UNIQUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SORT(array, [sort_index], [sort_order], [by_col]) - Sorts a range of data
// array: The range to sort
// sort_index: Which column/row to sort by (1-based, default: 1)
// sort_order: 1 = ascending (default), -1 = descending
// by_col: FALSE (default) = sort rows by column values, TRUE = sort columns by row values
EvalResult fn_SORT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// FILTER(array, include, [if_empty]) - Filters data based on boolean criteria
// array: The range to filter
// include: Boolean array determining which rows/columns to include
// if_empty: Value to return if all rows/columns are filtered out
EvalResult fn_FILTER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SEQUENCE(rows, [cols], [start], [step]) - Generates a sequence of numbers
// rows: Number of rows to generate
// cols: Number of columns to generate (default: 1)
// start: Starting value (default: 1)
// step: Increment between values (default: 1)
EvalResult fn_SEQUENCE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Forward declaration
class FunctionRegistry;

// Register array functions with the registry
void registerArrayFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_ARRAY_H_
