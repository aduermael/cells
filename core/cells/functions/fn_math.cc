#include "core/cells/functions/fn_math.h"

#include <cmath>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

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
    // Exception: positive base > 1 with large negative exponent underflows to 0.
    // Negative base with extreme exponent always returns #NUM! (parity unknown).
    const double absExp = std::abs(e);
    if (absExp >= 9007199254740992.0) {  // 2^53
        if (b > 1.0 && e < 0.0) {
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

    // Round toward zero (same as TRUNC)
    const double rounded = std::trunc(value * multiplier) / multiplier;

    if (std::isinf(rounded)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(rounded));
}

EvalResult fn_FLOOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::floor(num.getNumber())));
}

EvalResult fn_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(excelNormalize(std::ceil(num.getNumber())));
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
        result = std::ceil(value / absSig) * absSig;
    } else if (mode != 0) {
        // Negative number with mode: round away from zero (toward -infinity)
        result = -std::ceil(-value / absSig) * absSig;
    } else {
        // Negative number default: round toward +infinity (toward zero)
        result = -std::floor(-value / absSig) * absSig;
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
        result = std::floor(value / absSig) * absSig;
    } else if (mode != 0) {
        // Negative number with mode: round toward zero
        result = -std::floor(-value / absSig) * absSig;
    } else {
        // Negative number default: round toward -infinity (away from zero)
        result = -std::ceil(-value / absSig) * absSig;
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

    // Excel MOD: result has same sign as divisor
    // MOD(n, d) = n - d * INT(n/d)
    const double result = n - d * std::floor(n / d);
    if (std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(result));
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

    double result = 1.0;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return EvalResult::Number(result);
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

    const double result = std::trunc(numerator.getNumber() / d);
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
// Registration
// =============================================================================

void registerMathFunctions(FunctionRegistry& registry) {
    // Aggregate functions
    registry.registerFunction("SUM", fn_SUM, "(number1, [number2], ...)",
                              "Adds all numbers in a range", "Math");
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
    registry.registerFunction("POWER", fn_POWER, "(number, power)",
                              "Returns number raised to a power", "Math");
    registry.registerFunction("ROUND", fn_ROUND, "(number, [num_digits])",
                              "Rounds to specified digits", "Math");
    registry.registerFunction("ROUNDUP", fn_ROUNDUP, "(number, [num_digits])",
                              "Rounds away from zero", "Math");
    registry.registerFunction("ROUNDDOWN", fn_ROUNDDOWN, "(number, [num_digits])",
                              "Rounds toward zero", "Math");
    registry.registerFunction("FLOOR", fn_FLOOR, "(number)", "Rounds down to nearest integer",
                              "Math");
    registry.registerFunction("CEILING", fn_CEILING, "(number)", "Rounds up to nearest integer",
                              "Math");
    registry.registerFunction("CEILING_MATH", fn_CEILING_MATH, "(number, [significance], [mode])",
                              "Rounds up to nearest multiple of significance", "Math");
    registry.registerFunction("FLOOR_MATH", fn_FLOOR_MATH, "(number, [significance], [mode])",
                              "Rounds down to nearest multiple of significance", "Math");
    registry.registerFunction("MOD", fn_MOD, "(number, divisor)",
                              "Returns remainder after division", "Math");
    registry.registerFunction("INT", fn_INT, "(number)", "Truncates to an integer", "Math");
    registry.registerFunction("SIGN", fn_SIGN, "(number)", "Returns the sign of a number", "Math");
    registry.registerFunction("EXP", fn_EXP, "(number)", "Returns e raised to a power", "Math");
    registry.registerFunction("LN", fn_LN, "(number)", "Returns the natural logarithm", "Math");
    registry.registerFunction("TRUNC", fn_TRUNC, "(number, [num_digits])",
                              "Truncates to specified digits", "Math");
    registry.registerFunction("FACT", fn_FACT, "(number)", "Returns the factorial", "Math");
    registry.registerFunction("QUOTIENT", fn_QUOTIENT, "(numerator, denominator)",
                              "Returns integer portion of division", "Math");
    registry.registerFunction("LOG10", fn_LOG10, "(number)", "Returns the base-10 logarithm",
                              "Math");
    registry.registerFunction("LOG", fn_LOG, "(number, [base])", "Returns the logarithm", "Math");
}

}  // namespace cells
