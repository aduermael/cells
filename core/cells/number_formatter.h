// =============================================================================
// Number Formatter
// =============================================================================
//
// Formats numeric values according to NumberFormat definitions.
// Handles locale-aware formatting with configurable separators.
//
// Key responsibilities:
// - Format numbers according to format ID (General, Number, Currency, etc.)
// - Apply locale settings (decimal separator, thousands separator)
// - Format dates from Excel serial numbers (days since 1899-12-30)
// - Format times from fractional day values (0.5 = noon)
// - Handle accounting format (aligned currency, negatives in parens)
//
// Formatting functions:
// - formatNumber(): Main entry point, looks up format by ID
// - formatGeneral(): Auto-detect best representation
// - formatPercentage(): Value * 100 with % suffix
// - formatCurrency(): Currency symbol with alignment
// - formatDate()/formatTime(): Date/time from serial numbers
//
// Dependencies: number_format.h, types.h, id.h
// Used by: bindings.cc (cell display), formula_eval.cc
//
// =============================================================================

#ifndef CELLS_NUMBER_FORMATTER_H_
#define CELLS_NUMBER_FORMATTER_H_

#include <string>
#include <unordered_map>

#include "core/cells/id.h"
#include "core/cells/number_format.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
class NumberFormatRegistry;

// Locale settings for formatting
// Controls decimal separator, thousands separator, date/time formats
struct FormatLocale {
    char decimalSeparator{'.'};
    char thousandsSeparator{','};
    std::string dateFormat{"M/D/YYYY"};    // Short date format
    std::string timeFormat12h{"h:mm AM"};  // 12-hour time format
    std::string timeFormat24h{"HH:mm"};    // 24-hour time format
    std::string currencySymbol{"$"};
    bool currencySymbolBefore{true};  // Symbol before number

    // Default US locale
    static FormatLocale US();

    // Default European locale (comma decimal, period thousands)
    static FormatLocale EU();
};

// Result of formatting a cell value
struct FormattedValue {
    std::string text;     // Formatted display string
    bool isError{false};  // True if formatting failed
    std::string errorMessage;

    static FormattedValue success(const std::string& text);
    static FormattedValue error(const std::string& message);
};

// Format a numeric value according to a NumberFormat
// registry: Format registry to look up format by ID (may cache new dynamic formats)
// value: The numeric value to format
// formatId: ID of the format to use (null ID = GENERAL)
// locale: Locale settings for formatting
FormattedValue formatNumber(NumberFormatRegistry& registry, double value, const ID& formatId,
                            const FormatLocale& locale = FormatLocale::US());

// Format a numeric value, also checking workbook custom formats
// registry: Format registry (may cache new dynamic formats)
// customFormats: Custom format definitions from workbook (format ID -> format code)
// value: The numeric value to format
// formatId: ID of the format to use (null ID = GENERAL)
// locale: Locale settings for formatting
FormattedValue formatNumber(NumberFormatRegistry& registry,
                            const std::unordered_map<ID, std::string, IDHash>& customFormats,
                            double value, const ID& formatId,
                            const FormatLocale& locale = FormatLocale::US());

// Format a value using a specific format (without registry lookup)
FormattedValue formatWithFormat(double value, const NumberFormat& format,
                                const FormatLocale& locale = FormatLocale::US());

// Format a value as GENERAL format (auto-detect best representation)
// Shows integers without decimals, limits decimal places for floats
FormattedValue formatGeneral(double value, const FormatLocale& locale = FormatLocale::US());

// Format a value for editing (formula bar / cell editor)
// Returns human-readable "edit value" instead of raw underlying value:
// - DATE: Returns formatted date string (e.g., "12/12/2025" not "46003")
// - TIME: Returns formatted time string (e.g., "3:30 PM" not "0.645833")
// - DATETIME: Returns date + time string
// - PERCENTAGE: Returns percentage string (e.g., "15%" not "0.15")
// - CURRENCY/NUMBER/GENERAL: Returns raw numeric value as string
// registry: Format registry to look up format by ID (may cache new dynamic formats)
// value: The numeric value to format for editing
// formatId: ID of the format to use (null ID = GENERAL)
// locale: Locale settings for formatting
std::string formatEditValue(NumberFormatRegistry& registry, double value, const ID& formatId,
                            const FormatLocale& locale = FormatLocale::US());

// Format a plain number with specified decimal places and thousands separator
// decimalPlaces: Number of decimal places (0-15)
// useThousandsSeparator: Whether to add thousands separators
FormattedValue formatPlainNumber(double value, uint8_t decimalPlaces, bool useThousandsSeparator,
                                 const FormatLocale& locale = FormatLocale::US());

// Format as percentage (value 0.15 -> "15%")
// decimalPlaces: Number of decimal places (0-15)
FormattedValue formatPercentage(double value, uint8_t decimalPlaces,
                                const FormatLocale& locale = FormatLocale::US());

// Format as currency ($1,234.56)
// decimalPlaces: Number of decimal places (0-15)
// currencySymbol: The currency symbol to use
// isAccounting: Use accounting format (aligned symbols, negatives in parens)
FormattedValue formatCurrency(double value, uint8_t decimalPlaces,
                              const std::string& currencySymbol, bool isAccounting,
                              const FormatLocale& locale = FormatLocale::US());

// Format as scientific notation (1.50E+10)
// decimalPlaces: Number of decimal places in mantissa
FormattedValue formatScientific(double value, uint8_t decimalPlaces,
                                const FormatLocale& locale = FormatLocale::US());

// Format as date (serial date -> "1/15/2024")
// Serial date: Days since December 30, 1899 (Excel convention)
// formatCode: Date format code (SHORT, LONG, ISO)
FormattedValue formatDate(double serialDate, const ID& formatId,
                          const FormatLocale& locale = FormatLocale::US());

// Format as time (fractional day -> "12:30 PM")
// Fractional day: 0.0 = midnight, 0.5 = noon
// formatId: Time format ID (TIME_12H, TIME_24H)
FormattedValue formatTime(double fractionalDay, const ID& formatId,
                          const FormatLocale& locale = FormatLocale::US());

// Format as date and time combined
FormattedValue formatDateTime(double serialDateTime, const ID& formatId,
                              const FormatLocale& locale = FormatLocale::US());

}  // namespace cells

#endif  // CELLS_NUMBER_FORMATTER_H_
