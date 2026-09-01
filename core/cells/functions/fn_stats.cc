#include "core/cells/functions/fn_stats.h"

#include <cmath>

#include <algorithm>
#include <functional>
#include <limits>
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

double standardNormalInv(double p) {
    // Acklam's rational approximation, then Newton using our erf CDF.
    static const double a1 = -3.969683028665376e+01;
    static const double a2 = 2.209460984245205e+02;
    static const double a3 = -2.759285104469687e+02;
    static const double a4 = 1.383577509590705e+02;
    static const double a5 = -3.066479806614716e+01;
    static const double a6 = 2.506628277459239e+00;
    static const double b1 = -5.447609879822406e+01;
    static const double b2 = 1.615858368580409e+02;
    static const double b3 = -1.556989798598866e+02;
    static const double b4 = 6.680131188771972e+01;
    static const double b5 = -1.328068155288572e+01;
    static const double c1 = -7.784894002430293e-03;
    static const double c2 = -3.223964580411365e-01;
    static const double c3 = -2.400758277161838e+00;
    static const double c4 = -2.549732539343734e+00;
    static const double c5 = 4.374664141464968e+00;
    static const double c6 = 2.938163982698783e+00;
    static const double d1 = 7.784695709041462e-03;
    static const double d2 = 3.224671290700398e-01;
    static const double d3 = 2.445134137142996e+00;
    static const double d4 = 3.754408661907416e+00;
    const double pLow = 0.02425;
    const double pHigh = 1.0 - pLow;
    double x = 0.0;
    if (p < pLow) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
            ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    } else if (p <= pHigh) {
        const double q = p - 0.5;
        const double r = q * q;
        x = (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
            (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
            ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    }
    for (int i = 0; i < 3; ++i) {
        const double pdf = standardNormalPdf(x);
        if (pdf == 0.0) {
            break;
        }
        x -= (standardNormalCdf(x) - p) / pdf;
    }
    return x;
}

double poissonPmf(int k, double lambda) {
    double p = std::exp(-lambda);
    for (int i = 1; i <= k; ++i) {
        p *= lambda / static_cast<double>(i);
    }
    return p;
}

int truncNonNeg(double x) {
    return static_cast<int>(x);
}

double logCombin(int n, int k) {
    if (k < 0 || k > n) {
        return -std::numeric_limits<double>::infinity();
    }
    return std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0);
}

double binomPmf(int k, int n, double p) {
    if (k < 0 || k > n) {
        return 0.0;
    }
    if (p == 0.0) {
        return k == 0 ? 1.0 : 0.0;
    }
    if (p == 1.0) {
        return k == n ? 1.0 : 0.0;
    }
    const double logp = logCombin(n, k) + static_cast<double>(k) * std::log(p) +
                        static_cast<double>(n - k) * std::log(1.0 - p);
    return std::exp(logp);
}

double binomCdf(int k, int n, double p) {
    if (k < 0) {
        return 0.0;
    }
    if (k >= n) {
        return 1.0;
    }
    double pmf = binomPmf(0, n, p);
    double cdf = pmf;
    const double odds = (p == 1.0) ? 0.0 : p / (1.0 - p);
    for (int i = 0; i < k; ++i) {
        pmf *= static_cast<double>(n - i) / static_cast<double>(i + 1) * odds;
        cdf += pmf;
    }
    if (cdf > 1.0) {
        return 1.0;
    }
    return cdf;
}

int binomInv(int n, double p, double alpha) {
    double cdf = 0.0;
    for (int k = 0; k <= n; ++k) {
        cdf += binomPmf(k, n, p);
        if (cdf >= alpha) {
            return k;
        }
    }
    return n;
}

double hypgeomPmf(int k, int n, int K, int N) {
    const int lo = std::max(0, n - (N - K));
    const int hi = std::min(n, K);
    if (k < lo || k > hi) {
        return 0.0;
    }
    return std::exp(logCombin(K, k) + logCombin(N - K, n - k) - logCombin(N, n));
}

double hypgeomCdf(int k, int n, int K, int N) {
    const int lo = std::max(0, n - (N - K));
    const int hi = std::min(n, K);
    if (k < lo) {
        return 0.0;
    }
    if (k >= hi) {
        return 1.0;
    }
    double sum = 0.0;
    for (int i = lo; i <= k; ++i) {
        sum += hypgeomPmf(i, n, K, N);
    }
    if (sum > 1.0) {
        return 1.0;
    }
    return sum;
}

double negbinomPmf(int k, int r, double p) {
    if (k < 0 || r < 1) {
        return 0.0;
    }
    if (p == 0.0) {
        return 0.0;
    }
    if (p == 1.0) {
        return k == 0 ? 1.0 : 0.0;
    }
    const double logp = logCombin(k + r - 1, k) + static_cast<double>(r) * std::log(p) +
                        static_cast<double>(k) * std::log(1.0 - p);
    return std::exp(logp);
}

double negbinomCdf(int k, int r, double p) {
    double sum = 0.0;
    for (int i = 0; i <= k; ++i) {
        sum += negbinomPmf(i, r, p);
    }
    if (sum > 1.0) {
        return 1.0;
    }
    return sum;
}

double gammaSeries(double a, double x) {
    double ap = a;
    double sum = 1.0 / a;
    double del = sum;
    for (int n = 0; n < 200; ++n) {
        ap += 1.0;
        del *= x / ap;
        sum += del;
        if (std::fabs(del) < std::fabs(sum) * 1e-15) {
            break;
        }
    }
    return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}

double gammaContinued(double a, double x) {
    constexpr double kFpmin = 1e-300;
    double b = x + 1.0 - a;
    double c = 1.0 / kFpmin;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= 200; ++i) {
        const double an = -static_cast<double>(i) * (static_cast<double>(i) - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < kFpmin) {
            d = kFpmin;
        }
        c = b + an / c;
        if (std::fabs(c) < kFpmin) {
            c = kFpmin;
        }
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-15) {
            break;
        }
    }
    return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}

// Regularized lower incomplete gamma P(a, x) = γ(a,x)/Γ(a).
double regularizedGammaP(double a, double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    if (a <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double p = x < a + 1.0 ? gammaSeries(a, x) : 1.0 - gammaContinued(a, x);
    if (p < 0.0) {
        return 0.0;
    }
    if (p > 1.0) {
        return 1.0;
    }
    return p;
}

double gammaPdf(double x, double alpha, double beta) {
    if (x < 0.0) {
        return 0.0;
    }
    if (x == 0.0) {
        if (alpha < 1.0) {
            return std::numeric_limits<double>::infinity();
        }
        if (alpha == 1.0) {
            return 1.0 / beta;
        }
        return 0.0;
    }
    return std::exp((alpha - 1.0) * std::log(x) - x / beta - alpha * std::log(beta) -
                    std::lgamma(alpha));
}

double gammaInv(double p, double alpha, double beta) {
    if (p <= 0.0) {
        return 0.0;
    }
    // Wilson-Hilferty approximation, then Newton with bisection bounds.
    const double z = standardNormalInv(p);
    const double a3 = 1.0 / (9.0 * alpha);
    double x = alpha * beta * std::pow(1.0 - a3 + z * std::sqrt(a3), 3.0);
    if (!(x > 0.0) || !std::isfinite(x)) {
        x = alpha * beta;
    }
    double lo = 0.0;
    double hi = std::max(x * 2.0, alpha * beta * 4.0);
    while (regularizedGammaP(alpha, hi / beta) < p) {
        hi *= 2.0;
        if (hi > 1e300) {
            break;
        }
    }
    for (int i = 0; i < 40; ++i) {
        const double cdf = regularizedGammaP(alpha, x / beta);
        const double pdf = gammaPdf(x, alpha, beta);
        double next = x;
        if (pdf > 0.0 && std::isfinite(pdf)) {
            next = x - (cdf - p) / pdf;
        }
        if (!(next > lo && next < hi) || !std::isfinite(next)) {
            next = 0.5 * (lo + hi);
        }
        if (regularizedGammaP(alpha, next / beta) > p) {
            hi = next;
        } else {
            lo = next;
        }
        if (std::fabs(next - x) <= 1e-14 * std::max(1.0, std::fabs(x))) {
            x = next;
            break;
        }
        x = next;
    }
    return x;
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

EvalResult fn_NORMSINV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult p = evaluateAsNumber(args[0], ctx);
    if (p.isError()) {
        return p;
    }
    if (p.getNumber() <= 0.0 || p.getNumber() >= 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(standardNormalInv(p.getNumber())));
}

EvalResult fn_NORM_S_INV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_NORMSINV(args, ctx);
}

EvalResult fn_NORMINV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult p = evaluateAsNumber(args[0], ctx);
    if (p.isError()) {
        return p;
    }
    const EvalResult mean = evaluateAsNumber(args[1], ctx);
    if (mean.isError()) {
        return mean;
    }
    const EvalResult stdev = evaluateAsNumber(args[2], ctx);
    if (stdev.isError()) {
        return stdev;
    }
    if (p.getNumber() <= 0.0 || p.getNumber() >= 1.0 || stdev.getNumber() <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(
        excelNormalize(mean.getNumber() + stdev.getNumber() * standardNormalInv(p.getNumber())));
}

EvalResult fn_NORM_INV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_NORMINV(args, ctx);
}

EvalResult fn_TRIMMEAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, error] = collectNumericValues({args[0]}, ctx);
    if (error.isError()) {
        return error;
    }
    const EvalResult percentRes = evaluateAsNumber(args[1], ctx);
    if (percentRes.isError()) {
        return percentRes;
    }
    const double percent = percentRes.getNumber();
    if (percent < 0.0 || percent >= 1.0 || values.empty()) {
        return EvalResult::Error(CellError::NUM);
    }
    std::sort(values.begin(), values.end());
    const int drop =
        static_cast<int>(std::floor(static_cast<double>(values.size()) * percent / 2.0));
    if (drop * 2 >= static_cast<int>(values.size())) {
        return EvalResult::Error(CellError::NUM);
    }
    double sum = 0.0;
    const int end = static_cast<int>(values.size()) - drop;
    for (int i = drop; i < end; ++i) {
        sum += values[static_cast<size_t>(i)];
    }
    return EvalResult::Number(
        excelNormalize(sum / static_cast<double>(values.size() - static_cast<size_t>(drop) * 2)));
}

EvalResult fn_POISSON(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult meanRes = evaluateAsNumber(args[1], ctx);
    if (meanRes.isError()) {
        return meanRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[2], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    if (xRes.getNumber() < 0.0 || meanRes.getNumber() < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const int k = static_cast<int>(xRes.getNumber());
    const double lambda = meanRes.getNumber();
    if (cumRes.getBoolean()) {
        double cdf = 0.0;
        for (int i = 0; i <= k; ++i) {
            cdf += poissonPmf(i, lambda);
        }
        return EvalResult::Number(excelNormalize(cdf));
    }
    return EvalResult::Number(excelNormalize(poissonPmf(k, lambda)));
}

EvalResult fn_POISSON_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_POISSON(args, ctx);
}

EvalResult fn_EXPONDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult lambdaRes = evaluateAsNumber(args[1], ctx);
    if (lambdaRes.isError()) {
        return lambdaRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[2], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double x = xRes.getNumber();
    const double lambda = lambdaRes.getNumber();
    if (x < 0.0 || lambda <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    if (cumRes.getBoolean()) {
        return EvalResult::Number(excelNormalize(1.0 - std::exp(-lambda * x)));
    }
    return EvalResult::Number(excelNormalize(lambda * std::exp(-lambda * x)));
}

EvalResult fn_EXPON_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_EXPONDIST(args, ctx);
}

EvalResult fn_CONFIDENCE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult alpha = evaluateAsNumber(args[0], ctx);
    if (alpha.isError()) {
        return alpha;
    }
    const EvalResult stdev = evaluateAsNumber(args[1], ctx);
    if (stdev.isError()) {
        return stdev;
    }
    const EvalResult size = evaluateAsNumber(args[2], ctx);
    if (size.isError()) {
        return size;
    }
    if (alpha.getNumber() <= 0.0 || alpha.getNumber() >= 1.0 || stdev.getNumber() <= 0.0 ||
        size.getNumber() < 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double z = standardNormalInv(1.0 - alpha.getNumber() / 2.0);
    return EvalResult::Number(excelNormalize(z * stdev.getNumber() / std::sqrt(size.getNumber())));
}

EvalResult fn_CONFIDENCE_NORM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_CONFIDENCE(args, ctx);
}

EvalResult fn_MODE_MULT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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
    std::vector<EvalResult> modes;
    for (size_t i = 0; i < seen.size(); ++i) {
        if (counts[i] == counts[best]) {
            modes.push_back(EvalResult::Number(seen[i]));
        }
    }
    return EvalResult::ColumnArray(std::move(modes));
}

namespace {

EvalResult requireTruncInt(const ASTNode* arg, EvalContext& ctx, int* out, bool allowNeg) {
    const EvalResult n = evaluateAsNumber(arg, ctx);
    if (n.isError()) {
        return n;
    }
    if (!allowNeg && n.getNumber() < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    *out = truncNonNeg(n.getNumber());
    return EvalResult::Empty();
}

}  // namespace

EvalResult fn_BINOMDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    int ks = 0;
    int trials = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &ks, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[1], ctx, &trials, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult pRes = evaluateAsNumber(args[2], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[3], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double p = pRes.getNumber();
    if (p < 0.0 || p > 1.0 || ks > trials) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v = cumRes.getBoolean() ? binomCdf(ks, trials, p) : binomPmf(ks, trials, p);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
}

EvalResult fn_BINOM_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_BINOMDIST(args, ctx);
}

EvalResult fn_BINOM_INV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    int trials = 0;
    const EvalResult e = requireTruncInt(args[0], ctx, &trials, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult pRes = evaluateAsNumber(args[1], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const EvalResult aRes = evaluateAsNumber(args[2], ctx);
    if (aRes.isError()) {
        return aRes;
    }
    const double p = pRes.getNumber();
    const double alpha = aRes.getNumber();
    if (p < 0.0 || p > 1.0 || alpha <= 0.0 || alpha >= 1.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(static_cast<double>(binomInv(trials, p, alpha)));
}

EvalResult fn_CRITBINOM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_BINOM_INV(args, ctx);
}

EvalResult fn_BINOM_DIST_RANGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    int trials = 0;
    int s1 = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &trials, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult pRes = evaluateAsNumber(args[1], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    e = requireTruncInt(args[2], ctx, &s1, false);
    if (e.isError()) {
        return e;
    }
    int s2 = s1;
    if (args.size() == 4) {
        e = requireTruncInt(args[3], ctx, &s2, false);
        if (e.isError()) {
            return e;
        }
    }
    const double p = pRes.getNumber();
    if (p < 0.0 || p > 1.0 || s1 > trials || s2 > trials || s2 < s1) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v = args.size() == 4
                         ? binomCdf(s2, trials, p) - (s1 > 0 ? binomCdf(s1 - 1, trials, p) : 0.0)
                         : binomPmf(s1, trials, p);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
}

EvalResult fn_WEIBULL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult aRes = evaluateAsNumber(args[1], ctx);
    if (aRes.isError()) {
        return aRes;
    }
    const EvalResult bRes = evaluateAsNumber(args[2], ctx);
    if (bRes.isError()) {
        return bRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[3], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double x = xRes.getNumber();
    const double alpha = aRes.getNumber();
    const double beta = bRes.getNumber();
    if (x < 0.0 || alpha <= 0.0 || beta <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double xb = x / beta;
    if (cumRes.getBoolean()) {
        return EvalResult::Number(excelNormalize(1.0 - std::exp(-std::pow(xb, alpha))));
    }
    const double pdf = (alpha / beta) * std::pow(xb, alpha - 1.0) * std::exp(-std::pow(xb, alpha));
    if (!std::isfinite(pdf)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(pdf));
}

EvalResult fn_WEIBULL_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_WEIBULL(args, ctx);
}

EvalResult fn_LOGNORM_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult meanRes = evaluateAsNumber(args[1], ctx);
    if (meanRes.isError()) {
        return meanRes;
    }
    const EvalResult sdRes = evaluateAsNumber(args[2], ctx);
    if (sdRes.isError()) {
        return sdRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[3], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double x = xRes.getNumber();
    const double sd = sdRes.getNumber();
    if (x <= 0.0 || sd <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double z = (std::log(x) - meanRes.getNumber()) / sd;
    if (cumRes.getBoolean()) {
        return EvalResult::Number(excelNormalize(standardNormalCdf(z)));
    }
    return EvalResult::Number(excelNormalize(standardNormalPdf(z) / (x * sd)));
}

EvalResult fn_LOGNORMDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult meanRes = evaluateAsNumber(args[1], ctx);
    if (meanRes.isError()) {
        return meanRes;
    }
    const EvalResult sdRes = evaluateAsNumber(args[2], ctx);
    if (sdRes.isError()) {
        return sdRes;
    }
    const double x = xRes.getNumber();
    const double sd = sdRes.getNumber();
    if (x <= 0.0 || sd <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double z = (std::log(x) - meanRes.getNumber()) / sd;
    return EvalResult::Number(excelNormalize(standardNormalCdf(z)));
}

EvalResult fn_LOGNORM_INV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult pRes = evaluateAsNumber(args[0], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const EvalResult meanRes = evaluateAsNumber(args[1], ctx);
    if (meanRes.isError()) {
        return meanRes;
    }
    const EvalResult sdRes = evaluateAsNumber(args[2], ctx);
    if (sdRes.isError()) {
        return sdRes;
    }
    if (pRes.getNumber() <= 0.0 || pRes.getNumber() >= 1.0 || sdRes.getNumber() <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double z = standardNormalInv(pRes.getNumber());
    return EvalResult::Number(
        excelNormalize(std::exp(meanRes.getNumber() + sdRes.getNumber() * z)));
}

EvalResult fn_LOGINV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_LOGNORM_INV(args, ctx);
}

EvalResult fn_GAMMA_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    const EvalResult aRes = evaluateAsNumber(args[1], ctx);
    if (aRes.isError()) {
        return aRes;
    }
    const EvalResult bRes = evaluateAsNumber(args[2], ctx);
    if (bRes.isError()) {
        return bRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[3], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double x = xRes.getNumber();
    const double alpha = aRes.getNumber();
    const double beta = bRes.getNumber();
    if (x < 0.0 || alpha <= 0.0 || beta <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    if (cumRes.getBoolean()) {
        return EvalResult::Number(excelNormalize(regularizedGammaP(alpha, x / beta)));
    }
    const double pdf = gammaPdf(x, alpha, beta);
    if (!std::isfinite(pdf)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(pdf));
}

EvalResult fn_GAMMADIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_GAMMA_DIST(args, ctx);
}

EvalResult fn_GAMMA_INV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult pRes = evaluateAsNumber(args[0], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const EvalResult aRes = evaluateAsNumber(args[1], ctx);
    if (aRes.isError()) {
        return aRes;
    }
    const EvalResult bRes = evaluateAsNumber(args[2], ctx);
    if (bRes.isError()) {
        return bRes;
    }
    const double p = pRes.getNumber();
    const double alpha = aRes.getNumber();
    const double beta = bRes.getNumber();
    if (p < 0.0 || p >= 1.0 || alpha <= 0.0 || beta <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double x = gammaInv(p, alpha, beta);
    if (!std::isfinite(x) || x < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(x));
}

EvalResult fn_GAMMAINV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_GAMMA_INV(args, ctx);
}

EvalResult fn_HYPGEOM_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    int sampleS = 0;
    int numberSample = 0;
    int popS = 0;
    int popN = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &sampleS, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[1], ctx, &numberSample, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[2], ctx, &popS, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[3], ctx, &popN, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[4], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    if (numberSample > popN || popS > popN || sampleS > numberSample || sampleS > popS) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v = cumRes.getBoolean() ? hypgeomCdf(sampleS, numberSample, popS, popN)
                                         : hypgeomPmf(sampleS, numberSample, popS, popN);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
}

EvalResult fn_HYPGEOMDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    int sampleS = 0;
    int numberSample = 0;
    int popS = 0;
    int popN = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &sampleS, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[1], ctx, &numberSample, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[2], ctx, &popS, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[3], ctx, &popN, false);
    if (e.isError()) {
        return e;
    }
    if (numberSample > popN || popS > popN || sampleS > numberSample || sampleS > popS) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v = hypgeomPmf(sampleS, numberSample, popS, popN);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
}

EvalResult fn_NEGBINOM_DIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    int fails = 0;
    int success = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &fails, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[1], ctx, &success, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult pRes = evaluateAsNumber(args[2], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const EvalResult cumRes = evaluateAsBoolean(args[3], ctx);
    if (cumRes.isError()) {
        return cumRes;
    }
    const double p = pRes.getNumber();
    if (p <= 0.0 || p > 1.0 || success < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v =
        cumRes.getBoolean() ? negbinomCdf(fails, success, p) : negbinomPmf(fails, success, p);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
}

EvalResult fn_NEGBINOMDIST(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    int fails = 0;
    int success = 0;
    EvalResult e = requireTruncInt(args[0], ctx, &fails, false);
    if (e.isError()) {
        return e;
    }
    e = requireTruncInt(args[1], ctx, &success, false);
    if (e.isError()) {
        return e;
    }
    const EvalResult pRes = evaluateAsNumber(args[2], ctx);
    if (pRes.isError()) {
        return pRes;
    }
    const double p = pRes.getNumber();
    if (p <= 0.0 || p > 1.0 || success < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    const double v = negbinomPmf(fails, success, p);
    if (!std::isfinite(v)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(v));
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
    registry.registerFunction("NORMSINV", fn_NORMSINV, "(probability)",
                              "Inverse of the standard normal cumulative distribution",
                              "Statistics");
    registry.registerFunction("NORM.S.INV", fn_NORM_S_INV, "(probability)",
                              "Inverse of the standard normal cumulative distribution",
                              "Statistics");
    registry.registerAlias("NORM_S_INV", "NORM.S.INV");
    registry.registerFunction("NORMINV", fn_NORMINV, "(probability, mean, standard_dev)",
                              "Inverse of the normal cumulative distribution", "Statistics");
    registry.registerFunction("NORM.INV", fn_NORM_INV, "(probability, mean, standard_dev)",
                              "Inverse of the normal cumulative distribution", "Statistics");
    registry.registerAlias("NORM_INV", "NORM.INV");
    registry.registerFunction("TRIMMEAN", fn_TRIMMEAN, "(array, percent)",
                              "Mean excluding a fraction of data from the tails", "Statistics");
    registry.registerFunction("POISSON", fn_POISSON, "(x, mean, cumulative)",
                              "Poisson distribution", "Statistics");
    registry.registerFunction("POISSON.DIST", fn_POISSON_DIST, "(x, mean, cumulative)",
                              "Poisson distribution", "Statistics");
    registry.registerAlias("POISSON_DIST", "POISSON.DIST");
    registry.registerFunction("EXPONDIST", fn_EXPONDIST, "(x, lambda, cumulative)",
                              "Exponential distribution", "Statistics");
    registry.registerFunction("EXPON.DIST", fn_EXPON_DIST, "(x, lambda, cumulative)",
                              "Exponential distribution", "Statistics");
    registry.registerAlias("EXPON_DIST", "EXPON.DIST");
    registry.registerFunction("CONFIDENCE", fn_CONFIDENCE, "(alpha, standard_dev, size)",
                              "Confidence interval for a normal population", "Statistics");
    registry.registerFunction("CONFIDENCE.NORM", fn_CONFIDENCE_NORM, "(alpha, standard_dev, size)",
                              "Confidence interval for a normal population", "Statistics");
    registry.registerAlias("CONFIDENCE_NORM", "CONFIDENCE.NORM");
    registry.registerFunction("MODE.MULT", fn_MODE_MULT, "(number1, [number2], ...)",
                              "Vertical array of all modes", "Statistics");
    registry.registerAlias("MODE_MULT", "MODE.MULT");
    registry.registerFunction("BINOMDIST", fn_BINOMDIST,
                              "(number_s, trials, probability_s, cumulative)",
                              "Binomial distribution", "Statistics");
    registry.registerFunction("BINOM.DIST", fn_BINOM_DIST,
                              "(number_s, trials, probability_s, cumulative)",
                              "Binomial distribution", "Statistics");
    registry.registerAlias("BINOM_DIST", "BINOM.DIST");
    registry.registerFunction("BINOM.INV", fn_BINOM_INV, "(trials, probability_s, alpha)",
                              "Smallest value for which the binomial CDF is at least alpha",
                              "Statistics");
    registry.registerAlias("BINOM_INV", "BINOM.INV");
    registry.registerFunction("CRITBINOM", fn_CRITBINOM, "(trials, probability_s, alpha)",
                              "Smallest value for which the binomial CDF is at least alpha",
                              "Statistics");
    registry.registerFunction("BINOM.DIST.RANGE", fn_BINOM_DIST_RANGE,
                              "(trials, probability_s, number_s, [number_s2])",
                              "Probability of a binomial trial result in a range", "Statistics");
    registry.registerAlias("BINOM_DIST_RANGE", "BINOM.DIST.RANGE");
    registry.registerFunction("WEIBULL", fn_WEIBULL, "(x, alpha, beta, cumulative)",
                              "Weibull distribution", "Statistics");
    registry.registerFunction("WEIBULL.DIST", fn_WEIBULL_DIST, "(x, alpha, beta, cumulative)",
                              "Weibull distribution", "Statistics");
    registry.registerAlias("WEIBULL_DIST", "WEIBULL.DIST");
    registry.registerFunction("LOGNORMDIST", fn_LOGNORMDIST, "(x, mean, standard_dev)",
                              "Lognormal cumulative distribution", "Statistics");
    registry.registerFunction("LOGNORM.DIST", fn_LOGNORM_DIST,
                              "(x, mean, standard_dev, cumulative)", "Lognormal distribution",
                              "Statistics");
    registry.registerAlias("LOGNORM_DIST", "LOGNORM.DIST");
    registry.registerFunction("LOGINV", fn_LOGINV, "(probability, mean, standard_dev)",
                              "Inverse lognormal cumulative distribution", "Statistics");
    registry.registerFunction("LOGNORM.INV", fn_LOGNORM_INV, "(probability, mean, standard_dev)",
                              "Inverse lognormal cumulative distribution", "Statistics");
    registry.registerAlias("LOGNORM_INV", "LOGNORM.INV");
    registry.registerFunction("GAMMADIST", fn_GAMMADIST, "(x, alpha, beta, cumulative)",
                              "Gamma distribution", "Statistics");
    registry.registerFunction("GAMMA.DIST", fn_GAMMA_DIST, "(x, alpha, beta, cumulative)",
                              "Gamma distribution", "Statistics");
    registry.registerAlias("GAMMA_DIST", "GAMMA.DIST");
    registry.registerFunction("GAMMAINV", fn_GAMMAINV, "(probability, alpha, beta)",
                              "Inverse gamma cumulative distribution", "Statistics");
    registry.registerFunction("GAMMA.INV", fn_GAMMA_INV, "(probability, alpha, beta)",
                              "Inverse gamma cumulative distribution", "Statistics");
    registry.registerAlias("GAMMA_INV", "GAMMA.INV");
    registry.registerFunction("HYPGEOMDIST", fn_HYPGEOMDIST,
                              "(sample_s, number_sample, population_s, number_pop)",
                              "Hypergeometric probability", "Statistics");
    registry.registerFunction("HYPGEOM.DIST", fn_HYPGEOM_DIST,
                              "(sample_s, number_sample, population_s, number_pop, cumulative)",
                              "Hypergeometric distribution", "Statistics");
    registry.registerAlias("HYPGEOM_DIST", "HYPGEOM.DIST");
    registry.registerFunction("NEGBINOMDIST", fn_NEGBINOMDIST,
                              "(number_f, number_s, probability_s)",
                              "Negative binomial probability", "Statistics");
    registry.registerFunction("NEGBINOM.DIST", fn_NEGBINOM_DIST,
                              "(number_f, number_s, probability_s, cumulative)",
                              "Negative binomial distribution", "Statistics");
    registry.registerAlias("NEGBINOM_DIST", "NEGBINOM.DIST");
}

}  // namespace cells
