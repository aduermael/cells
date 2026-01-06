#ifndef CELLS_NUMBER_FORMAT_H_
#define CELLS_NUMBER_FORMAT_H_

#include <cstdint>

#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Number format categories following Excel conventions
enum class NumberFormatCategory : uint8_t {
    GENERAL,     // Default format, displays as entered
    NUMBER,      // Numeric with optional decimals and thousands separator
    CURRENCY,    // Currency format ($1,234.56)
    ACCOUNTING,  // Accounting format (aligned currency symbols, negatives in parens)
    PERCENTAGE,  // Percentage (15% stored as 0.15)
    DATE,        // Date only (1/15/2024)
    TIME,        // Time only (12:30 PM)
    DATE_TIME,   // Date and time combined
    SCIENTIFIC,  // Scientific notation (1.5E+10)
    FRACTION,    // Fractional display (1/4)
    TEXT,        // Text format (numbers displayed as text)
};

// Convert NumberFormatCategory to string for serialization
const char* formatCategoryToString(NumberFormatCategory category);

// Convert string to NumberFormatCategory for deserialization
NumberFormatCategory stringToFormatCategory(const std::string& str);

// NumberFormat represents a cell's display format
// Based on Excel's number format system
struct NumberFormat {
    ID id;                          // Unique identifier (8-char base62)
    NumberFormatCategory category;  // Format category
    std::string formatCode;         // Excel-style format code (e.g., "#,##0.00")
    uint8_t decimalPlaces;          // Number of decimal places (0-15)
    bool useThousandsSeparator;     // Whether to use thousand separators
    std::string currencySymbol;     // Currency symbol (e.g., "$", "€", "£")
    bool isAccounting;              // Accounting format (aligned symbols)

    // Default constructor creates a GENERAL format
    NumberFormat();

    // Construct with specific properties
    NumberFormat(const ID& id, NumberFormatCategory category, std::string formatCode = "",
                 uint8_t decimalPlaces = 2, bool useThousandsSeparator = false,
                 std::string currencySymbol = "", bool isAccounting = false);

    // Equality comparison
    bool operator==(const NumberFormat& other) const;
    bool operator!=(const NumberFormat& other) const;
};

// Built-in format IDs (well-known constants)
// Using specific IDs for built-in formats ensures consistency across files
namespace BuiltInFormats {
// General format (default)
extern const ID GENERAL;

// Number formats (0-4 decimal places)
extern const ID NUMBER_0;     // 0 decimal places
extern const ID NUMBER_1;     // 1 decimal place
extern const ID NUMBER_2;     // 2 decimal places
extern const ID NUMBER_3;     // 3 decimal places
extern const ID NUMBER_4;     // 4 decimal places
extern const ID NUMBER_SEP;   // Thousands separator, 0 decimals
extern const ID NUMBER_SEP2;  // Thousands separator, 2 decimals

// Currency formats
extern const ID CURRENCY_0;  // $1,234
extern const ID CURRENCY_2;  // $1,234.56

// Accounting formats
extern const ID ACCOUNTING_0;  // Accounting, 0 decimals
extern const ID ACCOUNTING_2;  // Accounting, 2 decimals

// Percentage formats
extern const ID PERCENTAGE_0;  // 15%
extern const ID PERCENTAGE_2;  // 15.00%

// Date formats
extern const ID DATE_SHORT;  // 1/15/2024
extern const ID DATE_LONG;   // January 15, 2024
extern const ID DATE_ISO;    // 2024-01-15

// Time formats
extern const ID TIME_12H;  // 12:30 PM
extern const ID TIME_24H;  // 14:30

// DateTime formats
extern const ID DATETIME_SHORT;  // 1/15/2024 12:30 PM

// Scientific
extern const ID SCIENTIFIC_2;  // 1.50E+10

// Text
extern const ID TEXT;  // Display as text
}  // namespace BuiltInFormats

// NumberFormatRegistry manages available formats
// Contains built-in formats and user-defined custom formats
class NumberFormatRegistry {
public:
    NumberFormatRegistry();

    // Get a format by ID (returns nullptr if not found)
    [[nodiscard]] const NumberFormat* getFormat(const ID& id) const;

    // Get the default (GENERAL) format
    [[nodiscard]] const NumberFormat* getDefaultFormat() const;

    // Register a custom format (returns false if ID already exists)
    bool registerFormat(const NumberFormat& format);

    // Get all registered formats
    [[nodiscard]] const std::unordered_map<ID, NumberFormat, IDHash>& getAllFormats() const;

    // Get formats by category
    [[nodiscard]] std::vector<const NumberFormat*> getFormatsByCategory(
        NumberFormatCategory category) const;

    // Check if a format ID exists
    [[nodiscard]] bool hasFormat(const ID& id) const;

private:
    std::unordered_map<ID, NumberFormat, IDHash> formats_;

    // Initialize built-in formats
    void initBuiltInFormats();
};

}  // namespace cells

#endif  // CELLS_NUMBER_FORMAT_H_
