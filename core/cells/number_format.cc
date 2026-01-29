#include "core/cells/number_format.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <vector>

#include "core/cells/format_code_parser.h"
#include "core/cells/formula_ast.h"
#include "core/cells/id.h"

namespace cells {

// --- Category string conversion ---

const char* formatCategoryToString(NumberFormatCategory category) {
    switch (category) {
        case NumberFormatCategory::GENERAL:
            return "GENERAL";
        case NumberFormatCategory::NUMBER:
            return "NUMBER";
        case NumberFormatCategory::CURRENCY:
            return "CURRENCY";
        case NumberFormatCategory::ACCOUNTING:
            return "ACCOUNTING";
        case NumberFormatCategory::PERCENTAGE:
            return "PERCENTAGE";
        case NumberFormatCategory::DATE:
            return "DATE";
        case NumberFormatCategory::TIME:
            return "TIME";
        case NumberFormatCategory::DATE_TIME:
            return "DATE_TIME";
        case NumberFormatCategory::SCIENTIFIC:
            return "SCIENTIFIC";
        case NumberFormatCategory::FRACTION:
            return "FRACTION";
        case NumberFormatCategory::TEXT:
            return "TEXT";
        case NumberFormatCategory::CUSTOM:
            return "CUSTOM";
    }
    return "GENERAL";
}

NumberFormatCategory stringToFormatCategory(const std::string& str) {
    if (str == "general" || str == "GENERAL") {
        return NumberFormatCategory::GENERAL;
    }
    if (str == "number" || str == "NUMBER") {
        return NumberFormatCategory::NUMBER;
    }
    if (str == "currency" || str == "CURRENCY") {
        return NumberFormatCategory::CURRENCY;
    }
    if (str == "accounting" || str == "ACCOUNTING") {
        return NumberFormatCategory::ACCOUNTING;
    }
    if (str == "percentage" || str == "PERCENTAGE") {
        return NumberFormatCategory::PERCENTAGE;
    }
    if (str == "date" || str == "DATE") {
        return NumberFormatCategory::DATE;
    }
    if (str == "time" || str == "TIME") {
        return NumberFormatCategory::TIME;
    }
    if (str == "datetime" || str == "DATE_TIME") {
        return NumberFormatCategory::DATE_TIME;
    }
    if (str == "scientific" || str == "SCIENTIFIC") {
        return NumberFormatCategory::SCIENTIFIC;
    }
    if (str == "fraction" || str == "FRACTION") {
        return NumberFormatCategory::FRACTION;
    }
    if (str == "text" || str == "TEXT") {
        return NumberFormatCategory::TEXT;
    }
    if (str == "custom" || str == "CUSTOM") {
        return NumberFormatCategory::CUSTOM;
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

// USD currency formats (8 chars: CUSD_0XX)
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

std::string getFormatDetails(const std::string& formatId) {
    // Handle special cases
    if (formatId.empty() || formatId == "~" || formatId == "FMT_GEN0") {
        return R"({"category":"GENERAL","decimals":0,"separator":false,"currency":null})";
    }

    // Try to parse as a dynamic format ID
    const ParsedFormatId parsed = parseFormatId(formatId);
    if (parsed.valid) {
        const std::string categoryStr = formatCategoryToString(parsed.category);
        const std::string currencyJson =
            parsed.currencyCode.empty() ? "null" : "\"" + parsed.currencyCode + "\"";
        return "{\"category\":\"" + categoryStr +
               "\",\"decimals\":" + std::to_string(parsed.decimalPlaces) +
               ",\"separator\":" + (parsed.useThousandsSeparator ? "true" : "false") +
               ",\"currency\":" + currencyJson + "}";
    }

    // Handle non-parseable built-in formats
    if (formatId == "FMT_A000" || formatId == "FMT_A002") {
        const int decimals = (formatId == "FMT_A002") ? 2 : 0;
        return "{\"category\":\"ACCOUNTING\",\"decimals\":" + std::to_string(decimals) +
               ",\"separator\":true,\"currency\":\"USD\"}";
    }
    if (formatId == "FMT_DSHT" || formatId == "FMT_DLNG" || formatId == "FMT_DISO") {
        return R"({"category":"DATE","decimals":0,"separator":false,"currency":null})";
    }
    if (formatId == "FMT_T12H" || formatId == "FMT_T24H") {
        return R"({"category":"TIME","decimals":0,"separator":false,"currency":null})";
    }
    if (formatId == "FMT_DTSH") {
        return R"({"category":"DATE_TIME","decimals":0,"separator":false,"currency":null})";
    }
    if (formatId == "FMT_SCI2") {
        return R"({"category":"SCIENTIFIC","decimals":2,"separator":false,"currency":null})";
    }
    if (formatId == "FMT_TEXT") {
        return R"({"category":"TEXT","decimals":0,"separator":false,"currency":null})";
    }

    return R"({"error":"Unknown format"})";
}

std::string makeFormatId(const std::string& category, int decimals, bool separator,
                         const std::string& currency) {
    // Validate decimals range
    if (decimals < 0 || decimals > 15) {
        return "";
    }

    // Format decimals as 2-digit string
    char decStr[3];
    std::snprintf(decStr, sizeof(decStr), "%02d", decimals);

    if (category == "percentage") {
        // FMT_P0XX
        return std::string("FMT_P0") + decStr;
    }

    if (category == "number") {
        if (separator) {
            // FMT_NSXX
            return std::string("FMT_NS") + decStr;
        }
        // FMT_N0XX
        return std::string("FMT_N0") + decStr;
    }

    if (category == "currency") {
        // Validate currency code
        if (currency.size() != 3) {
            return "";
        }
        for (const char c : currency) {
            if (std::isupper(static_cast<unsigned char>(c)) == 0) {
                return "";
            }
        }
        // CXXX_0YY
        return "C" + currency + "_0" + decStr;
    }

    // Other categories don't have dynamic format IDs
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
        !(id[6] == 'S')) {  // Distinguish from FMT_NSXX
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

    // Pattern: FMT_NSXX (number with separator, XX decimal places)
    // Example: FMT_NS05 = 5 decimal places, FMT_NS12 = 12 decimal places with separator
    if (id.size() == 8 && id.substr(0, 6) == "FMT_NS") {
        if (std::isdigit(static_cast<unsigned char>(id[6])) != 0 &&
            std::isdigit(static_cast<unsigned char>(id[7])) != 0) {
            const int decimals = (id[6] - '0') * 10 + (id[7] - '0');
            if (decimals <= 15) {
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

const NumberFormat* NumberFormatRegistry::getOrCreateFormat(const ID& id) {
    // First, check if already cached
    auto it = formats_.find(id);
    if (it != formats_.end()) {
        return &it->second;
    }

    // Try to parse as a dynamic format ID
    const std::string idStr = id.toString();
    const ParsedFormatId parsed = parseFormatId(idStr);

    if (!parsed.valid) {
        return nullptr;
    }

    // Create and cache the format
    NumberFormat format;
    format.id = id;
    format.category = parsed.category;
    format.decimalPlaces = parsed.decimalPlaces;
    format.useThousandsSeparator = parsed.useThousandsSeparator;
    format.currencySymbol = parsed.currencySymbol;
    format.formatCode = generateFormatCode(parsed);
    format.isAccounting = false;
    format.isCustom = false;

    formats_[id] = format;
    return &formats_[id];
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

    // General (default) - not parseable, must be registered manually
    formats_[BuiltInFormats::GENERAL] =
        NumberFormat(BuiltInFormats::GENERAL, Cat::GENERAL, "General", 0, false, "", false);

    // Number formats (0-4 decimal places) - use dynamic system
    // FMT_N0XX pattern: number without separator
    getOrCreateFormat(BuiltInFormats::NUMBER_0);
    getOrCreateFormat(BuiltInFormats::NUMBER_1);
    getOrCreateFormat(BuiltInFormats::NUMBER_2);
    getOrCreateFormat(BuiltInFormats::NUMBER_3);
    getOrCreateFormat(BuiltInFormats::NUMBER_4);

    // FMT_NSXX pattern: number with separator
    getOrCreateFormat(BuiltInFormats::NUMBER_SEP);
    getOrCreateFormat(BuiltInFormats::NUMBER_SEP1);
    getOrCreateFormat(BuiltInFormats::NUMBER_SEP2);
    getOrCreateFormat(BuiltInFormats::NUMBER_SEP3);
    getOrCreateFormat(BuiltInFormats::NUMBER_SEP4);

    // Currency formats - USD (CXXX_0YY pattern) - use dynamic system
    getOrCreateFormat(BuiltInFormats::CURRENCY_USD_0);
    getOrCreateFormat(BuiltInFormats::CURRENCY_USD_1);
    getOrCreateFormat(BuiltInFormats::CURRENCY_USD_2);
    getOrCreateFormat(BuiltInFormats::CURRENCY_USD_3);
    getOrCreateFormat(BuiltInFormats::CURRENCY_USD_4);

    // Currency formats - EUR
    getOrCreateFormat(BuiltInFormats::CURRENCY_EUR_0);
    getOrCreateFormat(BuiltInFormats::CURRENCY_EUR_1);
    getOrCreateFormat(BuiltInFormats::CURRENCY_EUR_2);
    getOrCreateFormat(BuiltInFormats::CURRENCY_EUR_3);
    getOrCreateFormat(BuiltInFormats::CURRENCY_EUR_4);

    // Currency formats - GBP
    getOrCreateFormat(BuiltInFormats::CURRENCY_GBP_0);
    getOrCreateFormat(BuiltInFormats::CURRENCY_GBP_1);
    getOrCreateFormat(BuiltInFormats::CURRENCY_GBP_2);
    getOrCreateFormat(BuiltInFormats::CURRENCY_GBP_3);
    getOrCreateFormat(BuiltInFormats::CURRENCY_GBP_4);

    // Currency formats - JPY
    getOrCreateFormat(BuiltInFormats::CURRENCY_JPY_0);
    getOrCreateFormat(BuiltInFormats::CURRENCY_JPY_1);
    getOrCreateFormat(BuiltInFormats::CURRENCY_JPY_2);
    getOrCreateFormat(BuiltInFormats::CURRENCY_JPY_3);
    getOrCreateFormat(BuiltInFormats::CURRENCY_JPY_4);

    // Currency formats - CNY
    getOrCreateFormat(BuiltInFormats::CURRENCY_CNY_0);
    getOrCreateFormat(BuiltInFormats::CURRENCY_CNY_1);
    getOrCreateFormat(BuiltInFormats::CURRENCY_CNY_2);
    getOrCreateFormat(BuiltInFormats::CURRENCY_CNY_3);
    getOrCreateFormat(BuiltInFormats::CURRENCY_CNY_4);

    // Accounting formats - not parseable, must be registered manually
    formats_[BuiltInFormats::ACCOUNTING_0] = NumberFormat(
        BuiltInFormats::ACCOUNTING_0, Cat::ACCOUNTING, "_($* #,##0_)", 0, true, "$", true);
    formats_[BuiltInFormats::ACCOUNTING_2] = NumberFormat(
        BuiltInFormats::ACCOUNTING_2, Cat::ACCOUNTING, "_($* #,##0.00_)", 2, true, "$", true);

    // Percentage formats (0-4 decimal places) - use dynamic system (FMT_P0XX pattern)
    getOrCreateFormat(BuiltInFormats::PERCENTAGE_0);
    getOrCreateFormat(BuiltInFormats::PERCENTAGE_1);
    getOrCreateFormat(BuiltInFormats::PERCENTAGE_2);
    getOrCreateFormat(BuiltInFormats::PERCENTAGE_3);
    getOrCreateFormat(BuiltInFormats::PERCENTAGE_4);

    // Date formats - not parseable, must be registered manually
    formats_[BuiltInFormats::DATE_SHORT] =
        NumberFormat(BuiltInFormats::DATE_SHORT, Cat::DATE, "m/d/yyyy", 0, false, "", false);
    formats_[BuiltInFormats::DATE_LONG] =
        NumberFormat(BuiltInFormats::DATE_LONG, Cat::DATE, "mmmm d, yyyy", 0, false, "", false);
    formats_[BuiltInFormats::DATE_ISO] =
        NumberFormat(BuiltInFormats::DATE_ISO, Cat::DATE, "yyyy-mm-dd", 0, false, "", false);

    // Time formats - not parseable, must be registered manually
    formats_[BuiltInFormats::TIME_12H] =
        NumberFormat(BuiltInFormats::TIME_12H, Cat::TIME, "h:mm AM/PM", 0, false, "", false);
    formats_[BuiltInFormats::TIME_24H] =
        NumberFormat(BuiltInFormats::TIME_24H, Cat::TIME, "h:mm", 0, false, "", false);

    // DateTime formats - not parseable, must be registered manually
    formats_[BuiltInFormats::DATETIME_SHORT] = NumberFormat(
        BuiltInFormats::DATETIME_SHORT, Cat::DATE_TIME, "m/d/yyyy h:mm AM/PM", 0, false, "", false);

    // Scientific - not parseable, must be registered manually
    formats_[BuiltInFormats::SCIENTIFIC_2] = NumberFormat(
        BuiltInFormats::SCIENTIFIC_2, Cat::SCIENTIFIC, "0.00E+00", 2, false, "", false);

    // Text - not parseable, must be registered manually
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

// --- Format Inheritance ---

int getFormatPriority(NumberFormatCategory category) {
    switch (category) {
        case NumberFormatCategory::DATE:
        case NumberFormatCategory::TIME:
        case NumberFormatCategory::DATE_TIME:
            return 100;  // Highest priority - dates are special

        case NumberFormatCategory::CURRENCY:
        case NumberFormatCategory::ACCOUNTING:
            return 80;  // Financial formats

        case NumberFormatCategory::PERCENTAGE:
            return 60;

        case NumberFormatCategory::NUMBER:
            return 40;

        case NumberFormatCategory::SCIENTIFIC:
            return 30;

        case NumberFormatCategory::FRACTION:
            return 20;

        case NumberFormatCategory::GENERAL:
        case NumberFormatCategory::TEXT:
        default:
            return 0;  // Lowest priority - no format
    }
}

namespace {

// Helper to collect all cell references from an AST
void collectCellRefs(const ASTNode* node, std::vector<std::string>& cellIds) {
    if (node == nullptr) {
        return;
    }

    switch (node->type) {
        case ASTNodeType::CELL_REF: {
            const auto* cellRef = static_cast<const CellRefNode*>(node);
            if (!cellRef->cellId.empty()) {
                cellIds.push_back(cellRef->cellId);
            }
            break;
        }

        case ASTNodeType::RANGE_REF: {
            // For ranges, we just collect the corner cells
            // The format of the first cell in a range determines the format
            const auto* rangeRef = static_cast<const RangeRefNode*>(node);
            if (rangeRef->topLeft != nullptr && !rangeRef->topLeft->cellId.empty()) {
                cellIds.push_back(rangeRef->topLeft->cellId);
            }
            if (rangeRef->bottomRight != nullptr && !rangeRef->bottomRight->cellId.empty()) {
                cellIds.push_back(rangeRef->bottomRight->cellId);
            }
            break;
        }

        case ASTNodeType::BINARY_OP: {
            const auto* binOp = static_cast<const BinaryOpNode*>(node);
            collectCellRefs(binOp->left.get(), cellIds);
            collectCellRefs(binOp->right.get(), cellIds);
            break;
        }

        case ASTNodeType::UNARY_OP: {
            const auto* unaryOp = static_cast<const UnaryOpNode*>(node);
            collectCellRefs(unaryOp->operand.get(), cellIds);
            break;
        }

        case ASTNodeType::FUNCTION_CALL: {
            const auto* funcCall = static_cast<const FunctionCallNode*>(node);
            for (const auto& arg : funcCall->args) {
                collectCellRefs(arg.get(), cellIds);
            }
            break;
        }

        // Literals don't contribute to format inheritance
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
        case ASTNodeType::NAMED_REF:
        case ASTNodeType::COLUMN_REF:
        case ASTNodeType::ROW_REF:
        case ASTNodeType::COLUMN_RANGE_REF:
        case ASTNodeType::ROW_RANGE_REF:
        case ASTNodeType::ERROR_NODE:
            break;
    }
}

// Compare two format IDs: returns true if candidate is "more specific" than current
// More specific means: higher priority category, or same category with more decimals
bool isMoreSpecific(const std::string& candidateId, const std::string& currentId) {
    if (candidateId.empty() || candidateId == "~" || candidateId == "FMT_GEN0") {
        return false;  // GENERAL is never more specific
    }
    if (currentId.empty() || currentId == "~" || currentId == "FMT_GEN0") {
        return true;  // Anything is more specific than GENERAL
    }

    // Parse both format IDs
    const ParsedFormatId candidate = parseFormatId(candidateId);
    const ParsedFormatId current = parseFormatId(currentId);

    // If either fails to parse, fall back to built-in handling
    if (!candidate.valid && !current.valid) {
        // Both unparseable - compare by string (arbitrary but stable)
        return candidateId > currentId;
    }
    if (!candidate.valid) {
        // Candidate is unparseable (custom format?) - it wins if current is basic
        return getFormatPriority(current.category) < 50;  // Custom formats beat basic NUMBER
    }
    if (!current.valid) {
        // Current is unparseable - candidate must have higher priority to win
        return getFormatPriority(candidate.category) >= 50;
    }

    // Both parsed successfully - compare by priority
    const int candidatePriority = getFormatPriority(candidate.category);
    const int currentPriority = getFormatPriority(current.category);

    if (candidatePriority > currentPriority) {
        return true;
    }
    if (candidatePriority < currentPriority) {
        return false;
    }

    // Same priority (likely same category) - compare specificity
    // More decimal places = more specific
    if (candidate.decimalPlaces > current.decimalPlaces) {
        return true;
    }
    if (candidate.decimalPlaces < current.decimalPlaces) {
        return false;
    }

    // Same decimals - prefer separator over no separator
    if (candidate.useThousandsSeparator && !current.useThousandsSeparator) {
        return true;
    }

    return false;  // Current wins (or tie)
}

}  // namespace

std::string inferFormatFromFormula(const ASTNode* ast, const FormatLookup& formatLookup) {
    if (ast == nullptr) {
        return "";  // No inference possible
    }

    // Collect all cell IDs referenced in the formula
    std::vector<std::string> cellIds;
    collectCellRefs(ast, cellIds);

    if (cellIds.empty()) {
        return "";  // No cell references - no format to inherit
    }

    // Find the "winning" format among all referenced cells
    std::string winningFormatId;

    for (const auto& cellIdStr : cellIds) {
        const std::string cellFormatId = formatLookup(cellIdStr);

        // Skip cells with GENERAL format
        if (cellFormatId.empty() || cellFormatId == "~" || cellFormatId == "FMT_GEN0") {
            continue;
        }

        // Check if this cell's format is more specific than current winner
        if (winningFormatId.empty() || isMoreSpecific(cellFormatId, winningFormatId)) {
            winningFormatId = cellFormatId;
        }
    }

    return winningFormatId;
}

}  // namespace cells
