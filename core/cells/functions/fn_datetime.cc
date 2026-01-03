#include "core/cells/functions/fn_datetime.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

#include <chrono>
#include <string>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

// =============================================================================
// Helper Functions
// =============================================================================

// Excel serial date system:
// Day 1 = January 1, 1900
// Day 2 = January 2, 1900
// ... etc.
// Note: Excel incorrectly treats 1900 as a leap year (Feb 29, 1900 exists as day 60)
// We replicate this bug for compatibility.

namespace {

// Helper: Convert year/month/day to Excel serial date
double dateToSerial(int year, int month, int day) {
    // Handle month overflow/underflow
    while (month > 12) {
        year++;
        month -= 12;
    }
    while (month < 1) {
        year--;
        month += 12;
    }

    // Days in each month (non-leap year)
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Check if leap year (but remember Excel's 1900 bug)
    auto isLeapYear = [](int y) -> bool {
        if (y == 1900) {
            return true;  // Excel bug: 1900 is treated as leap year
        }
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    // Calculate days from 1900-01-01
    double serial = 0;

    // Add days for complete years
    for (int y = 1900; y < year; y++) {
        serial += isLeapYear(y) ? 366 : 365;
    }

    // Add days for complete months in current year
    for (int m = 1; m < month; m++) {
        serial += daysInMonth[m - 1];
        if (m == 2 && isLeapYear(year)) {
            serial += 1;  // February in leap year
        }
    }

    // Add days in current month
    serial += day;

    return serial;
}

// Helper: Convert Excel serial date to year/month/day
void serialToDate(double serial, int& year, int& month, int& day) {
    // Days in each month (non-leap year)
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    auto isLeapYear = [](int y) -> bool {
        if (y == 1900) {
            return true;  // Excel bug
        }
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    int remaining = static_cast<int>(serial);
    year = 1900;

    // Find year
    while (true) {
        const int daysInYear = isLeapYear(year) ? 366 : 365;
        if (remaining <= daysInYear) {
            break;
        }
        remaining -= daysInYear;
        year++;
    }

    // Find month
    month = 1;
    while (month <= 12) {
        int daysThisMonth = daysInMonth[month - 1];
        if (month == 2 && isLeapYear(year)) {
            daysThisMonth = 29;
        }
        if (remaining <= daysThisMonth) {
            break;
        }
        remaining -= daysThisMonth;
        month++;
    }

    day = remaining;
}

// Helper: Convert time fraction to hours/minutes/seconds
void serialToTime(double serial, int& hour, int& minute, int& second) {
    // Time is fractional part of serial date
    const double timePart = serial - std::floor(serial);

    // Convert to seconds
    double totalSeconds = timePart * 24 * 60 * 60;

    // Round to avoid floating point errors
    totalSeconds = std::round(totalSeconds);

    hour = static_cast<int>(totalSeconds / 3600) % 24;
    minute = static_cast<int>(totalSeconds / 60) % 60;
    second = static_cast<int>(totalSeconds) % 60;
}

// Helper: Convert hours/minutes/seconds to time fraction
double timeToSerial(int hour, int minute, int second) {
    return (hour * 3600.0 + minute * 60.0 + second) / (24.0 * 60.0 * 60.0);
}

}  // namespace

// =============================================================================
// Volatile Date/Time Functions
// =============================================================================

EvalResult fn_NOW(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get current time
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&nowTime);

    // Calculate date serial
    const double dateSerial =
        dateToSerial(localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday);

    // Calculate time fraction
    const double timeSerial =
        timeToSerial(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

    return EvalResult::Number(dateSerial + timeSerial);
}

EvalResult fn_TODAY(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get current time
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* localTime = std::localtime(&nowTime);

    return EvalResult::Number(
        dateToSerial(localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday));
}

// =============================================================================
// Date Construction Functions
// =============================================================================

EvalResult fn_DATE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult yearResult = evaluateAsNumber(args[0], ctx);
    if (yearResult.isError()) {
        return yearResult;
    }

    EvalResult monthResult = evaluateAsNumber(args[1], ctx);
    if (monthResult.isError()) {
        return monthResult;
    }

    EvalResult dayResult = evaluateAsNumber(args[2], ctx);
    if (dayResult.isError()) {
        return dayResult;
    }

    int year = static_cast<int>(yearResult.getNumber());
    const int month = static_cast<int>(monthResult.getNumber());
    const int day = static_cast<int>(dayResult.getNumber());

    // Excel interprets 0-99 as 1900-1999 or 2000-2029
    if (year >= 0 && year <= 99) {
        if (year <= 29) {
            year += 2000;
        } else {
            year += 1900;
        }
    }

    if (year < 1900 || year > 9999) {
        return EvalResult::Error(CellError::NUM);
    }

    const double serial = dateToSerial(year, month, day);
    if (serial < 1 || serial > 2958465) {  // Excel date range
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(serial);
}

EvalResult fn_TIME(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult hourResult = evaluateAsNumber(args[0], ctx);
    if (hourResult.isError()) {
        return hourResult;
    }

    EvalResult minuteResult = evaluateAsNumber(args[1], ctx);
    if (minuteResult.isError()) {
        return minuteResult;
    }

    EvalResult secondResult = evaluateAsNumber(args[2], ctx);
    if (secondResult.isError()) {
        return secondResult;
    }

    const int hour = static_cast<int>(hourResult.getNumber());
    const int minute = static_cast<int>(minuteResult.getNumber());
    const int second = static_cast<int>(secondResult.getNumber());

    // Calculate total seconds and normalize
    int totalSeconds = hour * 3600 + minute * 60 + second;

    // Time wraps around at 24 hours (result is always 0-1)
    // Handle negative times too
    while (totalSeconds < 0) {
        totalSeconds += 24 * 3600;
    }
    totalSeconds = totalSeconds % (24 * 3600);

    return EvalResult::Number(static_cast<double>(totalSeconds) / (24.0 * 3600.0));
}

EvalResult fn_DATEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    const std::string& text = textResult.getString();

    // Try to parse common date formats
    // Format: YYYY-MM-DD
    int year = 0;
    int month = 0;
    int day = 0;

    // Try to parse common date formats
    // Each format parses into different variable positions
    const bool isIsoFormat = sscanf(text.c_str(), "%d-%d-%d", &year, &month, &day) == 3;
    if (!isIsoFormat) {
        // Try US format MM/DD/YYYY
        const bool isUsFormat = sscanf(text.c_str(), "%d/%d/%d", &month, &day, &year) == 3;
        if (!isUsFormat) {
            // Try European format DD.MM.YYYY
            const bool isEuFormat = sscanf(text.c_str(), "%d.%d.%d", &day, &month, &year) == 3;
            if (!isEuFormat) {
                return EvalResult::Error(CellError::VALUE);
            }
        }
    }

    // Handle 2-digit years
    if (year < 100) {
        if (year <= 29) {
            year += 2000;
        } else {
            year += 1900;
        }
    }

    if (year < 1900 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(dateToSerial(year, month, day));
}

EvalResult fn_TIMEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult textResult = evaluateAsString(args[0], ctx);
    if (textResult.isError()) {
        return textResult;
    }

    const std::string& text = textResult.getString();

    int hour = 0;
    int minute = 0;
    int second = 0;

    // Try to parse time formats
    if (sscanf(text.c_str(), "%d:%d:%d", &hour, &minute, &second) == 3) {
        // HH:MM:SS
    } else if (sscanf(text.c_str(), "%d:%d", &hour, &minute) == 2) {
        // HH:MM
        second = 0;
    } else {
        return EvalResult::Error(CellError::VALUE);
    }

    // Check for AM/PM suffix
    bool isPM = false;
    bool isAM = false;
    std::string upperText = text;
    for (char& c : upperText) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upperText.find("PM") != std::string::npos) {
        isPM = true;
    } else if (upperText.find("AM") != std::string::npos) {
        isAM = true;
    }

    // Convert 12-hour to 24-hour format
    if (isPM && hour < 12) {
        hour += 12;
    } else if (isAM && hour == 12) {
        hour = 0;
    }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Number(timeToSerial(hour, minute, second));
}

// =============================================================================
// Date Extraction Functions
// =============================================================================

EvalResult fn_YEAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(year);
}

EvalResult fn_MONTH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(month);
}

EvalResult fn_DAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);

    return EvalResult::Number(day);
}

EvalResult fn_HOUR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(hour);
}

EvalResult fn_MINUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(minute);
}

EvalResult fn_SECOND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    const double serial = serialResult.getNumber();
    if (serial < 0) {
        return EvalResult::Error(CellError::NUM);
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    serialToTime(serial, hour, minute, second);

    return EvalResult::Number(second);
}

EvalResult fn_WEEKDAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult serialResult = evaluateAsNumber(args[0], ctx);
    if (serialResult.isError()) {
        return serialResult;
    }

    int returnType = 1;  // Default: Sunday = 1
    if (args.size() == 2) {
        EvalResult typeResult = evaluateAsNumber(args[1], ctx);
        if (typeResult.isError()) {
            return typeResult;
        }
        returnType = static_cast<int>(typeResult.getNumber());
        if (returnType < 1 || returnType > 3) {
            return EvalResult::Error(CellError::NUM);
        }
    }

    const double serial = serialResult.getNumber();
    if (serial < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    // Excel day 1 (Jan 1, 1900) is treated as Sunday
    // Day 7 (Jan 7, 1900) is Saturday
    // Day 8 (Jan 8, 1900) is Sunday again
    //
    // Pattern for type 1 (Sunday=1, Saturday=7):
    // Day 1 -> Sunday (1)
    // Day 2 -> Monday (2)
    // ...
    // Day 7 -> Saturday (7)
    // Day 8 -> Sunday (1)
    //
    // Formula: ((serial - 1) % 7) + 1
    // Day 1: ((1-1) % 7) + 1 = 0 + 1 = 1 (Sunday) ✓
    // Day 7: ((7-1) % 7) + 1 = 6 + 1 = 7 (Saturday) ✓
    // Day 8: ((8-1) % 7) + 1 = 0 + 1 = 1 (Sunday) ✓

    const int daysSince = static_cast<int>(serial);
    int weekday = ((daysSince - 1) % 7) + 1;  // Type 1: Sunday = 1, Saturday = 7

    switch (returnType) {
        case 1:
            // Sunday = 1, Saturday = 7 (default)
            break;
        case 2:
            // Monday = 1, Sunday = 7
            // Sunday (1) -> 7, Mon (2) -> 1, Tue (3) -> 2, ..., Sat (7) -> 6
            weekday = (weekday == 1) ? 7 : weekday - 1;
            break;
        case 3:
            // Monday = 0, Sunday = 6
            // Sunday (1) -> 6, Mon (2) -> 0, Tue (3) -> 1, ..., Sat (7) -> 5
            weekday = (weekday + 5) % 7;
            break;
        default:
            // Already validated returnType is 1-3 above
            break;
    }

    return EvalResult::Number(weekday);
}

// =============================================================================
// Registration
// =============================================================================

void registerDateTimeFunctions(FunctionRegistry& registry) {
    // Volatile functions
    registry.registerFunction("NOW", fn_NOW, "()", "Returns the current date and time", "Date",
                              true);
    registry.registerFunction("TODAY", fn_TODAY, "()", "Returns the current date", "Date", true);

    // Date construction
    registry.registerFunction("DATE", fn_DATE, "(year, month, day)", "Creates a date value",
                              "Date");
    registry.registerFunction("TIME", fn_TIME, "(hour, minute, second)", "Creates a time value",
                              "Date");
    registry.registerFunction("DATEVALUE", fn_DATEVALUE, "(date_text)",
                              "Converts text to a date value", "Date");
    registry.registerFunction("TIMEVALUE", fn_TIMEVALUE, "(time_text)",
                              "Converts text to a time value", "Date");

    // Date extraction
    registry.registerFunction("YEAR", fn_YEAR, "(date)", "Extracts the year from a date", "Date");
    registry.registerFunction("MONTH", fn_MONTH, "(date)", "Extracts the month (1-12)", "Date");
    registry.registerFunction("DAY", fn_DAY, "(date)", "Extracts the day of month", "Date");
    registry.registerFunction("HOUR", fn_HOUR, "(time)", "Extracts the hour (0-23)", "Date");
    registry.registerFunction("MINUTE", fn_MINUTE, "(time)", "Extracts the minute (0-59)", "Date");
    registry.registerFunction("SECOND", fn_SECOND, "(time)", "Extracts the second (0-59)", "Date");
    registry.registerFunction("WEEKDAY", fn_WEEKDAY, "(date, [return_type])",
                              "Returns the day of the week", "Date");
}

}  // namespace cells
