#include "core/cells/functions/fn_stats.h"

#include <cmath>

#include <algorithm>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

namespace {

// Helper to compute variance (shared by STDEV and VAR)
// population: true for population variance (n denominator), false for sample variance (n-1)
std::pair<double, EvalResult> computeVariance(const std::vector<const ASTNode*>& args,
                                              EvalContext& ctx, bool population) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return {0.0, error};
    }

    if (values.empty()) {
        return {0.0, EvalResult::Error(CellError::NUM)};
    }

    // For sample variance, need at least 2 values
    if (!population && values.size() < 2) {
        return {0.0, EvalResult::Error(CellError::DIV)};
    }

    // Calculate mean
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(values.size());

    // Calculate sum of squared deviations
    double sumSquaredDev = 0.0;
    for (const double v : values) {
        const double dev = v - mean;
        sumSquaredDev += dev * dev;
    }

    // Divide by n (population) or n-1 (sample)
    const double denominator =
        population ? static_cast<double>(values.size()) : static_cast<double>(values.size() - 1);

    return {sumSquaredDev / denominator, EvalResult::Empty()};
}

// Helper to compute percentile
// inclusive: true for PERCENTILE.INC (0 <= k <= 1), false for PERCENTILE.EXC (0 < k < 1)
EvalResult computePercentile(const std::vector<const ASTNode*>& args, EvalContext& ctx,
                             bool inclusive) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    // First argument is the array/range
    auto [values, error] = collectNumericValues({args[0]}, ctx);
    if (error.isError()) {
        return error;
    }

    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }

    // Second argument is k (percentile value)
    EvalResult kResult = evaluateAsNumber(args[1], ctx);
    if (kResult.isError()) {
        return kResult;
    }

    const double k = kResult.getNumber();

    // Validate k range
    if (inclusive) {
        // PERCENTILE.INC: k must be in [0, 1]
        if (k < 0.0 || k > 1.0) {
            return EvalResult::Error(CellError::NUM);
        }
    } else {
        // PERCENTILE.EXC: k must be in (0, 1), also need enough data points
        // Excel requires: k > 1/(n+1) and k < n/(n+1)
        const auto nDouble = static_cast<double>(values.size());
        if (k <= 0.0 || k >= 1.0 || k < 1.0 / (nDouble + 1.0) || k > nDouble / (nDouble + 1.0)) {
            return EvalResult::Error(CellError::NUM);
        }
    }

    // Sort values
    std::sort(values.begin(), values.end());

    const size_t n = values.size();

    if (inclusive) {
        // PERCENTILE.INC formula: rank = k * (n - 1)
        // Then linear interpolation
        if (n == 1) {
            return EvalResult::Number(values[0]);
        }

        const double rank = k * static_cast<double>(n - 1);
        const auto lower = static_cast<size_t>(std::floor(rank));
        const auto upper = static_cast<size_t>(std::ceil(rank));

        if (lower == upper || upper >= n) {
            return EvalResult::Number(values[lower]);
        }

        // Linear interpolation
        const double fraction = rank - static_cast<double>(lower);
        const double result = values[lower] + fraction * (values[upper] - values[lower]);
        return EvalResult::Number(result);
    }

    // PERCENTILE.EXC formula: rank = k * (n + 1) - 1 (0-indexed)
    const double rank = k * static_cast<double>(n + 1) - 1.0;
    const auto lower = static_cast<size_t>(std::floor(rank));
    auto upper = static_cast<size_t>(std::ceil(rank));

    if (upper >= n) {
        upper = n - 1;
    }
    if (lower == upper) {
        return EvalResult::Number(values[lower]);
    }

    // Linear interpolation
    const double fraction = rank - static_cast<double>(lower);
    const double result = values[lower] + fraction * (values[upper] - values[lower]);
    return EvalResult::Number(result);
}

}  // namespace

EvalResult fn_MEDIAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }

    // Sort values
    std::sort(values.begin(), values.end());

    const size_t n = values.size();
    if (n % 2 == 1) {
        // Odd count: return middle value
        return EvalResult::Number(values[n / 2]);
    }

    // Even count: return average of two middle values
    const double mid1 = values[n / 2 - 1];
    const double mid2 = values[n / 2];
    return EvalResult::Number((mid1 + mid2) / 2.0);
}

EvalResult fn_STDEV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [variance, error] = computeVariance(args, ctx, false);  // Sample variance
    if (error.isError()) {
        return error;
    }
    return EvalResult::Number(std::sqrt(variance));
}

EvalResult fn_STDEV_S(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_STDEV(args, ctx);
}

EvalResult fn_STDEV_P(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [variance, error] = computeVariance(args, ctx, true);  // Population variance
    if (error.isError()) {
        return error;
    }
    return EvalResult::Number(std::sqrt(variance));
}

EvalResult fn_VAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [variance, error] = computeVariance(args, ctx, false);  // Sample variance
    if (error.isError()) {
        return error;
    }
    return EvalResult::Number(variance);
}

EvalResult fn_VAR_S(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_VAR(args, ctx);
}

EvalResult fn_VAR_P(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [variance, error] = computeVariance(args, ctx, true);  // Population variance
    if (error.isError()) {
        return error;
    }
    return EvalResult::Number(variance);
}

EvalResult fn_PERCENTILE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return computePercentile(args, ctx, true);  // Inclusive
}

EvalResult fn_PERCENTILE_INC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return computePercentile(args, ctx, true);  // Inclusive
}

EvalResult fn_PERCENTILE_EXC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return computePercentile(args, ctx, false);  // Exclusive
}

void registerStatsFunctions() {
    FunctionRegistry& registry = FunctionRegistry::instance();

    registry.registerFunction("MEDIAN", fn_MEDIAN);
    registry.registerFunction("STDEV", fn_STDEV);
    registry.registerFunction("STDEVS", fn_STDEV_S);  // STDEV.S alternative
    registry.registerFunction("STDEVP", fn_STDEV_P);  // STDEV.P alternative
    registry.registerFunction("VAR", fn_VAR);
    registry.registerFunction("VARS", fn_VAR_S);  // VAR.S alternative
    registry.registerFunction("VARP", fn_VAR_P);  // VAR.P alternative
    registry.registerFunction("PERCENTILE", fn_PERCENTILE);
    registry.registerFunction("PERCENTILEINC", fn_PERCENTILE_INC);  // PERCENTILE.INC alternative
    registry.registerFunction("PERCENTILEEXC", fn_PERCENTILE_EXC);  // PERCENTILE.EXC alternative
}

}  // namespace cells
