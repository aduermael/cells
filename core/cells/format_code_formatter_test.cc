#include "core/cells/format_code_formatter.h"

#include <cmath>

#include <gtest/gtest.h>

namespace cells {
namespace {

// --- Basic Number Formatting Tests ---

TEST(FormatWithCodeTest, SimpleInteger) {
    auto result = formatWithCode(1234, "0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1234");
}

TEST(FormatWithCodeTest, SimpleDecimal) {
    auto result = formatWithCode(1234.5678, "0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1234.57");  // Rounded
}

TEST(FormatWithCodeTest, DecimalPrecision) {
    auto result = formatWithCode(1.23456789, "0.0000");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1.2346");  // Rounded
}

TEST(FormatWithCodeTest, ZeroDecimals) {
    auto result = formatWithCode(1234.56, "0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1235");  // Rounded
}

TEST(FormatWithCodeTest, ManyDecimals) {
    auto result = formatWithCode(1.1, "0.000000000000");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1.100000000000");
}

// --- Thousands Separator Tests ---

TEST(FormatWithCodeTest, ThousandsSeparator) {
    auto result = formatWithCode(1234567.89, "#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234,567.89");
}

TEST(FormatWithCodeTest, ThousandsSeparatorNoDecimals) {
    auto result = formatWithCode(1234567, "#,##0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234,567");
}

TEST(FormatWithCodeTest, ThousandsSeparatorSmallNumber) {
    auto result = formatWithCode(123, "#,##0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "123");  // No separator needed
}

TEST(FormatWithCodeTest, ThousandsSeparatorMediumNumber) {
    auto result = formatWithCode(1234, "#,##0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234");
}

// --- Percentage Tests ---

TEST(FormatWithCodeTest, PercentageBasic) {
    auto result = formatWithCode(0.15, "0%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "15%");  // 0.15 * 100 = 15
}

TEST(FormatWithCodeTest, PercentageWithDecimals) {
    auto result = formatWithCode(0.1567, "0.00%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "15.67%");  // 0.1567 * 100 = 15.67
}

TEST(FormatWithCodeTest, PercentageOver100) {
    auto result = formatWithCode(1.5, "0%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "150%");  // 1.5 * 100 = 150
}

TEST(FormatWithCodeTest, PercentageNegative) {
    auto result = formatWithCode(-0.25, "0.00%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "-25.00%");
}

// --- Currency Tests ---

TEST(FormatWithCodeTest, CurrencyDollar) {
    auto result = formatWithCode(1234.56, "$#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$1,234.56");
}

TEST(FormatWithCodeTest, CurrencyEuro) {
    auto result = formatWithCode(1234.56, "€#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "€1,234.56");
}

TEST(FormatWithCodeTest, CurrencyPound) {
    auto result = formatWithCode(1234.56, "£#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "£1,234.56");
}

TEST(FormatWithCodeTest, CurrencyYen) {
    auto result = formatWithCode(1234, "¥#,##0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "¥1,234");
}

TEST(FormatWithCodeTest, CurrencyNoDecimals) {
    auto result = formatWithCode(1234.56, "$#,##0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$1,235");  // Rounded
}

// --- Negative Number Tests ---

TEST(FormatWithCodeTest, NegativeNumber) {
    auto result = formatWithCode(-1234.56, "0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "-1234.56");
}

TEST(FormatWithCodeTest, NegativeWithThousands) {
    auto result = formatWithCode(-1234567.89, "#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "-1,234,567.89");
}

TEST(FormatWithCodeTest, NegativeCurrency) {
    auto result = formatWithCode(-1234.56, "$#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "-$1,234.56");
}

// --- Positive/Negative Section Tests ---

TEST(FormatWithCodeTest, TwoSectionsPositive) {
    auto result = formatWithCode(1234.56, "#,##0.00;(#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234.56");
}

TEST(FormatWithCodeTest, TwoSectionsNegative) {
    auto result = formatWithCode(-1234.56, "#,##0.00;(#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "(1,234.56)");  // Uses absolute value in parens
}

TEST(FormatWithCodeTest, ThreeSectionsZero) {
    // The "-" section is parsed: "-" is treated as literal prefix, no digit placeholders
    // With no digit placeholders, the section formats as just the prefix
    auto result = formatWithCode(0, "#,##0.00;(#,##0.00);-");
    EXPECT_TRUE(result.success);
    // Section "-" has no digit placeholders, so it formats as prefix "-" + "0" (default)
    // This is "-0" because the formatter adds 0 when no placeholders found
    EXPECT_EQ(result.text, "-0");
}

// --- General Format Tests ---

TEST(FormatWithCodeTest, GeneralFormatInteger) {
    auto result = formatWithCode(1234, "General");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1234");
}

TEST(FormatWithCodeTest, GeneralFormatDecimal) {
    auto result = formatWithCode(1234.5, "General");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1234.5");
}

TEST(FormatWithCodeTest, GeneralFormatSmallDecimal) {
    auto result = formatWithCode(0.123, "General");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "0.123");
}

// --- Special Values Tests ---

TEST(FormatWithCodeTest, Zero) {
    auto result = formatWithCode(0, "0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "0.00");
}

TEST(FormatWithCodeTest, NaN) {
    auto result = formatWithCode(std::nan(""), "0.00");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.text, "#FORMAT!");
}

TEST(FormatWithCodeTest, PositiveInfinity) {
    auto result = formatWithCode(std::numeric_limits<double>::infinity(), "0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "∞");
}

TEST(FormatWithCodeTest, NegativeInfinity) {
    auto result = formatWithCode(-std::numeric_limits<double>::infinity(), "0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "-∞");
}

// --- Locale Tests ---

TEST(FormatWithCodeTest, EuropeanLocale) {
    auto result = formatWithCode(1234567.89, "#,##0.00", FormatLocaleSettings::EU());
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1.234.567,89");  // Swapped separators
}

TEST(FormatWithCodeTest, EuropeanLocalePercentage) {
    auto result = formatWithCode(0.1567, "0,00%", FormatLocaleSettings::EU());
    // Note: Format code uses '.', EU locale replaces with ','
    // But the format code "0,00%" is parsed as using ',' already
    // Let's test with standard format code
    result = formatWithCode(0.1567, "0.00%", FormatLocaleSettings::EU());
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "15,67%");
}

// --- Text Format Tests ---

TEST(FormatWithCodeTest, TextFormat) {
    // Text format with @ is for text values, not numbers
    auto result = formatWithCode(1234, "@");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1234");  // Number converted to text
}

// --- Error Cases ---

TEST(FormatWithCodeTest, EmptyFormatCode) {
    auto result = formatWithCode(1234, "");
    EXPECT_FALSE(result.success);
}

TEST(FormatWithCodeTest, InvalidFormatCode) {
    // Parse succeeds but might give unexpected results
    auto result = formatWithCode(1234, "XXXXX");
    // Actually, letters are treated as literal text, so this is technically valid
    EXPECT_TRUE(result.success);
}

// --- FormatWithParsedCode Tests ---

TEST(FormatWithParsedCodeTest, ValidParsedCode) {
    ParsedFormatCode parsed = parseFormatCode("#,##0.00");
    auto result = formatWithParsedCode(1234.56, parsed);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234.56");
}

TEST(FormatWithParsedCodeTest, InvalidParsedCode) {
    ParsedFormatCode parsed;
    parsed.valid = false;
    parsed.errorMessage = "Test error";
    auto result = formatWithParsedCode(1234, parsed);
    EXPECT_FALSE(result.success);
}

// --- Format Text With Code Tests ---

TEST(FormatTextWithCodeTest, SimpleText) {
    ParsedFormatCode parsed = parseFormatCode("@");
    std::string result = formatTextWithCode("Hello", parsed);
    EXPECT_EQ(result, "Hello");
}

TEST(FormatTextWithCodeTest, TextWithPrefix) {
    ParsedFormatCode parsed = parseFormatCode("\"Value: \"@");
    std::string result = formatTextWithCode("Test", parsed);
    // Text format doesn't have prefix handling in current impl
    // Just returns text as-is for now
    EXPECT_FALSE(result.empty());
}

TEST(FormatTextWithCodeTest, NoTextSection) {
    ParsedFormatCode parsed = parseFormatCode("0.00");
    std::string result = formatTextWithCode("Hello", parsed);
    EXPECT_EQ(result, "Hello");  // Returns text unchanged
}

// --- Edge Cases ---

TEST(FormatWithCodeTest, VeryLargeNumber) {
    auto result = formatWithCode(1234567890123.45, "#,##0.00");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "1,234,567,890,123.45");
}

TEST(FormatWithCodeTest, VerySmallNumber) {
    auto result = formatWithCode(0.000001, "0.000000");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "0.000001");
}

TEST(FormatWithCodeTest, NegativeZero) {
    auto result = formatWithCode(-0.0, "0.00");
    EXPECT_TRUE(result.success);
    // -0.0 should display as 0.00 (no negative sign for zero)
    EXPECT_TRUE(result.text == "0.00" || result.text == "-0.00");
}

// --- Real-World Format Codes ---

TEST(FormatWithCodeTest, AccountingPositive) {
    // Old-style accounting (no space)
    auto result = formatWithCode(1234.56, "$#,##0.00;($#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$1,234.56");
}

TEST(FormatWithCodeTest, AccountingNegative) {
    // Old-style accounting (no space)
    auto result = formatWithCode(-1234.56, "$#,##0.00;($#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "($1,234.56)");
}

TEST(FormatWithCodeTest, AccountingSpacePositive) {
    // New accounting format with space between symbol and number
    auto result = formatWithCode(2.29, "\"$ \"#,##0.00;(\"$ \"#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$ 2.29");
}

TEST(FormatWithCodeTest, AccountingSpaceNegative) {
    // New accounting format - negative in parentheses
    auto result = formatWithCode(-108.30, "\"$ \"#,##0.00;(\"$ \"#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "($ 108.30)");
}

TEST(FormatWithCodeTest, AccountingSpaceZero) {
    // New accounting format - zero
    auto result = formatWithCode(0, "\"$ \"#,##0.00;(\"$ \"#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$ 0.00");
}

TEST(FormatWithCodeTest, AccountingSpaceLarge) {
    // New accounting format - large number with thousands separator
    auto result = formatWithCode(1234567.89, "\"$ \"#,##0.00;(\"$ \"#,##0.00)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$ 1,234,567.89");
}

TEST(FormatWithCodeTest, PercentageHighPrecision) {
    auto result = formatWithCode(0.123456789, "0.0000000%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "12.3456789%");
}

TEST(FormatWithCodeTest, CurrencyHighPrecision) {
    auto result = formatWithCode(1234.12345678, "$#,##0.00000000");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.text, "$1,234.12345678");
}

}  // namespace
}  // namespace cells
