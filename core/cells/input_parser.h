#ifndef CELLS_INPUT_PARSER_H_
#define CELLS_INPUT_PARSER_H_

#include <string>

#include "core/cells/number_format.h"
#include "core/cells/types.h"

namespace cells {

// Result of parsing user input
// Contains the detected value and suggested format
struct ParsedInput {
    // The parsed numeric value (if applicable)
    double numericValue{0.0};

    // The string value (for text input)
    std::string stringValue;

    // Detected value type
    CellValueType valueType{CellValueType::STRING};

    // Suggested format ID (null ID = GENERAL format)
    ID formatId;

    // Suggested format category (for quick checks)
    NumberFormatCategory formatCategory{NumberFormatCategory::GENERAL};

    // Whether parsing was successful
    bool success{false};

    // Error message if parsing failed
    std::string errorMessage;

    // Create a successful numeric result
    static ParsedInput number(double value, const ID& formatId = ID{},
                              NumberFormatCategory category = NumberFormatCategory::NUMBER);

    // Create a successful text result
    static ParsedInput text(const std::string& value);

    // Create a failed result
    static ParsedInput error(const std::string& message);
};

// Parse user input and auto-detect format
// Recognizes:
// - Percentages: "15%", "15 %", "-15%"
// - Currency: "$1,234.56", "-$1,234", "$1234"
// - Dates: "1/15/2024", "2024-01-15", "15-Jan-2024"
// - Times: "12:30 PM", "14:30", "12:30:45"
// - Scientific: "1.5E+10", "1.5e-5"
// - Plain numbers: "1234", "1,234.56", "-123.45"
//
// Returns ParsedInput with detected value and suggested format
ParsedInput parseUserInput(const std::string& input);

// Parse percentage input (e.g., "15%" -> 0.15)
// Returns true if input is a valid percentage
ParsedInput parsePercentage(const std::string& input);

// Parse currency input (e.g., "$1,234.56" -> 1234.56)
// Returns true if input is a valid currency amount
ParsedInput parseCurrency(const std::string& input);

// Parse date input (e.g., "1/15/2024" -> serial date number)
// Returns true if input is a valid date
// Serial date: Days since December 30, 1899 (Excel convention)
ParsedInput parseDate(const std::string& input);

// Parse time input (e.g., "12:30 PM" -> 0.520833...)
// Returns true if input is a valid time
// Time value: Fraction of day (0.0 = midnight, 0.5 = noon)
ParsedInput parseTime(const std::string& input);

// Parse scientific notation (e.g., "1.5E+10")
// Returns true if input is in scientific notation
ParsedInput parseScientific(const std::string& input);

// Parse plain number (e.g., "1,234.56")
// Returns true if input is a valid number
ParsedInput parseNumber(const std::string& input);

// Date utility functions
namespace DateUtils {

// Convert year, month, day to Excel serial date
// Days since December 30, 1899 (Excel convention)
double toSerialDate(int year, int month, int day);

// Convert Excel serial date to year, month, day
void fromSerialDate(double serial, int& year, int& month, int& day);

// Check if year is a leap year
bool isLeapYear(int year);

// Get days in month (1-12) for given year
int daysInMonth(int year, int month);

}  // namespace DateUtils

// Time utility functions
namespace TimeUtils {

// Convert hours, minutes, seconds to fractional day
double toFractionalDay(int hours, int minutes, int seconds = 0);

// Convert fractional day to hours, minutes, seconds
void fromFractionalDay(double fraction, int& hours, int& minutes, int& seconds);

}  // namespace TimeUtils

}  // namespace cells

#endif  // CELLS_INPUT_PARSER_H_
