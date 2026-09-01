// =============================================================================
// Financial Functions
// =============================================================================
//
// Closed-form loan, depreciation, T-bill, and dollar-fraction helpers.
// Iterative solvers (RATE, IRR, XIRR) are intentionally not here.
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_FINANCIAL_H_
#define CELLS_FUNCTIONS_FN_FINANCIAL_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

struct ASTNode;
class FunctionRegistry;

EvalResult fn_SLN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_SYD(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_PV(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_FV(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_PMT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_NPER(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_NPV(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_EFFECT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_NOMINAL(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DOLLARDE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DOLLARFR(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_FVSCHEDULE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_PDURATION(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_RRI(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ISPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DDB(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DB(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_IPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_PPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_CUMIPMT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_CUMPRINC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TBILLEQ(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TBILLPRICE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_TBILLYIELD(const std::vector<const ASTNode*>& args, EvalContext& ctx);

void registerFinancialFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_FINANCIAL_H_
