// =============================================================================
// Text Functions
// =============================================================================
//
// Text manipulation formula functions.
// Includes LEN, LEFT, RIGHT, MID, TRIM, UPPER, LOWER, etc.
//
// Categories:
// - Basic: LEN, LEFT, RIGHT, MID, TRIM
// - Case: UPPER, LOWER, PROPER
// - Search/Replace: FIND, SEARCH, SUBSTITUTE, REPLACE
// - Concatenation: CONCAT, CONCATENATE, REPT
// - Conversion: TEXT, VALUE
// - Character: CHAR, CODE
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_TEXT_H_
#define CELLS_FUNCTIONS_FN_TEXT_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declarations
struct ASTNode;
class FunctionRegistry;

// =============================================================================
// Basic Text Functions
// =============================================================================

// LEN(text) - Returns the number of characters
EvalResult fn_LEN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// LEFT(text, [num_chars]) - Returns leftmost characters
EvalResult fn_LEFT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// RIGHT(text, [num_chars]) - Returns rightmost characters
EvalResult fn_RIGHT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MID(text, start_num, num_chars) - Returns characters from the middle
EvalResult fn_MID(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TRIM(text) - Removes extra spaces
EvalResult fn_TRIM(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Case Functions
// =============================================================================

// UPPER(text) - Converts to uppercase
EvalResult fn_UPPER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// LOWER(text) - Converts to lowercase
EvalResult fn_LOWER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// PROPER(text) - Capitalizes first letter of each word
EvalResult fn_PROPER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Search and Replace Functions
// =============================================================================

// FIND(find_text, within_text, [start_num]) - Case-sensitive search
EvalResult fn_FIND(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SEARCH(find_text, within_text, [start_num]) - Case-insensitive search
EvalResult fn_SEARCH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SUBSTITUTE(text, old_text, new_text, [instance_num]) - Replace occurrences
EvalResult fn_SUBSTITUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// REPLACE(old_text, start_num, num_chars, new_text) - Replace at position
EvalResult fn_REPLACE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Concatenation and Conversion Functions
// =============================================================================

// CONCAT(text1, [text2], ...) - Joins text strings
EvalResult fn_CONCAT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// CONCATENATE(text1, [text2], ...) - Legacy version of CONCAT
EvalResult fn_CONCATENATE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// REPT(text, number_times) - Repeats text
EvalResult fn_REPT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TEXT(value, format_text) - Formats number as text
EvalResult fn_TEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// VALUE(text) - Converts text to number
EvalResult fn_VALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Character Functions
// =============================================================================

// CHAR(number) - Returns character for code number
EvalResult fn_CHAR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// CODE(text) - Returns numeric code of first character
EvalResult fn_CODE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_TEXTJOIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_CLEAN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_UNICHAR(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_UNICODE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DOLLAR(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_FIXED(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_NUMBERVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TEXTAFTER(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TEXTBEFORE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TEXTSPLIT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_VALUETOTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ASC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ENCODEURL(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_JOIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_SPLIT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Registration
// =============================================================================

// Register text functions with the registry
void registerTextFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_TEXT_H_
