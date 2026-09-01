#include "core/cells/functions/fn_stats.h"

#include <cmath>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

namespace {

// Helper to compute variance from collected values.
// population: true for population variance (n denominator), false for sample variance (n-1)
std::pair<double, EvalResult> computeVarianceFromValues(const std::vector<double>& values,
                                                        bool population) {
    if (values.empty()) {
        return {0.0, EvalResult::Error(CellError::NUM)};
    }

    // For sample variance, need at least 2 values
    if (!population && values.size() < 2) {
        return {0.0, EvalResult::Error(CellError::DIV)};
    }

    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(values.size());

    double sumSquaredDev = 0.0;
    for (const double v : values) {
        const double dev = v - mean;
        sumSquaredDev += dev * dev;
    }

    const double denominator =
        population ? static_cast<double>(values.size()) : static_cast<double>(values.size() - 1);

    return {sumSquaredDev / denominator, EvalResult::Empty()};
}

std::pair<double, EvalResult> computeVariance(const std::vector<const ASTNode*>& args,
                                              EvalContext& ctx, bool population) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return {0.0, error};
    }
    return computeVarianceFromValues(values, population);
}

constexpr double kSqrt2Pi = 2.5066282746310002;  // sqrt(2*pi)
constexpr double kSqrt2 = 1.4142135623730951;

double standardNormalPdf(double x) {
    return std::exp(-0.5 * x * x) / kSqrt2Pi;
}

double standardNormalCdf(double x) {
    return 0.5 * (1.0 + std::erf(x / kSqrt2));
}

std::pair<double, double> meanAndDev(const std::vector<double>& values, bool population) {
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    const double n = static_cast<double>(values.size());
    const double mean = sum / n;
    double ss = 0.0;
    for (const double v : values) {
        const double d = v - mean;
        ss += d * d;
    }
    const double denom = population ? n : n - 1.0;
    return {mean, std::sqrt(ss / denom)};
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

struct LinReg {
    double slope = 0.0;
    double intercept = 0.0;
    double pearson = 0.0;
    double covarP = 0.0;
    double covarS = 0.0;
    double n = 0.0;
    double ssxx = 0.0;
    double ssyy = 0.0;
    double ssxy = 0.0;
    EvalResult error = EvalResult::Empty();
};

LinReg linearRegression(const ASTNode* knownY, const ASTNode* knownX, EvalContext& ctx) {
    LinReg out;
    auto [pairs, err] = collectPairedNumericValues(knownX, knownY, ctx);
    if (err.isError()) {
        out.error = err;
        return out;
    }
    if (pairs.size() < 2) {
        out.error = EvalResult::Error(CellError::DIV);
        return out;
    }
    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& p : pairs) {
        sumX += p.first;
        sumY += p.second;
    }
    const auto n = static_cast<double>(pairs.size());
    const double meanX = sumX / n;
    const double meanY = sumY / n;
    double ssxx = 0.0;
    double ssyy = 0.0;
    double ssxy = 0.0;
    for (const auto& p : pairs) {
        const double dx = p.first - meanX;
        const double dy = p.second - meanY;
        ssxx += dx * dx;
        ssyy += dy * dy;
        ssxy += dx * dy;
    }
    if (ssxx == 0.0) {
        out.error = EvalResult::Error(CellError::DIV);
        return out;
    }
    out.n = n;
    out.ssxx = ssxx;
    out.ssyy = ssyy;
    out.ssxy = ssxy;
    out.slope = ssxy / ssxx;
    out.intercept = meanY - out.slope * meanX;
    out.covarP = ssxy / n;
    out.covarS = ssxy / (n - 1.0);
    const double denom = std::sqrt(ssxx * ssyy);
    out.pearson = denom == 0.0 ? 0.0 : ssxy / denom;
    return out;
}

std::pair<std::vector<double>, EvalResult> collectAverageAValues(
    const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<double> values;
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);
    for (const EvalResult& val : expanded) {
        if (val.isError()) {
            return {{}, val};
        }
        if (val.isEmpty()) {
            continue;
        }
        if (val.isNumber()) {
            values.push_back(val.getNumber());
        } else if (val.isBoolean()) {
            values.push_back(val.getBoolean() ? 1.0 : 0.0);
        } else if (val.isString()) {
            values.push_back(0.0);
        }
    }
    return {values, EvalResult::Empty()};
}

EvalResult percentRankImpl(const std::vector<const ASTNode*>& args, EvalContext& ctx,
                           bool exclusive) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues({args[0]}, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.size() < 2) {
        return EvalResult::Error(CellError::NA);
    }
    const EvalResult xRes = evaluateAsNumber(args[1], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const double x = xRes.getNumber();
    int significance = 3;
    if (args.size() == 3) {
        const EvalResult s = evaluateAsNumber(args[2], ctx);
        if (s.isError()) {
            return s;
        }
        significance = static_cast<int>(std::floor(s.getNumber()));
        if (significance < 1) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    std::sort(values.begin(), values.end());
    if (x < values.front() || x > values.back()) {
        return EvalResult::Error(CellError::NA);
    }
    size_t lt = 0;
    size_t eq = 0;
    for (double v : values) {
        if (v < x) {
            ++lt;
        } else if (v == x) {
            ++eq;
        }
    }
    const auto n = static_cast<double>(values.size());
    double rank = 0.0;
    if (exclusive) {
        rank = (static_cast<double>(lt) + 1.0) / (n + 1.0);
        if (eq == 0) {
            // Interpolate between surrounding values.
            double lo = values.front();
            double hi = values.back();
            size_t loIdx = 0;
            size_t hiIdx = values.size() - 1;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] < x) {
                    lo = values[i];
                    loIdx = i;
                } else if (values[i] > x) {
                    hi = values[i];
                    hiIdx = i;
                    break;
                }
            }
            if (hi != lo) {
                const double loR = (static_cast<double>(loIdx) + 1.0) / (n + 1.0);
                const double hiR = (static_cast<double>(hiIdx) + 1.0) / (n + 1.0);
                rank = loR + (x - lo) / (hi - lo) * (hiR - loR);
            }
        }
    } else {
        if (n == 1.0) {
            rank = 1.0;
        } else if (eq > 0) {
            rank = static_cast<double>(lt) / (n - 1.0);
        } else {
            double lo = values.front();
            double hi = values.back();
            size_t loIdx = 0;
            size_t hiIdx = values.size() - 1;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] < x) {
                    lo = values[i];
                    loIdx = i;
                } else if (values[i] > x) {
                    hi = values[i];
                    hiIdx = i;
                    break;
                }
            }
            const double loR = static_cast<double>(loIdx) / (n - 1.0);
            const double hiR = static_cast<double>(hiIdx) / (n - 1.0);
            rank = loR + (x - lo) / (hi - lo) * (hiR - loR);
        }
    }
    const double scale = std::pow(10.0, significance);
    rank = std::floor(rank * scale + 1e-12) / scale;
    return EvalResult::Number(excelNormalize(rank));
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

EvalResult fn_AVEDEV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(values.size());
    double acc = 0.0;
    for (double v : values) {
        acc += std::abs(v - mean);
    }
    return EvalResult::Number(excelNormalize(acc / static_cast<double>(values.size())));
}

EvalResult fn_DEVSQ(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    const double mean = sum / static_cast<double>(values.size());
    double acc = 0.0;
    for (double v : values) {
        const double d = v - mean;
        acc += d * d;
    }
    return EvalResult::Number(excelNormalize(acc));
}

EvalResult fn_GEOMEAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    double logSum = 0.0;
    for (double v : values) {
        if (v <= 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
        logSum += std::log(v);
    }
    return EvalResult::Number(
        excelNormalize(std::exp(logSum / static_cast<double>(values.size()))));
}

EvalResult fn_HARMEAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    double recip = 0.0;
    for (double v : values) {
        if (v <= 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
        recip += 1.0 / v;
    }
    return EvalResult::Number(excelNormalize(static_cast<double>(values.size()) / recip));
}

EvalResult fn_STANDARDIZE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult x = evaluateAsNumber(args[0], ctx);
    if (x.isError()) {
        return x;
    }
    const EvalResult mean = evaluateAsNumber(args[1], ctx);
    if (mean.isError()) {
        return mean;
    }
    const EvalResult sd = evaluateAsNumber(args[2], ctx);
    if (sd.isError()) {
        return sd;
    }
    if (sd.getNumber() <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize((x.getNumber() - mean.getNumber()) / sd.getNumber()));
}

EvalResult fn_SLOPE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.slope));
}

EvalResult fn_INTERCEPT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.intercept));
}

EvalResult fn_FORECAST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult x = evaluateAsNumber(args[0], ctx);
    if (x.isError()) {
        return x;
    }
    const LinReg r = linearRegression(args[1], args[2], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.intercept + r.slope * x.getNumber()));
}

EvalResult fn_FORECAST_LINEAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_FORECAST(args, ctx);
}

EvalResult fn_PEARSON(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.pearson));
}

EvalResult fn_CORREL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_PEARSON(args, ctx);
}

EvalResult fn_RSQ(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.pearson * r.pearson));
}

EvalResult fn_COVARIANCE_P(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.covarP));
}

EvalResult fn_COVAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_COVARIANCE_P(args, ctx);
}

EvalResult fn_COVARIANCE_S(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    return EvalResult::Number(excelNormalize(r.covarS));
}

EvalResult fn_QUARTILE_EXC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult qRes = evaluateAsNumber(args[1], ctx);
    if (qRes.isError()) {
        return qRes;
    }
    const int quart = static_cast<int>(qRes.getNumber());
    if (quart < 1 || quart > 3) {
        return EvalResult::Error(CellError::NUM);
    }
    NumberLiteralNode kNode(static_cast<double>(quart) / 4.0);
    std::vector<const ASTNode*> pctArgs = {args[0], &kNode};
    return computePercentile(pctArgs, ctx, false);
}

EvalResult fn_RANK_AVG(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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
    size_t better = 0;
    size_t ties = 0;
    for (const double v : values) {
        if (v == number) {
            ++ties;
        } else if (order == 0 && v > number) {
            ++better;
        } else if (order != 0 && v < number) {
            ++better;
        }
    }
    if (ties == 0) {
        return EvalResult::Error(CellError::NA);
    }
    const double avg = static_cast<double>(better) + (static_cast<double>(ties) + 1.0) / 2.0;
    return EvalResult::Number(avg);
}

EvalResult fn_AVERAGEA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectAverageAValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::DIV);
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return EvalResult::Number(excelNormalize(sum / static_cast<double>(values.size())));
}

EvalResult fn_MINA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectAverageAValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Number(0.0);
    }
    double m = values[0];
    for (double v : values) {
        if (v < m) {
            m = v;
        }
    }
    return EvalResult::Number(m);
}

EvalResult fn_MAXA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectAverageAValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Number(0.0);
    }
    double m = values[0];
    for (double v : values) {
        if (v > m) {
            m = v;
        }
    }
    return EvalResult::Number(m);
}

EvalResult fn_PERCENTRANK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return percentRankImpl(args, ctx, false);
}

EvalResult fn_PERCENTRANK_INC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return percentRankImpl(args, ctx, false);
}

EvalResult fn_PERCENTRANK_EXC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return percentRankImpl(args, ctx, true);
}

EvalResult fn_FISHER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const double x = n.getNumber();
    if (x <= -1.0 || x >= 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(0.5 * std::log((1.0 + x) / (1.0 - x))));
}

EvalResult fn_FISHERINV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const double y = n.getNumber();
    const double e2 = std::exp(2.0 * y);
    if (!std::isfinite(e2)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize((e2 - 1.0) / (e2 + 1.0)));
}

EvalResult fn_PHI(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    return EvalResult::Number(excelNormalize(standardNormalPdf(n.getNumber())));
}

EvalResult fn_GAUSS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    return EvalResult::Number(excelNormalize(standardNormalCdf(n.getNumber()) - 0.5));
}

EvalResult fn_SKEW(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.size() < 3) {
        return EvalResult::Error(CellError::DIV);
    }
    auto [mean, s] = meanAndDev(values, false);
    if (s == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    double m3 = 0.0;
    for (const double v : values) {
        const double t = (v - mean) / s;
        m3 += t * t * t;
    }
    const double n = static_cast<double>(values.size());
    return EvalResult::Number(excelNormalize(n / ((n - 1.0) * (n - 2.0)) * m3));
}

EvalResult fn_SKEW_P(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.size() < 3) {
        return EvalResult::Error(CellError::DIV);
    }
    auto [mean, s] = meanAndDev(values, true);
    if (s == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    double m3 = 0.0;
    for (const double v : values) {
        const double t = (v - mean) / s;
        m3 += t * t * t;
    }
    return EvalResult::Number(excelNormalize(m3 / static_cast<double>(values.size())));
}

EvalResult fn_KURT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.size() < 4) {
        return EvalResult::Error(CellError::DIV);
    }
    auto [mean, s] = meanAndDev(values, false);
    if (s == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    double m4 = 0.0;
    for (const double v : values) {
        const double t = (v - mean) / s;
        m4 += t * t * t * t;
    }
    const double n = static_cast<double>(values.size());
    const double a = n * (n + 1.0) / ((n - 1.0) * (n - 2.0) * (n - 3.0));
    const double b = 3.0 * (n - 1.0) * (n - 1.0) / ((n - 2.0) * (n - 3.0));
    return EvalResult::Number(excelNormalize(a * m4 - b));
}

EvalResult fn_VARA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectAverageAValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    auto [var, verr] = computeVarianceFromValues(values, false);
    if (verr.isError()) {
        return verr;
    }
    return EvalResult::Number(excelNormalize(var));
}

EvalResult fn_VARPA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectAverageAValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    auto [var, verr] = computeVarianceFromValues(values, true);
    if (verr.isError()) {
        return verr;
    }
    return EvalResult::Number(excelNormalize(var));
}

EvalResult fn_STDEVA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    const EvalResult v = fn_VARA(args, ctx);
    if (v.isError()) {
        return v;
    }
    return EvalResult::Number(excelNormalize(std::sqrt(v.getNumber())));
}

EvalResult fn_STDEVPA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    const EvalResult v = fn_VARPA(args, ctx);
    if (v.isError()) {
        return v;
    }
    return EvalResult::Number(excelNormalize(std::sqrt(v.getNumber())));
}

EvalResult fn_STEYX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const LinReg r = linearRegression(args[0], args[1], ctx);
    if (r.error.isError()) {
        return r.error;
    }
    if (r.n < 3.0) {
        return EvalResult::Error(CellError::DIV);
    }
    const double sse = r.ssyy - (r.ssxy * r.ssxy / r.ssxx);
    if (sse < 0.0) {
        return EvalResult::Number(0.0);
    }
    return EvalResult::Number(excelNormalize(std::sqrt(sse / (r.n - 2.0))));
}

EvalResult fn_NORMSDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    return EvalResult::Number(excelNormalize(standardNormalCdf(n.getNumber())));
}

EvalResult fn_NORM_S_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult z = evaluateAsNumber(args[0], ctx);
    if (z.isError()) {
        return z;
    }
    const EvalResult cum = evaluateAsBoolean(args[1], ctx);
    if (cum.isError()) {
        return cum;
    }
    const double x = z.getNumber();
    if (cum.getBoolean()) {
        return EvalResult::Number(excelNormalize(standardNormalCdf(x)));
    }
    return EvalResult::Number(excelNormalize(standardNormalPdf(x)));
}

EvalResult fn_NORMDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    double x = 0.0;
    double mean = 0.0;
    double stdev = 0.0;
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    x = xRes.getNumber();
    const EvalResult mRes = evaluateAsNumber(args[1], ctx);
    if (mRes.isError()) {
        return mRes;
    }
    mean = mRes.getNumber();
    const EvalResult sRes = evaluateAsNumber(args[2], ctx);
    if (sRes.isError()) {
        return sRes;
    }
    stdev = sRes.getNumber();
    if (stdev <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const EvalResult cum = evaluateAsBoolean(args[3], ctx);
    if (cum.isError()) {
        return cum;
    }
    const double z = (x - mean) / stdev;
    if (cum.getBoolean()) {
        return EvalResult::Number(excelNormalize(standardNormalCdf(z)));
    }
    return EvalResult::Number(excelNormalize(standardNormalPdf(z) / stdev));
}

EvalResult fn_NORM_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_NORMDIST(args, ctx);
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
    registry.registerFunction("AVEDEV", fn_AVEDEV, "(number1, [number2], ...)",
                              "Average of absolute deviations", "Statistics");
    registry.registerFunction("DEVSQ", fn_DEVSQ, "(number1, [number2], ...)",
                              "Sum of squared deviations", "Statistics");
    registry.registerFunction("GEOMEAN", fn_GEOMEAN, "(number1, [number2], ...)", "Geometric mean",
                              "Statistics");
    registry.registerFunction("HARMEAN", fn_HARMEAN, "(number1, [number2], ...)", "Harmonic mean",
                              "Statistics");
    registry.registerFunction("STANDARDIZE", fn_STANDARDIZE, "(x, mean, standard_dev)",
                              "Normalized value from a distribution", "Statistics");
    registry.registerFunction("FORECAST", fn_FORECAST, "(x, known_y's, known_x's)",
                              "Linear forecast of y for x", "Statistics");
    registry.registerFunction("FORECAST.LINEAR", fn_FORECAST_LINEAR, "(x, known_y's, known_x's)",
                              "Linear forecast of y for x", "Statistics");
    registry.registerAlias("FORECAST_LINEAR", "FORECAST.LINEAR");
    registry.registerFunction("SLOPE", fn_SLOPE, "(known_y's, known_x's)",
                              "Slope of linear regression", "Statistics");
    registry.registerFunction("INTERCEPT", fn_INTERCEPT, "(known_y's, known_x's)",
                              "Intercept of linear regression", "Statistics");
    registry.registerFunction("PEARSON", fn_PEARSON, "(array1, array2)",
                              "Pearson correlation coefficient", "Statistics");
    registry.registerFunction("CORREL", fn_CORREL, "(array1, array2)", "Correlation coefficient",
                              "Statistics");
    registry.registerFunction("RSQ", fn_RSQ, "(known_y's, known_x's)",
                              "Square of Pearson correlation", "Statistics");
    registry.registerFunction("COVAR", fn_COVAR, "(array1, array2)", "Population covariance",
                              "Statistics");
    registry.registerFunction("COVARIANCE.P", fn_COVARIANCE_P, "(array1, array2)",
                              "Population covariance", "Statistics");
    registry.registerAlias("COVARIANCE_P", "COVARIANCE.P");
    registry.registerFunction("COVARIANCE.S", fn_COVARIANCE_S, "(array1, array2)",
                              "Sample covariance", "Statistics");
    registry.registerAlias("COVARIANCE_S", "COVARIANCE.S");
    registry.registerFunction("QUARTILE.EXC", fn_QUARTILE_EXC, "(array, quart)",
                              "Exclusive quartile", "Statistics");
    registry.registerAlias("QUARTILE_EXC", "QUARTILE.EXC");
    registry.registerAlias("QUARTILEEXC", "QUARTILE.EXC");
    registry.registerFunction("RANK.AVG", fn_RANK_AVG, "(number, ref, [order])",
                              "Rank of a number (ties averaged)", "Statistics");
    registry.registerAlias("RANK_AVG", "RANK.AVG");
    registry.registerAlias("RANKAVG", "RANK.AVG");
    registry.registerFunction("AVERAGEA", fn_AVERAGEA, "(value1, [value2], ...)",
                              "Average including text and logicals", "Statistics");
    registry.registerFunction("MINA", fn_MINA, "(value1, [value2], ...)",
                              "Minimum including text and logicals", "Statistics");
    registry.registerFunction("MAXA", fn_MAXA, "(value1, [value2], ...)",
                              "Maximum including text and logicals", "Statistics");
    registry.registerFunction("PERCENTRANK", fn_PERCENTRANK, "(array, x, [significance])",
                              "Percent rank of a value", "Statistics");
    registry.registerFunction("PERCENTRANK.INC", fn_PERCENTRANK_INC, "(array, x, [significance])",
                              "Inclusive percent rank", "Statistics");
    registry.registerAlias("PERCENTRANK_INC", "PERCENTRANK.INC");
    registry.registerFunction("PERCENTRANK.EXC", fn_PERCENTRANK_EXC, "(array, x, [significance])",
                              "Exclusive percent rank", "Statistics");
    registry.registerAlias("PERCENTRANK_EXC", "PERCENTRANK.EXC");

    // Excel dotted names (XLSX import also stores concatenated/underscore forms).
    registry.registerAlias("STDEV.S", "STDEVS");
    registry.registerAlias("STDEV.P", "STDEVP");
    registry.registerAlias("STDEV_S", "STDEVS");
    registry.registerAlias("STDEV_P", "STDEVP");
    registry.registerAlias("VAR.S", "VARS");
    registry.registerAlias("VAR.P", "VARP");
    registry.registerAlias("VAR_S", "VARS");
    registry.registerAlias("VAR_P", "VARP");
    registry.registerAlias("PERCENTILE.INC", "PERCENTILEINC");
    registry.registerAlias("PERCENTILE.EXC", "PERCENTILEEXC");
    registry.registerAlias("PERCENTILE_INC", "PERCENTILEINC");
    registry.registerAlias("PERCENTILE_EXC", "PERCENTILEEXC");

    registry.registerFunction("FISHER", fn_FISHER, "(x)", "Fisher transformation", "Statistics");
    registry.registerFunction("FISHERINV", fn_FISHERINV, "(y)", "Inverse Fisher transformation",
                              "Statistics");
    registry.registerFunction("PHI", fn_PHI, "(x)", "Standard normal probability density",
                              "Statistics");
    registry.registerFunction("GAUSS", fn_GAUSS, "(z)", "Standard normal CDF minus 0.5",
                              "Statistics");
    registry.registerFunction("SKEW", fn_SKEW, "(number1, [number2], ...)", "Sample skewness",
                              "Statistics");
    registry.registerFunction("SKEW.P", fn_SKEW_P, "(number1, [number2], ...)",
                              "Population skewness", "Statistics");
    registry.registerAlias("SKEW_P", "SKEW.P");
    registry.registerFunction("KURT", fn_KURT, "(number1, [number2], ...)",
                              "Sample excess kurtosis", "Statistics");
    registry.registerFunction("STDEVA", fn_STDEVA, "(value1, [value2], ...)",
                              "Sample standard deviation including text/logicals", "Statistics");
    registry.registerFunction("STDEVPA", fn_STDEVPA, "(value1, [value2], ...)",
                              "Population standard deviation including text/logicals",
                              "Statistics");
    registry.registerFunction("VARA", fn_VARA, "(value1, [value2], ...)",
                              "Sample variance including text/logicals", "Statistics");
    registry.registerFunction("VARPA", fn_VARPA, "(value1, [value2], ...)",
                              "Population variance including text/logicals", "Statistics");
    registry.registerFunction("STEYX", fn_STEYX, "(known_y's, known_x's)",
                              "Standard error of the predicted y-value", "Statistics");
    registry.registerFunction("NORMSDIST", fn_NORMSDIST, "(z)",
                              "Standard normal cumulative distribution", "Statistics");
    registry.registerFunction("NORM.S.DIST", fn_NORM_S_DIST, "(z, cumulative)",
                              "Standard normal distribution", "Statistics");
    registry.registerAlias("NORM_S_DIST", "NORM.S.DIST");
    registry.registerFunction("NORMDIST", fn_NORMDIST, "(x, mean, standard_dev, cumulative)",
                              "Normal distribution", "Statistics");
    registry.registerFunction("NORM.DIST", fn_NORM_DIST, "(x, mean, standard_dev, cumulative)",
                              "Normal distribution", "Statistics");
    registry.registerAlias("NORM_DIST", "NORM.DIST");
}

}  // namespace cells
