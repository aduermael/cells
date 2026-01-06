#include "core/cells/number_format.h"

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
      isAccounting(false) {}

NumberFormat::NumberFormat(const ID& id, NumberFormatCategory category, std::string formatCode,
                           uint8_t decimalPlaces, bool useThousandsSeparator,
                           std::string currencySymbol, bool isAccounting)
    : id(id),
      category(category),
      formatCode(std::move(formatCode)),
      decimalPlaces(decimalPlaces),
      useThousandsSeparator(useThousandsSeparator),
      currencySymbol(std::move(currencySymbol)),
      isAccounting(isAccounting) {}

bool NumberFormat::operator==(const NumberFormat& other) const {
    return id == other.id && category == other.category && formatCode == other.formatCode &&
           decimalPlaces == other.decimalPlaces &&
           useThousandsSeparator == other.useThousandsSeparator &&
           currencySymbol == other.currencySymbol && isAccounting == other.isAccounting;
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

const ID CURRENCY_0("FMT_C000");
const ID CURRENCY_1("FMT_C001");
const ID CURRENCY_2("FMT_C002");
const ID CURRENCY_3("FMT_C003");
const ID CURRENCY_4("FMT_C004");

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

    // Currency formats (0-4 decimal places)
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

}  // namespace cells
