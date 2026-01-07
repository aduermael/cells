#ifndef CELLS_FORMAT_CODE_FORMATTER_H_
#define CELLS_FORMAT_CODE_FORMATTER_H_

#include <string>

#include "core/cells/format_code_parser.h"

namespace cells {

/**
 * FormatLocaleSettings provides locale-specific formatting preferences.
 * This is a simplified version of FormatLocale from number_formatter.h.
 */
struct FormatLocaleSettings {
    char decimalSeparator{'.'};
    char thousandsSeparator{','};

    static FormatLocaleSettings US() {
        FormatLocaleSettings settings;
        settings.decimalSeparator = '.';
        settings.thousandsSeparator = ',';
        return settings;
    }

    static FormatLocaleSettings EU() {
        FormatLocaleSettings settings;
        settings.decimalSeparator = ',';
        settings.thousandsSeparator = '.';
        return settings;
    }
};

/**
 * Result of formatting a value with a format code.
 */
struct FormatCodeResult {
    std::string text;
    bool success{false};
    std::string errorMessage;

    static FormatCodeResult ok(const std::string& text) {
        FormatCodeResult result;
        result.text = text;
        result.success = true;
        return result;
    }

    static FormatCodeResult error(const std::string& message) {
        FormatCodeResult result;
        result.success = false;
        result.errorMessage = message;
        result.text = "#FORMAT!";
        return result;
    }
};

/**
 * Format a numeric value using a parsed format code.
 *
 * @param value The numeric value to format
 * @param formatCode The parsed format code structure
 * @param locale Locale settings for decimal/thousands separators
 * @return FormatCodeResult with the formatted text
 */
FormatCodeResult formatWithParsedCode(
    double value, const ParsedFormatCode& formatCode,
    const FormatLocaleSettings& locale = FormatLocaleSettings::US());

/**
 * Format a numeric value using a format code string.
 *
 * This is a convenience function that parses the format code first, then formats.
 *
 * @param value The numeric value to format
 * @param formatCode The Excel-style format code string
 * @param locale Locale settings for decimal/thousands separators
 * @return FormatCodeResult with the formatted text
 */
FormatCodeResult formatWithCode(double value, const std::string& formatCode,
                                const FormatLocaleSettings& locale = FormatLocaleSettings::US());

/**
 * Format a text value using a format code string.
 *
 * This handles the text section (@) of a format code.
 *
 * @param text The text value to format
 * @param formatCode The parsed format code structure
 * @return The formatted text
 */
std::string formatTextWithCode(const std::string& text, const ParsedFormatCode& formatCode);

}  // namespace cells

#endif  // CELLS_FORMAT_CODE_FORMATTER_H_
