// =============================================================================
// FormatBuffer Unit Tests
// =============================================================================

#include "core/cells/format_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Empty Format Tests
// =============================================================================

TEST(FormatBufferTest, EmptyFormat) {
    FormatBuffer f;
    EXPECT_TRUE(f.isEmpty());
    EXPECT_EQ(f.getFlags(), 0);
    EXPECT_EQ(f.data().size(), 1u);  // Just flag byte
}

TEST(FormatBufferTest, EmptyFormatRoundTrip) {
    FormatBuffer f;
    std::string b64 = f.toBase64();
    EXPECT_EQ(b64, "AA==");  // One zero byte

    auto decoded = FormatBuffer::fromBase64(b64);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isEmpty());
    EXPECT_EQ(decoded->getFlags(), 0);
}

TEST(FormatBufferTest, EmptyFormatCode) {
    FormatBuffer f;
    EXPECT_EQ(f.toFormatCode(), "General");
}

// =============================================================================
// Category Tests
// =============================================================================

TEST(FormatBufferTest, CategoryNumber) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);

    EXPECT_TRUE(f.hasCategory());
    EXPECT_EQ(f.getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_FALSE(f.isEmpty());

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->hasCategory());
    EXPECT_EQ(decoded->getCategory(), NumberFormatCategory::NUMBER);
}

TEST(FormatBufferTest, CategoryPercentage) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::PERCENTAGE);

    EXPECT_EQ(f.getCategory(), NumberFormatCategory::PERCENTAGE);

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCategory(), NumberFormatCategory::PERCENTAGE);
}

TEST(FormatBufferTest, CategoryCurrency) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::CURRENCY);

    EXPECT_EQ(f.getCategory(), NumberFormatCategory::CURRENCY);
}

TEST(FormatBufferTest, AllCategories) {
    // Test all category values
    for (auto cat : {NumberFormatCategory::GENERAL, NumberFormatCategory::NUMBER,
                     NumberFormatCategory::CURRENCY, NumberFormatCategory::ACCOUNTING,
                     NumberFormatCategory::PERCENTAGE, NumberFormatCategory::DATE,
                     NumberFormatCategory::TIME, NumberFormatCategory::DATE_TIME,
                     NumberFormatCategory::SCIENTIFIC, NumberFormatCategory::FRACTION,
                     NumberFormatCategory::TEXT, NumberFormatCategory::CUSTOM}) {
        FormatBuffer f;
        f.setCategory(cat);
        auto decoded = FormatBuffer::fromBase64(f.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->getCategory(), cat);
    }
}

TEST(FormatBufferTest, ClearCategory) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    EXPECT_TRUE(f.hasCategory());

    f.clearCategory();
    EXPECT_FALSE(f.hasCategory());
    EXPECT_EQ(f.getCategory(), NumberFormatCategory::GENERAL);  // Default
}

// =============================================================================
// Decimals Tests
// =============================================================================

TEST(FormatBufferTest, DecimalsZero) {
    FormatBuffer f;
    f.setDecimals(0);

    EXPECT_TRUE(f.hasDecimals());
    EXPECT_EQ(f.getDecimals(), 0);

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getDecimals(), 0);
}

TEST(FormatBufferTest, DecimalsTwo) {
    FormatBuffer f;
    f.setDecimals(2);

    EXPECT_TRUE(f.hasDecimals());
    EXPECT_EQ(f.getDecimals(), 2);

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getDecimals(), 2);
}

TEST(FormatBufferTest, DecimalsMax) {
    FormatBuffer f;
    f.setDecimals(255);

    EXPECT_EQ(f.getDecimals(), 255);
}

TEST(FormatBufferTest, DecimalsNotSet) {
    FormatBuffer f;
    EXPECT_FALSE(f.hasDecimals());
    EXPECT_EQ(f.getDecimals(), 0);  // Default
}

TEST(FormatBufferTest, ClearDecimals) {
    FormatBuffer f;
    f.setDecimals(5);
    EXPECT_TRUE(f.hasDecimals());

    f.clearDecimals();
    EXPECT_FALSE(f.hasDecimals());
    EXPECT_EQ(f.getDecimals(), 0);
}

// =============================================================================
// Thousands Separator Tests
// =============================================================================

TEST(FormatBufferTest, ThousandsSeparatorEnabled) {
    FormatBuffer f;
    f.setThousandsSeparator(true);

    EXPECT_TRUE(f.hasThousandsSeparator());
    EXPECT_TRUE(f.getThousandsSeparator());

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->getThousandsSeparator());
}

TEST(FormatBufferTest, ThousandsSeparatorDisabled) {
    FormatBuffer f;
    f.setThousandsSeparator(false);

    EXPECT_TRUE(f.hasThousandsSeparator());
    EXPECT_FALSE(f.getThousandsSeparator());

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->getThousandsSeparator());
}

TEST(FormatBufferTest, ClearThousandsSeparator) {
    FormatBuffer f;
    f.setThousandsSeparator(true);
    EXPECT_TRUE(f.hasThousandsSeparator());

    f.clearThousandsSeparator();
    EXPECT_FALSE(f.hasThousandsSeparator());
}

// =============================================================================
// Currency Symbol Tests
// =============================================================================

TEST(FormatBufferTest, CurrencySymbolDollar) {
    FormatBuffer f;
    f.setCurrencySymbol("$");

    EXPECT_TRUE(f.hasCurrencySymbol());
    EXPECT_EQ(f.getCurrencySymbol(), "$");

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCurrencySymbol(), "$");
}

TEST(FormatBufferTest, CurrencySymbolEuro) {
    FormatBuffer f;
    f.setCurrencySymbol("€");

    EXPECT_EQ(f.getCurrencySymbol(), "€");

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCurrencySymbol(), "€");
}

TEST(FormatBufferTest, CurrencySymbolYen) {
    FormatBuffer f;
    f.setCurrencySymbol("¥");

    EXPECT_EQ(f.getCurrencySymbol(), "¥");
}

TEST(FormatBufferTest, CurrencySymbolMultiChar) {
    FormatBuffer f;
    f.setCurrencySymbol("USD ");

    EXPECT_EQ(f.getCurrencySymbol(), "USD ");
}

TEST(FormatBufferTest, ClearCurrencySymbol) {
    FormatBuffer f;
    f.setCurrencySymbol("$");
    EXPECT_TRUE(f.hasCurrencySymbol());

    f.clearCurrencySymbol();
    EXPECT_FALSE(f.hasCurrencySymbol());
    EXPECT_EQ(f.getCurrencySymbol(), "");
}

// =============================================================================
// Custom Format Code Tests
// =============================================================================

TEST(FormatBufferTest, CustomFormatCodeSimple) {
    FormatBuffer f;
    f.setCustomFormatCode("# BANANA");

    EXPECT_TRUE(f.hasCustomFormatCode());
    EXPECT_EQ(f.getCustomFormatCode(), "# BANANA");

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCustomFormatCode(), "# BANANA");
}

TEST(FormatBufferTest, CustomFormatCodeAccounting) {
    FormatBuffer f;
    f.setCustomFormatCode("_($* #,##0.00_)");

    EXPECT_EQ(f.getCustomFormatCode(), "_($* #,##0.00_)");
}

TEST(FormatBufferTest, CustomFormatCodeLong) {
    FormatBuffer f;
    std::string longCode(1000, '#');
    f.setCustomFormatCode(longCode);

    EXPECT_EQ(f.getCustomFormatCode(), longCode);
}

TEST(FormatBufferTest, CustomFormatCodeVeryLong) {
    FormatBuffer f;
    std::string veryLongCode(65535, 'X');
    f.setCustomFormatCode(veryLongCode);

    EXPECT_EQ(f.getCustomFormatCode(), veryLongCode);
}

TEST(FormatBufferTest, CustomFormatCodeTruncate) {
    FormatBuffer f;
    std::string tooLongCode(70000, 'Y');
    f.setCustomFormatCode(tooLongCode);

    EXPECT_EQ(f.getCustomFormatCode().size(), 65535u);  // Truncated
}

TEST(FormatBufferTest, ClearCustomFormatCode) {
    FormatBuffer f;
    f.setCustomFormatCode("# BANANA");
    EXPECT_TRUE(f.hasCustomFormatCode());

    f.clearCustomFormatCode();
    EXPECT_FALSE(f.hasCustomFormatCode());
    EXPECT_EQ(f.getCustomFormatCode(), "");
}

// =============================================================================
// Combined Property Tests
// =============================================================================

TEST(FormatBufferTest, PercentageWithDecimals) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::PERCENTAGE);
    f.setDecimals(2);

    EXPECT_EQ(f.getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(f.getDecimals(), 2);

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(decoded->getDecimals(), 2);
}

TEST(FormatBufferTest, CurrencyWithAllProperties) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::CURRENCY);
    f.setDecimals(2);
    f.setThousandsSeparator(true);
    f.setCurrencySymbol("$");

    EXPECT_EQ(f.getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(f.getDecimals(), 2);
    EXPECT_TRUE(f.getThousandsSeparator());
    EXPECT_EQ(f.getCurrencySymbol(), "$");

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(decoded->getDecimals(), 2);
    EXPECT_TRUE(decoded->getThousandsSeparator());
    EXPECT_EQ(decoded->getCurrencySymbol(), "$");
}

TEST(FormatBufferTest, NumberWithThousands) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    f.setDecimals(0);
    f.setThousandsSeparator(true);

    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_TRUE(decoded->getThousandsSeparator());
}

// =============================================================================
// Determinism Tests
// =============================================================================

TEST(FormatBufferTest, Deterministic) {
    FormatBuffer f1, f2;

    // Set in different orders
    f1.setCategory(NumberFormatCategory::NUMBER);
    f1.setDecimals(2);

    f2.setDecimals(2);  // Different order
    f2.setCategory(NumberFormatCategory::NUMBER);

    // Same result regardless of order
    EXPECT_EQ(f1.toBase64(), f2.toBase64());
    EXPECT_EQ(f1, f2);
}

TEST(FormatBufferTest, ContentIdentity) {
    FormatBuffer f1, f2;
    f1.setCategory(NumberFormatCategory::NUMBER);
    f2.setCategory(NumberFormatCategory::NUMBER);

    // Same content = same base64 = same identity
    EXPECT_EQ(f1.toBase64(), f2.toBase64());
    EXPECT_EQ(f1, f2);

    FormatBuffer f3;
    f3.setCategory(NumberFormatCategory::PERCENTAGE);

    EXPECT_NE(f1.toBase64(), f3.toBase64());
    EXPECT_NE(f1, f3);
}

// =============================================================================
// Format Code Generation Tests
// =============================================================================

TEST(FormatBufferTest, ToFormatCodeNumber) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    f.setDecimals(2);
    f.setThousandsSeparator(true);

    EXPECT_EQ(f.toFormatCode(), "#,##0.00");
}

TEST(FormatBufferTest, ToFormatCodeNumberNoThousands) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    f.setDecimals(2);

    EXPECT_EQ(f.toFormatCode(), "0.00");
}

TEST(FormatBufferTest, ToFormatCodePercentage) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::PERCENTAGE);
    f.setDecimals(2);

    EXPECT_EQ(f.toFormatCode(), "0.00%");
}

TEST(FormatBufferTest, ToFormatCodePercentageNoDecimals) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::PERCENTAGE);
    f.setDecimals(0);

    EXPECT_EQ(f.toFormatCode(), "0%");
}

TEST(FormatBufferTest, ToFormatCodeCurrency) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::CURRENCY);
    f.setDecimals(2);
    f.setThousandsSeparator(true);
    f.setCurrencySymbol("$");

    EXPECT_EQ(f.toFormatCode(), "$#,##0.00");
}

TEST(FormatBufferTest, ToFormatCodeCurrencyEuro) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::CURRENCY);
    f.setDecimals(2);
    f.setThousandsSeparator(true);
    f.setCurrencySymbol("€");

    EXPECT_EQ(f.toFormatCode(), "€#,##0.00");
}

TEST(FormatBufferTest, ToFormatCodeScientific) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::SCIENTIFIC);
    f.setDecimals(2);

    EXPECT_EQ(f.toFormatCode(), "0.00E+00");
}

TEST(FormatBufferTest, ToFormatCodeDate) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::DATE);

    EXPECT_EQ(f.toFormatCode(), "yyyy-mm-dd");
}

TEST(FormatBufferTest, ToFormatCodeTime) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::TIME);

    EXPECT_EQ(f.toFormatCode(), "hh:mm:ss");
}

TEST(FormatBufferTest, ToFormatCodeDateTime) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::DATE_TIME);

    EXPECT_EQ(f.toFormatCode(), "yyyy-mm-dd hh:mm:ss");
}

TEST(FormatBufferTest, ToFormatCodeText) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::TEXT);

    EXPECT_EQ(f.toFormatCode(), "@");
}

TEST(FormatBufferTest, ToFormatCodeFraction) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::FRACTION);

    EXPECT_EQ(f.toFormatCode(), "# ?/?");
}

TEST(FormatBufferTest, ToFormatCodeCustom) {
    FormatBuffer f;
    f.setCustomFormatCode("# BANANA");

    EXPECT_EQ(f.toFormatCode(), "# BANANA");
}

// =============================================================================
// Format Code Parsing Tests
// =============================================================================

TEST(FormatBufferTest, FromFormatCodeGeneral) {
    auto f = FormatBuffer::fromFormatCode("General");
    ASSERT_TRUE(f.has_value());
    EXPECT_TRUE(f->isEmpty());
}

TEST(FormatBufferTest, FromFormatCodePercentage) {
    auto f = FormatBuffer::fromFormatCode("0.00%");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(f->getDecimals(), 2);
}

TEST(FormatBufferTest, FromFormatCodeCurrency) {
    auto f = FormatBuffer::fromFormatCode("$#,##0.00");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(f->getDecimals(), 2);
    EXPECT_TRUE(f->getThousandsSeparator());
    EXPECT_EQ(f->getCurrencySymbol(), "$");
}

TEST(FormatBufferTest, FromFormatCodeNumber) {
    auto f = FormatBuffer::fromFormatCode("#,##0.00");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_EQ(f->getDecimals(), 2);
    EXPECT_TRUE(f->getThousandsSeparator());
}

TEST(FormatBufferTest, FromFormatCodeScientific) {
    auto f = FormatBuffer::fromFormatCode("0.00E+00");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->getCategory(), NumberFormatCategory::SCIENTIFIC);
}

TEST(FormatBufferTest, FromFormatCodeText) {
    auto f = FormatBuffer::fromFormatCode("@");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->getCategory(), NumberFormatCategory::TEXT);
}

// =============================================================================
// JSON Tests
// =============================================================================

TEST(FormatBufferTest, ToJSONEmpty) {
    FormatBuffer f;
    EXPECT_EQ(f.toJSON(), "{}");
}

TEST(FormatBufferTest, ToJSONNumber) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    f.setDecimals(2);

    std::string json = f.toJSON();
    EXPECT_NE(json.find("\"category\":\"NUMBER\""), std::string::npos);
    EXPECT_NE(json.find("\"decimals\":2"), std::string::npos);
}

TEST(FormatBufferTest, ToJSONComplex) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::CURRENCY);
    f.setDecimals(2);
    f.setThousandsSeparator(true);
    f.setCurrencySymbol("$");

    std::string json = f.toJSON();
    EXPECT_NE(json.find("\"category\":\"CURRENCY\""), std::string::npos);
    EXPECT_NE(json.find("\"decimals\":2"), std::string::npos);
    EXPECT_NE(json.find("\"thousandsSeparator\":true"), std::string::npos);
    EXPECT_NE(json.find("\"currencySymbol\":\"$\""), std::string::npos);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(FormatBufferTest, InvalidBase64) {
    auto result = FormatBuffer::fromBase64("not valid base64!!!");
    EXPECT_FALSE(result.has_value());
}

TEST(FormatBufferTest, EmptyBase64) {
    auto result = FormatBuffer::fromBase64("");
    EXPECT_FALSE(result.has_value());
}

TEST(FormatBufferTest, UpdateCategory) {
    FormatBuffer f;
    f.setCategory(NumberFormatCategory::NUMBER);
    EXPECT_EQ(f.getCategory(), NumberFormatCategory::NUMBER);

    f.setCategory(NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(f.getCategory(), NumberFormatCategory::PERCENTAGE);
}

TEST(FormatBufferTest, UpdateDecimals) {
    FormatBuffer f;
    f.setDecimals(2);
    EXPECT_EQ(f.getDecimals(), 2);

    f.setDecimals(4);
    EXPECT_EQ(f.getDecimals(), 4);
}

TEST(FormatBufferTest, UpdateCurrencySymbol) {
    FormatBuffer f;
    f.setCurrencySymbol("$");
    EXPECT_EQ(f.getCurrencySymbol(), "$");

    f.setCurrencySymbol("€");
    EXPECT_EQ(f.getCurrencySymbol(), "€");

    // Verify round-trip after update
    auto decoded = FormatBuffer::fromBase64(f.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getCurrencySymbol(), "€");
}

TEST(FormatBufferTest, UpdateCustomFormatCode) {
    FormatBuffer f;
    f.setCustomFormatCode("# BANANA");
    EXPECT_EQ(f.getCustomFormatCode(), "# BANANA");

    f.setCustomFormatCode("# APPLE");
    EXPECT_EQ(f.getCustomFormatCode(), "# APPLE");
}

// =============================================================================
// Order Independence Tests
// =============================================================================

TEST(FormatBufferTest, PropertyOrderIndependence) {
    // Set properties in different orders, should get same binary

    FormatBuffer a;
    a.setCategory(NumberFormatCategory::CURRENCY);
    a.setDecimals(2);
    a.setThousandsSeparator(true);
    a.setCurrencySymbol("$");

    FormatBuffer b;
    b.setCurrencySymbol("$");
    b.setThousandsSeparator(true);
    b.setDecimals(2);
    b.setCategory(NumberFormatCategory::CURRENCY);

    FormatBuffer c;
    c.setDecimals(2);
    c.setCurrencySymbol("$");
    c.setCategory(NumberFormatCategory::CURRENCY);
    c.setThousandsSeparator(true);

    EXPECT_EQ(a.toBase64(), b.toBase64());
    EXPECT_EQ(b.toBase64(), c.toBase64());
}

// =============================================================================
// Real-World Format Examples
// =============================================================================

TEST(FormatBufferTest, StandardFormats) {
    // USD currency with 2 decimals
    {
        FormatBuffer f;
        f.setCategory(NumberFormatCategory::CURRENCY);
        f.setDecimals(2);
        f.setThousandsSeparator(true);
        f.setCurrencySymbol("$");

        auto decoded = FormatBuffer::fromBase64(f.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->toFormatCode(), "$#,##0.00");
    }

    // Percentage with 2 decimals
    {
        FormatBuffer f;
        f.setCategory(NumberFormatCategory::PERCENTAGE);
        f.setDecimals(2);

        auto decoded = FormatBuffer::fromBase64(f.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->toFormatCode(), "0.00%");
    }

    // Integer with thousands separator
    {
        FormatBuffer f;
        f.setCategory(NumberFormatCategory::NUMBER);
        f.setDecimals(0);
        f.setThousandsSeparator(true);

        auto decoded = FormatBuffer::fromBase64(f.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->toFormatCode(), "#,##0");
    }
}

}  // namespace
}  // namespace cells
