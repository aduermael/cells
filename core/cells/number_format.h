// =============================================================================
// Number Format Definitions
// =============================================================================
//
// Defines number formats for cell display: currency, percentage, dates, etc.
// Based on Excel's number format system with built-in and custom formats.
//
// Key responsibilities:
// - Define NumberFormat struct with category, format code, and display options
// - Provide built-in format IDs (GENERAL, NUMBER_2, CURRENCY_USD_2, etc.)
// - Parse dynamic format IDs (FMT_P007 = percentage with 7 decimals)
// - Registry for managing built-in and custom formats
// - Format inference from formula references
//
// Format categories:
// - GENERAL: Default display (auto-detect)
// - NUMBER: Plain numbers (0, 2, 4 decimal places)
// - CURRENCY: Currency symbols ($, €, £, ¥)
// - PERCENTAGE: Percent display (value * 100 + %)
// - DATE/TIME: Date and time formatting
// - SCIENTIFIC: Scientific notation (1.5E+10)
//
// Dependencies: types.h
// Used by: number_formatter.h, bindings.cc, input_parser.h
//
// =============================================================================

#ifndef CELLS_NUMBER_FORMAT_H_
#define CELLS_NUMBER_FORMAT_H_

#include <cstdint>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Number format categories following Excel conventions
enum class NumberFormatCategory : uint8_t {
    GENERAL = 0,     // Default format, displays as entered
    NUMBER = 1,      // Numeric with optional decimals and thousands separator
    CURRENCY = 2,    // Currency format ($1,234.56)
    ACCOUNTING = 3,  // Accounting format (aligned currency symbols, negatives in parens)
    PERCENTAGE = 4,  // Percentage (15% stored as 0.15)
    DATE = 5,        // Date only (1/15/2024)
    TIME = 6,        // Time only (12:30 PM)
    DATE_TIME = 7,   // Date and time combined
    SCIENTIFIC = 8,  // Scientific notation (1.5E+10)
    FRACTION = 9,    // Fractional display (1/4)
    TEXT = 10,       // Text format (numbers displayed as text)
    CUSTOM = 11,     // Custom format that doesn't fit standard categories
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
    bool isCustom;                  // True if this is a user-defined custom format

    // Default constructor creates a GENERAL format
    NumberFormat();

    // Construct with specific properties
    NumberFormat(const ID& id, NumberFormatCategory category, std::string formatCode = "",
                 uint8_t decimalPlaces = 2, bool useThousandsSeparator = false,
                 std::string currencySymbol = "", bool isAccounting = false, bool isCustom = false);

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
extern const ID NUMBER_SEP1;  // Thousands separator, 1 decimal
extern const ID NUMBER_SEP2;  // Thousands separator, 2 decimals
extern const ID NUMBER_SEP3;  // Thousands separator, 3 decimals
extern const ID NUMBER_SEP4;  // Thousands separator, 4 decimals

// Currency formats - USD
extern const ID CURRENCY_USD_0;  // $1,234
extern const ID CURRENCY_USD_1;  // $1,234.5
extern const ID CURRENCY_USD_2;  // $1,234.56
extern const ID CURRENCY_USD_3;  // $1,234.567
extern const ID CURRENCY_USD_4;  // $1,234.5678

// Currency formats - EUR
extern const ID CURRENCY_EUR_0;  // €1,234
extern const ID CURRENCY_EUR_1;  // €1,234.5
extern const ID CURRENCY_EUR_2;  // €1,234.56
extern const ID CURRENCY_EUR_3;  // €1,234.567
extern const ID CURRENCY_EUR_4;  // €1,234.5678

// Currency formats - GBP
extern const ID CURRENCY_GBP_0;  // £1,234
extern const ID CURRENCY_GBP_1;  // £1,234.5
extern const ID CURRENCY_GBP_2;  // £1,234.56
extern const ID CURRENCY_GBP_3;  // £1,234.567
extern const ID CURRENCY_GBP_4;  // £1,234.5678

// Currency formats - JPY
extern const ID CURRENCY_JPY_0;  // ¥1,234
extern const ID CURRENCY_JPY_1;  // ¥1,234.5
extern const ID CURRENCY_JPY_2;  // ¥1,234.56
extern const ID CURRENCY_JPY_3;  // ¥1,234.567
extern const ID CURRENCY_JPY_4;  // ¥1,234.5678

// Currency formats - CNY
extern const ID CURRENCY_CNY_0;  // ¥1,234
extern const ID CURRENCY_CNY_1;  // ¥1,234.5
extern const ID CURRENCY_CNY_2;  // ¥1,234.56
extern const ID CURRENCY_CNY_3;  // ¥1,234.567
extern const ID CURRENCY_CNY_4;  // ¥1,234.5678

// Accounting formats
extern const ID ACCOUNTING_0;  // Accounting, 0 decimals
extern const ID ACCOUNTING_2;  // Accounting, 2 decimals

// Percentage formats (0-4 decimal places)
extern const ID PERCENTAGE_0;  // 15%
extern const ID PERCENTAGE_1;  // 15.0%
extern const ID PERCENTAGE_2;  // 15.00%
extern const ID PERCENTAGE_3;  // 15.000%
extern const ID PERCENTAGE_4;  // 15.0000%

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

// =============================================================================
// Dynamic Format ID Parsing
// =============================================================================

/**
 * ParsedFormatId holds the parsed components of a dynamically-generated format ID.
 *
 * Format ID patterns supported:
 * - FMT_P0XX (percentage with XX decimal places, 00-15)
 * - FMT_N0XX (number with XX decimal places, 00-15)
 * - FMT_NSXX (number with separator, XX decimal places, 00-15)
 * - C<CURRENCY>_0XX (currency with 3-letter code and XX decimal places)
 *
 * The parser extracts category, decimal places, currency code, etc. from the ID.
 */
struct ParsedFormatId {
    NumberFormatCategory category{NumberFormatCategory::GENERAL};
    uint8_t decimalPlaces{0};
    bool useThousandsSeparator{false};
    std::string currencyCode;    // e.g., "USD", "EUR"
    std::string currencySymbol;  // e.g., "$", "€"
    bool valid{false};           // True if parsing succeeded
};

/**
 * Parse a format ID string into its components.
 *
 * Supported patterns:
 * - FMT_P0XX: Percentage with XX decimal places (e.g., FMT_P007 = 7 decimals)
 * - FMT_N0XX: Number with XX decimal places (e.g., FMT_N012 = 12 decimals)
 * - FMT_NSXX: Number with separator, XX decimal places (e.g., FMT_NS05, FMT_NS12)
 * - CXXX_0YY: Currency (XXX = currency code, YY = decimals, e.g., CUSD_008)
 *
 * Returns ParsedFormatId with valid=false if the ID doesn't match any pattern.
 */
ParsedFormatId parseFormatId(const std::string& id);

/**
 * Generate an Excel-style format code from a parsed format ID.
 *
 * Examples:
 * - Percentage with 7 decimals → "0.0000000%"
 * - Number with 12 decimals → "0.000000000000"
 * - Number with separator and 5 decimals → "#,##0.00000"
 * - USD with 8 decimals → "$#,##0.00000000"
 *
 * Returns empty string if the parsed format is invalid.
 */
std::string generateFormatCode(const ParsedFormatId& parsed);

/**
 * Get the currency symbol for a currency code.
 *
 * Supported codes: USD ($), EUR (€), GBP (£), JPY (¥), CNY (¥)
 * Returns empty string for unknown codes.
 */
std::string getCurrencySymbol(const std::string& currencyCode);

/**
 * Get detailed information about a format ID.
 *
 * Returns JSON string with format details:
 * - category: "number", "currency", "percentage", "general", etc.
 * - decimals: number of decimal places (0-15)
 * - separator: whether thousands separator is used
 * - currency: currency code if applicable (null otherwise)
 *
 * Example output: {"category":"number","decimals":2,"separator":true,"currency":null}
 * Returns {"error":"Unknown format"} if the format ID is not recognized.
 */
std::string getFormatDetails(const std::string& formatId);

/**
 * Generate a format ID for given parameters.
 *
 * @param category Format category: "number", "currency", "percentage"
 * @param decimals Decimal places (0-15)
 * @param separator Whether to use thousands separator (only for number category)
 * @param currency Currency code (for currency category, e.g., "USD", "EUR")
 * @return Format ID string (e.g., "FMT_N002", "FMT_NS02", "CUSD_002")
 *         Returns empty string if parameters are invalid.
 */
std::string makeFormatId(const std::string& category, int decimals, bool separator,
                         const std::string& currency);

// Forward declarations for format inference
struct ASTNode;

/**
 * Get the priority of a format category for format inheritance.
 * Higher priority formats win when multiple references have different formats.
 *
 * Priority (highest to lowest):
 * 1. DATE/TIME/DATE_TIME (most specific, dates are special)
 * 2. CURRENCY/ACCOUNTING (financial formats)
 * 3. PERCENTAGE
 * 4. NUMBER (with separator > without)
 * 5. GENERAL/TEXT (no format, lowest priority)
 */
int getFormatPriority(NumberFormatCategory category);

// Type for looking up a cell's format ID by cell UUID string
// Returns the format ID, or empty string for GENERAL/no format
using FormatLookup = std::function<std::string(const std::string& cellId)>;

/**
 * Infer the format for a formula based on referenced cells.
 *
 * When a formula is entered into a cell with GENERAL format, Excel automatically
 * inherits the format from referenced cells. This function implements that logic.
 *
 * Rules:
 * - Only cell references contribute to format inheritance (literals don't)
 * - Higher priority categories win (DATE > CURRENCY > PERCENTAGE > NUMBER)
 * - For same category, more specific formats win (e.g., more decimals)
 * - If all referenced cells are GENERAL, returns GENERAL
 *
 * @param ast The parsed formula AST (must be resolved with cell IDs)
 * @param formatLookup Function to get a cell's format ID from its UUID
 * @return The format ID to inherit, or empty string for GENERAL/no inheritance
 */
std::string inferFormatFromFormula(const ASTNode* ast, const FormatLookup& formatLookup);

/**
 * Result of creating a custom format.
 */
struct CreateCustomFormatResult {
    ID id;                     // The ID of the created format (or existing if reused)
    bool success{false};       // Whether creation succeeded
    std::string errorMessage;  // Error message if failed

    static CreateCustomFormatResult ok(const ID& id) {
        CreateCustomFormatResult result;
        result.id = id;
        result.success = true;
        return result;
    }

    static CreateCustomFormatResult error(const std::string& message) {
        CreateCustomFormatResult result;
        result.success = false;
        result.errorMessage = message;
        return result;
    }
};

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

    // Create a custom format from an Excel-style format code
    // Parses the format code, validates it, generates a new ID, and registers it
    // If an identical format code already exists, returns the existing format's ID
    CreateCustomFormatResult createCustomFormat(const std::string& formatCode);

    // Get all registered formats
    [[nodiscard]] const std::unordered_map<ID, NumberFormat, IDHash>& getAllFormats() const;

    // Get formats by category
    [[nodiscard]] std::vector<const NumberFormat*> getFormatsByCategory(
        NumberFormatCategory category) const;

    // Check if a format ID exists
    [[nodiscard]] bool hasFormat(const ID& id) const;

    // Find a format by format code (returns nullptr if not found)
    [[nodiscard]] const NumberFormat* findByFormatCode(const std::string& formatCode) const;

    // Get or create a format by ID
    // If the format exists in cache, returns it. Otherwise, attempts to parse
    // the format ID as a dynamic pattern (FMT_P0XX, FMT_N0XX, FMT_NSXX, CXXX_0YY),
    // creates the format, caches it, and returns it.
    // Returns nullptr if the ID is not cached and cannot be parsed.
    const NumberFormat* getOrCreateFormat(const ID& id);

private:
    std::unordered_map<ID, NumberFormat, IDHash> formats_;

    // Initialize built-in formats
    void initBuiltInFormats();
};

}  // namespace cells

#endif  // CELLS_NUMBER_FORMAT_H_
