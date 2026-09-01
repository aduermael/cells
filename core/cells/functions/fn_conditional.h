// =============================================================================
// Conditional aggregate functions
// =============================================================================
//
// Criteria-based aggregates and array products:
// SUMIF / SUMIFS / COUNTIF / COUNTIFS / AVERAGEIF / AVERAGEIFS /
// MINIFS / MAXIFS / SUMPRODUCT
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_CONDITIONAL_H_
#define CELLS_FUNCTIONS_FN_CONDITIONAL_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

struct ASTNode;
class FunctionRegistry;

EvalResult fn_SUMIF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_SUMIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_COUNTIF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_COUNTIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_AVERAGEIF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_AVERAGEIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_MINIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_MAXIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_SUMPRODUCT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

void registerConditionalFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_CONDITIONAL_H_
