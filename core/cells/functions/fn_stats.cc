#include "core/cells/functions/fn_stats.h"

#include <cmath>

#include <algorithm>
#include <functional>
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

EvalResult fn_LARGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, error] = collectNumericValues({args[0]}, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    const EvalResult kRes = evaluateAsNumber(args[1], ctx);
    if (kRes.isError()) {
        return kRes;
    }
    const int k = static_cast<int>(kRes.getNumber());
    if (k < 1 || static_cast<size_t>(k) > values.size()) {
        return EvalResult::Error(CellError::NUM);
    }
    std::sort(values.begin(), values.end(), std::greater<double>());
    return EvalResult::Number(values[static_cast<size_t>(k) - 1]);
}

EvalResult fn_SMALL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, error] = collectNumericValues({args[0]}, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    const EvalResult kRes = evaluateAsNumber(args[1], ctx);
    if (kRes.isError()) {
        return kRes;
    }
    const int k = static_cast<int>(kRes.getNumber());
    if (k < 1 || static_cast<size_t>(k) > values.size()) {
        return EvalResult::Error(CellError::NUM);
    }
    std::sort(values.begin(), values.end());
    return EvalResult::Number(values[static_cast<size_t>(k) - 1]);
}

EvalResult fn_RANK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult numRes = evaluateAsNumber(args[0], ctx);
    if (numRes.isError()) {
        return numRes;
    }
    const double number = numRes.getNumber();
    auto [values, error] = collectNumericValues({args[1]}, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NA);
    }
    int order = 0;
    if (args.size() == 3) {
        const EvalResult orderRes = evaluateAsNumber(args[2], ctx);
        if (orderRes.isError()) {
            return orderRes;
        }
        order = static_cast<int>(orderRes.getNumber()) != 0 ? 1 : 0;
    }
    bool found = false;
    size_t better = 0;
    for (const double v : values) {
        if (v == number) {
            found = true;
        } else if (order == 0 && v > number) {
            ++better;
        } else if (order != 0 && v < number) {
            ++better;
        }
    }
    if (!found) {
        return EvalResult::Error(CellError::NA);
    }
    return EvalResult::Number(static_cast<double>(better + 1));
}

EvalResult fn_RANK_EQ(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_RANK(args, ctx);
}

EvalResult fn_MODE_SNGL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NA);
    }
    std::vector<double> seen;
    std::vector<size_t> counts;
    for (const double v : values) {
        size_t idx = seen.size();
        for (size_t i = 0; i < seen.size(); ++i) {
            if (seen[i] == v) {
                idx = i;
                break;
            }
        }
        if (idx == seen.size()) {
            seen.push_back(v);
            counts.push_back(1);
        } else {
            ++counts[idx];
        }
    }
    size_t best = 0;
    for (size_t i = 1; i < counts.size(); ++i) {
        if (counts[i] > counts[best]) {
            best = i;
        }
    }
    if (counts[best] < 2) {
        return EvalResult::Error(CellError::NA);
    }
    return EvalResult::Number(seen[best]);
}

EvalResult fn_QUARTILE_INC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult qRes = evaluateAsNumber(args[1], ctx);
    if (qRes.isError()) {
        return qRes;
    }
    const double q = qRes.getNumber();
    if (q < 0.0 || q > 4.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const int quart = static_cast<int>(q);
    if (quart < 0 || quart > 4) {
        return EvalResult::Error(CellError::NUM);
    }
    // QUARTILE.INC(array, n) = PERCENTILE.INC(array, n/4)
    NumberLiteralNode kNode(static_cast<double>(quart) / 4.0);
    std::vector<const ASTNode*> pctArgs = {args[0], &kNode};
    return computePercentile(pctArgs, ctx, true);
}

EvalResult fn_COUNTBLANK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);
    size_t count = 0;
    for (const EvalResult& val : expanded) {
        if (val.isError()) {
            return val;
        }
        if (val.isEmpty()) {
            ++count;
        }
    }
    return EvalResult::Number(static_cast<double>(count));
}

void registerStatsFunctions(FunctionRegistry& registry) {
    registry.registerFunction("MEDIAN", fn_MEDIAN, "(number1, [number2], ...)",
                              "Returns the median value", "Statistics");
    registry.registerFunction("STDEV", fn_STDEV, "(number1, [number2], ...)",
                              "Sample standard deviation", "Statistics");
    registry.registerFunction("STDEVS", fn_STDEV_S, "(number1, [number2], ...)",
                              "Sample standard deviation", "Statistics");
    registry.registerFunction("STDEVP", fn_STDEV_P, "(number1, [number2], ...)",
                              "Population standard deviation", "Statistics");
    registry.registerFunction("VAR", fn_VAR, "(number1, [number2], ...)", "Sample variance",
                              "Statistics");
    registry.registerFunction("VARS", fn_VAR_S, "(number1, [number2], ...)", "Sample variance",
                              "Statistics");
    registry.registerFunction("VARP", fn_VAR_P, "(number1, [number2], ...)", "Population variance",
                              "Statistics");
    registry.registerFunction("PERCENTILE", fn_PERCENTILE, "(array, k)",
                              "Returns the k-th percentile", "Statistics");
    registry.registerFunction("PERCENTILEINC", fn_PERCENTILE_INC, "(array, k)",
                              "Inclusive percentile", "Statistics");
    registry.registerFunction("PERCENTILEEXC", fn_PERCENTILE_EXC, "(array, k)",
                              "Exclusive percentile", "Statistics");
    registry.registerFunction("LARGE", fn_LARGE, "(array, k)", "k-th largest value", "Statistics");
    registry.registerFunction("SMALL", fn_SMALL, "(array, k)", "k-th smallest value", "Statistics");
    registry.registerFunction("RANK", fn_RANK, "(number, ref, [order])",
                              "Rank of a number in a list", "Statistics");
    registry.registerFunction("RANK.EQ", fn_RANK_EQ, "(number, ref, [order])",
                              "Rank of a number (ties share rank)", "Statistics");
    registry.registerFunction("RANK_EQ", fn_RANK_EQ, "(number, ref, [order])",
                              "Rank of a number (ties share rank)", "Statistics");
    registry.registerFunction("RANKEQ", fn_RANK_EQ, "(number, ref, [order])",
                              "Rank of a number (ties share rank)", "Statistics");
    registry.registerFunction("MODE", fn_MODE_SNGL, "(number1, [number2], ...)",
                              "Most frequent number", "Statistics");
    registry.registerFunction("MODE.SNGL", fn_MODE_SNGL, "(number1, [number2], ...)",
                              "Most frequent number", "Statistics");
    registry.registerFunction("MODE_SNGL", fn_MODE_SNGL, "(number1, [number2], ...)",
                              "Most frequent number", "Statistics");
    registry.registerFunction("MODESNGL", fn_MODE_SNGL, "(number1, [number2], ...)",
                              "Most frequent number", "Statistics");
    registry.registerFunction("QUARTILE", fn_QUARTILE_INC, "(array, quart)", "Inclusive quartile",
                              "Statistics");
    registry.registerFunction("QUARTILE.INC", fn_QUARTILE_INC, "(array, quart)",
                              "Inclusive quartile", "Statistics");
    registry.registerFunction("QUARTILE_INC", fn_QUARTILE_INC, "(array, quart)",
                              "Inclusive quartile", "Statistics");
    registry.registerFunction("QUARTILEINC", fn_QUARTILE_INC, "(array, quart)",
                              "Inclusive quartile", "Statistics");
    registry.registerFunction("COUNTBLANK", fn_COUNTBLANK, "(range)", "Count empty cells",
                              "Statistics");
}

}  // namespace cells
