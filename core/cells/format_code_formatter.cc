#include "core/cells/format_code_formatter.h"

#include <cmath>

#include <iomanip>
#include <sstream>

namespace cells {

// Helper: Add thousands separators to a number string
static std::string addThousandsSeparators(const std::string& intPart, char separator) {
    if (intPart.empty()) {
        return intPart;
    }

    std::string result;
    result.reserve(intPart.size() + intPart.size() / 3);

    int count = 0;
    for (auto it = intPart.rbegin(); it != intPart.rend(); ++it) {
        if (count > 0 && count % 3 == 0 && *it != '-') {
            result.insert(result.begin(), separator);
        }
        result.insert(result.begin(), *it);
        if (*it != '-') {
            count++;
        }
    }
    return result;
}

// Helper: Format a number with specified decimal places
static std::string formatDecimal(double value, uint8_t decimalPlaces, char decimalSeparator) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimalPlaces) << std::abs(value);
    std::string result = ss.str();

    // Replace decimal separator if needed
    if (decimalSeparator != '.') {
        const auto pos = result.find('.');
        if (pos != std::string::npos) {
            result[pos] = decimalSeparator;
        }
    }

    return result;
}

// Helper: Format a value using a single section
static FormatCodeResult formatWithSection(double value, const FormatCodeSection& section,
                                          const FormatLocaleSettings& locale) {
    // Handle special case: text format section
    if (section.isTextFormat) {
        // Text format for numbers just converts to string
        std::ostringstream ss;
        ss << value;
        return FormatCodeResult::ok(ss.str());
    }

    // Handle special case: General format
    if (section.code == "General") {
        // For General format, display with reasonable precision
        if (std::isnan(value)) {
            return FormatCodeResult::error("NaN");
        }
        if (std::isinf(value)) {
            return FormatCodeResult::ok(value > 0 ? "∞" : "-∞");
        }

        // Check if it's essentially an integer
        if (std::abs(value) < 1e15 && std::abs(value - std::round(value)) < 1e-10) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(0) << value;
            return FormatCodeResult::ok(ss.str());
        }

        // Otherwise, show reasonable precision
        std::ostringstream ss;
        ss << std::setprecision(15) << value;
        std::string result = ss.str();

        // Trim trailing zeros after decimal point
        if (result.find('.') != std::string::npos) {
            const size_t lastNonZero = result.find_last_not_of('0');
            if (lastNonZero != std::string::npos) {
                if (result[lastNonZero] == '.') {
                    result = result.substr(0, lastNonZero);
                } else {
                    result = result.substr(0, lastNonZero + 1);
                }
            }
        }

        return FormatCodeResult::ok(result);
    }

    // Handle empty section
    if (section.isEmpty) {
        return FormatCodeResult::ok("");
    }

    // Check for NaN/Inf
    if (std::isnan(value)) {
        return FormatCodeResult::error("NaN");
    }
    if (std::isinf(value)) {
        return FormatCodeResult::ok(value > 0 ? "∞" : "-∞");
    }

    // Apply percentage multiplier if needed
    double displayValue = value;
    if (section.hasPercent) {
        displayValue = value * 100.0;
    }

    // Format the number portion
    std::string formatted =
        formatDecimal(displayValue, section.decimalPlaces, locale.decimalSeparator);

    // Add thousands separators if needed
    if (section.hasThousandsSeparator) {
        // Split integer and decimal parts
        const auto decPos = formatted.find(locale.decimalSeparator);
        std::string intPart;
        std::string decPart;

        if (decPos != std::string::npos) {
            intPart = formatted.substr(0, decPos);
            decPart = formatted.substr(decPos);
        } else {
            intPart = formatted;
        }

        // Add thousands separators to integer part
        intPart = addThousandsSeparators(intPart, locale.thousandsSeparator);
        formatted = intPart + decPart;
    }

    // Build final result with prefix and suffix
    std::string result;

    // Handle negative values with parentheses
    const bool isNegative = value < 0;
    if (isNegative && section.useParentheses) {
        // For parentheses format, don't add a minus sign
        result = section.prefix + formatted + section.suffix;
    } else {
        // Add negative sign if needed (and not already in prefix)
        std::string prefix = section.prefix;
        if (isNegative && prefix.find('-') == std::string::npos) {
            prefix = "-" + prefix;
        }
        result = prefix + formatted + section.suffix;
    }

    return FormatCodeResult::ok(result);
}

FormatCodeResult formatWithParsedCode(double value, const ParsedFormatCode& formatCode,
                                      const FormatLocaleSettings& locale) {
    if (!formatCode.valid) {
        return FormatCodeResult::error("Invalid format code: " + formatCode.errorMessage);
    }

    if (formatCode.sections.empty()) {
        return FormatCodeResult::error("No format sections");
    }

    // Determine which section to use based on value
    // Section 1: positive numbers (and all numbers if only 1 section)
    // Section 2: negative numbers
    // Section 3: zero
    // Section 4: text

    size_t sectionIndex = 0;
    double formatValue = value;

    if (formatCode.sections.size() == 1) {
        // Single section - use for all values
        sectionIndex = 0;
    } else if (formatCode.sections.size() >= 2) {
        if (value > 0) {
            sectionIndex = 0;
        } else if (value < 0) {
            sectionIndex = 1;
            // Typically negative section formats the absolute value
            formatValue = std::abs(value);
        } else {
            // Zero
            if (formatCode.sections.size() >= 3) {
                sectionIndex = 2;
            } else {
                sectionIndex = 0;
            }
        }
    }

    return formatWithSection(formatValue, formatCode.sections[sectionIndex], locale);
}

FormatCodeResult formatWithCode(double value, const std::string& formatCode,
                                const FormatLocaleSettings& locale) {
    const ParsedFormatCode parsed = parseFormatCode(formatCode);
    return formatWithParsedCode(value, parsed, locale);
}

std::string formatTextWithCode(const std::string& text, const ParsedFormatCode& formatCode) {
    // Find the text section (4th section, or first if it's a text-only format)
    for (const auto& section : formatCode.sections) {
        if (section.isTextFormat) {
            // @ is replaced with the text value
            return section.prefix + text + section.suffix;
        }
    }

    // No text section found - return text as-is
    return text;
}

}  // namespace cells
