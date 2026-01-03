#include "core/cells/input_parser.h"

#include <cctype>
#include <cmath>

#include <algorithm>
#include <charconv>
#include <regex>

namespace cells {

// ParsedInput factory methods
ParsedInput ParsedInput::number(double value, const ID& formatId, NumberFormatCategory category) {
    ParsedInput result;
    result.numericValue = value;
    result.valueType = CellValueType::NUMBER;
    result.formatId = formatId;
    result.formatCategory = category;
    result.success = true;
    return result;
}

ParsedInput ParsedInput::text(const std::string& value) {
    ParsedInput result;
    result.stringValue = value;
    result.valueType = CellValueType::STRING;
    result.formatCategory = NumberFormatCategory::TEXT;
    result.success = true;
    return result;
}

ParsedInput ParsedInput::error(const std::string& message) {
    ParsedInput result;
    result.success = false;
    result.errorMessage = message;
    return result;
}

// Helper: trim whitespace
static std::string trim(const std::string& str) {
    const size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

// Helper: remove all occurrences of a character
static std::string removeChar(const std::string& str, char c) {
    std::string result;
    result.reserve(str.size());
    for (const char ch : str) {
        if (ch != c) {
            result += ch;
        }
    }
    return result;
}

// Helper: parse a double from string (handles commas)
static bool parseDouble(const std::string& str, double& value) {
    const std::string clean = removeChar(str, ',');
    if (clean.empty()) {
        return false;
    }

    // Use std::from_chars for fast, locale-independent parsing
    const auto result = std::from_chars(clean.data(), clean.data() + clean.size(), value);
    return result.ec == std::errc{} && result.ptr == clean.data() + clean.size();
}

// Parse percentage input (e.g., "15%" -> 0.15)
ParsedInput parsePercentage(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    // Check for % sign at end
    if (trimmed.back() != '%') {
        return ParsedInput::error("Not a percentage");
    }

    // Remove % and any trailing whitespace before it
    const std::string numPart = trim(trimmed.substr(0, trimmed.size() - 1));
    if (numPart.empty()) {
        return ParsedInput::error("No number before %");
    }

    double value = 0.0;
    if (!parseDouble(numPart, value)) {
        return ParsedInput::error("Invalid number in percentage");
    }

    // Convert percentage to decimal (15% -> 0.15)
    value /= 100.0;

    // Determine decimal places: "15%" -> PERCENTAGE_0, "15.5%" -> PERCENTAGE_2
    const bool hasDecimals = numPart.find('.') != std::string::npos;
    const ID formatId = hasDecimals ? BuiltInFormats::PERCENTAGE_2 : BuiltInFormats::PERCENTAGE_0;

    return ParsedInput::number(value, formatId, NumberFormatCategory::PERCENTAGE);
}

// Parse currency input (e.g., "$1,234.56" -> 1234.56)
ParsedInput parseCurrency(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    // Check for currency symbol at start
    // Support: $, €, £, ¥
    bool isNegative = false;
    std::string numPart;

    size_t startIdx = 0;

    // Handle leading minus
    if (!trimmed.empty() && trimmed[0] == '-') {
        isNegative = true;
        startIdx = 1;
    }

    // Check for currency symbol
    char currencySymbol = '\0';
    if (startIdx < trimmed.size()) {
        const char c = trimmed[startIdx];
        if (c == '$') {
            currencySymbol = '$';
            startIdx++;
        }
        // Note: Multi-byte currency symbols (€, £, ¥) would need UTF-8 handling
    }

    // Handle negative after currency symbol: $-1234
    if (!isNegative && startIdx < trimmed.size() && trimmed[startIdx] == '-') {
        isNegative = true;
        startIdx++;
    }

    // Handle parentheses for negative: ($1,234)
    if (trimmed.front() == '(' && trimmed.back() == ')') {
        // Re-parse without parens
        const std::string inner = trimmed.substr(1, trimmed.size() - 2);
        return parseCurrency("-" + inner);
    }

    if (currencySymbol == '\0') {
        return ParsedInput::error("No currency symbol");
    }

    numPart = trimmed.substr(startIdx);
    if (numPart.empty()) {
        return ParsedInput::error("No number after currency symbol");
    }

    double value = 0.0;
    if (!parseDouble(numPart, value)) {
        return ParsedInput::error("Invalid number in currency");
    }

    if (isNegative) {
        value = -value;
    }

    // Determine decimal places
    const bool hasDecimals = numPart.find('.') != std::string::npos;
    const ID formatId = hasDecimals ? BuiltInFormats::CURRENCY_2 : BuiltInFormats::CURRENCY_0;

    return ParsedInput::number(value, formatId, NumberFormatCategory::CURRENCY);
}

// Date utilities
namespace DateUtils {

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return days[month];
}

double toSerialDate(int year, int month, int day) {
    // Excel serial date: Days since December 30, 1899
    // Note: Excel incorrectly treats 1900 as a leap year for compatibility
    // We follow this convention for maximum compatibility

    // Calculate days from 1900-01-01
    int totalDays = 0;

    // Add days for complete years
    for (int y = 1900; y < year; y++) {
        totalDays += isLeapYear(y) ? 366 : 365;
    }

    // Add days for complete months in current year
    for (int m = 1; m < month; m++) {
        totalDays += daysInMonth(year, m);
    }

    // Add days in current month
    totalDays += day;

    // Excel serial date 1 = January 1, 1900
    // But Excel has a bug: it thinks 1900 was a leap year
    // So we need to add 1 for dates after Feb 28, 1900
    if (year > 1900 || (year == 1900 && (month > 2 || (month == 2 && day > 28)))) {
        totalDays++;
    }

    return static_cast<double>(totalDays);
}

void fromSerialDate(double serial, int& year, int& month, int& day) {
    // Convert serial date back to year/month/day
    int days = static_cast<int>(serial);

    // Handle Excel's 1900 leap year bug
    if (days > 60) {
        days--;  // Account for the phantom Feb 29, 1900
    }

    year = 1900;
    while (days > (isLeapYear(year) ? 366 : 365)) {
        days -= isLeapYear(year) ? 366 : 365;
        year++;
    }

    month = 1;
    while (days > daysInMonth(year, month)) {
        days -= daysInMonth(year, month);
        month++;
    }

    day = days;
}

}  // namespace DateUtils

// Time utilities
namespace TimeUtils {

double toFractionalDay(int hours, int minutes, int seconds) {
    return (hours * 3600.0 + minutes * 60.0 + seconds) / 86400.0;
}

void fromFractionalDay(double fraction, int& hours, int& minutes, int& seconds) {
    const int totalSeconds = static_cast<int>(std::round(fraction * 86400.0));
    hours = totalSeconds / 3600;
    minutes = (totalSeconds % 3600) / 60;
    seconds = totalSeconds % 60;
}

}  // namespace TimeUtils

// Parse date input
ParsedInput parseDate(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    int year = 0, month = 0, day = 0;

    // Try MM/DD/YYYY or M/D/YYYY
    const std::regex slashDateRegex(R"((\d{1,2})/(\d{1,2})/(\d{4}))");
    std::smatch match;
    if (std::regex_match(trimmed, match, slashDateRegex)) {
        month = std::stoi(match[1].str());
        day = std::stoi(match[2].str());
        year = std::stoi(match[3].str());
    }
    // Try YYYY-MM-DD (ISO format)
    else {
        const std::regex isoDateRegex(R"((\d{4})-(\d{1,2})-(\d{1,2}))");
        if (std::regex_match(trimmed, match, isoDateRegex)) {
            year = std::stoi(match[1].str());
            month = std::stoi(match[2].str());
            day = std::stoi(match[3].str());
        } else {
            return ParsedInput::error("Unrecognized date format");
        }
    }

    // Validate date
    if (month < 1 || month > 12) {
        return ParsedInput::error("Invalid month");
    }
    if (day < 1 || day > DateUtils::daysInMonth(year, month)) {
        return ParsedInput::error("Invalid day");
    }
    if (year < 1900 || year > 9999) {
        return ParsedInput::error("Year out of range");
    }

    const double serial = DateUtils::toSerialDate(year, month, day);

    // Choose format based on input style
    const ID formatId = trimmed.find('-') != std::string::npos ? BuiltInFormats::DATE_ISO
                                                               : BuiltInFormats::DATE_SHORT;

    return ParsedInput::number(serial, formatId, NumberFormatCategory::DATE);
}

// Parse time input
ParsedInput parseTime(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    int hours = 0, minutes = 0, seconds = 0;
    bool isPM = false;
    bool is12Hour = false;

    // Check for AM/PM
    std::string upperInput = trimmed;
    std::transform(upperInput.begin(), upperInput.end(), upperInput.begin(), ::toupper);

    if (upperInput.find("PM") != std::string::npos) {
        isPM = true;
        is12Hour = true;
    } else if (upperInput.find("AM") != std::string::npos) {
        is12Hour = true;
    }

    // Remove AM/PM for parsing
    std::string timePart = trimmed;
    const auto amPos = upperInput.find("AM");
    const auto pmPos = upperInput.find("PM");
    if (amPos != std::string::npos) {
        timePart = trim(trimmed.substr(0, amPos));
    } else if (pmPos != std::string::npos) {
        timePart = trim(trimmed.substr(0, pmPos));
    }

    // Try HH:MM:SS or HH:MM
    const std::regex timeRegex(R"((\d{1,2}):(\d{2})(?::(\d{2}))?)");
    std::smatch match;
    if (!std::regex_match(timePart, match, timeRegex)) {
        return ParsedInput::error("Unrecognized time format");
    }

    hours = std::stoi(match[1].str());
    minutes = std::stoi(match[2].str());
    if (match[3].matched) {
        seconds = std::stoi(match[3].str());
    }

    // Convert 12-hour to 24-hour
    if (is12Hour) {
        if (hours == 12) {
            hours = isPM ? 12 : 0;
        } else if (isPM) {
            hours += 12;
        }
    }

    // Validate
    if (hours < 0 || hours > 23) {
        return ParsedInput::error("Invalid hours");
    }
    if (minutes < 0 || minutes > 59) {
        return ParsedInput::error("Invalid minutes");
    }
    if (seconds < 0 || seconds > 59) {
        return ParsedInput::error("Invalid seconds");
    }

    const double fraction = TimeUtils::toFractionalDay(hours, minutes, seconds);

    // Choose format based on input style
    const ID formatId = is12Hour ? BuiltInFormats::TIME_12H : BuiltInFormats::TIME_24H;

    return ParsedInput::number(fraction, formatId, NumberFormatCategory::TIME);
}

// Parse scientific notation
ParsedInput parseScientific(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    // Look for E or e with optional + or -
    std::string upperInput = trimmed;
    std::transform(upperInput.begin(), upperInput.end(), upperInput.begin(), ::toupper);

    const auto ePos = upperInput.find('E');
    if (ePos == std::string::npos) {
        return ParsedInput::error("No exponent marker");
    }

    // Must have digits before and after E
    if (ePos == 0 || ePos == trimmed.size() - 1) {
        return ParsedInput::error("Invalid scientific notation");
    }

    double value = 0.0;
    if (!parseDouble(trimmed, value)) {
        return ParsedInput::error("Invalid number in scientific notation");
    }

    return ParsedInput::number(value, BuiltInFormats::SCIENTIFIC_2,
                               NumberFormatCategory::SCIENTIFIC);
}

// Parse plain number
ParsedInput parseNumber(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    double value = 0.0;
    if (!parseDouble(trimmed, value)) {
        return ParsedInput::error("Invalid number");
    }

    // Determine format based on input characteristics
    const bool hasThousandsSeparator = trimmed.find(',') != std::string::npos;
    const bool hasDecimals = trimmed.find('.') != std::string::npos;

    ID formatId;
    if (hasThousandsSeparator && hasDecimals) {
        formatId = BuiltInFormats::NUMBER_SEP2;
    } else if (hasThousandsSeparator) {
        formatId = BuiltInFormats::NUMBER_SEP;
    } else if (hasDecimals) {
        formatId = BuiltInFormats::NUMBER_2;
    } else {
        formatId = BuiltInFormats::NUMBER_0;
    }

    return ParsedInput::number(value, formatId, NumberFormatCategory::NUMBER);
}

// Main entry point: parse user input and auto-detect format
ParsedInput parseUserInput(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::text("");
    }

    // Try each format in order of specificity

    // 1. Percentage (must check before number due to trailing %)
    if (trimmed.back() == '%') {
        ParsedInput result = parsePercentage(trimmed);
        if (result.success) {
            return result;
        }
    }

    // 2. Currency (check for $ at start)
    if (!trimmed.empty() &&
        (trimmed[0] == '$' || (trimmed[0] == '-' && trimmed.size() > 1 && trimmed[1] == '$') ||
         (trimmed[0] == '(' && trimmed.find('$') != std::string::npos))) {
        ParsedInput result = parseCurrency(trimmed);
        if (result.success) {
            return result;
        }
    }

    // 3. Scientific notation (contains E/e)
    {
        std::string upperInput = trimmed;
        std::transform(upperInput.begin(), upperInput.end(), upperInput.begin(), ::toupper);
        if (upperInput.find('E') != std::string::npos) {
            ParsedInput result = parseScientific(trimmed);
            if (result.success) {
                return result;
            }
        }
    }

    // 4. Time (contains : and possibly AM/PM)
    if (trimmed.find(':') != std::string::npos) {
        ParsedInput result = parseTime(trimmed);
        if (result.success) {
            return result;
        }
    }

    // 5. Date (contains / or - in date patterns)
    // Check for date-like patterns
    const std::regex datePattern(R"(\d{1,4}[/-]\d{1,2}[/-]\d{1,4})");
    if (std::regex_search(trimmed, datePattern)) {
        ParsedInput result = parseDate(trimmed);
        if (result.success) {
            return result;
        }
    }

    // 6. Plain number (try last)
    {
        ParsedInput result = parseNumber(trimmed);
        if (result.success) {
            // Use GENERAL format for plain numbers (let user choose format)
            result.formatId = ID{};  // null = GENERAL
            result.formatCategory = NumberFormatCategory::GENERAL;
            return result;
        }
    }

    // 7. Fall back to text
    return ParsedInput::text(trimmed);
}

}  // namespace cells
