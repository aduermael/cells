#include "core/cells/functions/fn_math.h"

#include <cctype>
#include <cmath>
#include <cstdint>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {
namespace {

// Portable pi (MSVC does not define M_PI without _USE_MATH_DEFINES).
constexpr double kPi = 3.14159265358979323846;
constexpr double kPiOver2 = kPi / 2.0;

// Pre-computed conversion constants (matches Excel's computation order)
constexpr double kDegreesToRadians = kPi / 180.0;
constexpr double kRadiansToDegrees = 180.0 / kPi;

// Round half away from zero to nearest integer (Excel ROUND(..., 0)).
double roundHalfAwayFromZero(double value) {
    if (value >= 0.0) {
        return std::floor(value + 0.5);
    }
    return std::ceil(value - 0.5);
}

// Euclidean GCD on non-negative integers.
std::int64_t gcdInt64(std::int64_t a, std::int64_t b) {
    while (b != 0) {
        const std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// FLOOR.PRECISE / ISO floor: toward -inf using |significance|.
EvalResult floorPreciseImpl(double value, double significance) {
    if (significance == 0.0) {
        return EvalResult::Number(0.0);
    }
    const double absSig = std::abs(significance);
    const double result = std::floor(excelNormalize(value / absSig)) * absSig;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

// CEILING.PRECISE / ISO.CEILING: toward +inf using |significance|.
EvalResult ceilingPreciseImpl(double value, double significance) {
    if (significance == 0.0) {
        return EvalResult::Number(0.0);
    }
    const double absSig = std::abs(significance);
    const double result = std::ceil(excelNormalize(value / absSig)) * absSig;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

// Classic Excel FLOOR(number, significance): toward zero; same sign required.
EvalResult floorClassic(double value, double significance) {
    if (significance == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    if (value == 0.0) {
        return EvalResult::Number(0.0);
    }
    // Mixed signs → #NUM! (Excel classic FLOOR).
    if ((value > 0.0) != (significance > 0.0)) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::trunc(value / significance) * significance;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

// Classic Excel CEILING(number, significance): away from zero; same sign required.
EvalResult ceilingClassic(double value, double significance) {
    if (significance == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    if (value == 0.0) {
        return EvalResult::Number(0.0);
    }
    if ((value > 0.0) != (significance > 0.0)) {
        return EvalResult::Error(CellError::NUM);
    }
    const double quotient = value / significance;
    double result = NAN;
    if (quotient == std::trunc(quotient)) {
        result = value;
    } else if (quotient > 0.0) {
        result = std::ceil(quotient) * significance;
    } else {
        result = std::floor(quotient) * significance;
    }
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

// Parse optional significance for PRECISE-style functions (default 1).
EvalResult parseOptionalSignificance(const std::vector<const ASTNode*>& args, EvalContext& ctx,
                                     double& significance) {
    significance = 1.0;
    if (args.size() >= 2) {
        EvalResult sigResult = evaluateAsNumber(args[1], ctx);
        if (sigResult.isError()) {
            return sigResult;
        }
        significance = sigResult.getNumber();
    }
    return EvalResult::Number(0.0);  // ok marker (caller checks isError)
}

}  // namespace

// =============================================================================
// Aggregate Functions
// =============================================================================

EvalResult fn_SUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    double sum = 0.0;
    for (const double val : values) {
        sum += val;
    }
    if (std::isinf(sum)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(sum));
}

EvalResult fn_PRODUCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    // Excel: PRODUCT with no numeric values returns 0.
    if (values.empty()) {
        return EvalResult::Number(0.0);
    }

    double product = 1.0;
    for (const double val : values) {
        product *= val;
        if (std::isinf(product)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(product));
}

EvalResult fn_SUMSQ(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    double sum = 0.0;
    for (const double val : values) {
        sum += val * val;
        if (std::isinf(sum)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(sum));
}

EvalResult fn_AVERAGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    if (values.empty()) {
        return EvalResult::Error(CellError::DIV);  // No values to average
    }

    double sum = 0.0;
    for (const double val : values) {
        sum += val;
    }
    const double result = sum / static_cast<double>(values.size());
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_COUNT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);

    size_t count = 0;
    for (const EvalResult& val : expanded) {
        // Propagate errors
        if (val.isError()) {
            return val;
        }
        // Count only numbers
        if (val.isNumber()) {
            count++;
        }
    }

    return EvalResult::Number(static_cast<double>(count));
}

EvalResult fn_COUNTA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);

    size_t count = 0;
    for (const EvalResult& val : expanded) {
        // Propagate errors
        if (val.isError()) {
            return val;
        }
        // Count anything that's not empty
        if (!val.isEmpty()) {
            count++;
        }
    }

    return EvalResult::Number(static_cast<double>(count));
}

EvalResult fn_MIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    if (values.empty()) {
        return EvalResult::Number(0.0);  // Excel behavior
    }

    double minVal = values[0];
    for (size_t i = 1; i < values.size(); i++) {
        if (values[i] < minVal) {
            minVal = values[i];
        }
    }
    return EvalResult::Number(excelNormalize(minVal));
}

EvalResult fn_MAX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }

    if (values.empty()) {
        return EvalResult::Number(0.0);  // Excel behavior
    }

    double maxVal = values[0];
    for (size_t i = 1; i < values.size(); i++) {
        if (values[i] > maxVal) {
            maxVal = values[i];
        }
    }
    return EvalResult::Number(excelNormalize(maxVal));
}

// =============================================================================
// Basic Math Functions
// =============================================================================

EvalResult fn_ABS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::abs(num.getNumber())));
}

EvalResult fn_SQRT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::sqrt(val)));
}

EvalResult fn_POWER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult base = evaluateAsNumber(args[0], ctx);
    if (base.isError()) {
        return base;
    }

    EvalResult exponent = evaluateAsNumber(args[1], ctx);
    if (exponent.isError()) {
        return exponent;
    }

    const double b = base.getNumber();
    const double e = exponent.getNumber();

    // Excel edge cases for zero base
    if (b == 0.0) {
        if (e == 0.0) {
            return EvalResult::Error(CellError::NUM);  // POWER(0, 0) = #NUM!
        }
        if (e < 0.0) {
            return EvalResult::Error(CellError::DIV);  // POWER(0, -n) = #DIV/0!
        }
        return EvalResult::Number(0.0);  // POWER(0, positive) = 0
    }

    // Base 1: always returns 1 regardless of exponent
    if (b == 1.0) {
        return EvalResult::Number(1.0);
    }

    // For extreme exponents (|exp| >= 2^53), Excel returns #NUM! for most cases.
    // Exceptions for large negative exponent:
    //   - base > 1: underflows to 0 (large base ^ huge negative exp → 0)
    //   - 0 < base < 1: underflows to 0 (small base ^ huge negative exp overflows
    //     in theory, but Excel's intermediate computation underflows to 0)
    // Negative base with extreme exponent always returns #NUM! (parity unknown).
    const double absExp = std::abs(e);
    if (absExp >= 9007199254740992.0) {  // 2^53
        if (b > 0.0 && e < 0.0) {
            return EvalResult::Number(0.0);  // Underflow to zero
        }
        return EvalResult::Error(CellError::NUM);
    }

    const double result = excelPow(b, e);
    if (std::isnan(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    if (std::isinf(result)) {
        // Overflow from reciprocal (1/0) is #DIV/0!, other overflow is #NUM!
        if (e < 0.0) {
            return EvalResult::Error(CellError::DIV);
        }
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_ROUND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    int digits = 0;
    if (args.size() == 2) {
        EvalResult digitsResult = evaluateAsNumber(args[1], ctx);
        if (digitsResult.isError()) {
            return digitsResult;
        }
        digits = static_cast<int>(digitsResult.getNumber());
    }

    const double multiplier = std::pow(10.0, digits);
    const double value = num.getNumber();

    // If scaling overflows, rounding has no effect — return original value
    if (std::isinf(value * multiplier) && !std::isinf(value)) {
        return EvalResult::Number(value);
    }

    // Round away from zero (Excel behavior)
    double rounded = NAN;
    if (value >= 0) {
        rounded = std::floor(value * multiplier + 0.5) / multiplier;
    } else {
        rounded = std::ceil(value * multiplier - 0.5) / multiplier;
    }

    if (std::isinf(rounded)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(rounded));
}

EvalResult fn_ROUNDUP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    int digits = 0;
    if (args.size() == 2) {
        EvalResult digitsResult = evaluateAsNumber(args[1], ctx);
        if (digitsResult.isError()) {
            return digitsResult;
        }
        digits = static_cast<int>(digitsResult.getNumber());
    }

    const double multiplier = std::pow(10.0, digits);
    const double value = num.getNumber();

    // If scaling overflows, rounding has no effect — return original value
    if (std::isinf(value * multiplier) && !std::isinf(value)) {
        return EvalResult::Number(value);
    }

    // Round away from zero
    double rounded = NAN;
    if (value >= 0) {
        rounded = std::ceil(value * multiplier) / multiplier;
    } else {
        rounded = std::floor(value * multiplier) / multiplier;
    }

    if (std::isinf(rounded)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(rounded));
}

EvalResult fn_ROUNDDOWN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    int digits = 0;
    if (args.size() == 2) {
        EvalResult digitsResult = evaluateAsNumber(args[1], ctx);
        if (digitsResult.isError()) {
            return digitsResult;
        }
        digits = static_cast<int>(digitsResult.getNumber());
    }

    const double multiplier = std::pow(10.0, digits);
    const double value = num.getNumber();

    // If scaling overflows, rounding has no effect — return original value
    if (std::isinf(value * multiplier) && !std::isinf(value)) {
        return EvalResult::Number(value);
    }

    // Round toward zero (same as TRUNC)
    const double rounded = std::trunc(value * multiplier) / multiplier;

    if (std::isinf(rounded)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(rounded));
}

EvalResult fn_FLOOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    // One-arg: mathematical floor (toward -inf) — preserved for existing callers.
    if (args.size() == 1) {
        return EvalResult::Number(excelNormalize(std::floor(num.getNumber())));
    }

    EvalResult sig = evaluateAsNumber(args[1], ctx);
    if (sig.isError()) {
        return sig;
    }
    return floorClassic(num.getNumber(), sig.getNumber());
}

EvalResult fn_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    // One-arg: mathematical ceil (toward +inf) — preserved for existing callers.
    if (args.size() == 1) {
        return EvalResult::Number(excelNormalize(std::ceil(num.getNumber())));
    }

    EvalResult sig = evaluateAsNumber(args[1], ctx);
    if (sig.isError()) {
        return sig;
    }
    return ceilingClassic(num.getNumber(), sig.getNumber());
}

EvalResult fn_FLOOR_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    double significance = 1.0;
    EvalResult sigOk = parseOptionalSignificance(args, ctx, significance);
    if (sigOk.isError()) {
        return sigOk;
    }
    return floorPreciseImpl(num.getNumber(), significance);
}

EvalResult fn_CEILING_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    double significance = 1.0;
    EvalResult sigOk = parseOptionalSignificance(args, ctx, significance);
    if (sigOk.isError()) {
        return sigOk;
    }
    return ceilingPreciseImpl(num.getNumber(), significance);
}

EvalResult fn_ISO_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // ISO.CEILING is identical to CEILING.PRECISE.
    return fn_CEILING_PRECISE(args, ctx);
}

EvalResult fn_MROUND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }
    EvalResult multiple = evaluateAsNumber(args[1], ctx);
    if (multiple.isError()) {
        return multiple;
    }

    const double value = num.getNumber();
    const double mult = multiple.getNumber();
    if (mult == 0.0) {
        return EvalResult::Number(0.0);
    }
    // Different signs → #NUM!
    if ((value > 0.0) != (mult > 0.0) && value != 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    const double result = roundHalfAwayFromZero(value / mult) * mult;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_EVEN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double value = num.getNumber();
    double result = NAN;
    if (value >= 0.0) {
        result = std::ceil(value);
        if (std::fmod(result, 2.0) != 0.0) {
            result += 1.0;
        }
    } else {
        result = std::floor(value);
        if (std::fmod(result, 2.0) != 0.0) {
            result -= 1.0;
        }
    }
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_ODD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double value = num.getNumber();
    double result = NAN;
    if (value >= 0.0) {
        result = std::ceil(value);
        if (std::fmod(result, 2.0) == 0.0) {
            result += 1.0;
        }
    } else {
        result = std::floor(value);
        // fmod of negative is negative or zero; treat even as |result| even.
        if (std::fmod(result, 2.0) == 0.0) {
            result -= 1.0;
        }
    }
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_SQRTPI(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(std::sqrt(val * kPi)));
}

EvalResult fn_CEILING_MATH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    double significance = 1.0;
    if (args.size() >= 2) {
        EvalResult sigResult = evaluateAsNumber(args[1], ctx);
        if (sigResult.isError()) {
            return sigResult;
        }
        significance = sigResult.getNumber();
    }

    int mode = 0;
    if (args.size() >= 3) {
        EvalResult modeResult = evaluateAsNumber(args[2], ctx);
        if (modeResult.isError()) {
            return modeResult;
        }
        mode = static_cast<int>(modeResult.getNumber());
    }

    const double value = num.getNumber();
    if (significance == 0.0) {
        return EvalResult::Number(0.0);
    }

    const double absSig = std::abs(significance);
    double result = NAN;
    if (value >= 0.0) {
        // Positive: always round toward +infinity
        result = std::ceil(excelNormalize(value / absSig)) * absSig;
    } else if (mode != 0) {
        // Negative number with mode: round away from zero (toward -infinity)
        result = -std::ceil(excelNormalize(-value / absSig)) * absSig;
    } else {
        // Negative number default: round toward +infinity (toward zero)
        result = -std::floor(excelNormalize(-value / absSig)) * absSig;
    }

    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_FLOOR_MATH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    double significance = 1.0;
    if (args.size() >= 2) {
        EvalResult sigResult = evaluateAsNumber(args[1], ctx);
        if (sigResult.isError()) {
            return sigResult;
        }
        significance = sigResult.getNumber();
    }

    int mode = 0;
    if (args.size() >= 3) {
        EvalResult modeResult = evaluateAsNumber(args[2], ctx);
        if (modeResult.isError()) {
            return modeResult;
        }
        mode = static_cast<int>(modeResult.getNumber());
    }

    const double value = num.getNumber();
    if (significance == 0.0) {
        return EvalResult::Number(0.0);
    }

    const double absSig = std::abs(significance);
    double result = NAN;
    if (value >= 0.0) {
        // Positive: always round toward -infinity (toward zero)
        result = std::floor(excelNormalize(value / absSig)) * absSig;
    } else if (mode != 0) {
        // Negative number with mode: round toward zero
        result = -std::floor(excelNormalize(-value / absSig)) * absSig;
    } else {
        // Negative number default: round toward -infinity (away from zero)
        result = -std::ceil(excelNormalize(-value / absSig)) * absSig;
    }

    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_MOD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult number = evaluateAsNumber(args[0], ctx);
    if (number.isError()) {
        return number;
    }

    EvalResult divisor = evaluateAsNumber(args[1], ctx);
    if (divisor.isError()) {
        return divisor;
    }

    const double n = number.getNumber();
    const double d = divisor.getNumber();

    if (d == 0) {
        return EvalResult::Error(CellError::DIV);
    }

    // Excel MOD: result has same sign as divisor.
    // Excel returns #NUM! when |n/d| exceeds 2^53 (integer precision limit).
    const double rawQuotient = n / d;
    if (std::isinf(rawQuotient) || std::fabs(rawQuotient) > 9007199254740992.0) {
        return EvalResult::Error(CellError::NUM);
    }

    // Use fmod for the core computation, then adjust sign to match divisor.
    // fmod gives the IEEE754 remainder with the sign of the numerator.
    double r = std::fmod(n, d);
    if (r == 0.0) {
        return EvalResult::Number(0.0);
    }

    // Flush subnormal remainder — indicates precision loss at extreme values.
    // E.g., MOD(1e-307, -1e-307) where magnitudes differ by a few ULP.
    r = excelNormalize(r);
    if (r == 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    // Adjust sign: fmod gives sign of n, Excel MOD gives sign of d.
    if ((r > 0.0) != (d > 0.0)) {
        r += d;
        r = excelNormalize(r);
        // If n was negligible compared to d, r+d == d exactly in floating
        // point (e.g., MOD(1e-307, -42.5): fmod=1e-307, r+d=-42.5==d).
        if (r == d) {
            return EvalResult::Number(0.0);
        }
        // Cancellation produced subnormal — precision lost.
        if (r == 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
    }

    return EvalResult::Number(excelNormalize(r));
}

EvalResult fn_INT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::floor(num.getNumber())));
}

EvalResult fn_SIGN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val > 0.0) {
        return EvalResult::Number(1.0);
    }
    if (val < 0.0) {
        return EvalResult::Number(-1.0);
    }
    return EvalResult::Number(0.0);
}

EvalResult fn_EXP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = std::exp(num.getNumber());
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_LN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::log(val)));
}

EvalResult fn_TRUNC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    int digits = 0;
    if (args.size() == 2) {
        EvalResult digitsResult = evaluateAsNumber(args[1], ctx);
        if (digitsResult.isError()) {
            return digitsResult;
        }
        digits = static_cast<int>(digitsResult.getNumber());
    }

    const double multiplier = std::pow(10.0, digits);
    const double value = num.getNumber();
    const double truncated = std::trunc(value * multiplier) / multiplier;

    if (std::isinf(truncated)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(truncated));
}

EvalResult fn_FACT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    // Excel truncates to integer, negative values return #NUM!
    if (val < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    const auto n = static_cast<int>(std::floor(val));
    // Excel supports up to FACT(170) = 7.257e+306; FACT(171) overflows
    if (n > 170) {
        return EvalResult::Error(CellError::NUM);
    }

    // Multiply in descending order (n * (n-1) * ... * 2) to match Excel's
    // rounding behavior. The order of floating-point multiplications affects
    // intermediate rounding, and descending order matches Excel exactly
    // (e.g., FACT(42) = 0x4A8E0AC0EA48D949 vs 0x...D947 ascending).
    double result = 1.0;
    for (int i = n; i >= 2; i--) {
        result *= i;
    }
    return EvalResult::Number(result);
}

EvalResult fn_FACTDOUBLE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    // Excel: negative → #NUM!
    if (val < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    const auto n = static_cast<int>(std::floor(val));
    double result = 1.0;
    for (int i = n; i >= 2; i -= 2) {
        result *= static_cast<double>(i);
        if (std::isinf(result)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_GCD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::int64_t acc = 0;
    for (const double val : values) {
        // Excel: any argument < 0 → #NUM!; non-integers are truncated toward zero.
        if (val < 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
        const auto n = static_cast<std::int64_t>(std::trunc(val));
        acc = gcdInt64(acc, n);
    }
    return EvalResult::Number(static_cast<double>(acc));
}

EvalResult fn_LCM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    auto [values, error] = collectNumericValues(args, ctx);
    if (error.isError()) {
        return error;
    }
    if (values.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::int64_t acc = 1;
    for (const double val : values) {
        // Excel: any argument < 0 → #NUM!; non-integers are truncated toward zero.
        if (val < 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
        const auto n = static_cast<std::int64_t>(std::trunc(val));
        if (n == 0) {
            return EvalResult::Number(0.0);
        }
        // LCM(a,b) = a / GCD(a,b) * b  (order avoids intermediate overflow when possible)
        const std::int64_t g = gcdInt64(acc, n);
        // Check overflow before multiplying.
        if (acc / g > (std::numeric_limits<std::int64_t>::max() / n)) {
            return EvalResult::Error(CellError::NUM);
        }
        acc = (acc / g) * n;
    }
    return EvalResult::Number(static_cast<double>(acc));
}

EvalResult fn_QUOTIENT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult numerator = evaluateAsNumber(args[0], ctx);
    if (numerator.isError()) {
        return numerator;
    }

    EvalResult denominator = evaluateAsNumber(args[1], ctx);
    if (denominator.isError()) {
        return denominator;
    }

    const double d = denominator.getNumber();
    if (d == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double quotient = numerator.getNumber() / d;
    if (std::isinf(quotient)) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::trunc(quotient);
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_LOG10(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::log10(val)));
}

EvalResult fn_LOG(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }

    double base = 10.0;  // Default base
    if (args.size() == 2) {
        EvalResult baseResult = evaluateAsNumber(args[1], ctx);
        if (baseResult.isError()) {
            return baseResult;
        }
        base = baseResult.getNumber();
        if (base <= 0.0 || base == 1.0) {
            return EvalResult::Error(CellError::NUM);
        }
    }

    const double result = std::log(val) / std::log(base);
    return EvalResult::Number(excelNormalize(result));
}

// =============================================================================
// Trigonometric Functions
// =============================================================================

EvalResult fn_PI(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Number(kPi);
}

EvalResult fn_SIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::sin(num.getNumber())));
}

EvalResult fn_COS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::cos(num.getNumber())));
}

EvalResult fn_TAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = std::tan(num.getNumber());
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_ASIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val < -1.0 || val > 1.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::asin(val)));
}

EvalResult fn_ACOS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val < -1.0 || val > 1.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::acos(val)));
}

EvalResult fn_ATAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::atan(num.getNumber())));
}

EvalResult fn_ATAN2(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult xNum = evaluateAsNumber(args[0], ctx);
    if (xNum.isError()) {
        return xNum;
    }

    EvalResult yNum = evaluateAsNumber(args[1], ctx);
    if (yNum.isError()) {
        return yNum;
    }

    const double x = xNum.getNumber();
    const double y = yNum.getNumber();

    if (x == 0.0 && y == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    // Excel ATAN2(x_num, y_num) = atan2(y_num, x_num) — args reversed vs C
    return EvalResult::Number(excelNormalize(std::atan2(y, x)));
}

EvalResult fn_CSC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double sinVal = std::sin(num.getNumber());
    if (sinVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double result = 1.0 / sinVal;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_SEC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double cosVal = std::cos(num.getNumber());
    if (cosVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double result = 1.0 / cosVal;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_COT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double sinVal = std::sin(num.getNumber());
    if (sinVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double result = std::cos(num.getNumber()) / sinVal;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_SINH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = std::sinh(num.getNumber());
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_COSH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = std::cosh(num.getNumber());
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_TANH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::tanh(num.getNumber())));
}

EvalResult fn_ASINH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::asinh(num.getNumber())));
}

EvalResult fn_ACOSH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val < 1.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::acosh(val)));
}

EvalResult fn_ATANH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    if (val <= -1.0 || val >= 1.0) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(excelNormalize(std::atanh(val)));
}

EvalResult fn_ACOT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    // Excel ACOT range is (0, π]: π/2 - atan(x).
    return EvalResult::Number(excelNormalize(kPiOver2 - std::atan(num.getNumber())));
}

EvalResult fn_ACOTH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double val = num.getNumber();
    // Domain: |x| > 1
    if (std::abs(val) <= 1.0) {
        return EvalResult::Error(CellError::NUM);
    }

    // ACOTH(x) = atanh(1/x)
    const double result = std::atanh(1.0 / val);
    if (std::isnan(result) || std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_CSCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double sinhVal = std::sinh(num.getNumber());
    if (sinhVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double result = 1.0 / sinhVal;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_SECH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double coshVal = std::cosh(num.getNumber());
    // cosh is always >= 1 for real inputs; still guard overflow of cosh itself.
    if (std::isinf(coshVal)) {
        return EvalResult::Number(0.0);
    }
    if (coshVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(1.0 / coshVal));
}

EvalResult fn_COTH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double x = num.getNumber();
    const double sinhVal = std::sinh(x);
    if (sinhVal == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }

    const double result = std::cosh(x) / sinhVal;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_RADIANS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = num.getNumber() * kDegreesToRadians;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_DEGREES(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    const double result = num.getNumber() * kRadiansToDegrees;
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

namespace {

EvalResult requireTruncNonNegInt(const ASTNode* arg, EvalContext& ctx, int& out) {
    EvalResult n = evaluateAsNumber(arg, ctx);
    if (n.isError()) {
        return n;
    }
    const double v = n.getNumber();
    if (!std::isfinite(v) || v < 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    out = static_cast<int>(std::floor(v));
    return EvalResult::Empty();
}

double combinDouble(int n, int k) {
    if (k < 0 || k > n) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (k > n - k) {
        k = n - k;
    }
    double result = 1.0;
    for (int i = 1; i <= k; ++i) {
        result *= static_cast<double>(n - k + i);
        result /= static_cast<double>(i);
    }
    return result;
}

int romanValue(char c) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
        case 'I':
            return 1;
        case 'V':
            return 5;
        case 'X':
            return 10;
        case 'L':
            return 50;
        case 'C':
            return 100;
        case 'D':
            return 500;
        case 'M':
            return 1000;
        default:
            return -1;
    }
}

std::string toRoman(int n) {
    static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* nums[] = {"M",  "CM", "D",  "CD", "C",  "XC", "L",
                                 "XL", "X",  "IX", "V",  "IV", "I"};
    std::string out;
    for (int i = 0; i < 13; ++i) {
        while (n >= vals[i]) {
            out += nums[i];
            n -= vals[i];
        }
    }
    return out;
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u >= 'A' && u <= 'Z') {
        return u - 'A' + 10;
    }
    return -1;
}

EvalResult sumXPair(const std::vector<const ASTNode*>& args, EvalContext& ctx, int kind) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [pairs, err] = collectPairedNumericValues(args[0], args[1], ctx);
    if (err.isError()) {
        return err;
    }
    double sum = 0.0;
    for (const auto& p : pairs) {
        if (kind == 0) {
            sum += p.first * p.first - p.second * p.second;
        } else if (kind == 1) {
            sum += p.first * p.first + p.second * p.second;
        } else {
            const double d = p.first - p.second;
            sum += d * d;
        }
    }
    return EvalResult::Number(excelNormalize(sum));
}

}  // namespace

EvalResult fn_COMBIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    int n = 0;
    int k = 0;
    EvalResult e = requireTruncNonNegInt(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    e = requireTruncNonNegInt(args[1], ctx, k);
    if (e.isError()) {
        return e;
    }
    if (k > n) {
        return EvalResult::Error(CellError::NUM);
    }
    const double r = combinDouble(n, k);
    if (!std::isfinite(r)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(std::round(r)));
}

EvalResult fn_COMBINA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    int n = 0;
    int k = 0;
    EvalResult e = requireTruncNonNegInt(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    e = requireTruncNonNegInt(args[1], ctx, k);
    if (e.isError()) {
        return e;
    }
    if (n == 0) {
        return k == 0 ? EvalResult::Number(1.0) : EvalResult::Error(CellError::NUM);
    }
    // Combinations with repetition: C(n + k - 1, k)
    const double r = combinDouble(n + k - 1, k);
    if (!std::isfinite(r)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(std::round(r)));
}

EvalResult fn_PERMUT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    int n = 0;
    int k = 0;
    EvalResult e = requireTruncNonNegInt(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    e = requireTruncNonNegInt(args[1], ctx, k);
    if (e.isError()) {
        return e;
    }
    if (k > n) {
        return EvalResult::Error(CellError::NUM);
    }
    double r = 1.0;
    for (int i = 0; i < k; ++i) {
        r *= static_cast<double>(n - i);
        if (!std::isfinite(r)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(r));
}

EvalResult fn_PERMUTATIONA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    int n = 0;
    int k = 0;
    EvalResult e = requireTruncNonNegInt(args[0], ctx, n);
    if (e.isError()) {
        return e;
    }
    e = requireTruncNonNegInt(args[1], ctx, k);
    if (e.isError()) {
        return e;
    }
    if (n == 0) {
        return k == 0 ? EvalResult::Number(1.0) : EvalResult::Number(0.0);
    }
    const double r = excelPow(static_cast<double>(n), static_cast<double>(k));
    if (!std::isfinite(r)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(r));
}

EvalResult fn_BASE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult numRes = evaluateAsNumber(args[0], ctx);
    if (numRes.isError()) {
        return numRes;
    }
    EvalResult radixRes = evaluateAsNumber(args[1], ctx);
    if (radixRes.isError()) {
        return radixRes;
    }
    const double num = std::floor(numRes.getNumber());
    const int radix = static_cast<int>(std::floor(radixRes.getNumber()));
    if (num < 0.0 || num >= 0x1p53 || radix < 2 || radix > 36) {
        return EvalResult::Error(CellError::NUM);
    }
    int minLen = 0;
    if (args.size() == 3) {
        EvalResult lenRes = evaluateAsNumber(args[2], ctx);
        if (lenRes.isError()) {
            return lenRes;
        }
        minLen = static_cast<int>(std::floor(lenRes.getNumber()));
        if (minLen < 0 || minLen > 255) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    auto u = static_cast<std::uint64_t>(num);
    std::string digits;
    if (u == 0) {
        digits = "0";
    } else {
        while (u > 0) {
            const int d = static_cast<int>(u % static_cast<unsigned>(radix));
            digits.push_back(static_cast<char>(d < 10 ? '0' + d : 'A' + (d - 10)));
            u /= static_cast<unsigned>(radix);
        }
        std::reverse(digits.begin(), digits.end());
    }
    if (minLen > static_cast<int>(digits.size())) {
        digits.insert(digits.begin(), static_cast<size_t>(minLen) - digits.size(), '0');
    }
    return EvalResult::String(digits);
}

EvalResult fn_DECIMAL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult textRes = evaluateAsString(args[0], ctx);
    if (textRes.isError()) {
        return textRes;
    }
    EvalResult radixRes = evaluateAsNumber(args[1], ctx);
    if (radixRes.isError()) {
        return radixRes;
    }
    const int radix = static_cast<int>(std::floor(radixRes.getNumber()));
    if (radix < 2 || radix > 36) {
        return EvalResult::Error(CellError::NUM);
    }
    const std::string& s = textRes.getString();
    if (s.empty()) {
        return EvalResult::Number(0.0);
    }
    double value = 0.0;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        const int d = digitValue(c);
        if (d < 0 || d >= radix) {
            return EvalResult::Error(CellError::NUM);
        }
        value = value * static_cast<double>(radix) + static_cast<double>(d);
        if (!std::isfinite(value)) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    return EvalResult::Number(excelNormalize(value));
}

EvalResult fn_ARABIC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult textRes = evaluateAsString(args[0], ctx);
    if (textRes.isError()) {
        return textRes;
    }
    std::string s = textRes.getString();
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    s = s.substr(start, end - start);
    if (s.empty()) {
        return EvalResult::Number(0.0);
    }
    int sign = 1;
    size_t i = 0;
    if (s[0] == '-') {
        sign = -1;
        ++i;
    }
    if (i >= s.size()) {
        return EvalResult::Error(CellError::VALUE);
    }
    int total = 0;
    int prev = 0;
    for (; i < s.size(); ++i) {
        const int v = romanValue(s[i]);
        if (v < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
        if (v > prev) {
            total += v - 2 * prev;
        } else {
            total += v;
        }
        prev = v;
    }
    return EvalResult::Number(static_cast<double>(sign * total));
}

EvalResult fn_ROMAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult numRes = evaluateAsNumber(args[0], ctx);
    if (numRes.isError()) {
        return numRes;
    }
    const int n = static_cast<int>(std::floor(numRes.getNumber()));
    if (n < 0 || n > 3999) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (args.size() == 2) {
        EvalResult formRes = evaluateAsNumber(args[1], ctx);
        if (formRes.isError()) {
            return formRes;
        }
        const int form = static_cast<int>(std::floor(formRes.getNumber()));
        if (form < 0 || form > 4) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    if (n == 0) {
        return EvalResult::String("");
    }
    return EvalResult::String(toRoman(n));
}

EvalResult fn_MULTINOMIAL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [values, err] = collectNumericValues(args, ctx);
    if (err.isError()) {
        return err;
    }
    if (values.empty()) {
        return EvalResult::Number(1.0);
    }
    int sum = 0;
    std::vector<int> ks;
    ks.reserve(values.size());
    for (const double v : values) {
        if (v < 0.0) {
            return EvalResult::Error(CellError::NUM);
        }
        const int k = static_cast<int>(std::floor(v));
        ks.push_back(k);
        sum += k;
    }
    double result = 1.0;
    int filled = 0;
    for (const int k : ks) {
        for (int i = 1; i <= k; ++i) {
            ++filled;
            result *= static_cast<double>(filled);
            result /= static_cast<double>(i);
            if (!std::isfinite(result)) {
                return EvalResult::Error(CellError::NUM);
            }
        }
    }
    (void)sum;
    return EvalResult::Number(excelNormalize(std::round(result)));
}

EvalResult fn_SERIESSUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult xRes = evaluateAsNumber(args[0], ctx);
    if (xRes.isError()) {
        return xRes;
    }
    EvalResult nRes = evaluateAsNumber(args[1], ctx);
    if (nRes.isError()) {
        return nRes;
    }
    EvalResult mRes = evaluateAsNumber(args[2], ctx);
    if (mRes.isError()) {
        return mRes;
    }
    auto [coefs, err] = collectNumericValues({args[3]}, ctx);
    if (err.isError()) {
        return err;
    }
    const double x = xRes.getNumber();
    const double n = nRes.getNumber();
    const double m = mRes.getNumber();
    double sum = 0.0;
    for (size_t i = 0; i < coefs.size(); ++i) {
        const double exp = n + static_cast<double>(i) * m;
        const double term = coefs[i] * excelPow(x, exp);
        if (!std::isfinite(term)) {
            return EvalResult::Error(CellError::NUM);
        }
        sum += term;
    }
    return EvalResult::Number(excelNormalize(sum));
}

EvalResult fn_SUMX2MY2(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return sumXPair(args, ctx, 0);
}

EvalResult fn_SUMX2PY2(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return sumXPair(args, ctx, 1);
}

EvalResult fn_SUMXMY2(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return sumXPair(args, ctx, 2);
}

EvalResult fn_GAMMA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const double x = n.getNumber();
    if (x == 0.0 || (x < 0.0 && x == std::floor(x))) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::tgamma(x);
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_GAMMALN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const double x = n.getNumber();
    if (x <= 0.0) {
        return EvalResult::Error(CellError::NUM);
    }
    const double result = std::lgamma(x);
    if (!std::isfinite(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
}

EvalResult fn_GAMMALN_PRECISE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_GAMMALN(args, ctx);
}

EvalResult fn_PERCENTOF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }
    EvalResult den = evaluateAsNumber(args[1], ctx);
    if (den.isError()) {
        return den;
    }
    if (den.getNumber() == 0.0) {
        return EvalResult::Error(CellError::DIV);
    }
    return EvalResult::Number(excelNormalize(num.getNumber() / den.getNumber()));
}

// =============================================================================
// Registration
// =============================================================================

void registerMathFunctions(FunctionRegistry& registry) {
    // Aggregate functions
    registry.registerFunction("SUM", fn_SUM, "(number1, [number2], ...)",
                              "Adds all numbers in a range", "Math");
    registry.registerFunction("PRODUCT", fn_PRODUCT, "(number1, [number2], ...)",
                              "Multiplies all numbers", "Math");
    registry.registerFunction("SUMSQ", fn_SUMSQ, "(number1, [number2], ...)",
                              "Returns the sum of squares", "Math");
    registry.registerFunction("AVERAGE", fn_AVERAGE, "(number1, [number2], ...)",
                              "Returns the arithmetic mean", "Math");
    registry.registerFunction("COUNT", fn_COUNT, "(value1, [value2], ...)",
                              "Counts cells containing numbers", "Math");
    registry.registerFunction("COUNTA", fn_COUNTA, "(value1, [value2], ...)",
                              "Counts non-empty cells", "Math");
    registry.registerFunction("MIN", fn_MIN, "(number1, [number2], ...)",
                              "Returns the smallest value", "Math");
    registry.registerFunction("MAX", fn_MAX, "(number1, [number2], ...)",
                              "Returns the largest value", "Math");

    // Basic math functions
    registry.registerFunction("ABS", fn_ABS, "(number)", "Returns the absolute value", "Math");
    registry.registerFunction("SQRT", fn_SQRT, "(number)", "Returns the square root", "Math");
    registry.registerFunction("SQRTPI", fn_SQRTPI, "(number)",
                              "Returns the square root of (number * pi)", "Math");
    registry.registerFunction("POWER", fn_POWER, "(number, power)",
                              "Returns number raised to a power", "Math");
    registry.registerFunction("ROUND", fn_ROUND, "(number, [num_digits])",
                              "Rounds to specified digits", "Math");
    registry.registerFunction("ROUNDUP", fn_ROUNDUP, "(number, [num_digits])",
                              "Rounds away from zero", "Math");
    registry.registerFunction("ROUNDDOWN", fn_ROUNDDOWN, "(number, [num_digits])",
                              "Rounds toward zero", "Math");
    registry.registerFunction("FLOOR", fn_FLOOR, "(number, [significance])",
                              "Rounds down (1-arg toward -inf; 2-arg classic)", "Math");
    registry.registerFunction("CEILING", fn_CEILING, "(number, [significance])",
                              "Rounds up (1-arg toward +inf; 2-arg classic)", "Math");
    registry.registerFunction("CEILING_MATH", fn_CEILING_MATH, "(number, [significance], [mode])",
                              "Rounds up to nearest multiple of significance", "Math");
    registry.registerFunction("FLOOR_MATH", fn_FLOOR_MATH, "(number, [significance], [mode])",
                              "Rounds down to nearest multiple of significance", "Math");
    registry.registerFunction("FLOOR_PRECISE", fn_FLOOR_PRECISE, "(number, [significance])",
                              "Rounds down using absolute significance", "Math");
    registry.registerFunction("CEILING_PRECISE", fn_CEILING_PRECISE, "(number, [significance])",
                              "Rounds up using absolute significance", "Math");
    registry.registerFunction("ISO_CEILING", fn_ISO_CEILING, "(number, [significance])",
                              "Rounds up using absolute significance (ISO)", "Math");
    registry.registerFunction("MROUND", fn_MROUND, "(number, multiple)",
                              "Rounds to the nearest multiple", "Math");
    registry.registerFunction("EVEN", fn_EVEN, "(number)",
                              "Rounds away from zero to nearest even integer", "Math");
    registry.registerFunction("ODD", fn_ODD, "(number)",
                              "Rounds away from zero to nearest odd integer", "Math");
    registry.registerFunction("MOD", fn_MOD, "(number, divisor)",
                              "Returns remainder after division", "Math");
    registry.registerFunction("INT", fn_INT, "(number)", "Truncates to an integer", "Math");
    registry.registerFunction("SIGN", fn_SIGN, "(number)", "Returns the sign of a number", "Math");
    registry.registerFunction("EXP", fn_EXP, "(number)", "Returns e raised to a power", "Math");
    registry.registerFunction("LN", fn_LN, "(number)", "Returns the natural logarithm", "Math");
    registry.registerFunction("TRUNC", fn_TRUNC, "(number, [num_digits])",
                              "Truncates to specified digits", "Math");
    registry.registerFunction("FACT", fn_FACT, "(number)", "Returns the factorial", "Math");
    registry.registerFunction("FACTDOUBLE", fn_FACTDOUBLE, "(number)",
                              "Returns the double factorial", "Math");
    registry.registerFunction("GCD", fn_GCD, "(number1, [number2], ...)",
                              "Returns the greatest common divisor", "Math");
    registry.registerFunction("LCM", fn_LCM, "(number1, [number2], ...)",
                              "Returns the least common multiple", "Math");
    registry.registerFunction("QUOTIENT", fn_QUOTIENT, "(numerator, denominator)",
                              "Returns integer portion of division", "Math");
    registry.registerFunction("LOG10", fn_LOG10, "(number)", "Returns the base-10 logarithm",
                              "Math");
    registry.registerFunction("LOG", fn_LOG, "(number, [base])", "Returns the logarithm", "Math");

    // Trigonometric functions
    registry.registerFunction("PI", fn_PI, "()", "Returns the value of Pi", "Math");
    registry.registerFunction("SIN", fn_SIN, "(number)", "Returns the sine of an angle", "Math");
    registry.registerFunction("COS", fn_COS, "(number)", "Returns the cosine of an angle", "Math");
    registry.registerFunction("TAN", fn_TAN, "(number)", "Returns the tangent of an angle", "Math");
    registry.registerFunction("ASIN", fn_ASIN, "(number)", "Returns the arcsine", "Math");
    registry.registerFunction("ACOS", fn_ACOS, "(number)", "Returns the arccosine", "Math");
    registry.registerFunction("ATAN", fn_ATAN, "(number)", "Returns the arctangent", "Math");
    registry.registerFunction("ATAN2", fn_ATAN2, "(x_num, y_num)",
                              "Returns the arctangent of x and y coordinates", "Math");
    registry.registerFunction("ACOT", fn_ACOT, "(number)", "Returns the arccotangent", "Math");
    registry.registerFunction("CSC", fn_CSC, "(number)", "Returns the cosecant", "Math");
    registry.registerFunction("SEC", fn_SEC, "(number)", "Returns the secant", "Math");
    registry.registerFunction("COT", fn_COT, "(number)", "Returns the cotangent", "Math");
    registry.registerFunction("SINH", fn_SINH, "(number)", "Returns the hyperbolic sine", "Math");
    registry.registerFunction("COSH", fn_COSH, "(number)", "Returns the hyperbolic cosine", "Math");
    registry.registerFunction("TANH", fn_TANH, "(number)", "Returns the hyperbolic tangent",
                              "Math");
    registry.registerFunction("ASINH", fn_ASINH, "(number)", "Returns the inverse hyperbolic sine",
                              "Math");
    registry.registerFunction("ACOSH", fn_ACOSH, "(number)",
                              "Returns the inverse hyperbolic cosine", "Math");
    registry.registerFunction("ATANH", fn_ATANH, "(number)",
                              "Returns the inverse hyperbolic tangent", "Math");
    registry.registerFunction("ACOTH", fn_ACOTH, "(number)",
                              "Returns the inverse hyperbolic cotangent", "Math");
    registry.registerFunction("CSCH", fn_CSCH, "(number)", "Returns the hyperbolic cosecant",
                              "Math");
    registry.registerFunction("SECH", fn_SECH, "(number)", "Returns the hyperbolic secant", "Math");
    registry.registerFunction("COTH", fn_COTH, "(number)", "Returns the hyperbolic cotangent",
                              "Math");
    registry.registerFunction("RADIANS", fn_RADIANS, "(angle)", "Converts degrees to radians",
                              "Math");
    registry.registerFunction("DEGREES", fn_DEGREES, "(angle)", "Converts radians to degrees",
                              "Math");

    registry.registerFunction("COMBIN", fn_COMBIN, "(n, k)", "Number of combinations", "Math");
    registry.registerFunction("COMBINA", fn_COMBINA, "(n, k)", "Combinations with repetition",
                              "Math");
    registry.registerFunction("PERMUT", fn_PERMUT, "(n, k)", "Number of permutations", "Math");
    registry.registerFunction("PERMUTATIONA", fn_PERMUTATIONA, "(n, k)",
                              "Permutations with repetition", "Math");
    registry.registerFunction("BASE", fn_BASE, "(number, radix, [min_length])",
                              "Converts a number to text of the given radix", "Math");
    registry.registerFunction("DECIMAL", fn_DECIMAL, "(text, radix)",
                              "Converts text of the given radix to a number", "Math");
    registry.registerFunction("ARABIC", fn_ARABIC, "(text)", "Converts a Roman numeral to a number",
                              "Math");
    registry.registerFunction("ROMAN", fn_ROMAN, "(number, [form])",
                              "Converts a number to a Roman numeral", "Math");
    registry.registerFunction("MULTINOMIAL", fn_MULTINOMIAL, "(number1, [number2], ...)",
                              "Multinomial coefficient", "Math");
    registry.registerFunction("SERIESSUM", fn_SERIESSUM, "(x, n, m, coefficients)",
                              "Sum of a power series", "Math");
    registry.registerFunction("SUMX2MY2", fn_SUMX2MY2, "(array_x, array_y)",
                              "Sum of difference of squares", "Math");
    registry.registerFunction("SUMX2PY2", fn_SUMX2PY2, "(array_x, array_y)",
                              "Sum of sum of squares", "Math");
    registry.registerFunction("SUMXMY2", fn_SUMXMY2, "(array_x, array_y)",
                              "Sum of squared differences", "Math");
    registry.registerFunction("GAMMA", fn_GAMMA, "(number)", "Gamma function", "Math");
    registry.registerFunction("GAMMALN", fn_GAMMALN, "(number)",
                              "Natural log of the gamma function", "Math");
    registry.registerFunction("GAMMALN.PRECISE", fn_GAMMALN_PRECISE, "(number)",
                              "Natural log of the gamma function", "Math");
    registry.registerAlias("GAMMALN_PRECISE", "GAMMALN.PRECISE");
    registry.registerFunction("PERCENTOF", fn_PERCENTOF, "(numerator, denominator)",
                              "Returns a value as a percentage of another", "Math");

    // Excel dotted names (XLSX import also stores underscore forms).
    registry.registerAlias("CEILING.MATH", "CEILING_MATH");
    registry.registerAlias("FLOOR.MATH", "FLOOR_MATH");
    registry.registerAlias("CEILING.PRECISE", "CEILING_PRECISE");
    registry.registerAlias("FLOOR.PRECISE", "FLOOR_PRECISE");
    registry.registerAlias("ISO.CEILING", "ISO_CEILING");
}

}  // namespace cells
