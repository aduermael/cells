#include "core/cells/formula_functions.h"

#include <cctype>
#include <cmath>
#include <ctime>

#include <algorithm>
#include <chrono>
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

// =============================================================================
// Built-in Function Implementations - Text Functions
// =============================================================================

// LEN(text)
// Returns the number of characters in a text string
static EvalResult fn_LEN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    return EvalResult::Number(static_cast<double>(text.getString().length()));
}

// LEFT(text, [num_chars])
// Returns the leftmost characters from a text string
static EvalResult fn_LEFT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    int numChars = 1;  // Default
    if (args.size() == 2) {
        EvalResult num = evaluateAsNumber(args[1], ctx);
        if (num.isError()) {
            return num;
        }
        numChars = static_cast<int>(num.getNumber());
        if (numChars < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    const std::string& str = text.getString();
    if (numChars >= static_cast<int>(str.length())) {
        return text;
    }
    return EvalResult::String(str.substr(0, numChars));
}

// RIGHT(text, [num_chars])
// Returns the rightmost characters from a text string
static EvalResult fn_RIGHT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    int numChars = 1;  // Default
    if (args.size() == 2) {
        EvalResult num = evaluateAsNumber(args[1], ctx);
        if (num.isError()) {
            return num;
        }
        numChars = static_cast<int>(num.getNumber());
        if (numChars < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    const std::string& str = text.getString();
    if (numChars >= static_cast<int>(str.length())) {
        return text;
    }
    return EvalResult::String(str.substr(str.length() - numChars));
}

// MID(text, start_num, num_chars)
// Returns characters from the middle of a text string (1-indexed)
static EvalResult fn_MID(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    EvalResult startNum = evaluateAsNumber(args[1], ctx);
    if (startNum.isError()) {
        return startNum;
    }

    EvalResult numChars = evaluateAsNumber(args[2], ctx);
    if (numChars.isError()) {
        return numChars;
    }

    const int start = static_cast<int>(startNum.getNumber());
    const int count = static_cast<int>(numChars.getNumber());

    if (start < 1 || count < 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    const std::string& str = text.getString();
    const auto startIdx = static_cast<size_t>(start - 1);  // Convert to 0-indexed

    if (startIdx >= str.length()) {
        return EvalResult::String("");
    }

    return EvalResult::String(str.substr(startIdx, count));
}

// TRIM(text)
// Removes extra spaces (leading, trailing, and multiple internal spaces)
static EvalResult fn_TRIM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    const std::string& str = text.getString();
    std::string result;
    result.reserve(str.length());

    bool inSpace = false;
    bool started = false;

    for (const char c : str) {
        if (c == ' ') {
            if (started && !inSpace) {
                result += ' ';
                inSpace = true;
            }
        } else {
            result += c;
            inSpace = false;
            started = true;
        }
    }

    // Remove trailing space if any
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return EvalResult::String(result);
}

// UPPER(text)
// Converts text to uppercase
static EvalResult fn_UPPER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    std::string result = text.getString();
    for (char& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return EvalResult::String(result);
}

// LOWER(text)
// Converts text to lowercase
static EvalResult fn_LOWER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    std::string result = text.getString();
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return EvalResult::String(result);
}

// PROPER(text)
// Capitalizes the first letter of each word
static EvalResult fn_PROPER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    std::string result = text.getString();
    bool capitalizeNext = true;

    for (char& c : result) {
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            if (capitalizeNext) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                capitalizeNext = false;
            } else {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        } else {
            // Non-letter triggers capitalization of next letter
            capitalizeNext = true;
        }
    }

    return EvalResult::String(result);
}

// FIND(find_text, within_text, [start_num])
// Case-sensitive search, returns 1-indexed position or #VALUE! if not found
static EvalResult fn_FIND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult findText = evaluateAsString(args[0], ctx);
    if (findText.isError()) {
        return findText;
    }

    EvalResult withinText = evaluateAsString(args[1], ctx);
    if (withinText.isError()) {
        return withinText;
    }

    int startNum = 1;
    if (args.size() == 3) {
        EvalResult start = evaluateAsNumber(args[2], ctx);
        if (start.isError()) {
            return start;
        }
        startNum = static_cast<int>(start.getNumber());
        if (startNum < 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    const std::string& needle = findText.getString();
    const std::string& haystack = withinText.getString();

    // Empty string is always found at start position
    if (needle.empty()) {
        return EvalResult::Number(startNum);
    }

    const auto startIdx = static_cast<size_t>(startNum - 1);
    if (startIdx >= haystack.length()) {
        return EvalResult::Error(CellError::VALUE);
    }

    const size_t pos = haystack.find(needle, startIdx);
    if (pos == std::string::npos) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(static_cast<double>(pos + 1));  // 1-indexed
}

// SEARCH(find_text, within_text, [start_num])
// Case-insensitive search, returns 1-indexed position or #VALUE! if not found
static EvalResult fn_SEARCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult findText = evaluateAsString(args[0], ctx);
    if (findText.isError()) {
        return findText;
    }

    EvalResult withinText = evaluateAsString(args[1], ctx);
    if (withinText.isError()) {
        return withinText;
    }

    int startNum = 1;
    if (args.size() == 3) {
        EvalResult start = evaluateAsNumber(args[2], ctx);
        if (start.isError()) {
            return start;
        }
        startNum = static_cast<int>(start.getNumber());
        if (startNum < 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    std::string needle = findText.getString();
    std::string haystack = withinText.getString();

    // Convert both to lowercase for case-insensitive search
    for (char& c : needle) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (char& c : haystack) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Empty string is always found at start position
    if (needle.empty()) {
        return EvalResult::Number(startNum);
    }

    const auto startIdx = static_cast<size_t>(startNum - 1);
    if (startIdx >= haystack.length()) {
        return EvalResult::Error(CellError::VALUE);
    }

    const size_t pos = haystack.find(needle, startIdx);
    if (pos == std::string::npos) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(static_cast<double>(pos + 1));  // 1-indexed
}

// SUBSTITUTE(text, old_text, new_text, [instance_num])
// Replaces occurrences of old_text with new_text
static EvalResult fn_SUBSTITUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    EvalResult oldTextResult = evaluateAsString(args[1], ctx);
    if (oldTextResult.isError()) {
        return oldTextResult;
    }

    EvalResult newTextResult = evaluateAsString(args[2], ctx);
    if (newTextResult.isError()) {
        return newTextResult;
    }

    int instanceNum = 0;  // 0 means replace all
    if (args.size() == 4) {
        EvalResult instance = evaluateAsNumber(args[3], ctx);
        if (instance.isError()) {
            return instance;
        }
        instanceNum = static_cast<int>(instance.getNumber());
        if (instanceNum < 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    const std::string& text = textResult.getString();
    const std::string& oldText = oldTextResult.getString();
    const std::string& newText = newTextResult.getString();

    if (oldText.empty()) {
        return textResult;  // Nothing to replace
    }

    std::string result;
    result.reserve(text.length());

    size_t pos = 0;
    int occurrence = 0;

    while (pos < text.length()) {
        const size_t foundPos = text.find(oldText, pos);
        if (foundPos == std::string::npos) {
            result += text.substr(pos);
            break;
        }

        result += text.substr(pos, foundPos - pos);
        occurrence++;

        if (instanceNum == 0 || occurrence == instanceNum) {
            result += newText;
        } else {
            result += oldText;
        }

        pos = foundPos + oldText.length();
    }

    return EvalResult::String(result);
}

// REPLACE(old_text, start_num, num_chars, new_text)
// Replaces characters at a specific position
static EvalResult fn_REPLACE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult oldTextResult = evaluateAsString(args[0], ctx);
    if (oldTextResult.isError()) {
        return oldTextResult;
    }

    EvalResult startNumResult = evaluateAsNumber(args[1], ctx);
    if (startNumResult.isError()) {
        return startNumResult;
    }

    EvalResult numCharsResult = evaluateAsNumber(args[2], ctx);
    if (numCharsResult.isError()) {
        return numCharsResult;
    }

    EvalResult newTextResult = evaluateAsString(args[3], ctx);
    if (newTextResult.isError()) {
        return newTextResult;
    }

    const std::string& oldText = oldTextResult.getString();
    const int startNum = static_cast<int>(startNumResult.getNumber());
    const int numChars = static_cast<int>(numCharsResult.getNumber());
    const std::string& newText = newTextResult.getString();

    if (startNum < 1 || numChars < 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    const auto startIdx = static_cast<size_t>(startNum - 1);

    std::string result;
    if (startIdx >= oldText.length()) {
        result = oldText + newText;
    } else {
        result = oldText.substr(0, startIdx) + newText + oldText.substr(startIdx + numChars);
    }

    return EvalResult::String(result);
}

// CONCAT(text1, [text2], ...)
// Joins text strings (newer Excel function)
static EvalResult fn_CONCAT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::string result;

    for (const ASTNode* arg : args) {
        EvalResult val = evaluate(arg, ctx);

        if (val.isError()) {
            return val;
        }

        if (val.isRange()) {
            // Expand range and concatenate all values
            const std::vector<EvalResult> rangeValues =
                collectRangeValues(val.getRangeBounds(), ctx);
            for (const EvalResult& rv : rangeValues) {
                if (rv.isError()) {
                    return rv;
                }
                if (!rv.isEmpty()) {
                    EvalResult strVal = rv.toString();
                    if (strVal.isError()) {
                        return strVal;
                    }
                    result += strVal.getString();
                }
            }
        } else if (!val.isEmpty()) {
            EvalResult strVal = val.toString();
            if (strVal.isError()) {
                return strVal;
            }
            result += strVal.getString();
        }
    }

    return EvalResult::String(result);
}

// CONCATENATE(text1, [text2], ...)
// Legacy version of CONCAT
static EvalResult fn_CONCATENATE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_CONCAT(args, ctx);
}

// REPT(text, number_times)
// Repeats text a specified number of times
static EvalResult fn_REPT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    EvalResult timesResult = evaluateAsNumber(args[1], ctx);
    if (timesResult.isError()) {
        return timesResult;
    }

    const std::string& text = textResult.getString();
    const int times = static_cast<int>(timesResult.getNumber());

    if (times < 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    if (times == 0 || text.empty()) {
        return EvalResult::String("");
    }

    // Guard against excessive memory usage
    constexpr size_t MAX_RESULT_LENGTH = 32767;  // Excel limit
    if (text.length() * times > MAX_RESULT_LENGTH) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::string result;
    result.reserve(text.length() * times);
    for (int i = 0; i < times; i++) {
        result += text;
    }

    return EvalResult::String(result);
}

// TEXT(value, format_text)
// Formats a number as text using a format string (simplified implementation)
static EvalResult fn_TEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult valueResult = evaluateAsNumber(args[0], ctx);
    if (valueResult.isError()) {
        return valueResult;
    }

    EvalResult formatResult = evaluateAsString(args[1], ctx);
    if (formatResult.isError()) {
        return formatResult;
    }

    const double value = valueResult.getNumber();
    const std::string& format = formatResult.getString();

    // Simplified format support
    // Full Excel format parsing is complex; we support common cases

    // Percentage format
    if (format.find('%') != std::string::npos) {
        // Count decimal places after the decimal point before %
        int decimalPlaces = 0;
        const size_t dotPos = format.find('.');
        const size_t percentPos = format.find('%');
        if (dotPos != std::string::npos && dotPos < percentPos) {
            // Count digits between . and %
            for (size_t i = dotPos + 1; i < percentPos; i++) {
                if (format[i] == '0' || format[i] == '#') {
                    decimalPlaces++;
                }
            }
        }

        const double percentage = value * 100.0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f%%", decimalPlaces, percentage);
        return EvalResult::String(buf);
    }

    // Count decimal places in format
    int decimalPlaces = 0;
    const size_t dotPos = format.find('.');
    if (dotPos != std::string::npos) {
        for (size_t i = dotPos + 1; i < format.length(); i++) {
            if (format[i] == '0' || format[i] == '#') {
                decimalPlaces++;
            } else {
                break;
            }
        }
    }

    // Check for currency prefix
    std::string prefix;
    if (!format.empty() && format[0] == '$') {
        prefix = "$";
    }

    // Check for thousands separator
    const bool useThousands = format.find(',') != std::string::npos;

    // Format the number
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, std::abs(value));

    std::string numStr = buf;

    // Add thousands separator if needed
    if (useThousands) {
        size_t decPos = numStr.find('.');
        if (decPos == std::string::npos) {
            decPos = numStr.length();
        }

        const std::string intPart = numStr.substr(0, decPos);
        const std::string decPart = (decPos < numStr.length()) ? numStr.substr(decPos) : "";

        std::string formatted;
        int count = 0;
        for (auto it = intPart.rbegin(); it != intPart.rend(); ++it) {
            if (count > 0 && count % 3 == 0) {
                formatted.insert(formatted.begin(), ',');
            }
            formatted.insert(formatted.begin(), *it);
            count++;
        }
        numStr = formatted + decPart;
    }

    // Add prefix and handle negative
    std::string result;
    if (value < 0) {
        result = "-" + prefix + numStr;
    } else {
        result = prefix + numStr;
    }

    return EvalResult::String(result);
}

// VALUE(text)
// Converts a text string that represents a number to a number
static EvalResult fn_VALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    std::string text = textResult.getString();

    // Strip leading/trailing whitespace
    const size_t start = text.find_first_not_of(" \t\n\r");
    const size_t end = text.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return EvalResult::Number(0);  // Empty or whitespace-only string
    }
    text = text.substr(start, end - start + 1);

    // Handle currency symbols
    if (!text.empty() && text[0] == '$') {
        text = text.substr(1);
    }

    // Handle percentage
    bool isPercent = false;
    if (!text.empty() && text.back() == '%') {
        text.pop_back();
        isPercent = true;
    }

    // Remove commas (thousands separator)
    std::string cleaned;
    for (const char c : text) {
        if (c != ',') {
            cleaned += c;
        }
    }

    // Try to parse as number
    char* endPtr = nullptr;  // NOLINT(misc-const-correctness)
    const double value = std::strtod(cleaned.c_str(), &endPtr);

    if (endPtr == cleaned.c_str() || *endPtr != '\0') {
        return EvalResult::Error(CellError::VALUE);
    }

    if (isPercent) {
        return EvalResult::Number(value / 100.0);
    }

    return EvalResult::Number(value);
}

// CHAR(number)
// Returns the character specified by a code number
static EvalResult fn_CHAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult numResult = evaluateAsNumber(args[0], ctx);
    if (numResult.isError()) {
        return numResult;
    }

    const int code = static_cast<int>(numResult.getNumber());
    if (code < 1 || code > 255) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::String(std::string(1, static_cast<char>(code)));
}

// CODE(text)
// Returns the numeric code of the first character
static EvalResult fn_CODE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    const std::string& text = textResult.getString();
    if (text.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(static_cast<double>(static_cast<unsigned char>(text[0])));
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
// Built-in Function Implementations - Date/Time Functions
// =============================================================================

// Excel serial date system:
// Day 1 = January 1, 1900
// Day 2 = January 2, 1900
// ... etc.
// Note: Excel incorrectly treats 1900 as a leap year (Feb 29, 1900 exists as day 60)
// We replicate this bug for compatibility.

// Helper: Convert year/month/day to Excel serial date
static double dateToSerial(int year, int month, int day) {
    // Handle month overflow/underflow
    while (month > 12) {
        year++;
        month -= 12;
    }
    while (month < 1) {
        year--;
        month += 12;
    }

    // Days in each month (non-leap year)
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Check if leap year (but remember Excel's 1900 bug)
    auto isLeapYear = [](int y) -> bool {
        if (y == 1900) {
            return true;  // Excel bug: 1900 is treated as leap year
        }
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    // Calculate days from 1900-01-01
    double serial = 0;

    // Add days for complete years
    for (int y = 1900; y < year; y++) {
        serial += isLeapYear(y) ? 366 : 365;
    }

    // Add days for complete months in current year
    for (int m = 1; m < month; m++) {
        serial += daysInMonth[m - 1];
        if (m == 2 && isLeapYear(year)) {
            serial += 1;  // February in leap year
        }
    }

    // Add days in current month
    serial += day;

    return serial;
}

// Helper: Convert Excel serial date to year/month/day
static void serialToDate(double serial, int& year, int& month, int& day) {
    // Days in each month (non-leap year)
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    auto isLeapYear = [](int y) -> bool {
        if (y == 1900) {
            return true;  // Excel bug
        }
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    int remaining = static_cast<int>(serial);
    year = 1900;

    // Find year
    while (true) {
        const int daysInYear = isLeapYear(year) ? 366 : 365;
        if (remaining <= daysInYear) {
            break;
        }
        remaining -= daysInYear;
        year++;
    }

    // Find month
    month = 1;
    while (month <= 12) {
        int daysThisMonth = daysInMonth[month - 1];
        if (month == 2 && isLeapYear(year)) {
            daysThisMonth = 29;
        }
        if (remaining <= daysThisMonth) {
            break;
        }
        remaining -= daysThisMonth;
        month++;
    }

    day = remaining;
}

// Helper: Convert time fraction to hours/minutes/seconds
static void serialToTime(double serial, int& hour, int& minute, int& second) {
    // Time is fractional part of serial date
    const double timePart = serial - std::floor(serial);

    // Convert to seconds
    double totalSeconds = timePart * 24 * 60 * 60;

    // Round to avoid floating point errors
    totalSeconds = std::round(totalSeconds);

    hour = static_cast<int>(totalSeconds / 3600) % 24;
    minute = static_cast<int>(totalSeconds / 60) % 60;
    second = static_cast<int>(totalSeconds) % 60;
}

// Helper: Convert hours/minutes/seconds to time fraction
static double timeToSerial(int hour, int minute, int second) {
    return (hour * 3600.0 + minute * 60.0 + second) / (24.0 * 60.0 * 60.0);
}

// NOW()
// Returns current date and time as Excel serial date (volatile)
static EvalResult fn_NOW(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get current time
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&nowTime);

    // Calculate date serial
    const double dateSerial =
        dateToSerial(localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday);

    // Calculate time fraction
    const double timeSerial =
        timeToSerial(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

    return EvalResult::Number(dateSerial + timeSerial);
}

// TODAY()
// Returns current date as Excel serial date (volatile)
static EvalResult fn_TODAY(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get current time
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&nowTime);

    return EvalResult::Number(
        dateToSerial(localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday));
}

// DATE(year, month, day)
// Constructs a date from year, month, day components
static EvalResult fn_DATE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult yearResult = evaluateAsNumber(args[0], ctx);
    if (yearResult.isError()) {
        return yearResult;
    }

    EvalResult monthResult = evaluateAsNumber(args[1], ctx);
    if (monthResult.isError()) {
        return monthResult;
    }

    EvalResult dayResult = evaluateAsNumber(args[2], ctx);
    if (dayResult.isError()) {
        return dayResult;
    }

    int year = static_cast<int>(yearResult.getNumber());
    const int month = static_cast<int>(monthResult.getNumber());
    const int day = static_cast<int>(dayResult.getNumber());

    // Excel interprets 0-99 as 1900-1999 or 2000-2029
    if (year >= 0 && year <= 99) {
        if (year <= 29) {
            year += 2000;
        } else {
            year += 1900;
        }
    }

    if (year < 1900 || year > 9999) {
        return EvalResult::Error(CellError::NUM);
    }

    const double serial = dateToSerial(year, month, day);
    if (serial < 1 || serial > 2958465) {  // Excel date range
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(serial);
}

// TIME(hour, minute, second)
// Constructs a time from hour, minute, second components
static EvalResult fn_TIME(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult hourResult = evaluateAsNumber(args[0], ctx);
    if (hourResult.isError()) {
        return hourResult;
    }

    EvalResult minuteResult = evaluateAsNumber(args[1], ctx);
    if (minuteResult.isError()) {
        return minuteResult;
    }

    EvalResult secondResult = evaluateAsNumber(args[2], ctx);
    if (secondResult.isError()) {
        return secondResult;
    }

    const int hour = static_cast<int>(hourResult.getNumber());
    const int minute = static_cast<int>(minuteResult.getNumber());
    const int second = static_cast<int>(secondResult.getNumber());

    // Calculate total seconds and normalize
    int totalSeconds = hour * 3600 + minute * 60 + second;

    // Time wraps around at 24 hours (result is always 0-1)
    // Handle negative times too
    while (totalSeconds < 0) {
        totalSeconds += 24 * 3600;
    }
    totalSeconds = totalSeconds % (24 * 3600);

    return EvalResult::Number(static_cast<double>(totalSeconds) / (24.0 * 3600.0));
}

// DATEVALUE(date_text)
// Converts a date string to serial date
static EvalResult fn_DATEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    const std::string& text = textResult.getString();

    // Try to parse common date formats
    // Format: YYYY-MM-DD
    int year = 0;
    int month = 0;
    int day = 0;

    // Try to parse common date formats
    // Each format parses into different variable positions
    const bool isIsoFormat = sscanf(text.c_str(), "%d-%d-%d", &year, &month, &day) == 3;
    if (!isIsoFormat) {
        // Try US format MM/DD/YYYY
        const bool isUsFormat = sscanf(text.c_str(), "%d/%d/%d", &month, &day, &year) == 3;
        if (!isUsFormat) {
            // Try European format DD.MM.YYYY
            const bool isEuFormat = sscanf(text.c_str(), "%d.%d.%d", &day, &month, &year) == 3;
            if (!isEuFormat) {
                return EvalResult::Error(CellError::VALUE);
            }
        }
    }

    // Handle 2-digit years
    if (year < 100) {
        if (year <= 29) {
            year += 2000;
        } else {
            year += 1900;
        }
    }

    if (year < 1900 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(dateToSerial(year, month, day));
}

// TIMEVALUE(time_text)
// Converts a time string to serial time fraction
static EvalResult fn_TIMEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    const std::string& text = textResult.getString();

    int hour = 0;
    int minute = 0;
    int second = 0;

    // Try to parse time formats
    if (sscanf(text.c_str(), "%d:%d:%d", &hour, &minute, &second) == 3) {
        // HH:MM:SS
    } else if (sscanf(text.c_str(), "%d:%d", &hour, &minute) == 2) {
        // HH:MM
        second = 0;
    } else {
        return EvalResult::Error(CellError::VALUE);
    }

    // Check for AM/PM suffix
    bool isPM = false;
    bool isAM = false;
    std::string upperText = text;
    for (char& c : upperText) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upperText.find("PM") != std::string::npos) {
        isPM = true;
    } else if (upperText.find("AM") != std::string::npos) {
        isAM = true;
    }

    // Convert 12-hour to 24-hour format
    if (isPM && hour < 12) {
        hour += 12;
    } else if (isAM && hour == 12) {
        hour = 0;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(timeToSerial(hour, minute, second));
}

// YEAR(serial_number)
// Extracts year from a date serial number
static EvalResult fn_YEAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(year);
}

// MONTH(serial_number)
// Extracts month from a date serial number (1-12)
static EvalResult fn_MONTH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(month);
}

// DAY(serial_number)
// Extracts day of month from a date serial number
static EvalResult fn_DAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(day);
}

// HOUR(serial_number)
// Extracts hour from a time serial number (0-23)
static EvalResult fn_HOUR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(hour);
}

// MINUTE(serial_number)
// Extracts minute from a time serial number (0-59)
static EvalResult fn_MINUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(minute);
}

// SECOND(serial_number)
// Extracts second from a time serial number (0-59)
static EvalResult fn_SECOND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(second);
}

// WEEKDAY(serial_number, [return_type])
// Returns day of week (1-7)
// return_type: 1 = Sunday=1 (default), 2 = Monday=1, 3 = Monday=0
static EvalResult fn_WEEKDAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    int returnType = 1;  // Default: Sunday = 1
    if (args.size() == 2) {
        EvalResult typeResult = evaluateAsNumber(args[1], ctx);
        if (typeResult.isError()) {
            return typeResult;
        }
        returnType = static_cast<int>(typeResult.getNumber());
        if (returnType < 1 || returnType > 3) {
            return EvalResult::Error(CellError::NUM);
        }
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    // Excel day 1 (Jan 1, 1900) is treated as Sunday
    // Day 7 (Jan 7, 1900) is Saturday
    // Day 8 (Jan 8, 1900) is Sunday again
    //
    // Pattern for type 1 (Sunday=1, Saturday=7):
    // Day 1 -> Sunday (1)
    // Day 2 -> Monday (2)
    // ...
    // Day 7 -> Saturday (7)
    // Day 8 -> Sunday (1)
    //
    // Formula: ((serial - 1) % 7) + 1
    // Day 1: ((1-1) % 7) + 1 = 0 + 1 = 1 (Sunday) ✓
    // Day 7: ((7-1) % 7) + 1 = 6 + 1 = 7 (Saturday) ✓
    // Day 8: ((8-1) % 7) + 1 = 0 + 1 = 1 (Sunday) ✓

    const int daysSince = static_cast<int>(serial);
    int weekday = ((daysSince - 1) % 7) + 1;  // Type 1: Sunday = 1, Saturday = 7

    switch (returnType) {
        case 1:
            // Sunday = 1, Saturday = 7 (default)
            break;
        case 2:
            // Monday = 1, Sunday = 7
            // Sunday (1) -> 7, Mon (2) -> 1, Tue (3) -> 2, ..., Sat (7) -> 6
            weekday = (weekday == 1) ? 7 : weekday - 1;
            break;
        case 3:
            // Monday = 0, Sunday = 6
            // Sunday (1) -> 6, Mon (2) -> 0, Tue (3) -> 1, ..., Sat (7) -> 5
            weekday = (weekday + 5) % 7;
            break;
        default:
            // Already validated returnType is 1-3 above
            break;
    }

    return EvalResult::Number(weekday);
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

    // Text functions
    registry.registerFunction("LEN", fn_LEN);
    registry.registerFunction("LEFT", fn_LEFT);
    registry.registerFunction("RIGHT", fn_RIGHT);
    registry.registerFunction("MID", fn_MID);
    registry.registerFunction("TRIM", fn_TRIM);
    registry.registerFunction("UPPER", fn_UPPER);
    registry.registerFunction("LOWER", fn_LOWER);
    registry.registerFunction("PROPER", fn_PROPER);
    registry.registerFunction("FIND", fn_FIND);
    registry.registerFunction("SEARCH", fn_SEARCH);
    registry.registerFunction("SUBSTITUTE", fn_SUBSTITUTE);
    registry.registerFunction("REPLACE", fn_REPLACE);
    registry.registerFunction("CONCAT", fn_CONCAT);
    registry.registerFunction("CONCATENATE", fn_CONCATENATE);
    registry.registerFunction("REPT", fn_REPT);
    registry.registerFunction("TEXT", fn_TEXT);
    registry.registerFunction("VALUE", fn_VALUE);
    registry.registerFunction("CHAR", fn_CHAR);
    registry.registerFunction("CODE", fn_CODE);

    // Date/Time functions (volatile functions marked with true)
    registry.registerFunction("NOW", fn_NOW, true);      // Volatile
    registry.registerFunction("TODAY", fn_TODAY, true);  // Volatile
    registry.registerFunction("DATE", fn_DATE);
    registry.registerFunction("TIME", fn_TIME);
    registry.registerFunction("DATEVALUE", fn_DATEVALUE);
    registry.registerFunction("TIMEVALUE", fn_TIMEVALUE);
    registry.registerFunction("YEAR", fn_YEAR);
    registry.registerFunction("MONTH", fn_MONTH);
    registry.registerFunction("DAY", fn_DAY);
    registry.registerFunction("HOUR", fn_HOUR);
    registry.registerFunction("MINUTE", fn_MINUTE);
    registry.registerFunction("SECOND", fn_SECOND);
    registry.registerFunction("WEEKDAY", fn_WEEKDAY);
}

}  // namespace cells
