#ifndef CELLS_FUNCTIONS_FN_LOOKUP_H_
#define CELLS_FUNCTIONS_FN_LOOKUP_H_

#include "core/cells/formula_eval.h"

namespace cells {

// INDEX(array, row_num, [col_num])
// Returns the value at a specified position in a range
// array: Range of cells or array constant
// row_num: Row position (1-indexed)
// col_num: Column position (1-indexed), defaults to 1
EvalResult fn_INDEX(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MATCH(lookup_value, lookup_array, [match_type])
// Returns the relative position of a value in a range
// lookup_value: Value to find
// lookup_array: 1D range to search
// match_type: 1 (default) = largest <= lookup, 0 = exact, -1 = smallest >=
EvalResult fn_MATCH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// VLOOKUP(lookup_value, table_array, col_index, [range_lookup])
// Vertical lookup - searches first column, returns value from specified column
// lookup_value: Value to find in first column
// table_array: Range containing data
// col_index: Column number to return value from (1-indexed)
// range_lookup: TRUE (default) = approximate match, FALSE = exact match
EvalResult fn_VLOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// HLOOKUP(lookup_value, table_array, row_index, [range_lookup])
// Horizontal lookup - searches first row, returns value from specified row
// lookup_value: Value to find in first row
// table_array: Range containing data
// row_index: Row number to return value from (1-indexed)
// range_lookup: TRUE (default) = approximate match, FALSE = exact match
EvalResult fn_HLOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Forward declaration
class FunctionRegistry;

// Register lookup functions with the registry
void registerLookupFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_LOOKUP_H_
