// =============================================================================
// Format Code Parser
// =============================================================================
//
// Parses Excel-style format code strings (e.g., "#,##0.00;(#,##0.00)") into
// a structured representation for formatting.
//
// Key responsibilities:
// - Parse format code syntax: digits (0, #), separators (., ,), symbols ($, %)
// - Handle multi-section codes: positive;negative;zero;text
// - Extract decimal places, thousands separators, currency symbols
// - Validate format codes and report errors
//
// Format code syntax:
// - 0: Digit placeholder (shows 0 if no digit)
// - #: Digit placeholder (omits leading zeros)
// - .: Decimal separator
// - ,: Thousands separator (in integer portion)
// - %: Percentage display (multiplies by 100)
// - $, €, £, ¥: Currency symbols
// - @: Text placeholder
// - ;: Section separator (positive;negative;zero;text)
// - "text": Literal text
//
// Dependencies: None
// Used by: format_code_formatter.h, number_format.h (custom format creation)
//
// =============================================================================

#ifndef CELLS_FORMAT_CODE_PARSER_H_
#define CELLS_FORMAT_CODE_PARSER_H_

#include <cstdint>

#include <optional>
#include <string>
#include <vector>

namespace cells {

/**
 * Represents a parsed section of an Excel format code.
 *
 * Excel format codes can have up to 4 sections separated by semicolons:
 * - Section 1: Format for positive numbers
 * - Section 2: Format for negative numbers (optional)
 * - Section 3: Format for zero values (optional)
 * - Section 4: Format for text (optional)
 *
 * Example: "#,##0.00;(#,##0.00);-;@"
 */
struct FormatCodeSection {
    // The raw format code string for this section
    std::string code;

    // Number of decimal places (0 means no decimals)
    uint8_t decimalPlaces{0};

    // Whether the integer part uses thousand separators (#,##0 pattern)
    bool hasThousandsSeparator{false};

    // Whether this section has a percent sign (multiplies value by 100)
    bool hasPercent{false};

    // Currency symbol if present (e.g., "$", "€", "£")
    std::string currencySymbol;

    // Prefix text (literal text before the number)
    std::string prefix;

    // Suffix text (literal text after the number)
    std::string suffix;

    // Whether to show negative numbers in parentheses
    bool useParentheses{false};

    // Whether this is a text format section (@)
    bool isTextFormat{false};

    // Whether the section is empty/omitted
    bool isEmpty{false};
};

/**
 * Represents a fully parsed Excel format code with all sections.
 */
struct ParsedFormatCode {
    // The original format code string
    std::string originalCode;

    // Parsed sections (1-4 sections)
    std::vector<FormatCodeSection> sections;

    // Quick access flags (derived from first/positive section)
    uint8_t decimalPlaces{0};
    bool hasThousandsSeparator{false};
    bool hasPercent{false};
    std::string currencySymbol;

    // Whether parsing succeeded
    bool valid{false};

    // Error message if parsing failed
    std::string errorMessage;
};

/**
 * Parse an Excel-style format code string.
 *
 * Supported format code syntax:
 * - `0` - Digit placeholder (shows 0 if no digit)
 * - `#` - Digit placeholder (omits leading zeros)
 * - `.` - Decimal separator
 * - `,` - Thousands separator (when placed in integer portion, e.g., `#,##0`)
 * - `%` - Percentage display (multiplies stored value by 100)
 * - `$`, `€`, `£`, `¥` - Currency symbols
 * - `@` - Text placeholder
 * - `;` - Section separator
 * - `"text"` - Literal text (quoted)
 *
 * Examples:
 * - "0.00" → 2 decimal places
 * - "#,##0.00" → 2 decimals with thousands separator
 * - "0.00%" → percentage with 2 decimals
 * - "$#,##0.00" → currency with 2 decimals
 * - "#,##0.00;(#,##0.00)" → positive;negative sections
 * - "General" → default format (special case)
 *
 * @param formatCode The format code string to parse
 * @return ParsedFormatCode with valid=true on success, valid=false on error
 */
ParsedFormatCode parseFormatCode(const std::string& formatCode);

/**
 * Parse a single format code section (no semicolons).
 *
 * @param section The section string to parse
 * @return FormatCodeSection with parsed properties
 */
FormatCodeSection parseFormatCodeSection(const std::string& section);

/**
 * Check if a character is a known currency symbol.
 *
 * Recognizes: $, €, £, ¥, ¤ (generic currency)
 *
 * @param c The character to check (may be multi-byte for unicode)
 * @return true if it's a currency symbol
 */
bool isCurrencySymbol(const std::string& str, size_t pos, size_t& symbolLen);

/**
 * Validate a format code string.
 *
 * @param formatCode The format code to validate
 * @return std::nullopt if valid, or error message string if invalid
 */
std::optional<std::string> validateFormatCode(const std::string& formatCode);

}  // namespace cells

#endif  // CELLS_FORMAT_CODE_PARSER_H_
