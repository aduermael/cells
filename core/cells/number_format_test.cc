#include "core/cells/number_format.h"

#include <gtest/gtest.h>

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

    auto currencyFormats = registry.getFormatsByCategory(NumberFormatCategory::CURRENCY);
    EXPECT_EQ(currencyFormats.size(), 5);  // CURRENCY_0 through CURRENCY_4 (0-4 decimal places)

    auto dateFormats = registry.getFormatsByCategory(NumberFormatCategory::DATE);
    EXPECT_EQ(dateFormats.size(), 3);  // DATE_SHORT, DATE_LONG, DATE_ISO

    auto percentageFormats = registry.getFormatsByCategory(NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(percentageFormats.size(),
              5);  // PERCENTAGE_0 through PERCENTAGE_4 (0-4 decimal places)
}

TEST(NumberFormatRegistryTest, GetAllFormats) {
    NumberFormatRegistry registry;

    const auto& allFormats = registry.getAllFormats();
    // Should have all built-in formats (31 total)
    // 1 General + 5 Number (0-4 decimals) + 5 Number with separators (0-4 decimals) +
    // 5 Currency (0-4 decimals) + 2 Accounting + 5 Percentage (0-4 decimals) +
    // 3 Date + 2 Time + 1 DateTime + 1 Scientific + 1 Text
    EXPECT_EQ(allFormats.size(), 31);
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

}  // namespace
}  // namespace cells
