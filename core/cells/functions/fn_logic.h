// =============================================================================
// Logic Functions
// =============================================================================
//
// Logical and conditional formula functions.
// Includes IF, AND, OR, NOT, IFERROR, type-checking functions.
//
// Categories:
// - Core logic: IF, AND, OR, NOT
// - Error handling: IFERROR, IFNA
// - Type checking: ISBLANK, ISNUMBER, ISTEXT, ISERROR, ISLOGICAL, ISNA
// - Boolean constants: TRUE, FALSE
// - Comparison: EXACT
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_LOGIC_H_
#define CELLS_FUNCTIONS_FN_LOGIC_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declarations
struct ASTNode;
class FunctionRegistry;

// =============================================================================
// Core Logic Functions
// =============================================================================

// IF(condition, value_if_true, [value_if_false])
EvalResult fn_IF(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// AND(logical1, [logical2], ...) - Returns TRUE if all arguments are true
EvalResult fn_AND(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// OR(logical1, [logical2], ...) - Returns TRUE if any argument is true
EvalResult fn_OR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// NOT(logical) - Returns the opposite boolean value
EvalResult fn_NOT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// XOR(logical1, [logical2], ...) - Returns TRUE if an odd number of args are TRUE
EvalResult fn_XOR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// NA() - Returns the error value #N/A
EvalResult fn_NA(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SWITCH(expression, value1, result1, ..., [default]) - Matches expression against values
EvalResult fn_SWITCH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// IFS(condition1, value1, ...) - Returns value for first TRUE condition
EvalResult fn_IFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Error Handling Functions
// =============================================================================

// IFERROR(value, value_if_error) - Returns value if not an error
EvalResult fn_IFERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// IFNA(value, value_if_na) - Returns value if not #N/A
EvalResult fn_IFNA(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Type Checking Functions
// =============================================================================

// EXACT(text1, text2) - Case-sensitive string comparison
EvalResult fn_EXACT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISBLANK(value) - Returns TRUE if cell is empty
EvalResult fn_ISBLANK(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISNUMBER(value) - Returns TRUE if value is a number
EvalResult fn_ISNUMBER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISTEXT(value) - Returns TRUE if value is text
EvalResult fn_ISTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISERROR(value) - Returns TRUE if value is any error
EvalResult fn_ISERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISLOGICAL(value) - Returns TRUE if value is a boolean
EvalResult fn_ISLOGICAL(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISNA(value) - Returns TRUE if value is #N/A
EvalResult fn_ISNA(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISERR(value) - TRUE if value is an error other than #N/A
EvalResult fn_ISERR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISNONTEXT(value) - TRUE if value is not text
EvalResult fn_ISNONTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISEVEN(number) / ISODD(number) - parity of truncated number
EvalResult fn_ISEVEN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ISODD(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TYPE(value) - Excel type code (1 number, 2 text, 4 bool, 16 error, 64 array)
EvalResult fn_TYPE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// N(value) - Convert to number (text → 0, TRUE → 1)
EvalResult fn_N(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ERROR.TYPE(error_val) - Numeric code for an error value
EvalResult fn_ERROR_TYPE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ISREF(value) - TRUE if argument is a reference
EvalResult fn_ISREF(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_ISBETWEEN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Boolean Constants
// =============================================================================

// TRUE() - Returns the boolean value TRUE
EvalResult fn_TRUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// FALSE() - Returns the boolean value FALSE
EvalResult fn_FALSE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Registration
// =============================================================================

// Register logic functions with the registry
void registerLogicFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_LOGIC_H_
