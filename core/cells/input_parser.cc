#include "core/cells/input_parser.h"

#include <cctype>
#include <cmath>
#include <ctime>

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

// Helper: count decimal places in a number string
// "15" -> 0, "15.5" -> 1, "15.50" -> 2, "15.500" -> 3
static uint8_t countDecimalPlaces(const std::string& str) {
    const auto dotPos = str.find('.');
    if (dotPos == std::string::npos) {
        return 0;
    }
    // Count digits after decimal point
    const size_t decimalDigits = str.size() - dotPos - 1;
    // Cap at 4 decimal places
    return static_cast<uint8_t>(std::min(decimalDigits, static_cast<size_t>(4)));
}

// Helper: get percentage format ID for given decimal places (0-4)
static ID getPercentageFormatId(uint8_t decimals) {
    switch (decimals) {
        case 0:
            return BuiltInFormats::PERCENTAGE_0;
        case 1:
            return BuiltInFormats::PERCENTAGE_1;
        case 2:
            return BuiltInFormats::PERCENTAGE_2;
        case 3:
            return BuiltInFormats::PERCENTAGE_3;
        case 4:
        default:
            return BuiltInFormats::PERCENTAGE_4;
    }
}

// Helper: get currency format ID for given decimal places (0-4)
static ID getCurrencyFormatId(uint8_t decimals) {
    switch (decimals) {
        case 0:
            return BuiltInFormats::CURRENCY_0;
        case 1:
            return BuiltInFormats::CURRENCY_1;
        case 2:
            return BuiltInFormats::CURRENCY_2;
        case 3:
            return BuiltInFormats::CURRENCY_3;
        case 4:
        default:
            return BuiltInFormats::CURRENCY_4;
    }
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

    // Count exact decimal places and get matching format ID
    // "15%" -> 0 decimals, "15.5%" -> 1 decimal, "15.50%" -> 2 decimals
    const uint8_t decimals = countDecimalPlaces(numPart);
    const ID formatId = getPercentageFormatId(decimals);

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

    // Count exact decimal places and get matching format ID
    // "$100" -> 0 decimals, "$99.9" -> 1 decimal, "$99.99" -> 2 decimals
    const uint8_t decimals = countDecimalPlaces(numPart);
    const ID formatId = getCurrencyFormatId(decimals);

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

// Helper: convert 2-digit year to 4-digit year
// Excel convention: 00-29 -> 2000-2029, 30-99 -> 1930-1999
static int expandTwoDigitYear(int twoDigitYear) {
    if (twoDigitYear >= 0 && twoDigitYear <= 29) {
        return 2000 + twoDigitYear;
    }
    return 1900 + twoDigitYear;
}

// Helper: parse month name to month number (1-12), returns 0 if not found
static int parseMonthName(const std::string& name) {
    // Convert to lowercase for comparison
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Full month names
    static const std::string fullNames[] = {"january",   "february", "march",    "april",
                                            "may",       "june",     "july",     "august",
                                            "september", "october",  "november", "december"};
    // Abbreviated month names
    static const std::string shortNames[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                             "jul", "aug", "sep", "oct", "nov", "dec"};

    for (int i = 0; i < 12; i++) {
        if (lower == fullNames[i] || lower == shortNames[i]) {
            return i + 1;
        }
    }
    return 0;  // Not found
}

// Helper: get current year (for short date formats)
static int getCurrentYear() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    return 1900 + local->tm_year;
}

// Parse date input
ParsedInput parseDate(const std::string& input) {
    const std::string trimmed = trim(input);
    if (trimmed.empty()) {
        return ParsedInput::error("Empty input");
    }

    int year = 0, month = 0, day = 0;
    bool parsed = false;

    std::smatch match;

    // Try MM/DD/YYYY or M/D/YYYY (4-digit year)
    const std::regex slashDate4Regex(R"((\d{1,2})/(\d{1,2})/(\d{4}))");
    if (std::regex_match(trimmed, match, slashDate4Regex)) {
        month = std::stoi(match[1].str());
        day = std::stoi(match[2].str());
        year = std::stoi(match[3].str());
        parsed = true;
    }

    // Try MM/DD/YY or M/D/YY (2-digit year)
    if (!parsed) {
        const std::regex slashDate2Regex(R"((\d{1,2})/(\d{1,2})/(\d{2}))");
        if (std::regex_match(trimmed, match, slashDate2Regex)) {
            month = std::stoi(match[1].str());
            day = std::stoi(match[2].str());
            year = expandTwoDigitYear(std::stoi(match[3].str()));
            parsed = true;
        }
    }

    // Try YYYY-MM-DD (ISO format)
    if (!parsed) {
        const std::regex isoDateRegex(R"((\d{4})-(\d{1,2})-(\d{1,2}))");
        if (std::regex_match(trimmed, match, isoDateRegex)) {
            year = std::stoi(match[1].str());
            month = std::stoi(match[2].str());
            day = std::stoi(match[3].str());
            parsed = true;
        }
    }

    // Try "Jan 15, 2025" or "January 15, 2025"
    if (!parsed) {
        const std::regex monthNameDayYearRegex(R"(([A-Za-z]+)\s+(\d{1,2}),?\s+(\d{4}))");
        if (std::regex_match(trimmed, match, monthNameDayYearRegex)) {
            month = parseMonthName(match[1].str());
            day = std::stoi(match[2].str());
            year = std::stoi(match[3].str());
            if (month > 0) {
                parsed = true;
            }
        }
    }

    // Try "15 Jan 2025" or "15-Jan-2025"
    if (!parsed) {
        const std::regex dayMonthNameYearRegex(R"((\d{1,2})[\s-]+([A-Za-z]+)[\s-]+(\d{4}))");
        if (std::regex_match(trimmed, match, dayMonthNameYearRegex)) {
            day = std::stoi(match[1].str());
            month = parseMonthName(match[2].str());
            year = std::stoi(match[3].str());
            if (month > 0) {
                parsed = true;
            }
        }
    }

    // Try short formats without year: "1/15" -> January 15 of current year
    if (!parsed) {
        const std::regex shortSlashRegex(R"((\d{1,2})/(\d{1,2}))");
        if (std::regex_match(trimmed, match, shortSlashRegex)) {
            month = std::stoi(match[1].str());
            day = std::stoi(match[2].str());
            year = getCurrentYear();
            parsed = true;
        }
    }

    // Try "Jan 15" or "January 15" -> January 15 of current year
    if (!parsed) {
        const std::regex monthNameDayRegex(R"(([A-Za-z]+)\s+(\d{1,2}))");
        if (std::regex_match(trimmed, match, monthNameDayRegex)) {
            month = parseMonthName(match[1].str());
            day = std::stoi(match[2].str());
            year = getCurrentYear();
            if (month > 0) {
                parsed = true;
            }
        }
    }

    if (!parsed) {
        return ParsedInput::error("Unrecognized date format");
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

    // Normalize input: remove periods from a.m./p.m. notation
    std::string normalizedInput = trimmed;
    // Replace "a.m." with "am" and "p.m." with "pm" (case insensitive)
    std::string lowerInput = trimmed;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
    auto amDotPos = lowerInput.find("a.m.");
    if (amDotPos != std::string::npos) {
        normalizedInput = trimmed.substr(0, amDotPos) + "AM" + trimmed.substr(amDotPos + 4);
    }
    auto pmDotPos = lowerInput.find("p.m.");
    if (pmDotPos != std::string::npos) {
        normalizedInput = trimmed.substr(0, pmDotPos) + "PM" + trimmed.substr(pmDotPos + 4);
    }

    // Check for AM/PM
    std::string upperInput = normalizedInput;
    std::transform(upperInput.begin(), upperInput.end(), upperInput.begin(), ::toupper);

    if (upperInput.find("PM") != std::string::npos) {
        isPM = true;
        is12Hour = true;
    } else if (upperInput.find("AM") != std::string::npos) {
        is12Hour = true;
    }

    // Remove AM/PM for parsing
    std::string timePart = normalizedInput;
    const auto amPos = upperInput.find("AM");
    const auto pmPos = upperInput.find("PM");
    if (amPos != std::string::npos) {
        timePart = trim(normalizedInput.substr(0, amPos));
    } else if (pmPos != std::string::npos) {
        timePart = trim(normalizedInput.substr(0, pmPos));
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

    // 5. Date (contains / or - in date patterns, or month names)
    // Check for numeric date patterns (MM/DD/YYYY, YYYY-MM-DD, M/D, etc.)
    const std::regex numericDatePattern(R"(\d{1,4}[/-]\d{1,2}(?:[/-]\d{1,4})?)");
    // Check for text month patterns (Jan 15, 15 Jan, January 15, 2025, etc.)
    const std::regex textMonthPattern(
        R"([A-Za-z]{3,9}\s+\d{1,2}|"
        R"(\d{1,2}[\s-]+[A-Za-z]{3,9}))");
    if (std::regex_search(trimmed, numericDatePattern) ||
        std::regex_search(trimmed, textMonthPattern)) {
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
