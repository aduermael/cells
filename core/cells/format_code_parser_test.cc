#include "core/cells/format_code_parser.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

// --- Basic Format Code Parsing Tests ---

TEST(ParseFormatCodeTest, EmptyFormatCode) {
    auto parsed = parseFormatCode("");
    EXPECT_FALSE(parsed.valid);
    EXPECT_EQ(parsed.errorMessage, "Empty format code");
}

TEST(ParseFormatCodeTest, GeneralFormat) {
    auto parsed = parseFormatCode("General");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 1);
    EXPECT_EQ(parsed.sections[0].code, "General");
}

TEST(ParseFormatCodeTest, GeneralFormatCaseInsensitive) {
    auto parsed = parseFormatCode("general");
    EXPECT_TRUE(parsed.valid);

    parsed = parseFormatCode("GENERAL");
    EXPECT_TRUE(parsed.valid);
}

TEST(ParseFormatCodeTest, SimpleDecimalFormat) {
    auto parsed = parseFormatCode("0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 2);
    EXPECT_FALSE(parsed.hasThousandsSeparator);
    EXPECT_FALSE(parsed.hasPercent);
    EXPECT_TRUE(parsed.currencySymbol.empty());
}

TEST(ParseFormatCodeTest, NoDecimalFormat) {
    auto parsed = parseFormatCode("0");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 0);
}

TEST(ParseFormatCodeTest, ManyDecimalPlaces) {
    auto parsed = parseFormatCode("0.000000000000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 12);
}

TEST(ParseFormatCodeTest, HashDecimalFormat) {
    auto parsed = parseFormatCode("#.##");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 2);
}

// --- Thousands Separator Tests ---

TEST(ParseFormatCodeTest, ThousandsSeparator) {
    auto parsed = parseFormatCode("#,##0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 2);
    EXPECT_TRUE(parsed.hasThousandsSeparator);
}

TEST(ParseFormatCodeTest, ThousandsSeparatorNoDecimals) {
    auto parsed = parseFormatCode("#,##0");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_TRUE(parsed.hasThousandsSeparator);
}

TEST(ParseFormatCodeTest, ThousandsSeparatorManyDecimals) {
    auto parsed = parseFormatCode("#,##0.00000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 5);
    EXPECT_TRUE(parsed.hasThousandsSeparator);
}

// --- Percentage Tests ---

TEST(ParseFormatCodeTest, PercentageFormat) {
    auto parsed = parseFormatCode("0.00%");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 2);
    EXPECT_TRUE(parsed.hasPercent);
}

TEST(ParseFormatCodeTest, PercentageNoDecimals) {
    auto parsed = parseFormatCode("0%");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 0);
    EXPECT_TRUE(parsed.hasPercent);
}

TEST(ParseFormatCodeTest, PercentageManyDecimals) {
    auto parsed = parseFormatCode("0.0000000%");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.decimalPlaces, 7);
    EXPECT_TRUE(parsed.hasPercent);
}

// --- Currency Tests ---

TEST(ParseFormatCodeTest, DollarCurrency) {
    auto parsed = parseFormatCode("$#,##0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "$");
    EXPECT_EQ(parsed.decimalPlaces, 2);
    EXPECT_TRUE(parsed.hasThousandsSeparator);
    EXPECT_EQ(parsed.sections[0].prefix, "$");
}

TEST(ParseFormatCodeTest, EuroCurrency) {
    auto parsed = parseFormatCode("€#,##0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "€");
}

TEST(ParseFormatCodeTest, PoundCurrency) {
    auto parsed = parseFormatCode("£#,##0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "£");
}

TEST(ParseFormatCodeTest, YenCurrency) {
    auto parsed = parseFormatCode("¥#,##0");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "¥");
    EXPECT_EQ(parsed.decimalPlaces, 0);
}

TEST(ParseFormatCodeTest, CurrencyNoDecimals) {
    auto parsed = parseFormatCode("$#,##0");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "$");
    EXPECT_EQ(parsed.decimalPlaces, 0);
}

TEST(ParseFormatCodeTest, CurrencyManyDecimals) {
    auto parsed = parseFormatCode("$#,##0.00000000");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.currencySymbol, "$");
    EXPECT_EQ(parsed.decimalPlaces, 8);
}

// --- Multi-Section Format Tests ---

TEST(ParseFormatCodeTest, TwoSections) {
    auto parsed = parseFormatCode("#,##0.00;(#,##0.00)");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 2);

    // Positive section
    EXPECT_EQ(parsed.sections[0].decimalPlaces, 2);
    EXPECT_TRUE(parsed.sections[0].hasThousandsSeparator);

    // Negative section
    EXPECT_EQ(parsed.sections[1].decimalPlaces, 2);
    EXPECT_TRUE(parsed.sections[1].hasThousandsSeparator);
    EXPECT_TRUE(parsed.sections[1].useParentheses);
}

TEST(ParseFormatCodeTest, ThreeSections) {
    auto parsed = parseFormatCode("#,##0.00;(#,##0.00);-");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 3);
}

TEST(ParseFormatCodeTest, FourSections) {
    auto parsed = parseFormatCode("#,##0.00;(#,##0.00);-;@");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 4);
    EXPECT_TRUE(parsed.sections[3].isTextFormat);
}

TEST(ParseFormatCodeTest, TooManySections) {
    auto parsed = parseFormatCode("0;0;0;0;0");
    EXPECT_FALSE(parsed.valid);
    EXPECT_EQ(parsed.errorMessage, "Too many sections (max 4)");
}

// --- Text Format Tests ---

TEST(ParseFormatCodeTest, TextFormat) {
    auto parsed = parseFormatCode("@");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 1);
    EXPECT_TRUE(parsed.sections[0].isTextFormat);
}

// --- Quoted String Tests ---

TEST(ParseFormatCodeTest, QuotedPrefix) {
    auto parsed = parseFormatCode("\"Total: \"0.00");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections[0].prefix, "Total: ");
    EXPECT_EQ(parsed.decimalPlaces, 2);
}

TEST(ParseFormatCodeTest, QuotedSuffix) {
    auto parsed = parseFormatCode("0.00\" units\"");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections[0].suffix, " units");
    EXPECT_EQ(parsed.decimalPlaces, 2);
}

TEST(ParseFormatCodeTest, QuotedPrefixAndSuffix) {
    auto parsed = parseFormatCode("\"$\"#,##0.00\" USD\"");
    EXPECT_TRUE(parsed.valid);
    // Note: The currency symbol is also detected but quoted text is added to prefix/suffix
    EXPECT_TRUE(parsed.sections[0].prefix.find("$") != std::string::npos);
}

// --- Parentheses (Negative) Format Tests ---

TEST(ParseFormatCodeTest, ParenthesesFormat) {
    auto parsed = parseFormatCode("(#,##0.00)");
    EXPECT_TRUE(parsed.valid);
    EXPECT_TRUE(parsed.sections[0].useParentheses);
}

// --- Currency Symbol Detection Tests ---

TEST(IsCurrencySymbolTest, DollarSign) {
    std::string str = "$123";
    size_t len = 0;
    EXPECT_TRUE(isCurrencySymbol(str, 0, len));
    EXPECT_EQ(len, 1);
}

TEST(IsCurrencySymbolTest, EuroSign) {
    std::string str = "€123";
    size_t len = 0;
    EXPECT_TRUE(isCurrencySymbol(str, 0, len));
    EXPECT_EQ(len, 3);  // UTF-8 euro is 3 bytes
}

TEST(IsCurrencySymbolTest, PoundSign) {
    std::string str = "£123";
    size_t len = 0;
    EXPECT_TRUE(isCurrencySymbol(str, 0, len));
    EXPECT_EQ(len, 2);  // UTF-8 pound is 2 bytes
}

TEST(IsCurrencySymbolTest, YenSign) {
    std::string str = "¥123";
    size_t len = 0;
    EXPECT_TRUE(isCurrencySymbol(str, 0, len));
    EXPECT_EQ(len, 2);  // UTF-8 yen is 2 bytes
}

TEST(IsCurrencySymbolTest, NotCurrencySymbol) {
    std::string str = "ABC";
    size_t len = 0;
    EXPECT_FALSE(isCurrencySymbol(str, 0, len));
    EXPECT_EQ(len, 0);
}

TEST(IsCurrencySymbolTest, CurrencyInMiddle) {
    std::string str = "USD $100";
    size_t len = 0;
    EXPECT_FALSE(isCurrencySymbol(str, 0, len));  // 'U' is not currency
    EXPECT_TRUE(isCurrencySymbol(str, 4, len));   // '$' at position 4
    EXPECT_EQ(len, 1);
}

// --- Validation Tests ---

TEST(ValidateFormatCodeTest, ValidFormats) {
    EXPECT_FALSE(validateFormatCode("0.00").has_value());
    EXPECT_FALSE(validateFormatCode("#,##0.00").has_value());
    EXPECT_FALSE(validateFormatCode("$#,##0.00").has_value());
    EXPECT_FALSE(validateFormatCode("0%").has_value());
    EXPECT_FALSE(validateFormatCode("General").has_value());
}

TEST(ValidateFormatCodeTest, EmptyFormat) {
    auto error = validateFormatCode("");
    EXPECT_TRUE(error.has_value());
    EXPECT_EQ(*error, "Empty format code");
}

TEST(ValidateFormatCodeTest, UnbalancedQuotes) {
    auto error = validateFormatCode("\"unterminated");
    EXPECT_TRUE(error.has_value());
    EXPECT_EQ(*error, "Unbalanced quotes");
}

TEST(ValidateFormatCodeTest, UnbalancedParentheses) {
    auto error = validateFormatCode("(#,##0.00");
    EXPECT_TRUE(error.has_value());
    EXPECT_EQ(*error, "Unbalanced parentheses");
}

// --- Section Parsing Tests ---

TEST(ParseFormatCodeSectionTest, EmptySection) {
    auto section = parseFormatCodeSection("");
    EXPECT_TRUE(section.isEmpty);
}

TEST(ParseFormatCodeSectionTest, SimpleNumber) {
    auto section = parseFormatCodeSection("0.00");
    EXPECT_FALSE(section.isEmpty);
    EXPECT_EQ(section.decimalPlaces, 2);
    EXPECT_FALSE(section.hasThousandsSeparator);
    EXPECT_FALSE(section.hasPercent);
}

TEST(ParseFormatCodeSectionTest, NumberWithThousands) {
    auto section = parseFormatCodeSection("#,##0");
    EXPECT_FALSE(section.isEmpty);
    EXPECT_EQ(section.decimalPlaces, 0);
    EXPECT_TRUE(section.hasThousandsSeparator);
}

TEST(ParseFormatCodeSectionTest, PercentSection) {
    auto section = parseFormatCodeSection("0.00%");
    EXPECT_TRUE(section.hasPercent);
    EXPECT_EQ(section.decimalPlaces, 2);
    EXPECT_EQ(section.suffix, "%");
}

TEST(ParseFormatCodeSectionTest, CurrencySection) {
    auto section = parseFormatCodeSection("$#,##0.00");
    EXPECT_EQ(section.currencySymbol, "$");
    EXPECT_EQ(section.prefix, "$");
    EXPECT_TRUE(section.hasThousandsSeparator);
    EXPECT_EQ(section.decimalPlaces, 2);
}

TEST(ParseFormatCodeSectionTest, TextSection) {
    auto section = parseFormatCodeSection("@");
    EXPECT_TRUE(section.isTextFormat);
}

TEST(ParseFormatCodeSectionTest, QuotedText) {
    auto section = parseFormatCodeSection("\"prefix\"0.00\"suffix\"");
    EXPECT_EQ(section.prefix, "prefix");
    EXPECT_EQ(section.suffix, "suffix");
    EXPECT_EQ(section.decimalPlaces, 2);
}

TEST(ParseFormatCodeSectionTest, SpacePadding) {
    // _) means add a space the width of a closing paren
    auto section = parseFormatCodeSection("_)#,##0.00_)");
    EXPECT_TRUE(section.hasThousandsSeparator);
    EXPECT_EQ(section.decimalPlaces, 2);
}

TEST(ParseFormatCodeSectionTest, Parentheses) {
    auto section = parseFormatCodeSection("(#,##0.00)");
    EXPECT_TRUE(section.useParentheses);
    EXPECT_EQ(section.prefix, "(");
    EXPECT_EQ(section.suffix, ")");
}

// --- Real-World Format Code Tests ---

TEST(ParseFormatCodeTest, AccountingFormat) {
    // Excel accounting format: _($* #,##0.00_);_($* (#,##0.00);_($* "-"??_);_(@_)
    // Simplified version:
    auto parsed = parseFormatCode("$#,##0.00;($#,##0.00)");
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.sections.size(), 2);
    EXPECT_EQ(parsed.currencySymbol, "$");
    EXPECT_TRUE(parsed.sections[1].useParentheses);
}

TEST(ParseFormatCodeTest, DateFormatAsText) {
    // Date formats are typically parsed as text patterns, not numbers
    auto parsed = parseFormatCode("yyyy-mm-dd");
    EXPECT_TRUE(parsed.valid);
    // The parser sees letters as literal text - this is fine
    // Date formatting is handled separately by the formatter
}

TEST(ParseFormatCodeTest, ScientificFormat) {
    auto parsed = parseFormatCode("0.00E+00");
    EXPECT_TRUE(parsed.valid);
    // 'E' is treated as literal text in our parser
    // Scientific formatting is handled separately
}

}  // namespace
}  // namespace cells
