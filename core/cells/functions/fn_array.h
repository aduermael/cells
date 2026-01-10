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

// Forward declaration
class FunctionRegistry;

// Register array functions with the registry
void registerArrayFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_ARRAY_H_
