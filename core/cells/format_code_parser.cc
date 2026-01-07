#include "core/cells/format_code_parser.h"

#include <cctype>
#include <cstring>

namespace cells {

// Currency symbols we recognize (including multi-byte UTF-8)
// $, €, £, ¥, ¤
static const char* const kCurrencySymbols[] = {
    "$",      // U+0024 Dollar sign (1 byte)
    "€",      // U+20AC Euro sign (3 bytes: E2 82 AC)
    "£",      // U+00A3 Pound sign (2 bytes: C2 A3)
    "¥",      // U+00A5 Yen sign (2 bytes: C2 A5)
    "¤",      // U+00A4 Generic currency (2 bytes: C2 A4)
    nullptr,  // Sentinel
};

bool isCurrencySymbol(const std::string& str, size_t pos, size_t& symbolLen) {
    for (size_t i = 0; kCurrencySymbols[i] != nullptr; ++i) {
        const char* symbol = kCurrencySymbols[i];
        const size_t len = std::strlen(symbol);
        if (pos + len <= str.size() && str.compare(pos, len, symbol) == 0) {
            symbolLen = len;
            return true;
        }
    }
    symbolLen = 0;
    return false;
}

// Helper: Split a format code string by semicolons (respecting quoted strings)
static std::vector<std::string> splitSections(const std::string& formatCode) {
    std::vector<std::string> sections;
    std::string current;
    bool inQuote = false;

    for (const char ch : formatCode) {
        if (ch == '"') {
            inQuote = !inQuote;
            current += ch;
        } else if (ch == ';' && !inQuote) {
            sections.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    sections.push_back(current);

    return sections;
}

FormatCodeSection parseFormatCodeSection(const std::string& section) {
    FormatCodeSection result;
    result.code = section;

    if (section.empty()) {
        result.isEmpty = true;
        return result;
    }

    // Special case: text format
    if (section == "@") {
        result.isTextFormat = true;
        return result;
    }

    // State machine for parsing
    bool inDecimalPart = false;
    bool inQuote = false;
    bool foundDigitPlaceholder = false;
    std::string quotedText;

    for (size_t i = 0; i < section.size(); ++i) {
        const char ch = section[i];

        // Handle quoted strings
        if (ch == '"') {
            if (inQuote) {
                // End of quoted string - add to prefix or suffix
                if (!foundDigitPlaceholder) {
                    result.prefix += quotedText;
                } else {
                    result.suffix += quotedText;
                }
                quotedText.clear();
            }
            inQuote = !inQuote;
            continue;
        }

        if (inQuote) {
            quotedText += ch;
            continue;
        }

        // Check for currency symbols (multi-byte)
        size_t symbolLen = 0;
        if (isCurrencySymbol(section, i, symbolLen)) {
            result.currencySymbol = section.substr(i, symbolLen);
            if (!foundDigitPlaceholder) {
                result.prefix += result.currencySymbol;
            } else {
                result.suffix += result.currencySymbol;
            }
            i += symbolLen - 1;  // -1 because loop will increment
            continue;
        }

        switch (ch) {
            case '0':
            case '#':
                foundDigitPlaceholder = true;
                if (inDecimalPart) {
                    result.decimalPlaces++;
                }
                break;

            case '.':
                inDecimalPart = true;
                break;

            case ',':
                // Comma in the integer part indicates thousands separator
                // (only if before decimal point and after a digit placeholder)
                if (!inDecimalPart && foundDigitPlaceholder) {
                    result.hasThousandsSeparator = true;
                }
                break;

            case '%':
                result.hasPercent = true;
                if (!foundDigitPlaceholder) {
                    result.prefix += ch;
                } else {
                    result.suffix += ch;
                }
                break;

            case '(':
                if (!foundDigitPlaceholder) {
                    result.prefix += ch;
                    result.useParentheses = true;
                } else {
                    result.suffix += ch;
                }
                break;

            case ')':
                if (!foundDigitPlaceholder) {
                    result.prefix += ch;
                } else {
                    result.suffix += ch;
                    result.useParentheses = true;
                }
                break;

            case '@':
                result.isTextFormat = true;
                break;

            case '_':
            case '*':
                // Space padding / Repeat character - skip the next character
                if (i + 1 < section.size()) {
                    ++i;
                }
                break;

            default:
                // Space, sign characters, and other characters are treated as literal text
                if (!foundDigitPlaceholder) {
                    result.prefix += ch;
                } else {
                    result.suffix += ch;
                }
                break;
        }
    }

    return result;
}

ParsedFormatCode parseFormatCode(const std::string& formatCode) {
    ParsedFormatCode result;
    result.originalCode = formatCode;

    // Handle empty format code
    if (formatCode.empty()) {
        result.errorMessage = "Empty format code";
        return result;
    }

    // Special case: "General" format
    if (formatCode == "General" || formatCode == "general" || formatCode == "GENERAL") {
        result.valid = true;
        FormatCodeSection generalSection;
        generalSection.code = "General";
        result.sections.push_back(generalSection);
        return result;
    }

    // Split into sections by semicolon
    const std::vector<std::string> sectionStrings = splitSections(formatCode);

    // Limit to 4 sections
    if (sectionStrings.size() > 4) {
        result.errorMessage = "Too many sections (max 4)";
        return result;
    }

    // Parse each section
    for (const auto& sectionStr : sectionStrings) {
        const FormatCodeSection section = parseFormatCodeSection(sectionStr);
        result.sections.push_back(section);
    }

    // Extract quick-access properties from first (positive) section
    if (!result.sections.empty()) {
        const FormatCodeSection& first = result.sections[0];
        result.decimalPlaces = first.decimalPlaces;
        result.hasThousandsSeparator = first.hasThousandsSeparator;
        result.hasPercent = first.hasPercent;
        result.currencySymbol = first.currencySymbol;
    }

    result.valid = true;
    return result;
}

std::optional<std::string> validateFormatCode(const std::string& formatCode) {
    if (formatCode.empty()) {
        return "Empty format code";
    }

    // Try parsing to validate
    ParsedFormatCode parsed = parseFormatCode(formatCode);
    if (!parsed.valid) {
        return parsed.errorMessage;
    }

    // Check for unbalanced quotes
    int quoteCount = 0;
    for (const char ch : formatCode) {
        if (ch == '"') {
            quoteCount++;
        }
    }
    if (quoteCount % 2 != 0) {
        return "Unbalanced quotes";
    }

    // Check for unbalanced parentheses
    int parenCount = 0;
    bool inQuote = false;
    for (const char ch : formatCode) {
        if (ch == '"') {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == '(') {
                parenCount++;
            } else if (ch == ')') {
                parenCount--;
            }
        }
    }
    if (parenCount != 0) {
        return "Unbalanced parentheses";
    }

    return std::nullopt;  // Valid
}

}  // namespace cells
