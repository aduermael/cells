#include "core/cells/functions/fn_datetime.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

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

int daysInMonthOf(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = kDays[month - 1];
    if (month == 2) {
        const bool leap =
            (year == 1900) || ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        if (leap) {
            d = 29;
        }
    }
    return d;
}

double addMonthsToSerial(double serial, int months) {
    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serial, year, month, day);
    month += months;
    while (month > 12) {
        year++;
        month -= 12;
    }
    while (month < 1) {
        year--;
        month += 12;
    }
    const int last = daysInMonthOf(year, month);
    if (day > last) {
        day = last;
    }
    return dateToSerial(year, month, day);
}

int weekdaySun1(int serialInt) {
    return ((serialInt - 1) % 7) + 1;
}

int daysInYearOf(int year) {
    if (year == 1900) {
        return 366;
    }
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        return 366;
    }
    return 365;
}

int isoWeekFromSerial(int serialInt) {
    const int wd = weekdaySun1(serialInt);   // 1=Sun ... 7=Sat
    const int isoWd = wd == 1 ? 7 : wd - 1;  // 1=Mon ... 7=Sun
    const int thursday = serialInt + (4 - isoWd);
    int ty = 0;
    int tm = 0;
    int td = 0;
    serialToDate(static_cast<double>(thursday), ty, tm, td);
    const int jan4 = static_cast<int>(dateToSerial(ty, 1, 4));
    const int jan4Wd = weekdaySun1(jan4);
    const int jan4Iso = jan4Wd == 1 ? 7 : jan4Wd - 1;
    const int week1Mon = jan4 - (jan4Iso - 1);
    return (thursday - week1Mon) / 7 + 1;
}

int days360Count(int y1, int m1, int d1, int y2, int m2, int d2, bool european) {
    if (european) {
        if (d1 == 31) {
            d1 = 30;
        }
        if (d2 == 31) {
            d2 = 30;
        }
    } else {
        const int last1 = daysInMonthOf(y1, m1);
        if (d1 == last1) {
            d1 = 30;
        }
        if (d2 == 31 && d1 >= 30) {
            d2 = 30;
        }
    }
    return (y2 - y1) * 360 + (m2 - m1) * 30 + (d2 - d1);
}

// Bit 0 = Sunday ... bit 6 = Saturday.
int weekendMaskFromCode(int code) {
    switch (code) {
        case 1:
            return (1 << 0) | (1 << 6);  // Sun Sat
        case 2:
            return (1 << 0) | (1 << 1);  // Sun Mon
        case 3:
            return (1 << 1) | (1 << 2);
        case 4:
            return (1 << 2) | (1 << 3);
        case 5:
            return (1 << 3) | (1 << 4);
        case 6:
            return (1 << 4) | (1 << 5);
        case 7:
            return (1 << 5) | (1 << 6);
        case 11:
            return 1 << 0;
        case 12:
            return 1 << 1;
        case 13:
            return 1 << 2;
        case 14:
            return 1 << 3;
        case 15:
            return 1 << 4;
        case 16:
            return 1 << 5;
        case 17:
            return 1 << 6;
        default:
            return -1;
    }
}

int weekendMaskFromString(const std::string& s) {
    if (s.size() != 7) {
        return -1;
    }
    int mask = 0;
    for (size_t i = 0; i < 7; ++i) {
        const char c = s[i];
        if (c != '0' && c != '1') {
            return -1;
        }
        if (c == '1') {
            // String starts Monday; bit 0 is Sunday.
            const int sunBased = static_cast<int>((i + 1) % 7);
            mask |= 1 << sunBased;
        }
    }
    return mask;
}

bool isWeekendMask(int serialInt, int mask) {
    const int wd = weekdaySun1(serialInt);  // 1=Sun
    return (mask & (1 << (wd - 1))) != 0;
}

bool isHoliday(int serialInt, const std::vector<int>& holidays);

EvalResult weekendMaskFromArg(const ASTNode* arg, EvalContext& ctx, int* mask) {
    const EvalResult w = evaluate(arg, ctx);
    if (w.isError()) {
        return w;
    }
    if (w.isNumber() || w.isBoolean() || w.isEmpty()) {
        const EvalResult n = w.toNumber();
        if (n.isError()) {
            return n;
        }
        *mask = weekendMaskFromCode(static_cast<int>(n.getNumber()));
        if (*mask < 0) {
            return EvalResult::Error(CellError::NUM);
        }
        return EvalResult::Empty();
    }
    const EvalResult s = w.toString();
    if (s.isError()) {
        return EvalResult::Error(CellError::NUM);
    }
    *mask = weekendMaskFromString(s.getString());
    if (*mask < 0) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Empty();
}

int countNetworkDays(int start, int end, int mask, const std::vector<int>& holidays) {
    int sign = 1;
    if (end < start) {
        std::swap(start, end);
        sign = -1;
    }
    int count = 0;
    for (int d = start; d <= end; ++d) {
        if (!isWeekendMask(d, mask) && !isHoliday(d, holidays)) {
            ++count;
        }
    }
    return sign * count;
}

int shiftWorkday(int start, int days, int mask, const std::vector<int>& holidays) {
    if (days == 0) {
        return start;
    }
    const int step = days > 0 ? 1 : -1;
    int remaining = days > 0 ? days : -days;
    int d = start;
    while (remaining > 0) {
        d += step;
        if (!isWeekendMask(d, mask) && !isHoliday(d, holidays)) {
            --remaining;
        }
    }
    return d;
}

std::vector<int> holidaySerials(const ASTNode* arg, EvalContext& ctx, EvalResult* error) {
    std::vector<int> out;
    const EvalResult r = evaluate(arg, ctx);
    if (r.isError()) {
        *error = r;
        return out;
    }
    std::vector<EvalResult> vals;
    if (r.isRange()) {
        vals = collectRangeValues(r, ctx);
    } else {
        vals.push_back(r);
    }
    for (const EvalResult& v : vals) {
        if (v.isError()) {
            *error = v;
            return {};
        }
        if (v.isEmpty()) {
            continue;
        }
        const EvalResult n = v.toNumber();
        if (n.isError()) {
            continue;
        }
        out.push_back(static_cast<int>(n.getNumber()));
    }
    return out;
}

bool isHoliday(int serialInt, const std::vector<int>& holidays) {
    for (int h : holidays) {
        if (h == serialInt) {
            return true;
        }
    }
    return false;
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

EvalResult fn_EOMONTH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult startDateResult = evaluateAsNumber(args[0], ctx);
    if (startDateResult.isError()) {
        return startDateResult;
    }

    EvalResult monthsResult = evaluateAsNumber(args[1], ctx);
    if (monthsResult.isError()) {
        return monthsResult;
    }

    const double startDate = startDateResult.getNumber();
    if (startDate < 1) {
        return EvalResult::Error(CellError::NUM);
    }

    const int months = static_cast<int>(monthsResult.getNumber());

    // Convert serial date to year/month/day
    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(startDate, year, month, day);

    // Add months
    month += months;

    // Handle month overflow/underflow
    while (month > 12) {
        year++;
        month -= 12;
    }
    while (month < 1) {
        year--;
        month += 12;
    }

    // Check year range
    if (year < 1900 || year > 9999) {
        return EvalResult::Error(CellError::NUM);
    }

    // Days in each month (non-leap year)
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Calculate last day of the target month
    int lastDay = daysInMonth[month - 1];
    if (month == 2) {
        // Check for leap year (with Excel's 1900 bug)
        const bool isLeap =
            (year == 1900) || ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        if (isLeap) {
            lastDay = 29;
        }
    }

    // Return serial date for the last day of target month
    const double result = dateToSerial(year, month, lastDay);
    if (result < 1 || result > 2958465) {  // Excel date range
        return EvalResult::Error(CellError::NUM);
    }

    return EvalResult::Number(result);
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

EvalResult fn_EDATE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult start = evaluateAsNumber(args[0], ctx);
    if (start.isError()) {
        return start;
    }
    const EvalResult months = evaluateAsNumber(args[1], ctx);
    if (months.isError()) {
        return months;
    }
    const double result =
        addMonthsToSerial(start.getNumber(), static_cast<int>(months.getNumber()));
    if (result < 1 || result > 2958465) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(result);
}

EvalResult fn_DAYS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult end = evaluateAsNumber(args[0], ctx);
    if (end.isError()) {
        return end;
    }
    const EvalResult start = evaluateAsNumber(args[1], ctx);
    if (start.isError()) {
        return start;
    }
    return EvalResult::Number(static_cast<double>(static_cast<int>(end.getNumber()) -
                                                  static_cast<int>(start.getNumber())));
}

EvalResult fn_DATEDIF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult endRes = evaluateAsNumber(args[1], ctx);
    if (endRes.isError()) {
        return endRes;
    }
    const EvalResult unitRes = evaluateAsString(args[2], ctx);
    if (unitRes.isError()) {
        return unitRes;
    }
    const double start = startRes.getNumber();
    const double end = endRes.getNumber();
    if (end < start) {
        return EvalResult::Error(CellError::NUM);
    }
    int y1 = 0;
    int m1 = 0;
    int d1 = 0;
    int y2 = 0;
    int m2 = 0;
    int d2 = 0;
    serialToDate(start, y1, m1, d1);
    serialToDate(end, y2, m2, d2);

    std::string unit = unitRes.getString();
    for (char& c : unit) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (unit == "Y") {
        int years = y2 - y1;
        if (m2 < m1 || (m2 == m1 && d2 < d1)) {
            --years;
        }
        return EvalResult::Number(static_cast<double>(years));
    }
    if (unit == "M") {
        int months = (y2 - y1) * 12 + (m2 - m1);
        if (d2 < d1) {
            --months;
        }
        return EvalResult::Number(static_cast<double>(months));
    }
    if (unit == "D") {
        return EvalResult::Number(
            static_cast<double>(static_cast<int>(end) - static_cast<int>(start)));
    }
    if (unit == "YM") {
        int months = m2 - m1;
        if (d2 < d1) {
            --months;
        }
        if (months < 0) {
            months += 12;
        }
        return EvalResult::Number(static_cast<double>(months));
    }
    if (unit == "MD") {
        int days = d2 - d1;
        if (days < 0) {
            int month = m2 - 1;
            int year = y2;
            if (month < 1) {
                month = 12;
                --year;
            }
            days += daysInMonthOf(year, month);
        }
        return EvalResult::Number(static_cast<double>(days));
    }
    if (unit == "YD") {
        int startDoy = static_cast<int>(start) - static_cast<int>(dateToSerial(y1, 1, 1));
        int endDoy = static_cast<int>(end) - static_cast<int>(dateToSerial(y2, 1, 1));
        if (endDoy < startDoy) {
            endDoy += (y1 % 4 == 0 ? 366 : 365);
        }
        return EvalResult::Number(static_cast<double>(endDoy - startDoy));
    }
    return EvalResult::Error(CellError::NUM);
}

EvalResult fn_WEEKNUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult serialRes = evaluateAsNumber(args[0], ctx);
    if (serialRes.isError()) {
        return serialRes;
    }
    int returnType = 1;
    if (args.size() == 2) {
        const EvalResult t = evaluateAsNumber(args[1], ctx);
        if (t.isError()) {
            return t;
        }
        returnType = static_cast<int>(t.getNumber());
    }
    const int serialInt = static_cast<int>(serialRes.getNumber());
    if (serialInt < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    int year = 0;
    int month = 0;
    int day = 0;
    serialToDate(serialRes.getNumber(), year, month, day);
    const int jan1 = static_cast<int>(dateToSerial(year, 1, 1));
    const int jan1Wd = weekdaySun1(jan1);  // 1=Sun
    int startWd = 1;                       // Sunday
    if (returnType == 2 || returnType == 21 || returnType == 11) {
        startWd = 2;  // Monday
    } else if (returnType != 1) {
        return EvalResult::Error(CellError::NUM);
    }
    if (returnType == 21) {
        return EvalResult::Number(static_cast<double>(isoWeekFromSerial(serialInt)));
    }
    const int jan1Off = (jan1Wd - startWd + 7) % 7;
    const int doy = serialInt - jan1;
    return EvalResult::Number(static_cast<double>((doy + jan1Off) / 7 + 1));
}

EvalResult fn_NETWORKDAYS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult endRes = evaluateAsNumber(args[1], ctx);
    if (endRes.isError()) {
        return endRes;
    }
    std::vector<int> holidays;
    if (args.size() == 3) {
        EvalResult err = EvalResult::Empty();
        holidays = holidaySerials(args[2], ctx, &err);
        if (err.isError()) {
            return err;
        }
    }
    const int start = static_cast<int>(startRes.getNumber());
    const int end = static_cast<int>(endRes.getNumber());
    const int mask = weekendMaskFromCode(1);
    return EvalResult::Number(static_cast<double>(countNetworkDays(start, end, mask, holidays)));
}

EvalResult fn_WORKDAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult daysRes = evaluateAsNumber(args[1], ctx);
    if (daysRes.isError()) {
        return daysRes;
    }
    std::vector<int> holidays;
    if (args.size() == 3) {
        EvalResult err = EvalResult::Empty();
        holidays = holidaySerials(args[2], ctx, &err);
        if (err.isError()) {
            return err;
        }
    }
    const int days = static_cast<int>(daysRes.getNumber());
    const int start = static_cast<int>(startRes.getNumber());
    const int mask = weekendMaskFromCode(1);
    return EvalResult::Number(static_cast<double>(shiftWorkday(start, days, mask, holidays)));
}

EvalResult fn_NETWORKDAYS_INTL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult endRes = evaluateAsNumber(args[1], ctx);
    if (endRes.isError()) {
        return endRes;
    }
    int mask = weekendMaskFromCode(1);
    if (args.size() >= 3) {
        const EvalResult parsed = weekendMaskFromArg(args[2], ctx, &mask);
        if (parsed.isError()) {
            return parsed;
        }
    }
    std::vector<int> holidays;
    if (args.size() == 4) {
        EvalResult err = EvalResult::Empty();
        holidays = holidaySerials(args[3], ctx, &err);
        if (err.isError()) {
            return err;
        }
    }
    return EvalResult::Number(static_cast<double>(
        countNetworkDays(static_cast<int>(startRes.getNumber()),
                         static_cast<int>(endRes.getNumber()), mask, holidays)));
}

EvalResult fn_WORKDAY_INTL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult daysRes = evaluateAsNumber(args[1], ctx);
    if (daysRes.isError()) {
        return daysRes;
    }
    int mask = weekendMaskFromCode(1);
    if (args.size() >= 3) {
        const EvalResult parsed = weekendMaskFromArg(args[2], ctx, &mask);
        if (parsed.isError()) {
            return parsed;
        }
    }
    std::vector<int> holidays;
    if (args.size() == 4) {
        EvalResult err = EvalResult::Empty();
        holidays = holidaySerials(args[3], ctx, &err);
        if (err.isError()) {
            return err;
        }
    }
    return EvalResult::Number(
        static_cast<double>(shiftWorkday(static_cast<int>(startRes.getNumber()),
                                         static_cast<int>(daysRes.getNumber()), mask, holidays)));
}

EvalResult fn_DAYS360(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult endRes = evaluateAsNumber(args[1], ctx);
    if (endRes.isError()) {
        return endRes;
    }
    bool european = false;
    if (args.size() == 3) {
        const EvalResult m = evaluateAsBoolean(args[2], ctx);
        if (m.isError()) {
            return m;
        }
        european = m.getBoolean();
    }
    int y1 = 0;
    int m1 = 0;
    int d1 = 0;
    int y2 = 0;
    int m2 = 0;
    int d2 = 0;
    serialToDate(startRes.getNumber(), y1, m1, d1);
    serialToDate(endRes.getNumber(), y2, m2, d2);
    return EvalResult::Number(static_cast<double>(days360Count(y1, m1, d1, y2, m2, d2, european)));
}

EvalResult fn_YEARFRAC(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult startRes = evaluateAsNumber(args[0], ctx);
    if (startRes.isError()) {
        return startRes;
    }
    const EvalResult endRes = evaluateAsNumber(args[1], ctx);
    if (endRes.isError()) {
        return endRes;
    }
    int basis = 0;
    if (args.size() == 3) {
        const EvalResult b = evaluateAsNumber(args[2], ctx);
        if (b.isError()) {
            return b;
        }
        basis = static_cast<int>(std::floor(b.getNumber()));
        if (basis < 0 || basis > 4) {
            return EvalResult::Error(CellError::NUM);
        }
    }
    double start = startRes.getNumber();
    double end = endRes.getNumber();
    const int sign = end < start ? -1 : 1;
    if (end < start) {
        const double tmp = start;
        start = end;
        end = tmp;
    }
    const int startInt = static_cast<int>(start);
    const int endInt = static_cast<int>(end);
    int y1 = 0;
    int m1 = 0;
    int d1 = 0;
    int y2 = 0;
    int m2 = 0;
    int d2 = 0;
    serialToDate(start, y1, m1, d1);
    serialToDate(end, y2, m2, d2);
    double frac = 0.0;
    if (basis == 0 || basis == 4) {
        frac = static_cast<double>(days360Count(y1, m1, d1, y2, m2, d2, basis == 4)) / 360.0;
    } else if (basis == 2) {
        frac = static_cast<double>(endInt - startInt) / 360.0;
    } else if (basis == 3) {
        frac = static_cast<double>(endInt - startInt) / 365.0;
    } else {
        if (y1 == y2) {
            frac = static_cast<double>(endInt - startInt) / static_cast<double>(daysInYearOf(y1));
        } else {
            double acc = 0.0;
            const int nYears = y2 - y1 + 1;
            for (int y = y1; y <= y2; ++y) {
                acc += static_cast<double>(daysInYearOf(y));
            }
            frac = static_cast<double>(endInt - startInt) / (acc / static_cast<double>(nYears));
        }
    }
    return EvalResult::Number(excelNormalize(frac * static_cast<double>(sign)));
}

EvalResult fn_ISOWEEKNUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    const EvalResult serialRes = evaluateAsNumber(args[0], ctx);
    if (serialRes.isError()) {
        return serialRes;
    }
    const int serialInt = static_cast<int>(serialRes.getNumber());
    if (serialInt < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(static_cast<double>(isoWeekFromSerial(serialInt)));
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
    registry.registerFunction("EOMONTH", fn_EOMONTH, "(start_date, months)",
                              "Returns the last day of the month N months from start_date", "Date");
    registry.registerFunction("EDATE", fn_EDATE, "(start_date, months)",
                              "Date the specified number of months from start_date", "Date");
    registry.registerFunction("DAYS", fn_DAYS, "(end_date, start_date)",
                              "Number of days between two dates", "Date");
    registry.registerFunction("DATEDIF", fn_DATEDIF, "(start_date, end_date, unit)",
                              "Difference between dates in years, months, or days", "Date");
    registry.registerFunction("WEEKNUM", fn_WEEKNUM, "(date, [return_type])",
                              "Week number of a date", "Date");
    registry.registerFunction("NETWORKDAYS", fn_NETWORKDAYS, "(start_date, end_date, [holidays])",
                              "Working days between two dates", "Date");
    registry.registerFunction("WORKDAY", fn_WORKDAY, "(start_date, days, [holidays])",
                              "Date a given number of working days from start_date", "Date");
    registry.registerFunction("NETWORKDAYS.INTL", fn_NETWORKDAYS_INTL,
                              "(start_date, end_date, [weekend], [holidays])",
                              "Working days with a custom weekend", "Date");
    registry.registerAlias("NETWORKDAYS_INTL", "NETWORKDAYS.INTL");
    registry.registerFunction("WORKDAY.INTL", fn_WORKDAY_INTL,
                              "(start_date, days, [weekend], [holidays])",
                              "Workday with a custom weekend", "Date");
    registry.registerAlias("WORKDAY_INTL", "WORKDAY.INTL");
    registry.registerFunction("DAYS360", fn_DAYS360, "(start_date, end_date, [method])",
                              "Days between dates on a 360-day year", "Date");
    registry.registerFunction("YEARFRAC", fn_YEARFRAC, "(start_date, end_date, [basis])",
                              "Fraction of a year between two dates", "Date");
    registry.registerFunction("ISOWEEKNUM", fn_ISOWEEKNUM, "(date)",
                              "ISO 8601 week number of a date", "Date");
}

}  // namespace cells
