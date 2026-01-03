#include "core/cells/number_formatter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "core/cells/input_parser.h"

namespace cells {

// FormatLocale presets
FormatLocale FormatLocale::US() {
    FormatLocale locale;
    locale.decimalSeparator = '.';
    locale.thousandsSeparator = ',';
    locale.dateFormat = "M/D/YYYY";
    locale.timeFormat12h = "h:mm AM";
    locale.timeFormat24h = "HH:mm";
    locale.currencySymbol = "$";
    locale.currencySymbolBefore = true;
    return locale;
}

FormatLocale FormatLocale::EU() {
    FormatLocale locale;
    locale.decimalSeparator = ',';
    locale.thousandsSeparator = '.';
    locale.dateFormat = "D/M/YYYY";
    locale.timeFormat12h = "h:mm AM";
    locale.timeFormat24h = "HH:mm";
    locale.currencySymbol = "€";
    locale.currencySymbolBefore = false;  // €100 vs 100€
    return locale;
}

// FormattedValue factory methods
FormattedValue FormattedValue::success(const std::string& text) {
    FormattedValue result;
    result.text = text;
    result.isError = false;
    return result;
}

FormattedValue FormattedValue::error(const std::string& message) {
    FormattedValue result;
    result.isError = true;
    result.errorMessage = message;
    result.text = "#FORMAT!";
    return result;
}

// Helper: Add thousands separators to a number string
static std::string addThousandsSeparators(const std::string& intPart, char separator) {
    std::string result;
    int count = 0;
    for (auto it = intPart.rbegin(); it != intPart.rend(); ++it) {
        if (count > 0 && count % 3 == 0 && *it != '-') {
            result = separator + result;
        }
        result = *it + result;
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
        auto pos = result.find('.');
        if (pos != std::string::npos) {
            result[pos] = decimalSeparator;
        }
    }

    // Add negative sign if needed
    if (value < 0) {
        result = "-" + result;
    }

    return result;
}

// Format as GENERAL format
FormattedValue formatGeneral(double value, const FormatLocale& locale) {
    // Handle special cases
    if (std::isnan(value)) {
        return FormattedValue::error("NaN");
    }
    if (std::isinf(value)) {
        return FormattedValue::success(value > 0 ? "∞" : "-∞");
    }

    // Check if it's essentially an integer
    if (std::abs(value) < 1e15 && std::abs(value - std::round(value)) < 1e-10) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(0) << value;
        return FormattedValue::success(ss.str());
    }

    // For very large or very small numbers, use scientific notation
    if (std::abs(value) >= 1e10 || (std::abs(value) < 1e-4 && value != 0)) {
        return formatScientific(value, 2, locale);
    }

    // Otherwise, show up to 10 significant decimal places, trimming trailing zeros
    std::ostringstream ss;
    ss << std::setprecision(10) << value;
    std::string result = ss.str();

    // Trim trailing zeros after decimal point
    if (result.find('.') != std::string::npos) {
        size_t lastNonZero = result.find_last_not_of('0');
        if (lastNonZero != std::string::npos) {
            if (result[lastNonZero] == '.') {
                result = result.substr(0, lastNonZero);
            } else {
                result = result.substr(0, lastNonZero + 1);
            }
        }
    }

    // Replace decimal separator if needed
    if (locale.decimalSeparator != '.') {
        auto pos = result.find('.');
        if (pos != std::string::npos) {
            result[pos] = locale.decimalSeparator;
        }
    }

    return FormattedValue::success(result);
}

// Format a plain number
FormattedValue formatPlainNumber(double value, uint8_t decimalPlaces, bool useThousandsSeparator,
                                 const FormatLocale& locale) {
    if (std::isnan(value)) {
        return FormattedValue::error("NaN");
    }
    if (std::isinf(value)) {
        return FormattedValue::success(value > 0 ? "∞" : "-∞");
    }

    std::string formatted = formatDecimal(value, decimalPlaces, locale.decimalSeparator);

    if (useThousandsSeparator) {
        // Split integer and decimal parts
        auto decPos = formatted.find(locale.decimalSeparator);
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

    return FormattedValue::success(formatted);
}

// Format as percentage
FormattedValue formatPercentage(double value, uint8_t decimalPlaces, const FormatLocale& locale) {
    if (std::isnan(value)) {
        return FormattedValue::error("NaN");
    }

    // Multiply by 100 to get percentage
    double percentValue = value * 100.0;
    std::string formatted = formatDecimal(percentValue, decimalPlaces, locale.decimalSeparator);
    return FormattedValue::success(formatted + "%");
}

// Format as currency
FormattedValue formatCurrency(double value, uint8_t decimalPlaces, const std::string& currencySymbol,
                              bool isAccounting, const FormatLocale& locale) {
    if (std::isnan(value)) {
        return FormattedValue::error("NaN");
    }

    bool isNegative = value < 0;
    double absValue = std::abs(value);

    // Format the number part with thousands separators
    auto numberResult = formatPlainNumber(absValue, decimalPlaces, true, locale);
    if (numberResult.isError) {
        return numberResult;
    }

    std::string formatted;

    if (isAccounting) {
        // Accounting format: symbol aligned, negatives in parentheses
        if (locale.currencySymbolBefore) {
            formatted = currencySymbol + " " + numberResult.text;
        } else {
            formatted = numberResult.text + " " + currencySymbol;
        }

        if (isNegative) {
            formatted = "(" + formatted + ")";
        }
    } else {
        // Regular currency format
        if (locale.currencySymbolBefore) {
            formatted = currencySymbol + numberResult.text;
        } else {
            formatted = numberResult.text + currencySymbol;
        }

        if (isNegative) {
            formatted = "-" + formatted;
        }
    }

    return FormattedValue::success(formatted);
}

// Format as scientific notation
FormattedValue formatScientific(double value, uint8_t decimalPlaces, const FormatLocale& locale) {
    if (std::isnan(value)) {
        return FormattedValue::error("NaN");
    }
    if (std::isinf(value)) {
        return FormattedValue::success(value > 0 ? "∞" : "-∞");
    }
    if (value == 0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(decimalPlaces) << 0.0 << "E+00";
        return FormattedValue::success(ss.str());
    }

    // Calculate exponent
    int exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
    double mantissa = value / std::pow(10.0, exponent);

    // Format mantissa
    std::string mantissaStr = formatDecimal(mantissa, decimalPlaces, locale.decimalSeparator);

    // Format exponent
    std::ostringstream expSs;
    expSs << (exponent >= 0 ? "E+" : "E-") << std::setfill('0') << std::setw(2)
          << std::abs(exponent);

    return FormattedValue::success(mantissaStr + expSs.str());
}

// Helper: Month names
static const char* const MONTH_NAMES[] = {"",        "January", "February",  "March",   "April",
                                          "May",     "June",    "July",      "August",  "September",
                                          "October", "November", "December"};

// Format as date
FormattedValue formatDate(double serialDate, const ID& formatId, const FormatLocale& locale) {
    if (std::isnan(serialDate) || serialDate < 1) {
        return FormattedValue::error("Invalid date");
    }

    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(serialDate, year, month, day);

    std::ostringstream ss;

    if (formatId == BuiltInFormats::DATE_ISO) {
        // ISO format: YYYY-MM-DD
        ss << year << "-" << std::setfill('0') << std::setw(2) << month << "-" << std::setw(2)
           << day;
    } else if (formatId == BuiltInFormats::DATE_LONG) {
        // Long format: January 15, 2024
        if (month >= 1 && month <= 12) {
            ss << MONTH_NAMES[month] << " " << day << ", " << year;
        } else {
            return FormattedValue::error("Invalid month");
        }
    } else {
        // Short format (default): M/D/YYYY
        ss << month << "/" << day << "/" << year;
    }

    return FormattedValue::success(ss.str());
}

// Format as time
FormattedValue formatTime(double fractionalDay, const ID& formatId, const FormatLocale& locale) {
    // Handle values outside 0-1 range (wrap around)
    fractionalDay = std::fmod(fractionalDay, 1.0);
    if (fractionalDay < 0) {
        fractionalDay += 1.0;
    }

    int hours = 0, minutes = 0, seconds = 0;
    TimeUtils::fromFractionalDay(fractionalDay, hours, minutes, seconds);

    std::ostringstream ss;

    if (formatId == BuiltInFormats::TIME_12H) {
        // 12-hour format: 12:30 PM
        int displayHour = hours % 12;
        if (displayHour == 0) {
            displayHour = 12;
        }
        const char* ampm = hours < 12 ? "AM" : "PM";
        ss << displayHour << ":" << std::setfill('0') << std::setw(2) << minutes << " " << ampm;
    } else {
        // 24-hour format (default): HH:MM
        ss << std::setfill('0') << std::setw(2) << hours << ":" << std::setw(2) << minutes;
    }

    return FormattedValue::success(ss.str());
}

// Format as date and time
FormattedValue formatDateTime(double serialDateTime, const ID& formatId,
                              const FormatLocale& locale) {
    double intPart = 0.0;
    double fracPart = std::modf(serialDateTime, &intPart);

    auto dateResult = formatDate(intPart, BuiltInFormats::DATE_SHORT, locale);
    auto timeResult = formatTime(fracPart, BuiltInFormats::TIME_12H, locale);

    if (dateResult.isError) {
        return dateResult;
    }
    if (timeResult.isError) {
        return timeResult;
    }

    return FormattedValue::success(dateResult.text + " " + timeResult.text);
}

// Format using a specific NumberFormat
FormattedValue formatWithFormat(double value, const NumberFormat& format,
                                const FormatLocale& locale) {
    switch (format.category) {
        case NumberFormatCategory::GENERAL:
            return formatGeneral(value, locale);

        case NumberFormatCategory::NUMBER:
            return formatPlainNumber(value, format.decimalPlaces, format.useThousandsSeparator,
                                     locale);

        case NumberFormatCategory::CURRENCY:
        case NumberFormatCategory::ACCOUNTING:
            return formatCurrency(value, format.decimalPlaces,
                                  format.currencySymbol.empty() ? locale.currencySymbol
                                                                : format.currencySymbol,
                                  format.isAccounting, locale);

        case NumberFormatCategory::PERCENTAGE:
            return formatPercentage(value, format.decimalPlaces, locale);

        case NumberFormatCategory::SCIENTIFIC:
            return formatScientific(value, format.decimalPlaces, locale);

        case NumberFormatCategory::DATE:
            return formatDate(value, format.id, locale);

        case NumberFormatCategory::TIME:
            return formatTime(value, format.id, locale);

        case NumberFormatCategory::DATE_TIME:
            return formatDateTime(value, format.id, locale);

        case NumberFormatCategory::FRACTION:
            // TODO: Implement fraction formatting
            return formatGeneral(value, locale);

        case NumberFormatCategory::TEXT:
            // TEXT format: display number as-is
            return formatGeneral(value, locale);
    }

    return FormattedValue::error("Unknown format category");
}

// Main formatting function with registry lookup
FormattedValue formatNumber(const NumberFormatRegistry& registry, double value, const ID& formatId,
                            const FormatLocale& locale) {
    // Null ID means GENERAL format
    if (formatId.isNull()) {
        return formatGeneral(value, locale);
    }

    const NumberFormat* format = registry.getFormat(formatId);
    if (format == nullptr) {
        // Unknown format ID, fall back to GENERAL
        return formatGeneral(value, locale);
    }

    return formatWithFormat(value, *format, locale);
}

}  // namespace cells
