// =============================================================================
// Statistical Functions
// =============================================================================
//
// Statistical formula functions for data analysis.
// Includes MEDIAN, STDEV, VAR, PERCENTILE, etc.
//
// Functions:
// - MEDIAN: Middle value of dataset
// - STDEV, STDEV.S, STDEV.P: Standard deviation (sample/population)
// - VAR, VAR.S, VAR.P: Variance (sample/population)
// - PERCENTILE, PERCENTILE.INC, PERCENTILE.EXC: Percentile calculations
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_STATS_H_
#define CELLS_FUNCTIONS_FN_STATS_H_

#include "core/cells/formula_eval.h"

namespace cells {

// MEDIAN(number1, [number2], ...) - Returns the median of the given numbers
// For an odd count, returns the middle value
// For an even count, returns the average of the two middle values
EvalResult fn_MEDIAN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// STDEV(number1, [number2], ...) - Sample standard deviation
// Uses n-1 denominator (sample standard deviation)
EvalResult fn_STDEV(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// STDEV.S(number1, [number2], ...) - Same as STDEV (sample standard deviation)
EvalResult fn_STDEV_S(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// STDEV.P(number1, [number2], ...) - Population standard deviation
// Uses n denominator (population standard deviation)
EvalResult fn_STDEV_P(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// VAR(number1, [number2], ...) - Sample variance
// Uses n-1 denominator (sample variance)
EvalResult fn_VAR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// VAR.S(number1, [number2], ...) - Same as VAR (sample variance)
EvalResult fn_VAR_S(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// VAR.P(number1, [number2], ...) - Population variance
// Uses n denominator (population variance)
EvalResult fn_VAR_P(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// PERCENTILE(array, k) - Returns the k-th percentile of values in a range
// k must be between 0 and 1 (inclusive)
EvalResult fn_PERCENTILE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// PERCENTILE.INC(array, k) - Same as PERCENTILE (inclusive)
EvalResult fn_PERCENTILE_INC(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// PERCENTILE.EXC(array, k) - Exclusive percentile
// k must be between 0 and 1 (exclusive)
EvalResult fn_PERCENTILE_EXC(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_LARGE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_SMALL(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_RANK(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_RANK_EQ(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_MODE_SNGL(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_QUARTILE_INC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_COUNTBLANK(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Forward declaration
class FunctionRegistry;

// Register statistics functions with the registry
void registerStatsFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_STATS_H_
