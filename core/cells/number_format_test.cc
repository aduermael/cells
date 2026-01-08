#include "core/cells/number_format.h"

#include <gtest/gtest.h>

#include "core/cells/formula_ast.h"

namespace cells {
namespace {

TEST(NumberFormatCategoryTest, ToString) {
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::GENERAL), "general");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::NUMBER), "number");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::CURRENCY), "currency");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::ACCOUNTING), "accounting");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::PERCENTAGE), "percentage");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::DATE), "date");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::TIME), "time");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::DATE_TIME), "datetime");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::SCIENTIFIC), "scientific");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::FRACTION), "fraction");
    EXPECT_STREQ(formatCategoryToString(NumberFormatCategory::TEXT), "text");
}

TEST(NumberFormatCategoryTest, FromString) {
    EXPECT_EQ(stringToFormatCategory("general"), NumberFormatCategory::GENERAL);
    EXPECT_EQ(stringToFormatCategory("number"), NumberFormatCategory::NUMBER);
    EXPECT_EQ(stringToFormatCategory("currency"), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(stringToFormatCategory("accounting"), NumberFormatCategory::ACCOUNTING);
    EXPECT_EQ(stringToFormatCategory("percentage"), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(stringToFormatCategory("date"), NumberFormatCategory::DATE);
    EXPECT_EQ(stringToFormatCategory("time"), NumberFormatCategory::TIME);
    EXPECT_EQ(stringToFormatCategory("datetime"), NumberFormatCategory::DATE_TIME);
    EXPECT_EQ(stringToFormatCategory("scientific"), NumberFormatCategory::SCIENTIFIC);
    EXPECT_EQ(stringToFormatCategory("fraction"), NumberFormatCategory::FRACTION);
    EXPECT_EQ(stringToFormatCategory("text"), NumberFormatCategory::TEXT);

    // Unknown string defaults to GENERAL
    EXPECT_EQ(stringToFormatCategory("unknown"), NumberFormatCategory::GENERAL);
    EXPECT_EQ(stringToFormatCategory(""), NumberFormatCategory::GENERAL);
}

TEST(NumberFormatTest, DefaultConstructor) {
    NumberFormat fmt;
    EXPECT_TRUE(fmt.id.isNull());
    EXPECT_EQ(fmt.category, NumberFormatCategory::GENERAL);
    EXPECT_EQ(fmt.formatCode, "General");
    EXPECT_EQ(fmt.decimalPlaces, 0);
    EXPECT_FALSE(fmt.useThousandsSeparator);
    EXPECT_TRUE(fmt.currencySymbol.empty());
    EXPECT_FALSE(fmt.isAccounting);
}

TEST(NumberFormatTest, ParameterizedConstructor) {
    ID id("TEST1234");
    NumberFormat fmt(id, NumberFormatCategory::CURRENCY, "$#,##0.00", 2, true, "$", false);

    EXPECT_EQ(fmt.id, id);
    EXPECT_EQ(fmt.category, NumberFormatCategory::CURRENCY);
    EXPECT_EQ(fmt.formatCode, "$#,##0.00");
    EXPECT_EQ(fmt.decimalPlaces, 2);
    EXPECT_TRUE(fmt.useThousandsSeparator);
    EXPECT_EQ(fmt.currencySymbol, "$");
    EXPECT_FALSE(fmt.isAccounting);
}

TEST(NumberFormatTest, Equality) {
    ID id("TEST1234");
    NumberFormat fmt1(id, NumberFormatCategory::CURRENCY, "$#,##0.00", 2, true, "$", false);
    NumberFormat fmt2(id, NumberFormatCategory::CURRENCY, "$#,##0.00", 2, true, "$", false);
    NumberFormat fmt3(id, NumberFormatCategory::PERCENTAGE, "$#,##0.00", 2, true, "$", false);

    EXPECT_EQ(fmt1, fmt2);
    EXPECT_NE(fmt1, fmt3);
}

TEST(NumberFormatRegistryTest, BuiltInFormatsExist) {
    NumberFormatRegistry registry;

    // Check that all built-in formats exist
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::GENERAL));

    // Number formats (0-4 decimal places)
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_0));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_1));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_2));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_3));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_4));

    // Number with separator formats (0-4 decimal places)
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_SEP));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_SEP1));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_SEP2));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_SEP3));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::NUMBER_SEP4));

    // Currency formats (0-4 decimal places)
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::CURRENCY_0));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::CURRENCY_1));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::CURRENCY_2));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::CURRENCY_3));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::CURRENCY_4));

    // Accounting formats
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::ACCOUNTING_0));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::ACCOUNTING_2));

    // Percentage formats (0-4 decimal places)
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::PERCENTAGE_0));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::PERCENTAGE_1));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::PERCENTAGE_2));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::PERCENTAGE_3));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::PERCENTAGE_4));

    // Date formats
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::DATE_SHORT));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::DATE_LONG));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::DATE_ISO));

    // Time formats
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::TIME_12H));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::TIME_24H));

    // DateTime formats
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::DATETIME_SHORT));

    // Other formats
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::SCIENTIFIC_2));
    EXPECT_TRUE(registry.hasFormat(BuiltInFormats::TEXT));
}

TEST(NumberFormatRegistryTest, GetDefaultFormat) {
    NumberFormatRegistry registry;
    const NumberFormat* defaultFmt = registry.getDefaultFormat();

    ASSERT_NE(defaultFmt, nullptr);
    EXPECT_EQ(defaultFmt->id, BuiltInFormats::GENERAL);
    EXPECT_EQ(defaultFmt->category, NumberFormatCategory::GENERAL);
}

TEST(NumberFormatRegistryTest, GetFormat) {
    NumberFormatRegistry registry;

    const NumberFormat* currencyFmt = registry.getFormat(BuiltInFormats::CURRENCY_2);
    ASSERT_NE(currencyFmt, nullptr);
    EXPECT_EQ(currencyFmt->category, NumberFormatCategory::CURRENCY);
    EXPECT_EQ(currencyFmt->decimalPlaces, 2);
    EXPECT_EQ(currencyFmt->currencySymbol, "$");

    // Non-existent format
    ID nonExistent("NOTFOUND");
    EXPECT_EQ(registry.getFormat(nonExistent), nullptr);
}

TEST(NumberFormatRegistryTest, RegisterCustomFormat) {
    NumberFormatRegistry registry;

    ID customId("CUSTOM01");
    NumberFormat customFmt(customId, NumberFormatCategory::NUMBER, "0.000", 3, false, "", false);

    EXPECT_TRUE(registry.registerFormat(customFmt));
    EXPECT_TRUE(registry.hasFormat(customId));

    const NumberFormat* retrieved = registry.getFormat(customId);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->decimalPlaces, 3);
}

TEST(NumberFormatRegistryTest, CannotRegisterDuplicate) {
    NumberFormatRegistry registry;

    // Try to register a format with an existing ID (built-in)
    NumberFormat duplicate(BuiltInFormats::GENERAL, NumberFormatCategory::NUMBER, "0", 0, false, "",
                           false);

    EXPECT_FALSE(registry.registerFormat(duplicate));
}

TEST(NumberFormatRegistryTest, GetFormatsByCategory) {
    NumberFormatRegistry registry;

    // Currency formats: 5 legacy USD + 5 USD + 5 EUR + 5 GBP + 5 JPY + 5 CNY = 30
    auto currencyFormats = registry.getFormatsByCategory(NumberFormatCategory::CURRENCY);
    EXPECT_EQ(currencyFormats.size(), 30);

    auto dateFormats = registry.getFormatsByCategory(NumberFormatCategory::DATE);
    EXPECT_EQ(dateFormats.size(), 3);  // DATE_SHORT, DATE_LONG, DATE_ISO

    auto percentageFormats = registry.getFormatsByCategory(NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(percentageFormats.size(),
              5);  // PERCENTAGE_0 through PERCENTAGE_4 (0-4 decimal places)
}

TEST(NumberFormatRegistryTest, GetAllFormats) {
    NumberFormatRegistry registry;

    const auto& allFormats = registry.getAllFormats();
    // Should have all built-in formats (56 total)
    // 1 General + 5 Number (0-4 decimals) + 5 Number with separators (0-4 decimals) +
    // 30 Currency (5 legacy + 5 USD + 5 EUR + 5 GBP + 5 JPY + 5 CNY) + 2 Accounting +
    // 5 Percentage (0-4 decimals) + 3 Date + 2 Time + 1 DateTime + 1 Scientific + 1 Text
    EXPECT_EQ(allFormats.size(), 56);
}

TEST(BuiltInFormatsTest, VerifyFormatCodes) {
    NumberFormatRegistry registry;

    // Verify specific format codes are correct
    const NumberFormat* pct = registry.getFormat(BuiltInFormats::PERCENTAGE_0);
    ASSERT_NE(pct, nullptr);
    EXPECT_EQ(pct->formatCode, "0%");

    const NumberFormat* accounting = registry.getFormat(BuiltInFormats::ACCOUNTING_2);
    ASSERT_NE(accounting, nullptr);
    EXPECT_TRUE(accounting->isAccounting);
    EXPECT_EQ(accounting->decimalPlaces, 2);

    const NumberFormat* dateISO = registry.getFormat(BuiltInFormats::DATE_ISO);
    ASSERT_NE(dateISO, nullptr);
    EXPECT_EQ(dateISO->formatCode, "yyyy-mm-dd");
}

// --- Dynamic Format ID Parsing Tests ---

TEST(ParseFormatIdTest, PercentageFormats) {
    // FMT_P0XX - percentage with XX decimal places
    auto parsed = parseFormatId("FMT_P000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.category, NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_FALSE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_P007");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.category, NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(parsed.decimalPlaces, 7);

    parsed = parseFormatId("FMT_P015");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 15);

    // Invalid: too many decimals
    parsed = parseFormatId("FMT_P016");
    EXPECT_FALSE(parsed.valid);

    // Invalid: wrong prefix
    parsed = parseFormatId("FMT_X007");
    EXPECT_FALSE(parsed.valid);
}

TEST(ParseFormatIdTest, NumberFormats) {
    // FMT_N0XX - number with XX decimal places (no separator)
    auto parsed = parseFormatId("FMT_N000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.category, NumberFormatCategory::NUMBER);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_FALSE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_N012");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 12);
    EXPECT_FALSE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_N015");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 15);
}

TEST(ParseFormatIdTest, NumberWithSeparatorFormats) {
    // FMT_NSXX - number with separator, XX decimal places (00-15)
    auto parsed = parseFormatId("FMT_NS00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.category, NumberFormatCategory::NUMBER);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_TRUE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_NS05");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 5);
    EXPECT_TRUE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_NS09");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 9);

    // Extended range (10-15 decimals) now supported
    parsed = parseFormatId("FMT_NS12");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 12);
    EXPECT_TRUE(parsed.useThousandsSeparator);

    parsed = parseFormatId("FMT_NS15");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 15);

    // Invalid: too many decimals (> 15)
    parsed = parseFormatId("FMT_NS16");
    EXPECT_FALSE(parsed.valid);
}

TEST(ParseFormatIdTest, CurrencyFormats) {
    // CXXX_0YY - currency with 3-letter code and YY decimal places
    auto parsed = parseFormatId("CUSD_002");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.category, NumberFormatCategory::CURRENCY);
    EXPECT_EQ(parsed.decimalPlaces, 2);
    EXPECT_TRUE(parsed.useThousandsSeparator);
    EXPECT_EQ(parsed.currencyCode, "USD");
    EXPECT_EQ(parsed.currencySymbol, "$");

    parsed = parseFormatId("CEUR_008");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 8);
    EXPECT_EQ(parsed.currencyCode, "EUR");
    EXPECT_EQ(parsed.currencySymbol, "€");

    parsed = parseFormatId("CGBP_015");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 15);
    EXPECT_EQ(parsed.currencyCode, "GBP");
    EXPECT_EQ(parsed.currencySymbol, "£");

    parsed = parseFormatId("CJPY_000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_EQ(parsed.currencyCode, "JPY");
    EXPECT_EQ(parsed.currencySymbol, "¥");

    parsed = parseFormatId("CCNY_004");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencyCode, "CNY");
    EXPECT_EQ(parsed.currencySymbol, "¥");

    // Invalid: unknown currency
    parsed = parseFormatId("CXYZ_002");
    EXPECT_FALSE(parsed.valid);

    // Invalid: lowercase currency
    parsed = parseFormatId("Cusd_002");
    EXPECT_FALSE(parsed.valid);
}

TEST(ParseFormatIdTest, InvalidFormats) {
    // Too short
    auto parsed = parseFormatId("FMT_P0");
    EXPECT_FALSE(parsed.valid);

    // Empty
    parsed = parseFormatId("");
    EXPECT_FALSE(parsed.valid);

    // Not a recognized pattern
    parsed = parseFormatId("XXXXXXXX");
    EXPECT_FALSE(parsed.valid);

    // Legacy format (not parsed dynamically, should be looked up in registry)
    parsed = parseFormatId("FMT_GEN0");
    EXPECT_FALSE(parsed.valid);
}

TEST(GenerateFormatCodeTest, PercentageFormats) {
    ParsedFormatId parsed;
    parsed.valid = true;
    parsed.category = NumberFormatCategory::PERCENTAGE;

    parsed.decimalPlaces = 0;
    EXPECT_EQ(generateFormatCode(parsed), "0%");

    parsed.decimalPlaces = 2;
    EXPECT_EQ(generateFormatCode(parsed), "0.00%");

    parsed.decimalPlaces = 7;
    EXPECT_EQ(generateFormatCode(parsed), "0.0000000%");

    parsed.decimalPlaces = 15;
    EXPECT_EQ(generateFormatCode(parsed), "0.000000000000000%");
}

TEST(GenerateFormatCodeTest, NumberFormats) {
    ParsedFormatId parsed;
    parsed.valid = true;
    parsed.category = NumberFormatCategory::NUMBER;

    // Without separator
    parsed.useThousandsSeparator = false;
    parsed.decimalPlaces = 0;
    EXPECT_EQ(generateFormatCode(parsed), "0");

    parsed.decimalPlaces = 3;
    EXPECT_EQ(generateFormatCode(parsed), "0.000");

    parsed.decimalPlaces = 12;
    EXPECT_EQ(generateFormatCode(parsed), "0.000000000000");

    // With separator
    parsed.useThousandsSeparator = true;
    parsed.decimalPlaces = 0;
    EXPECT_EQ(generateFormatCode(parsed), "#,##0");

    parsed.decimalPlaces = 5;
    EXPECT_EQ(generateFormatCode(parsed), "#,##0.00000");
}

TEST(GenerateFormatCodeTest, CurrencyFormats) {
    ParsedFormatId parsed;
    parsed.valid = true;
    parsed.category = NumberFormatCategory::CURRENCY;
    parsed.useThousandsSeparator = true;

    parsed.currencySymbol = "$";
    parsed.decimalPlaces = 2;
    EXPECT_EQ(generateFormatCode(parsed), "$#,##0.00");

    parsed.currencySymbol = "€";
    parsed.decimalPlaces = 8;
    EXPECT_EQ(generateFormatCode(parsed), "€#,##0.00000000");

    parsed.currencySymbol = "£";
    parsed.decimalPlaces = 0;
    EXPECT_EQ(generateFormatCode(parsed), "£#,##0");
}

TEST(GenerateFormatCodeTest, InvalidParsedFormat) {
    ParsedFormatId parsed;
    parsed.valid = false;
    EXPECT_EQ(generateFormatCode(parsed), "");
}

TEST(GetCurrencySymbolTest, KnownCurrencies) {
    EXPECT_EQ(getCurrencySymbol("USD"), "$");
    EXPECT_EQ(getCurrencySymbol("EUR"), "€");
    EXPECT_EQ(getCurrencySymbol("GBP"), "£");
    EXPECT_EQ(getCurrencySymbol("JPY"), "¥");
    EXPECT_EQ(getCurrencySymbol("CNY"), "¥");
}

TEST(GetCurrencySymbolTest, UnknownCurrencies) {
    EXPECT_EQ(getCurrencySymbol("XYZ"), "");
    EXPECT_EQ(getCurrencySymbol("ABC"), "");
    EXPECT_EQ(getCurrencySymbol(""), "");
}

// --- getOrCreateFormat Tests ---

TEST(NumberFormatRegistryTest, GetOrCreateFormatCachesBuiltIn) {
    NumberFormatRegistry registry;

    // Built-in formats should already be cached
    const NumberFormat* fmt = registry.getOrCreateFormat(BuiltInFormats::NUMBER_2);
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(fmt->category, NumberFormatCategory::NUMBER);
    EXPECT_EQ(fmt->decimalPlaces, 2);
    EXPECT_EQ(fmt->formatCode, "0.00");
}

TEST(NumberFormatRegistryTest, GetOrCreateFormatCachesDynamic) {
    NumberFormatRegistry registry;

    // Dynamic format not initially in registry
    ID dynamicId("FMT_N012");
    EXPECT_FALSE(registry.hasFormat(dynamicId));

    // First call should create and cache
    const NumberFormat* fmt1 = registry.getOrCreateFormat(dynamicId);
    ASSERT_NE(fmt1, nullptr);
    EXPECT_EQ(fmt1->category, NumberFormatCategory::NUMBER);
    EXPECT_EQ(fmt1->decimalPlaces, 12);
    EXPECT_EQ(fmt1->formatCode, "0.000000000000");

    // Should now be in registry
    EXPECT_TRUE(registry.hasFormat(dynamicId));

    // Second call should return cached
    const NumberFormat* fmt2 = registry.getOrCreateFormat(dynamicId);
    EXPECT_EQ(fmt1, fmt2);  // Same pointer
}

TEST(NumberFormatRegistryTest, GetOrCreateFormatReturnsNullForInvalid) {
    NumberFormatRegistry registry;

    // Invalid format ID
    ID invalidId("INVALID!");
    const NumberFormat* fmt = registry.getOrCreateFormat(invalidId);
    EXPECT_EQ(fmt, nullptr);

    // Non-parseable format ID
    ID nonParseable("FMT_GEN1");  // Close to GENERAL but not valid
    fmt = registry.getOrCreateFormat(nonParseable);
    EXPECT_EQ(fmt, nullptr);
}

TEST(NumberFormatRegistryTest, GetOrCreateFormatDynamicCurrency) {
    NumberFormatRegistry registry;

    // Dynamic currency format with more than 4 decimals (not pre-registered)
    ID dynamicCurrency("CUSD_010");
    EXPECT_FALSE(registry.hasFormat(dynamicCurrency));

    const NumberFormat* fmt = registry.getOrCreateFormat(dynamicCurrency);
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(fmt->category, NumberFormatCategory::CURRENCY);
    EXPECT_EQ(fmt->decimalPlaces, 10);
    EXPECT_EQ(fmt->currencySymbol, "$");
    EXPECT_EQ(fmt->formatCode, "$#,##0.0000000000");
    EXPECT_TRUE(fmt->useThousandsSeparator);
}

TEST(NumberFormatRegistryTest, BuiltInFormatsUsesDynamicSystem) {
    NumberFormatRegistry registry;

    // Verify built-in NUMBER formats use dynamic system (same format code)
    const NumberFormat* num2 = registry.getFormat(BuiltInFormats::NUMBER_2);
    ASSERT_NE(num2, nullptr);
    EXPECT_EQ(num2->formatCode, "0.00");

    const NumberFormat* numSep2 = registry.getFormat(BuiltInFormats::NUMBER_SEP2);
    ASSERT_NE(numSep2, nullptr);
    EXPECT_EQ(numSep2->formatCode, "#,##0.00");

    // Verify percentage formats
    const NumberFormat* pct2 = registry.getFormat(BuiltInFormats::PERCENTAGE_2);
    ASSERT_NE(pct2, nullptr);
    EXPECT_EQ(pct2->formatCode, "0.00%");

    // Verify currency formats (new naming scheme)
    const NumberFormat* usd2 = registry.getFormat(BuiltInFormats::CURRENCY_USD_2);
    ASSERT_NE(usd2, nullptr);
    EXPECT_EQ(usd2->formatCode, "$#,##0.00");
    EXPECT_EQ(usd2->currencySymbol, "$");
}

// --- getFormatDetails Tests ---

TEST(GetFormatDetailsTest, General) {
    EXPECT_EQ(getFormatDetails("~"),
              R"({"category":"general","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails(""),
              R"({"category":"general","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_GEN0"),
              R"({"category":"general","decimals":0,"separator":false,"currency":null})");
}

TEST(GetFormatDetailsTest, Number) {
    EXPECT_EQ(getFormatDetails("FMT_N000"),
              R"({"category":"number","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_N002"),
              R"({"category":"number","decimals":2,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_N012"),
              R"({"category":"number","decimals":12,"separator":false,"currency":null})");
}

TEST(GetFormatDetailsTest, NumberWithSeparator) {
    EXPECT_EQ(getFormatDetails("FMT_NS00"),
              R"({"category":"number","decimals":0,"separator":true,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_NS02"),
              R"({"category":"number","decimals":2,"separator":true,"currency":null})");
}

TEST(GetFormatDetailsTest, Percentage) {
    EXPECT_EQ(getFormatDetails("FMT_P000"),
              R"({"category":"percentage","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_P002"),
              R"({"category":"percentage","decimals":2,"separator":false,"currency":null})");
}

TEST(GetFormatDetailsTest, Currency) {
    EXPECT_EQ(getFormatDetails("CUSD_002"),
              R"({"category":"currency","decimals":2,"separator":true,"currency":"USD"})");
    EXPECT_EQ(getFormatDetails("CEUR_004"),
              R"({"category":"currency","decimals":4,"separator":true,"currency":"EUR"})");
}

TEST(GetFormatDetailsTest, LegacyCurrency) {
    EXPECT_EQ(getFormatDetails("FMT_C002"),
              R"({"category":"currency","decimals":2,"separator":true,"currency":"USD"})");
}

TEST(GetFormatDetailsTest, BuiltInFormats) {
    EXPECT_EQ(getFormatDetails("FMT_A002"),
              R"({"category":"accounting","decimals":2,"separator":true,"currency":"USD"})");
    EXPECT_EQ(getFormatDetails("FMT_DSHT"),
              R"({"category":"date","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_T12H"),
              R"({"category":"time","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_DTSH"),
              R"({"category":"datetime","decimals":0,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_SCI2"),
              R"({"category":"scientific","decimals":2,"separator":false,"currency":null})");
    EXPECT_EQ(getFormatDetails("FMT_TEXT"),
              R"({"category":"text","decimals":0,"separator":false,"currency":null})");
}

TEST(GetFormatDetailsTest, UnknownFormat) {
    EXPECT_EQ(getFormatDetails("INVALID!"), R"({"error":"Unknown format"})");
    EXPECT_EQ(getFormatDetails("XXXXXXXX"), R"({"error":"Unknown format"})");
}

// --- makeFormatId Tests ---

TEST(MakeFormatIdTest, Number) {
    EXPECT_EQ(makeFormatId("number", 0, false, ""), "FMT_N000");
    EXPECT_EQ(makeFormatId("number", 2, false, ""), "FMT_N002");
    EXPECT_EQ(makeFormatId("number", 12, false, ""), "FMT_N012");
    EXPECT_EQ(makeFormatId("number", 15, false, ""), "FMT_N015");
}

TEST(MakeFormatIdTest, NumberWithSeparator) {
    EXPECT_EQ(makeFormatId("number", 0, true, ""), "FMT_NS00");
    EXPECT_EQ(makeFormatId("number", 2, true, ""), "FMT_NS02");
    EXPECT_EQ(makeFormatId("number", 15, true, ""), "FMT_NS15");
}

TEST(MakeFormatIdTest, Percentage) {
    EXPECT_EQ(makeFormatId("percentage", 0, false, ""), "FMT_P000");
    EXPECT_EQ(makeFormatId("percentage", 2, false, ""), "FMT_P002");
    EXPECT_EQ(makeFormatId("percentage", 15, false, ""), "FMT_P015");
}

TEST(MakeFormatIdTest, Currency) {
    EXPECT_EQ(makeFormatId("currency", 2, true, "USD"), "CUSD_002");
    EXPECT_EQ(makeFormatId("currency", 4, true, "EUR"), "CEUR_004");
    EXPECT_EQ(makeFormatId("currency", 0, true, "GBP"), "CGBP_000");
}

TEST(MakeFormatIdTest, InvalidDecimals) {
    EXPECT_EQ(makeFormatId("number", -1, false, ""), "");
    EXPECT_EQ(makeFormatId("number", 16, false, ""), "");
}

TEST(MakeFormatIdTest, InvalidCurrency) {
    // Wrong length
    EXPECT_EQ(makeFormatId("currency", 2, true, "US"), "");
    EXPECT_EQ(makeFormatId("currency", 2, true, "USDD"), "");
    // Lowercase
    EXPECT_EQ(makeFormatId("currency", 2, true, "usd"), "");
}

TEST(MakeFormatIdTest, UnsupportedCategory) {
    // These categories don't have dynamic format IDs
    EXPECT_EQ(makeFormatId("date", 0, false, ""), "");
    EXPECT_EQ(makeFormatId("time", 0, false, ""), "");
    EXPECT_EQ(makeFormatId("general", 0, false, ""), "");
}

// --- Format Priority Tests ---

TEST(FormatPriorityTest, DateTimeHighestPriority) {
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::DATE), 100);
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::TIME), 100);
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::DATE_TIME), 100);
}

TEST(FormatPriorityTest, CurrencySecondPriority) {
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::CURRENCY), 80);
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::ACCOUNTING), 80);
}

TEST(FormatPriorityTest, PercentageThirdPriority) {
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::PERCENTAGE), 60);
}

TEST(FormatPriorityTest, NumberFourthPriority) {
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::NUMBER), 40);
}

TEST(FormatPriorityTest, GeneralLowestPriority) {
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::GENERAL), 0);
    EXPECT_EQ(getFormatPriority(NumberFormatCategory::TEXT), 0);
}

TEST(FormatPriorityTest, PriorityOrdering) {
    // Verify the ordering is correct
    EXPECT_GT(getFormatPriority(NumberFormatCategory::DATE),
              getFormatPriority(NumberFormatCategory::CURRENCY));
    EXPECT_GT(getFormatPriority(NumberFormatCategory::CURRENCY),
              getFormatPriority(NumberFormatCategory::PERCENTAGE));
    EXPECT_GT(getFormatPriority(NumberFormatCategory::PERCENTAGE),
              getFormatPriority(NumberFormatCategory::NUMBER));
    EXPECT_GT(getFormatPriority(NumberFormatCategory::NUMBER),
              getFormatPriority(NumberFormatCategory::GENERAL));
}

// --- Format Inheritance Tests ---

TEST(InferFormatTest, NullAstReturnsEmpty) {
    FormatLookup lookup = [](const std::string&) { return "CUSD_002"; };
    EXPECT_EQ(inferFormatFromFormula(nullptr, lookup), "");
}

TEST(InferFormatTest, NoReferencesReturnsEmpty) {
    // Create an AST with just a number literal
    NumberLiteralNode node(42.0);
    FormatLookup lookup = [](const std::string&) { return "CUSD_002"; };
    EXPECT_EQ(inferFormatFromFormula(&node, lookup), "");
}

TEST(InferFormatTest, SingleCellRefInheritsCurrency) {
    // Create an AST with a cell reference that resolves to a cell ID
    CellRefNode cellRef("A", 1, false, false);
    cellRef.cellId = "CELL_001";  // Resolved cell ID

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_001") {
            return std::string("CUSD_002");  // USD currency, 2 decimals
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "CUSD_002");
}

TEST(InferFormatTest, SingleCellRefInheritsPercentage) {
    CellRefNode cellRef("B", 2, true, true);
    cellRef.cellId = "CELL_002";

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_002") {
            return std::string("FMT_P002");  // Percentage, 2 decimals
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "FMT_P002");
}

TEST(InferFormatTest, GeneralFormatNotInherited) {
    CellRefNode cellRef("A", 1, false, false);
    cellRef.cellId = "CELL_001";

    // Cell has GENERAL format - should not be inherited
    FormatLookup lookup = [](const std::string&) { return std::string("FMT_GEN0"); };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "");
}

TEST(InferFormatTest, EmptyFormatNotInherited) {
    CellRefNode cellRef("A", 1, false, false);
    cellRef.cellId = "CELL_001";

    // Cell has no format - should not be inherited
    FormatLookup lookup = [](const std::string&) { return std::string(""); };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "");
}

TEST(InferFormatTest, TildeFormatNotInherited) {
    CellRefNode cellRef("A", 1, false, false);
    cellRef.cellId = "CELL_001";

    // "~" means null format - should not be inherited
    FormatLookup lookup = [](const std::string&) { return std::string("~"); };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "");
}

TEST(InferFormatTest, BinaryOpBothSameFormat) {
    // =A1+B1 where both have currency format
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<CellRefNode>("B", 1, false, false);
    right->cellId = "CELL_B1";

    BinaryOpNode binOp(BinaryOp::ADD, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1" || cellId == "CELL_B1") {
            return std::string("CUSD_002");  // Both have currency
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "CUSD_002");
}

TEST(InferFormatTest, BinaryOpCurrencyWinsOverPercentage) {
    // =A1+B1 where A1 is percentage, B1 is currency
    // Currency has higher priority, so it should win
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<CellRefNode>("B", 1, false, false);
    right->cellId = "CELL_B1";

    BinaryOpNode binOp(BinaryOp::ADD, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("FMT_P002");  // Percentage
        }
        if (cellId == "CELL_B1") {
            return std::string("CUSD_002");  // Currency (higher priority)
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "CUSD_002");
}

TEST(InferFormatTest, BinaryOpCurrencyWinsRegardlessOfOrder) {
    // Same as above but with reversed order in formula
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<CellRefNode>("B", 1, false, false);
    right->cellId = "CELL_B1";

    BinaryOpNode binOp(BinaryOp::ADD, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("CUSD_002");  // Currency (higher priority)
        }
        if (cellId == "CELL_B1") {
            return std::string("FMT_P002");  // Percentage
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "CUSD_002");
}

TEST(InferFormatTest, MoreDecimalsWinsInSameCategory) {
    // =A1+B1 where both are NUMBER but with different decimals
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<CellRefNode>("B", 1, false, false);
    right->cellId = "CELL_B1";

    BinaryOpNode binOp(BinaryOp::ADD, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("FMT_N002");  // NUMBER, 2 decimals
        }
        if (cellId == "CELL_B1") {
            return std::string("FMT_N004");  // NUMBER, 4 decimals (more specific)
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "FMT_N004");
}

TEST(InferFormatTest, LiteralWithRefInheritsFromRef) {
    // =A1*2 where A1 has currency format
    // Literal (2) doesn't affect format inheritance
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<NumberLiteralNode>(2.0);

    BinaryOpNode binOp(BinaryOp::MULTIPLY, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("CUSD_002");
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "CUSD_002");
}

TEST(InferFormatTest, FunctionCallInheritsFromArgs) {
    // =SUM(A1, B1) where both have percentage format
    auto arg1 = std::make_unique<CellRefNode>("A", 1, false, false);
    arg1->cellId = "CELL_A1";
    auto arg2 = std::make_unique<CellRefNode>("B", 1, false, false);
    arg2->cellId = "CELL_B1";

    FunctionCallNode funcCall("SUM");
    funcCall.args.push_back(std::move(arg1));
    funcCall.args.push_back(std::move(arg2));

    FormatLookup lookup = [](const std::string&) {
        return std::string("FMT_P002");  // Both have percentage
    };

    EXPECT_EQ(inferFormatFromFormula(&funcCall, lookup), "FMT_P002");
}

TEST(InferFormatTest, RangeRefInheritsFromCorners) {
    // Range A1:B2 - corners are A1 and B2
    auto topLeft = std::make_unique<CellRefNode>("A", 1, false, false);
    topLeft->cellId = "CELL_A1";
    auto bottomRight = std::make_unique<CellRefNode>("B", 2, false, false);
    bottomRight->cellId = "CELL_B2";

    RangeRefNode rangeRef(std::move(topLeft), std::move(bottomRight));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("CUSD_002");  // Currency
        }
        if (cellId == "CELL_B2") {
            return std::string("FMT_P002");  // Percentage
        }
        return std::string("");
    };

    // Currency wins over percentage
    EXPECT_EQ(inferFormatFromFormula(&rangeRef, lookup), "CUSD_002");
}

TEST(InferFormatTest, UnaryOpInheritsFromOperand) {
    // =-A1 (negation)
    auto operand = std::make_unique<CellRefNode>("A", 1, false, false);
    operand->cellId = "CELL_A1";

    UnaryOpNode unaryOp(UnaryOp::NEGATE, std::move(operand));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("CUSD_002");
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&unaryOp, lookup), "CUSD_002");
}

TEST(InferFormatTest, UseCellFormatNotUnderlyingFormula) {
    // When A1 contains =B1 and has currency format, and B1 has percentage format,
    // referencing =A1 should use A1's format (currency), not B1's (percentage)
    // This is tested by the format lookup returning A1's explicit format
    CellRefNode cellRef("A", 1, false, false);
    cellRef.cellId = "CELL_A1";

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            // A1's formatId is currency (it has a formula =B1 but format is currency)
            return std::string("CUSD_002");
        }
        return std::string("");
    };

    EXPECT_EQ(inferFormatFromFormula(&cellRef, lookup), "CUSD_002");
}

TEST(InferFormatTest, SeparatorBeatsNoSeparatorSameDecimals) {
    // =A1+B1 where A1 has NUMBER, B1 has NUMBER with separator (same decimals)
    auto left = std::make_unique<CellRefNode>("A", 1, false, false);
    left->cellId = "CELL_A1";
    auto right = std::make_unique<CellRefNode>("B", 1, false, false);
    right->cellId = "CELL_B1";

    BinaryOpNode binOp(BinaryOp::ADD, std::move(left), std::move(right));

    FormatLookup lookup = [](const std::string& cellId) {
        if (cellId == "CELL_A1") {
            return std::string("FMT_N002");  // NUMBER, 2 decimals, no separator
        }
        if (cellId == "CELL_B1") {
            return std::string("FMT_NS02");  // NUMBER, 2 decimals, with separator
        }
        return std::string("");
    };

    // FMT_NS02 (with separator) should win
    EXPECT_EQ(inferFormatFromFormula(&binOp, lookup), "FMT_NS02");
}

}  // namespace
}  // namespace cells
