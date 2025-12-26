#include "core/cells/formula_functions.h"

#include <cctype>
#include <cmath>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"

namespace cells {

// =============================================================================
// FunctionRegistry Implementation
// =============================================================================

FunctionRegistry& FunctionRegistry::instance() {
    static FunctionRegistry registry;
    return registry;
}

FunctionRegistry::FunctionRegistry() {
    // Initialize all built-in functions
    initializeBuiltinFunctions(*this);
}

std::string FunctionRegistry::toUpper(const std::string& s) {
    std::string result = s;
    for (char& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

void FunctionRegistry::registerFunction(const std::string& name, FormulaFunction fn,
                                        bool isVolatile) {
    const std::string upperName = toUpper(name);
    functions_[upperName] = std::move(fn);
    if (isVolatile) {
        volatileFunctions_.insert(upperName);
    }
}

EvalResult FunctionRegistry::call(const std::string& name, const std::vector<const ASTNode*>& args,
                                  EvalContext& ctx) const {
    const std::string upperName = toUpper(name);
    auto it = functions_.find(upperName);
    if (it == functions_.end()) {
        return EvalResult::Error(CellError::NAME);
    }
    return it->second(args, ctx);
}

bool FunctionRegistry::exists(const std::string& name) const {
    return functions_.count(toUpper(name)) > 0;
}

bool FunctionRegistry::isVolatile(const std::string& name) const {
    return volatileFunctions_.count(toUpper(name)) > 0;
}

std::vector<std::string> FunctionRegistry::getFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& [name, fn] : functions_) {
        (void)fn;  // Suppress unused warning
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// =============================================================================
// Helper Functions
// =============================================================================

std::vector<EvalResult> expandArguments(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<EvalResult> results;

    for (const ASTNode* arg : args) {
        const EvalResult result = evaluate(arg, ctx);

        if (result.isRange()) {
            // Expand range into individual cell values
            std::vector<EvalResult> rangeValues = collectRangeValues(result.getRangeBounds(), ctx);
            results.insert(results.end(), rangeValues.begin(), rangeValues.end());
        } else {
            results.push_back(result);
        }
    }

    return results;
}

std::pair<std::vector<double>, EvalResult> collectNumericValues(
    const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<double> values;
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);

    for (const EvalResult& val : expanded) {
        // Propagate errors
        if (val.isError()) {
            return {{}, val};
        }

        // Skip empty cells
        if (val.isEmpty()) {
            continue;
        }

        // Skip non-numeric values in ranges (Excel behavior)
        // But for direct arguments, try to coerce
        if (val.isString()) {
            // Try to parse string as number
            const EvalResult num = val.toNumber();
            if (num.isError()) {
                // In ranges, non-numeric strings are silently skipped
                // For direct arguments, this would be an error
                // Since we can't distinguish here, we'll skip (consistent with Excel)
                continue;
            }
            values.push_back(num.getNumber());
        } else if (val.isNumber()) {
            values.push_back(val.getNumber());
        } else if (val.isBoolean()) {
            // Booleans in ranges are typically skipped, but direct TRUE/FALSE count
            // For simplicity, include them (1 for TRUE, 0 for FALSE)
            values.push_back(val.getBoolean() ? 1.0 : 0.0);
        }
    }

    return {values, EvalResult::Empty()};
}

EvalResult evaluateAsNumber(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toNumber();
}

EvalResult evaluateAsString(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toString();
}

EvalResult evaluateAsBoolean(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toBoolean();
}

// =============================================================================
// Built-in Function Implementations - Math Functions
// =============================================================================

// SUM(value1, [value2], ...)
// Adds all numbers in the argument list
static EvalResult fn_SUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// AVERAGE(value1, [value2], ...)
// Returns arithmetic mean of numbers
static EvalResult fn_AVERAGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// COUNT(value1, [value2], ...)
// Counts numbers only
static EvalResult fn_COUNT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// COUNTA(value1, [value2], ...)
// Counts non-empty values
static EvalResult fn_COUNTA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// MIN(value1, [value2], ...)
// Returns smallest number
static EvalResult fn_MIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// MAX(value1, [value2], ...)
// Returns largest number
static EvalResult fn_MAX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// ABS(number)
// Returns absolute value
static EvalResult fn_ABS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::abs(num.getNumber()));
}

// SQRT(number)
// Returns square root
static EvalResult fn_SQRT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// POWER(number, power)
// Returns number raised to power (same as ^ operator)
static EvalResult fn_POWER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// ROUND(number, [num_digits])
// Rounds to specified number of digits
static EvalResult fn_ROUND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// FLOOR(number)
// Rounds down toward negative infinity
static EvalResult fn_FLOOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::floor(num.getNumber()));
}

// CEILING(number)
// Rounds up toward positive infinity
static EvalResult fn_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult num = evaluateAsNumber(args[0], ctx);
    if (num.isError()) {
        return num;
    }

    return EvalResult::Number(std::ceil(num.getNumber()));
}

// MOD(number, divisor)
// Returns remainder after division (with Excel's sign convention)
static EvalResult fn_MOD(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// INT(number)
// Truncates to integer (rounds down toward negative infinity)
static EvalResult fn_INT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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
// Built-in Function Implementations - Logic Functions
// =============================================================================

// IF(condition, value_if_true, [value_if_false])
// Returns value_if_true if condition is true, value_if_false otherwise
static EvalResult fn_IF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate condition
    EvalResult condition = evaluate(args[0], ctx);
    if (condition.isError()) {
        return condition;
    }

    // Convert to boolean
    EvalResult condBool = condition.toBoolean();
    if (condBool.isError()) {
        return condBool;
    }

    if (condBool.getBoolean()) {
        // Return value_if_true
        return evaluate(args[1], ctx);
    }
    // Return value_if_false (or FALSE if not provided)
    if (args.size() == 3) {
        return evaluate(args[2], ctx);
    }
    return EvalResult::Boolean(false);
}

// AND(logical1, [logical2], ...)
// Returns TRUE if all arguments are true
static EvalResult fn_AND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Boolean(true);  // Vacuous truth
    }

    for (const ASTNode* arg : args) {
        EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            return result;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues =
                collectRangeValues(result.getRangeBounds(), ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    return val;
                }
                if (val.isEmpty()) {
                    continue;  // Skip empty cells
                }
                EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    return boolVal;
                }
                if (!boolVal.getBoolean()) {
                    return EvalResult::Boolean(false);
                }
            }
        } else {
            EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                return boolVal;
            }
            if (!boolVal.getBoolean()) {
                return EvalResult::Boolean(false);
            }
        }
    }

    return EvalResult::Boolean(true);
}

// OR(logical1, [logical2], ...)
// Returns TRUE if any argument is true
static EvalResult fn_OR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Boolean(false);  // No true values found
    }

    for (const ASTNode* arg : args) {
        EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            return result;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues =
                collectRangeValues(result.getRangeBounds(), ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    return val;
                }
                if (val.isEmpty()) {
                    continue;  // Skip empty cells
                }
                EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    return boolVal;
                }
                if (boolVal.getBoolean()) {
                    return EvalResult::Boolean(true);
                }
            }
        } else {
            EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                return boolVal;
            }
            if (boolVal.getBoolean()) {
                return EvalResult::Boolean(true);
            }
        }
    }

    return EvalResult::Boolean(false);
}

// NOT(logical)
// Returns the opposite boolean value
static EvalResult fn_NOT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult result = evaluate(args[0], ctx);
    if (result.isError()) {
        return result;
    }

    EvalResult boolVal = result.toBoolean();
    if (boolVal.isError()) {
        return boolVal;
    }

    return EvalResult::Boolean(!boolVal.getBoolean());
}

// IFERROR(value, value_if_error)
// Returns value if not an error, otherwise value_if_error
static EvalResult fn_IFERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult value = evaluate(args[0], ctx);
    if (value.isError()) {
        return evaluate(args[1], ctx);
    }
    return value;
}

// IFNA(value, value_if_na)
// Returns value if not #N/A, otherwise value_if_na
static EvalResult fn_IFNA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult value = evaluate(args[0], ctx);
    if (value.isError() && value.getError() == CellError::NA) {
        return evaluate(args[1], ctx);
    }
    return value;
}

// EXACT(text1, text2)
// Case-sensitive string comparison
static EvalResult fn_EXACT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text1 = evaluateAsString(args[0], ctx);
    if (text1.isError()) {
        return text1;
    }

    EvalResult text2 = evaluateAsString(args[1], ctx);
    if (text2.isError()) {
        return text2;
    }

    return EvalResult::Boolean(text1.getString() == text2.getString());
}

// ISBLANK(value)
// Returns TRUE if cell is empty
static EvalResult fn_ISBLANK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isEmpty());
}

// ISNUMBER(value)
// Returns TRUE if value is a number
static EvalResult fn_ISNUMBER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isNumber());
}

// ISTEXT(value)
// Returns TRUE if value is text
static EvalResult fn_ISTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isString());
}

// ISERROR(value)
// Returns TRUE if value is any error
static EvalResult fn_ISERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isError());
}

// ISLOGICAL(value)
// Returns TRUE if value is a boolean
static EvalResult fn_ISLOGICAL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isBoolean());
}

// ISNA(value)
// Returns TRUE if value is #N/A
static EvalResult fn_ISNA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isError() && result.getError() == CellError::NA);
}

// TRUE()
// Returns the boolean value TRUE
static EvalResult fn_TRUE(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Boolean(true);
}

// FALSE()
// Returns the boolean value FALSE
static EvalResult fn_FALSE(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Boolean(false);
}

// =============================================================================
// Register Built-in Functions
// =============================================================================

void initializeBuiltinFunctions(FunctionRegistry& registry) {
    // Aggregate functions
    registry.registerFunction("SUM", fn_SUM);
    registry.registerFunction("AVERAGE", fn_AVERAGE);
    registry.registerFunction("COUNT", fn_COUNT);
    registry.registerFunction("COUNTA", fn_COUNTA);
    registry.registerFunction("MIN", fn_MIN);
    registry.registerFunction("MAX", fn_MAX);

    // Basic math functions
    registry.registerFunction("ABS", fn_ABS);
    registry.registerFunction("SQRT", fn_SQRT);
    registry.registerFunction("POWER", fn_POWER);
    registry.registerFunction("ROUND", fn_ROUND);
    registry.registerFunction("FLOOR", fn_FLOOR);
    registry.registerFunction("CEILING", fn_CEILING);
    registry.registerFunction("MOD", fn_MOD);
    registry.registerFunction("INT", fn_INT);

    // Logic functions
    registry.registerFunction("IF", fn_IF);
    registry.registerFunction("AND", fn_AND);
    registry.registerFunction("OR", fn_OR);
    registry.registerFunction("NOT", fn_NOT);
    registry.registerFunction("IFERROR", fn_IFERROR);
    registry.registerFunction("IFNA", fn_IFNA);

    // Type checking functions
    registry.registerFunction("EXACT", fn_EXACT);
    registry.registerFunction("ISBLANK", fn_ISBLANK);
    registry.registerFunction("ISNUMBER", fn_ISNUMBER);
    registry.registerFunction("ISTEXT", fn_ISTEXT);
    registry.registerFunction("ISERROR", fn_ISERROR);
    registry.registerFunction("ISLOGICAL", fn_ISLOGICAL);
    registry.registerFunction("ISNA", fn_ISNA);
    registry.registerFunction("TRUE", fn_TRUE);
    registry.registerFunction("FALSE", fn_FALSE);
}

}  // namespace cells
