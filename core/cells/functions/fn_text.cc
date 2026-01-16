#include "core/cells/functions/fn_text.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <string>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

// =============================================================================
// Basic Text Functions
// =============================================================================

EvalResult fn_LEN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }

    return EvalResult::Number(static_cast<double>(text.getString().length()));
}

EvalResult fn_LEFT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_RIGHT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_MID(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_TRIM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// =============================================================================
// Case Functions
// =============================================================================

EvalResult fn_UPPER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_LOWER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_PROPER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// =============================================================================
// Search and Replace Functions
// =============================================================================

EvalResult fn_FIND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_SEARCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_SUBSTITUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_REPLACE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// =============================================================================
// Concatenation and Conversion Functions
// =============================================================================

EvalResult fn_CONCAT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::string result;

    for (const ASTNode* arg : args) {
        EvalResult val = evaluate(arg, ctx);

        if (val.isError()) {
            return val;
        }

        if (val.isRange()) {
            // Expand range and concatenate all values
            const std::vector<EvalResult> rangeValues = collectRangeValues(val, ctx);
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

EvalResult fn_CONCATENATE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return fn_CONCAT(args, ctx);
}

EvalResult fn_REPT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_TEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_VALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// =============================================================================
// Character Functions
// =============================================================================

EvalResult fn_CHAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

EvalResult fn_CODE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
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

// =============================================================================
// Registration
// =============================================================================

void registerTextFunctions(FunctionRegistry& registry) {
    // Basic text functions
    registry.registerFunction("LEN", fn_LEN, "(text)", "Returns the number of characters", "Text");
    registry.registerFunction("LEFT", fn_LEFT, "(text, [num_chars])", "Returns leftmost characters",
                              "Text");
    registry.registerFunction("RIGHT", fn_RIGHT, "(text, [num_chars])",
                              "Returns rightmost characters", "Text");
    registry.registerFunction("MID", fn_MID, "(text, start_num, num_chars)",
                              "Returns characters from the middle", "Text");
    registry.registerFunction("TRIM", fn_TRIM, "(text)", "Removes extra spaces from text", "Text");

    // Case functions
    registry.registerFunction("UPPER", fn_UPPER, "(text)", "Converts text to uppercase", "Text");
    registry.registerFunction("LOWER", fn_LOWER, "(text)", "Converts text to lowercase", "Text");
    registry.registerFunction("PROPER", fn_PROPER, "(text)",
                              "Capitalizes first letter of each word", "Text");

    // Search and replace
    registry.registerFunction("FIND", fn_FIND, "(find_text, within_text, [start_num])",
                              "Case-sensitive text search", "Text");
    registry.registerFunction("SEARCH", fn_SEARCH, "(find_text, within_text, [start_num])",
                              "Case-insensitive text search", "Text");
    registry.registerFunction("SUBSTITUTE", fn_SUBSTITUTE,
                              "(text, old_text, new_text, [instance_num])",
                              "Replaces text occurrences", "Text");
    registry.registerFunction("REPLACE", fn_REPLACE, "(old_text, start_num, num_chars, new_text)",
                              "Replaces characters by position", "Text");

    // Concatenation and conversion
    registry.registerFunction("CONCAT", fn_CONCAT, "(text1, [text2], ...)", "Joins text strings",
                              "Text");
    registry.registerFunction("CONCATENATE", fn_CONCATENATE, "(text1, [text2], ...)",
                              "Joins text strings (legacy)", "Text");
    registry.registerFunction("REPT", fn_REPT, "(text, number_times)", "Repeats text", "Text");
    registry.registerFunction("TEXT", fn_TEXT, "(value, format_text)", "Formats number as text",
                              "Text");
    registry.registerFunction("VALUE", fn_VALUE, "(text)", "Converts text to number", "Text");

    // Character functions
    registry.registerFunction("CHAR", fn_CHAR, "(number)", "Returns character for ASCII code",
                              "Text");
    registry.registerFunction("CODE", fn_CODE, "(text)", "Returns ASCII code of first character",
                              "Text");
}

}  // namespace cells
