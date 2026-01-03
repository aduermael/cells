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
    return EvalResult::Number(sum);
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
    return EvalResult::Number(sum / static_cast<double>(values.size()));
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
    return EvalResult::Number(minVal);
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
    return EvalResult::Number(maxVal);
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

    return EvalResult::Number(std::abs(num.getNumber()));
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

    return EvalResult::Number(std::sqrt(val));
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

    const double result = std::pow(base.getNumber(), exponent.getNumber());
    if (std::isnan(result) || std::isinf(result)) {
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(result);
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

    return EvalResult::Number(rounded);
}

EvalResult fn_FLOOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::floor(num.getNumber()));
}

EvalResult fn_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::ceil(num.getNumber()));
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
    return EvalResult::Number(result);
}

EvalResult fn_INT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::floor(num.getNumber()));
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
    registry.registerFunction("FLOOR", fn_FLOOR, "(number)", "Rounds down to nearest integer",
                              "Math");
    registry.registerFunction("CEILING", fn_CEILING, "(number)", "Rounds up to nearest integer",
                              "Math");
    registry.registerFunction("MOD", fn_MOD, "(number, divisor)",
                              "Returns remainder after division", "Math");
    registry.registerFunction("INT", fn_INT, "(number)", "Truncates to an integer", "Math");
}

}  // namespace cells
