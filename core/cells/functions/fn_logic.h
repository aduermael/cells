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
