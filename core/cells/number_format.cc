#include "core/cells/number_format.h"

#include <cctype>
#include <cstdlib>

#include "core/cells/format_code_parser.h"
#include "core/cells/id.h"

namespace cells {

// --- Category string conversion ---

const char* formatCategoryToString(NumberFormatCategory category) {
    switch (category) {
        case NumberFormatCategory::GENERAL:
            return "general";
        case NumberFormatCategory::NUMBER:
            return "number";
        case NumberFormatCategory::CURRENCY:
            return "currency";
        case NumberFormatCategory::ACCOUNTING:
            return "accounting";
        case NumberFormatCategory::PERCENTAGE:
            return "percentage";
        case NumberFormatCategory::DATE:
            return "date";
        case NumberFormatCategory::TIME:
            return "time";
        case NumberFormatCategory::DATE_TIME:
            return "datetime";
        case NumberFormatCategory::SCIENTIFIC:
            return "scientific";
        case NumberFormatCategory::FRACTION:
            return "fraction";
        case NumberFormatCategory::TEXT:
            return "text";
    }
    return "general";
}

NumberFormatCategory stringToFormatCategory(const std::string& str) {
    if (str == "general") {
        return NumberFormatCategory::GENERAL;
    }
    if (str == "number") {
        return NumberFormatCategory::NUMBER;
    }
    if (str == "currency") {
        return NumberFormatCategory::CURRENCY;
    }
    if (str == "accounting") {
        return NumberFormatCategory::ACCOUNTING;
    }
    if (str == "percentage") {
        return NumberFormatCategory::PERCENTAGE;
    }
    if (str == "date") {
        return NumberFormatCategory::DATE;
    }
    if (str == "time") {
        return NumberFormatCategory::TIME;
    }
    if (str == "datetime") {
        return NumberFormatCategory::DATE_TIME;
    }
    if (str == "scientific") {
        return NumberFormatCategory::SCIENTIFIC;
    }
    if (str == "fraction") {
        return NumberFormatCategory::FRACTION;
    }
    if (str == "text") {
        return NumberFormatCategory::TEXT;
    }
    return NumberFormatCategory::GENERAL;
}

// --- NumberFormat ---

NumberFormat::NumberFormat()
    : id(),
      category(NumberFormatCategory::GENERAL),
      formatCode("General"),
      decimalPlaces(0),
      useThousandsSeparator(false),
      currencySymbol(),
      isAccounting(false),
      isCustom(false) {}

NumberFormat::NumberFormat(const ID& id, NumberFormatCategory category, std::string formatCode,
                           uint8_t decimalPlaces, bool useThousandsSeparator,
                           std::string currencySymbol, bool isAccounting, bool isCustom)
    : id(id),
      category(category),
      formatCode(std::move(formatCode)),
      decimalPlaces(decimalPlaces),
      useThousandsSeparator(useThousandsSeparator),
      currencySymbol(std::move(currencySymbol)),
      isAccounting(isAccounting),
      isCustom(isCustom) {}

bool NumberFormat::operator==(const NumberFormat& other) const {
    return id == other.id && category == other.category && formatCode == other.formatCode &&
           decimalPlaces == other.decimalPlaces &&
           useThousandsSeparator == other.useThousandsSeparator &&
           currencySymbol == other.currencySymbol && isAccounting == other.isAccounting &&
           isCustom == other.isCustom;
}

bool NumberFormat::operator!=(const NumberFormat& other) const {
    return !(*this == other);
}

// --- Built-in Format IDs ---
// Using readable base62 IDs for built-in formats

namespace BuiltInFormats {
const ID GENERAL("FMT_GEN0");

const ID NUMBER_0("FMT_N000");
const ID NUMBER_1("FMT_N001");
const ID NUMBER_2("FMT_N002");
const ID NUMBER_3("FMT_N003");
const ID NUMBER_4("FMT_N004");
const ID NUMBER_SEP("FMT_NS00");
const ID NUMBER_SEP1("FMT_NS01");
const ID NUMBER_SEP2("FMT_NS02");
const ID NUMBER_SEP3("FMT_NS03");
const ID NUMBER_SEP4("FMT_NS04");

// Legacy USD currency format IDs (for backward compatibility)
const ID CURRENCY_0("FMT_C000");
const ID CURRENCY_1("FMT_C001");
const ID CURRENCY_2("FMT_C002");
const ID CURRENCY_3("FMT_C003");
const ID CURRENCY_4("FMT_C004");

// USD currency formats (new naming, 8 chars: CUSD_0XX)
const ID CURRENCY_USD_0("CUSD_000");
const ID CURRENCY_USD_1("CUSD_001");
const ID CURRENCY_USD_2("CUSD_002");
const ID CURRENCY_USD_3("CUSD_003");
const ID CURRENCY_USD_4("CUSD_004");

// EUR currency formats (8 chars: CEUR_0XX)
const ID CURRENCY_EUR_0("CEUR_000");
const ID CURRENCY_EUR_1("CEUR_001");
const ID CURRENCY_EUR_2("CEUR_002");
const ID CURRENCY_EUR_3("CEUR_003");
const ID CURRENCY_EUR_4("CEUR_004");

// GBP currency formats (8 chars: CGBP_0XX)
const ID CURRENCY_GBP_0("CGBP_000");
const ID CURRENCY_GBP_1("CGBP_001");
const ID CURRENCY_GBP_2("CGBP_002");
const ID CURRENCY_GBP_3("CGBP_003");
const ID CURRENCY_GBP_4("CGBP_004");

// JPY currency formats (8 chars: CJPY_0XX)
const ID CURRENCY_JPY_0("CJPY_000");
const ID CURRENCY_JPY_1("CJPY_001");
const ID CURRENCY_JPY_2("CJPY_002");
const ID CURRENCY_JPY_3("CJPY_003");
const ID CURRENCY_JPY_4("CJPY_004");

// CNY currency formats (8 chars: CCNY_0XX)
const ID CURRENCY_CNY_0("CCNY_000");
const ID CURRENCY_CNY_1("CCNY_001");
const ID CURRENCY_CNY_2("CCNY_002");
const ID CURRENCY_CNY_3("CCNY_003");
const ID CURRENCY_CNY_4("CCNY_004");

const ID ACCOUNTING_0("FMT_A000");
const ID ACCOUNTING_2("FMT_A002");

const ID PERCENTAGE_0("FMT_P000");
const ID PERCENTAGE_1("FMT_P001");
const ID PERCENTAGE_2("FMT_P002");
const ID PERCENTAGE_3("FMT_P003");
const ID PERCENTAGE_4("FMT_P004");

const ID DATE_SHORT("FMT_DSHT");
const ID DATE_LONG("FMT_DLNG");
const ID DATE_ISO("FMT_DISO");

const ID TIME_12H("FMT_T12H");
const ID TIME_24H("FMT_T24H");

const ID DATETIME_SHORT("FMT_DTSH");

const ID SCIENTIFIC_2("FMT_SCI2");

const ID TEXT("FMT_TEXT");
}  // namespace BuiltInFormats

// --- Dynamic Format ID Parsing ---

std::string getCurrencySymbol(const std::string& currencyCode) {
    if (currencyCode == "USD") {
        return "$";
    }
    if (currencyCode == "EUR") {
        return "€";
    }
    if (currencyCode == "GBP") {
        return "£";
    }
    if (currencyCode == "JPY" || currencyCode == "CNY") {
        return "¥";
    }
    return "";
}

ParsedFormatId parseFormatId(const std::string& id) {
    ParsedFormatId result;

    // Minimum length check
    if (id.size() < 8) {
        return result;
    }

    // Pattern: FMT_P0XX (percentage with XX decimal places)
    // Example: FMT_P007 = 7 decimal places
    if (id.size() == 8 && id.substr(0, 5) == "FMT_P" && id[5] == '0') {
        if (std::isdigit(static_cast<unsigned char>(id[6])) != 0 &&
            std::isdigit(static_cast<unsigned char>(id[7])) != 0) {
            const int decimals = (id[6] - '0') * 10 + (id[7] - '0');
            if (decimals <= 15) {
                result.category = NumberFormatCategory::PERCENTAGE;
                result.decimalPlaces = static_cast<uint8_t>(decimals);
                result.useThousandsSeparator = false;
                result.valid = true;
                return result;
            }
        }
    }

    // Pattern: FMT_N0XX (number with XX decimal places, no separator)
    // Example: FMT_N012 = 12 decimal places
    if (id.size() == 8 && id.substr(0, 5) == "FMT_N" && id[5] == '0' &&
        !(id[6] == 'S')) {  // Distinguish from FMT_NS0X
        if (std::isdigit(static_cast<unsigned char>(id[6])) != 0 &&
            std::isdigit(static_cast<unsigned char>(id[7])) != 0) {
            const int decimals = (id[6] - '0') * 10 + (id[7] - '0');
            if (decimals <= 15) {
                result.category = NumberFormatCategory::NUMBER;
                result.decimalPlaces = static_cast<uint8_t>(decimals);
                result.useThousandsSeparator = false;
                result.valid = true;
                return result;
            }
        }
    }

    // Pattern: FMT_NS0X (number with separator, X decimal places)
    // Example: FMT_NS05 = 5 decimal places with thousands separator
    if (id.size() == 8 && id.substr(0, 6) == "FMT_NS" && id[6] == '0') {
        if (std::isdigit(static_cast<unsigned char>(id[7])) != 0) {
            const int decimals = id[7] - '0';
            if (decimals <= 9) {
                result.category = NumberFormatCategory::NUMBER;
                result.decimalPlaces = static_cast<uint8_t>(decimals);
                result.useThousandsSeparator = true;
                result.valid = true;
                return result;
            }
        }
    }

    // Pattern: CXXX_0YY (currency with 3-letter code and YY decimal places)
    // Example: CUSD_008 = USD with 8 decimal places
    if (id.size() == 8 && id[0] == 'C' && id[4] == '_' && id[5] == '0') {
        // Extract currency code (3 uppercase letters)
        const std::string currencyCode = id.substr(1, 3);
        bool validCurrency = true;
        for (const char c : currencyCode) {
            if (std::isupper(static_cast<unsigned char>(c)) == 0) {
                validCurrency = false;
                break;
            }
        }

        if (validCurrency && std::isdigit(static_cast<unsigned char>(id[6])) != 0 &&
            std::isdigit(static_cast<unsigned char>(id[7])) != 0) {
            const int decimals = (id[6] - '0') * 10 + (id[7] - '0');
            const std::string symbol = getCurrencySymbol(currencyCode);

            if (decimals <= 15 && !symbol.empty()) {
                result.category = NumberFormatCategory::CURRENCY;
                result.decimalPlaces = static_cast<uint8_t>(decimals);
                result.useThousandsSeparator = true;  // Currency always has separator
                result.currencyCode = currencyCode;
                result.currencySymbol = symbol;
                result.valid = true;
                return result;
            }
        }
    }

    return result;
}

std::string generateFormatCode(const ParsedFormatId& parsed) {
    if (!parsed.valid) {
        return "";
    }

    std::string decimalPart;
    if (parsed.decimalPlaces > 0) {
        decimalPart = "." + std::string(parsed.decimalPlaces, '0');
    }

    switch (parsed.category) {
        case NumberFormatCategory::PERCENTAGE:
            // Format: 0.0000000%
            return "0" + decimalPart + "%";

        case NumberFormatCategory::NUMBER:
            if (parsed.useThousandsSeparator) {
                // Format: #,##0.00000
                return "#,##0" + decimalPart;
            }
            // Format: 0.000000000000
            return "0" + decimalPart;

        case NumberFormatCategory::CURRENCY:
            // Format: $#,##0.00000000
            return parsed.currencySymbol + "#,##0" + decimalPart;

        default:
            return "";
    }
}

// --- NumberFormatRegistry ---

NumberFormatRegistry::NumberFormatRegistry() {
    initBuiltInFormats();
}

const NumberFormat* NumberFormatRegistry::getFormat(const ID& id) const {
    auto it = formats_.find(id);
    if (it != formats_.end()) {
        return &it->second;
    }
    return nullptr;
}

const NumberFormat* NumberFormatRegistry::getDefaultFormat() const {
    return getFormat(BuiltInFormats::GENERAL);
}

bool NumberFormatRegistry::registerFormat(const NumberFormat& format) {
    if (formats_.find(format.id) != formats_.end()) {
        return false;  // ID already exists
    }
    formats_[format.id] = format;
    return true;
}

const std::unordered_map<ID, NumberFormat, IDHash>& NumberFormatRegistry::getAllFormats() const {
    return formats_;
}

std::vector<const NumberFormat*> NumberFormatRegistry::getFormatsByCategory(
    NumberFormatCategory category) const {
    std::vector<const NumberFormat*> result;
    for (const auto& pair : formats_) {
        if (pair.second.category == category) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

bool NumberFormatRegistry::hasFormat(const ID& id) const {
    return formats_.find(id) != formats_.end();
}

void NumberFormatRegistry::initBuiltInFormats() {
    using Cat = NumberFormatCategory;

    // General (default)
    formats_[BuiltInFormats::GENERAL] =
        NumberFormat(BuiltInFormats::GENERAL, Cat::GENERAL, "General", 0, false, "", false);

    // Number formats (0-4 decimal places)
    formats_[BuiltInFormats::NUMBER_0] =
        NumberFormat(BuiltInFormats::NUMBER_0, Cat::NUMBER, "0", 0, false, "", false);
    formats_[BuiltInFormats::NUMBER_1] =
        NumberFormat(BuiltInFormats::NUMBER_1, Cat::NUMBER, "0.0", 1, false, "", false);
    formats_[BuiltInFormats::NUMBER_2] =
        NumberFormat(BuiltInFormats::NUMBER_2, Cat::NUMBER, "0.00", 2, false, "", false);
    formats_[BuiltInFormats::NUMBER_3] =
        NumberFormat(BuiltInFormats::NUMBER_3, Cat::NUMBER, "0.000", 3, false, "", false);
    formats_[BuiltInFormats::NUMBER_4] =
        NumberFormat(BuiltInFormats::NUMBER_4, Cat::NUMBER, "0.0000", 4, false, "", false);
    formats_[BuiltInFormats::NUMBER_SEP] =
        NumberFormat(BuiltInFormats::NUMBER_SEP, Cat::NUMBER, "#,##0", 0, true, "", false);
    formats_[BuiltInFormats::NUMBER_SEP1] =
        NumberFormat(BuiltInFormats::NUMBER_SEP1, Cat::NUMBER, "#,##0.0", 1, true, "", false);
    formats_[BuiltInFormats::NUMBER_SEP2] =
        NumberFormat(BuiltInFormats::NUMBER_SEP2, Cat::NUMBER, "#,##0.00", 2, true, "", false);
    formats_[BuiltInFormats::NUMBER_SEP3] =
        NumberFormat(BuiltInFormats::NUMBER_SEP3, Cat::NUMBER, "#,##0.000", 3, true, "", false);
    formats_[BuiltInFormats::NUMBER_SEP4] =
        NumberFormat(BuiltInFormats::NUMBER_SEP4, Cat::NUMBER, "#,##0.0000", 4, true, "", false);

    // Currency formats - USD (legacy IDs for backward compatibility)
    formats_[BuiltInFormats::CURRENCY_0] =
        NumberFormat(BuiltInFormats::CURRENCY_0, Cat::CURRENCY, "$#,##0", 0, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_1] =
        NumberFormat(BuiltInFormats::CURRENCY_1, Cat::CURRENCY, "$#,##0.0", 1, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_2] =
        NumberFormat(BuiltInFormats::CURRENCY_2, Cat::CURRENCY, "$#,##0.00", 2, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_3] =
        NumberFormat(BuiltInFormats::CURRENCY_3, Cat::CURRENCY, "$#,##0.000", 3, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_4] =
        NumberFormat(BuiltInFormats::CURRENCY_4, Cat::CURRENCY, "$#,##0.0000", 4, true, "$", false);

    // Currency formats - USD (new naming scheme)
    formats_[BuiltInFormats::CURRENCY_USD_0] =
        NumberFormat(BuiltInFormats::CURRENCY_USD_0, Cat::CURRENCY, "$#,##0", 0, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_USD_1] = NumberFormat(
        BuiltInFormats::CURRENCY_USD_1, Cat::CURRENCY, "$#,##0.0", 1, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_USD_2] = NumberFormat(
        BuiltInFormats::CURRENCY_USD_2, Cat::CURRENCY, "$#,##0.00", 2, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_USD_3] = NumberFormat(
        BuiltInFormats::CURRENCY_USD_3, Cat::CURRENCY, "$#,##0.000", 3, true, "$", false);
    formats_[BuiltInFormats::CURRENCY_USD_4] = NumberFormat(
        BuiltInFormats::CURRENCY_USD_4, Cat::CURRENCY, "$#,##0.0000", 4, true, "$", false);

    // Currency formats - EUR
    formats_[BuiltInFormats::CURRENCY_EUR_0] =
        NumberFormat(BuiltInFormats::CURRENCY_EUR_0, Cat::CURRENCY, "€#,##0", 0, true, "€", false);
    formats_[BuiltInFormats::CURRENCY_EUR_1] = NumberFormat(
        BuiltInFormats::CURRENCY_EUR_1, Cat::CURRENCY, "€#,##0.0", 1, true, "€", false);
    formats_[BuiltInFormats::CURRENCY_EUR_2] = NumberFormat(
        BuiltInFormats::CURRENCY_EUR_2, Cat::CURRENCY, "€#,##0.00", 2, true, "€", false);
    formats_[BuiltInFormats::CURRENCY_EUR_3] = NumberFormat(
        BuiltInFormats::CURRENCY_EUR_3, Cat::CURRENCY, "€#,##0.000", 3, true, "€", false);
    formats_[BuiltInFormats::CURRENCY_EUR_4] = NumberFormat(
        BuiltInFormats::CURRENCY_EUR_4, Cat::CURRENCY, "€#,##0.0000", 4, true, "€", false);

    // Currency formats - GBP
    formats_[BuiltInFormats::CURRENCY_GBP_0] =
        NumberFormat(BuiltInFormats::CURRENCY_GBP_0, Cat::CURRENCY, "£#,##0", 0, true, "£", false);
    formats_[BuiltInFormats::CURRENCY_GBP_1] = NumberFormat(
        BuiltInFormats::CURRENCY_GBP_1, Cat::CURRENCY, "£#,##0.0", 1, true, "£", false);
    formats_[BuiltInFormats::CURRENCY_GBP_2] = NumberFormat(
        BuiltInFormats::CURRENCY_GBP_2, Cat::CURRENCY, "£#,##0.00", 2, true, "£", false);
    formats_[BuiltInFormats::CURRENCY_GBP_3] = NumberFormat(
        BuiltInFormats::CURRENCY_GBP_3, Cat::CURRENCY, "£#,##0.000", 3, true, "£", false);
    formats_[BuiltInFormats::CURRENCY_GBP_4] = NumberFormat(
        BuiltInFormats::CURRENCY_GBP_4, Cat::CURRENCY, "£#,##0.0000", 4, true, "£", false);

    // Currency formats - JPY (typically 0 decimals for yen)
    formats_[BuiltInFormats::CURRENCY_JPY_0] =
        NumberFormat(BuiltInFormats::CURRENCY_JPY_0, Cat::CURRENCY, "¥#,##0", 0, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_JPY_1] = NumberFormat(
        BuiltInFormats::CURRENCY_JPY_1, Cat::CURRENCY, "¥#,##0.0", 1, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_JPY_2] = NumberFormat(
        BuiltInFormats::CURRENCY_JPY_2, Cat::CURRENCY, "¥#,##0.00", 2, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_JPY_3] = NumberFormat(
        BuiltInFormats::CURRENCY_JPY_3, Cat::CURRENCY, "¥#,##0.000", 3, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_JPY_4] = NumberFormat(
        BuiltInFormats::CURRENCY_JPY_4, Cat::CURRENCY, "¥#,##0.0000", 4, true, "¥", false);

    // Currency formats - CNY (Chinese Yuan, uses same yen symbol)
    formats_[BuiltInFormats::CURRENCY_CNY_0] =
        NumberFormat(BuiltInFormats::CURRENCY_CNY_0, Cat::CURRENCY, "¥#,##0", 0, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_CNY_1] = NumberFormat(
        BuiltInFormats::CURRENCY_CNY_1, Cat::CURRENCY, "¥#,##0.0", 1, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_CNY_2] = NumberFormat(
        BuiltInFormats::CURRENCY_CNY_2, Cat::CURRENCY, "¥#,##0.00", 2, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_CNY_3] = NumberFormat(
        BuiltInFormats::CURRENCY_CNY_3, Cat::CURRENCY, "¥#,##0.000", 3, true, "¥", false);
    formats_[BuiltInFormats::CURRENCY_CNY_4] = NumberFormat(
        BuiltInFormats::CURRENCY_CNY_4, Cat::CURRENCY, "¥#,##0.0000", 4, true, "¥", false);

    // Accounting formats
    formats_[BuiltInFormats::ACCOUNTING_0] = NumberFormat(
        BuiltInFormats::ACCOUNTING_0, Cat::ACCOUNTING, "_($* #,##0_)", 0, true, "$", true);
    formats_[BuiltInFormats::ACCOUNTING_2] = NumberFormat(
        BuiltInFormats::ACCOUNTING_2, Cat::ACCOUNTING, "_($* #,##0.00_)", 2, true, "$", true);

    // Percentage formats (0-4 decimal places)
    formats_[BuiltInFormats::PERCENTAGE_0] =
        NumberFormat(BuiltInFormats::PERCENTAGE_0, Cat::PERCENTAGE, "0%", 0, false, "", false);
    formats_[BuiltInFormats::PERCENTAGE_1] =
        NumberFormat(BuiltInFormats::PERCENTAGE_1, Cat::PERCENTAGE, "0.0%", 1, false, "", false);
    formats_[BuiltInFormats::PERCENTAGE_2] =
        NumberFormat(BuiltInFormats::PERCENTAGE_2, Cat::PERCENTAGE, "0.00%", 2, false, "", false);
    formats_[BuiltInFormats::PERCENTAGE_3] =
        NumberFormat(BuiltInFormats::PERCENTAGE_3, Cat::PERCENTAGE, "0.000%", 3, false, "", false);
    formats_[BuiltInFormats::PERCENTAGE_4] =
        NumberFormat(BuiltInFormats::PERCENTAGE_4, Cat::PERCENTAGE, "0.0000%", 4, false, "", false);

    // Date formats
    formats_[BuiltInFormats::DATE_SHORT] =
        NumberFormat(BuiltInFormats::DATE_SHORT, Cat::DATE, "m/d/yyyy", 0, false, "", false);
    formats_[BuiltInFormats::DATE_LONG] =
        NumberFormat(BuiltInFormats::DATE_LONG, Cat::DATE, "mmmm d, yyyy", 0, false, "", false);
    formats_[BuiltInFormats::DATE_ISO] =
        NumberFormat(BuiltInFormats::DATE_ISO, Cat::DATE, "yyyy-mm-dd", 0, false, "", false);

    // Time formats
    formats_[BuiltInFormats::TIME_12H] =
        NumberFormat(BuiltInFormats::TIME_12H, Cat::TIME, "h:mm AM/PM", 0, false, "", false);
    formats_[BuiltInFormats::TIME_24H] =
        NumberFormat(BuiltInFormats::TIME_24H, Cat::TIME, "h:mm", 0, false, "", false);

    // DateTime formats
    formats_[BuiltInFormats::DATETIME_SHORT] = NumberFormat(
        BuiltInFormats::DATETIME_SHORT, Cat::DATE_TIME, "m/d/yyyy h:mm AM/PM", 0, false, "", false);

    // Scientific
    formats_[BuiltInFormats::SCIENTIFIC_2] = NumberFormat(
        BuiltInFormats::SCIENTIFIC_2, Cat::SCIENTIFIC, "0.00E+00", 2, false, "", false);

    // Text
    formats_[BuiltInFormats::TEXT] =
        NumberFormat(BuiltInFormats::TEXT, Cat::TEXT, "@", 0, false, "", false);
}

const NumberFormat* NumberFormatRegistry::findByFormatCode(const std::string& formatCode) const {
    for (const auto& pair : formats_) {
        if (pair.second.formatCode == formatCode) {
            return &pair.second;
        }
    }
    return nullptr;
}

CreateCustomFormatResult NumberFormatRegistry::createCustomFormat(const std::string& formatCode) {
    // Validate the format code
    auto validationError = validateFormatCode(formatCode);
    if (validationError) {
        return CreateCustomFormatResult::error(*validationError);
    }

    // Check if an identical format code already exists
    const NumberFormat* existing = findByFormatCode(formatCode);
    if (existing != nullptr) {
        // Return the existing format's ID
        return CreateCustomFormatResult::ok(existing->id);
    }

    // Parse the format code to extract properties
    const ParsedFormatCode parsed = parseFormatCode(formatCode);
    if (!parsed.valid) {
        return CreateCustomFormatResult::error("Failed to parse format code: " +
                                               parsed.errorMessage);
    }

    // Determine the category based on the parsed format code
    NumberFormatCategory category = NumberFormatCategory::NUMBER;
    if (parsed.hasPercent) {
        category = NumberFormatCategory::PERCENTAGE;
    } else if (!parsed.currencySymbol.empty()) {
        category = NumberFormatCategory::CURRENCY;
    }

    // Generate a new unique ID
    const ID newId = generate_id();

    // Create the custom format
    NumberFormat customFormat;
    customFormat.id = newId;
    customFormat.category = category;
    customFormat.formatCode = formatCode;
    customFormat.decimalPlaces = parsed.decimalPlaces;
    customFormat.useThousandsSeparator = parsed.hasThousandsSeparator;
    customFormat.currencySymbol = parsed.currencySymbol;
    customFormat.isAccounting = false;
    customFormat.isCustom = true;

    // Register the format
    formats_[newId] = customFormat;

    return CreateCustomFormatResult::ok(newId);
}

}  // namespace cells
