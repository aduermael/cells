// =============================================================================
// Engineering Functions
// =============================================================================
//
// Bitwise ops, radix conversion, ERF, and comparison helpers (DELTA/GESTEP).
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_ENGINEERING_H_
#define CELLS_FUNCTIONS_FN_ENGINEERING_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

struct ASTNode;
class FunctionRegistry;

EvalResult fn_BITAND(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BITOR(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BITXOR(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BITLSHIFT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BITRSHIFT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_BIN2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BIN2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_BIN2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DEC2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DEC2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DEC2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_HEX2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_HEX2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_HEX2OCT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_OCT2BIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_OCT2DEC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_OCT2HEX(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_DELTA(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_GESTEP(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ERF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ERFC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ERF_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ERFC_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_COMPLEX(const std::vector<const ASTNode*>& args, EvalContext& ctx);

void registerEngineeringFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_ENGINEERING_H_
