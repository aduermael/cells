#include "core/cells/functions/fn_text.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <string>
#include <vector>

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

EvalResult fn_TEXTJOIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult delimRes = evaluateAsString(args[0], ctx);
    if (delimRes.isError()) {
        return delimRes;
    }
    const EvalResult ignoreRes = evaluateAsBoolean(args[1], ctx);
    if (ignoreRes.isError()) {
        return ignoreRes;
    }
    const std::string& delim = delimRes.getString();
    const bool ignoreEmpty = ignoreRes.getBoolean();

    std::vector<std::string> parts;
    for (size_t i = 2; i < args.size(); ++i) {
        const EvalResult val = evaluate(args[i], ctx);
        if (val.isError()) {
            return val;
        }
        std::vector<EvalResult> items;
        if (val.isRange()) {
            items = collectRangeValues(val, ctx);
        } else if (val.isArray()) {
            for (const auto& row : val.getArray()) {
                items.insert(items.end(), row.begin(), row.end());
            }
        } else {
            items.push_back(val);
        }
        for (const EvalResult& item : items) {
            if (item.isError()) {
                return item;
            }
            if (item.isEmpty()) {
                if (!ignoreEmpty) {
                    parts.emplace_back("");
                }
                continue;
            }
            const EvalResult s = item.toString();
            if (s.isError()) {
                return s;
            }
            if (ignoreEmpty && s.getString().empty()) {
                continue;
            }
            parts.push_back(s.getString());
        }
    }
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += delim;
        }
        result += parts[i];
    }
    return EvalResult::String(result);
}

EvalResult fn_CLEAN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult text = evaluateAsString(args[0], ctx);
    if (text.isError()) {
        return text;
    }
    std::string out;
    out.reserve(text.getString().size());
    for (unsigned char c : text.getString()) {
        if (c >= 32) {
            out.push_back(static_cast<char>(c));
        }
    }
    return EvalResult::String(out);
}

namespace {

std::string utf8Encode(std::uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::uint32_t utf8DecodeFirst(const std::string& s, bool& ok) {
    ok = false;
    if (s.empty()) {
        return 0;
    }
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) {
        ok = true;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && s.size() >= 2) {
        const auto b1 = static_cast<unsigned char>(s[1]);
        if ((b1 & 0xC0) == 0x80) {
            ok = true;
            return (static_cast<std::uint32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F);
        }
    } else if ((b0 & 0xF0) == 0xE0 && s.size() >= 3) {
        const auto b1 = static_cast<unsigned char>(s[1]);
        const auto b2 = static_cast<unsigned char>(s[2]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
            ok = true;
            return (static_cast<std::uint32_t>(b0 & 0x0F) << 12) |
                   (static_cast<std::uint32_t>(b1 & 0x3F) << 6) | (b2 & 0x3F);
        }
    } else if ((b0 & 0xF8) == 0xF0 && s.size() >= 4) {
        const auto b1 = static_cast<unsigned char>(s[1]);
        const auto b2 = static_cast<unsigned char>(s[2]);
        const auto b3 = static_cast<unsigned char>(s[3]);
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
            ok = true;
            return (static_cast<std::uint32_t>(b0 & 0x07) << 18) |
                   (static_cast<std::uint32_t>(b1 & 0x3F) << 12) |
                   (static_cast<std::uint32_t>(b2 & 0x3F) << 6) | (b3 & 0x3F);
        }
    }
    return 0;
}

double roundHalfAway(double value) {
    if (value >= 0.0) {
        return std::floor(value + 0.5);
    }
    return std::ceil(value - 0.5);
}

std::string formatWithDecimals(double value, int decimals, bool thousands) {
    double scaled = value;
    if (decimals >= 0) {
        const double p = std::pow(10.0, decimals);
        scaled = roundHalfAway(value * p) / p;
    } else {
        const double p = std::pow(10.0, -decimals);
        scaled = roundHalfAway(value / p) * p;
        decimals = 0;
    }
    const bool neg = scaled < 0.0;
    double mag = std::abs(scaled);
    auto intPart = static_cast<long long>(std::floor(mag + 1e-12));
    double frac = mag - static_cast<double>(intPart);
    if (frac < 0.0) {
        frac = 0.0;
    }
    std::string intStr = std::to_string(intPart);
    if (thousands && intStr.size() > 3) {
        std::string grouped;
        int count = 0;
        for (int i = static_cast<int>(intStr.size()) - 1; i >= 0; --i) {
            if (count > 0 && count % 3 == 0) {
                grouped.push_back(',');
            }
            grouped.push_back(intStr[static_cast<size_t>(i)]);
            ++count;
        }
        std::reverse(grouped.begin(), grouped.end());
        intStr = grouped;
    }
    std::string out = intStr;
    if (decimals > 0) {
        double f = frac;
        std::string fracStr;
        for (int i = 0; i < decimals; ++i) {
            f *= 10.0;
            int digit = static_cast<int>(f + 1e-9);
            if (digit > 9) {
                digit = 9;
            }
            fracStr.push_back(static_cast<char>('0' + digit));
            f -= digit;
        }
        out += "." + fracStr;
    }
    if (neg) {
        return "-" + out;
    }
    return out;
}

}  // namespace

EvalResult fn_UNICHAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    const auto cp = static_cast<std::int64_t>(std::floor(n.getNumber()));
    if (cp < 1 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::String(utf8Encode(static_cast<std::uint32_t>(cp)));
}

EvalResult fn_UNICODE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult t = evaluateAsString(args[0], ctx);
    if (t.isError()) {
        return t;
    }
    if (t.getString().empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    bool ok = false;
    const std::uint32_t cp = utf8DecodeFirst(t.getString(), ok);
    if (!ok || cp == 0) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Number(static_cast<double>(cp));
}

EvalResult fn_FIXED(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    int decimals = 2;
    if (args.size() >= 2) {
        const EvalResult d = evaluateAsNumber(args[1], ctx);
        if (d.isError()) {
            return d;
        }
        decimals = static_cast<int>(std::floor(d.getNumber()));
        if (decimals > 127 || decimals < -127) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    bool noCommas = false;
    if (args.size() == 3) {
        const EvalResult c = evaluateAsBoolean(args[2], ctx);
        if (c.isError()) {
            return c;
        }
        noCommas = c.getBoolean();
    }
    return EvalResult::String(formatWithDecimals(n.getNumber(), decimals, !noCommas));
}

EvalResult fn_DOLLAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult n = evaluateAsNumber(args[0], ctx);
    if (n.isError()) {
        return n;
    }
    int decimals = 2;
    if (args.size() == 2) {
        const EvalResult d = evaluateAsNumber(args[1], ctx);
        if (d.isError()) {
            return d;
        }
        decimals = static_cast<int>(std::floor(d.getNumber()));
        if (decimals > 127 || decimals < -127) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    const double v = n.getNumber();
    std::string body = formatWithDecimals(std::abs(v), decimals, true);
    if (v < 0.0) {
        return EvalResult::String("($" + body + ")");
    }
    return EvalResult::String("$" + body);
}

EvalResult fn_NUMBERVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult textRes = evaluateAsString(args[0], ctx);
    if (textRes.isError()) {
        return textRes;
    }
    std::string dec = ".";
    std::string group = ",";
    if (args.size() >= 2) {
        const EvalResult d = evaluateAsString(args[1], ctx);
        if (d.isError()) {
            return d;
        }
        if (!d.getString().empty()) {
            dec = d.getString().substr(0, 1);
        }
    }
    if (args.size() == 3) {
        const EvalResult g = evaluateAsString(args[2], ctx);
        if (g.isError()) {
            return g;
        }
        if (!g.getString().empty()) {
            group = g.getString().substr(0, 1);
        }
    }
    std::string s = textRes.getString();
    const size_t start = s.find_first_not_of(" \t\n\r");
    const size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return EvalResult::Number(0.0);
    }
    s = s.substr(start, end - start + 1);
    bool percent = false;
    if (!s.empty() && s.back() == '%') {
        percent = true;
        s.pop_back();
    }
    std::string cleaned;
    for (char c : s) {
        if (c == group[0]) {
            continue;
        }
        if (c == dec[0]) {
            cleaned.push_back('.');
        } else {
            cleaned.push_back(c);
        }
    }
    if (cleaned.empty()) {
        return EvalResult::Number(0.0);
    }
    char* endptr = nullptr;
    const double val = std::strtod(cleaned.c_str(), &endptr);
    if (endptr == cleaned.c_str()) {
        return EvalResult::Error(CellError::VALUE);
    }
    while (*endptr != '\0' && std::isspace(static_cast<unsigned char>(*endptr)) != 0) {
        ++endptr;
    }
    if (*endptr != '\0') {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Number(excelNormalize(percent ? val / 100.0 : val));
}

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
    registry.registerFunction("TEXTJOIN", fn_TEXTJOIN,
                              "(delimiter, ignore_empty, text1, [text2], ...)",
                              "Joins text with a delimiter", "Text");
    registry.registerFunction("CLEAN", fn_CLEAN, "(text)", "Removes non-printable characters",
                              "Text");
    registry.registerFunction("UNICHAR", fn_UNICHAR, "(number)",
                              "Unicode character for a code point", "Text");
    registry.registerFunction("UNICODE", fn_UNICODE, "(text)", "Code point of the first character",
                              "Text");
    registry.registerFunction("DOLLAR", fn_DOLLAR, "(number, [decimals])",
                              "Formats a number as currency text", "Text");
    registry.registerFunction("FIXED", fn_FIXED, "(number, [decimals], [no_commas])",
                              "Formats a number as text with fixed decimals", "Text");
    registry.registerFunction("NUMBERVALUE", fn_NUMBERVALUE,
                              "(text, [decimal_separator], [group_separator])",
                              "Converts locale-formatted text to a number", "Text");
    // Unicode Excel treats DBCS byte variants as the character implementations.
    registry.registerAlias("LENB", "LEN");
    registry.registerAlias("LEFTB", "LEFT");
    registry.registerAlias("RIGHTB", "RIGHT");
    registry.registerAlias("MIDB", "MID");
    registry.registerAlias("FINDB", "FIND");
    registry.registerAlias("SEARCHB", "SEARCH");
    registry.registerAlias("REPLACEB", "REPLACE");
}

}  // namespace cells
