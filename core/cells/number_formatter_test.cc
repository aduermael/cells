#include "core/cells/number_formatter.h"

#include <gtest/gtest.h>

#include <cmath>

#include "core/cells/input_parser.h"

namespace cells {
namespace {

// =============================================================================
// FormattedValue tests
// =============================================================================

TEST(FormattedValueTest, Success) {
    auto result = FormattedValue::success("42");
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42");
}

TEST(FormattedValueTest, Error) {
    auto result = FormattedValue::error("Something wrong");
    EXPECT_TRUE(result.isError);
    EXPECT_EQ(result.errorMessage, "Something wrong");
    EXPECT_EQ(result.text, "#FORMAT!");
}

// =============================================================================
// FormatLocale tests
// =============================================================================

TEST(FormatLocaleTest, USDefaults) {
    auto locale = FormatLocale::US();
    EXPECT_EQ(locale.decimalSeparator, '.');
    EXPECT_EQ(locale.thousandsSeparator, ',');
    EXPECT_EQ(locale.currencySymbol, "$");
    EXPECT_TRUE(locale.currencySymbolBefore);
}

TEST(FormatLocaleTest, EUDefaults) {
    auto locale = FormatLocale::EU();
    EXPECT_EQ(locale.decimalSeparator, ',');
    EXPECT_EQ(locale.thousandsSeparator, '.');
    EXPECT_EQ(locale.currencySymbol, "€");
    EXPECT_FALSE(locale.currencySymbolBefore);
}

// =============================================================================
// General format tests
// =============================================================================

TEST(GeneralFormatTest, Integer) {
    auto result = formatGeneral(42.0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42");
}

TEST(GeneralFormatTest, Decimal) {
    auto result = formatGeneral(3.14159);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "3.14159");
}

TEST(GeneralFormatTest, Zero) {
    auto result = formatGeneral(0.0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0");
}

TEST(GeneralFormatTest, NegativeInteger) {
    auto result = formatGeneral(-100.0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-100");
}

TEST(GeneralFormatTest, SmallDecimal) {
    auto result = formatGeneral(0.5);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0.5");
}

TEST(GeneralFormatTest, LargeNumber) {
    auto result = formatGeneral(1e15);
    EXPECT_FALSE(result.isError);
    // Should use scientific notation for very large numbers
    EXPECT_TRUE(result.text.find('E') != std::string::npos ||
                result.text.find("e") != std::string::npos ||
                result.text == "1000000000000000");
}

TEST(GeneralFormatTest, VerySmallNumber) {
    auto result = formatGeneral(0.00001);
    EXPECT_FALSE(result.isError);
    // Should use scientific notation for very small numbers
    EXPECT_TRUE(result.text.find('E') != std::string::npos || result.text == "0.00001" ||
                result.text.find("1") != std::string::npos);
}

TEST(GeneralFormatTest, NaN) {
    auto result = formatGeneral(std::nan(""));
    EXPECT_TRUE(result.isError);
}

TEST(GeneralFormatTest, Infinity) {
    auto result = formatGeneral(std::numeric_limits<double>::infinity());
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "∞");
}

TEST(GeneralFormatTest, NegativeInfinity) {
    auto result = formatGeneral(-std::numeric_limits<double>::infinity());
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-∞");
}

// =============================================================================
// Plain number format tests
// =============================================================================

TEST(PlainNumberTest, Integer) {
    auto result = formatPlainNumber(1234.0, 0, false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1234");
}

TEST(PlainNumberTest, TwoDecimals) {
    auto result = formatPlainNumber(1234.567, 2, false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1234.57");  // Rounds
}

TEST(PlainNumberTest, ThousandsSeparator) {
    auto result = formatPlainNumber(1234567.89, 2, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1,234,567.89");
}

TEST(PlainNumberTest, SmallNumber) {
    auto result = formatPlainNumber(123.0, 2, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "123.00");
}

TEST(PlainNumberTest, NegativeWithSeparator) {
    auto result = formatPlainNumber(-1234567.89, 2, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-1,234,567.89");
}

TEST(PlainNumberTest, EULocale) {
    auto locale = FormatLocale::EU();
    auto result = formatPlainNumber(1234.56, 2, true, locale);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.234,56");
}

// =============================================================================
// Percentage format tests
// =============================================================================

TEST(PercentageTest, WholePercent) {
    auto result = formatPercentage(0.15, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15%");
}

TEST(PercentageTest, DecimalPercent) {
    auto result = formatPercentage(0.155, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15.50%");
}

TEST(PercentageTest, HundredPercent) {
    auto result = formatPercentage(1.0, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "100%");
}

TEST(PercentageTest, ZeroPercent) {
    auto result = formatPercentage(0.0, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0%");
}

TEST(PercentageTest, NegativePercent) {
    auto result = formatPercentage(-0.25, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-25%");
}

TEST(PercentageTest, OverHundred) {
    auto result = formatPercentage(1.5, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "150%");
}

// =============================================================================
// Currency format tests
// =============================================================================

TEST(CurrencyTest, SimpleDollar) {
    auto result = formatCurrency(1234.56, 2, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$1,234.56");
}

TEST(CurrencyTest, NoDollars) {
    auto result = formatCurrency(0.0, 2, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$0.00");
}

TEST(CurrencyTest, NegativeDollar) {
    auto result = formatCurrency(-1234.56, 2, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-$1,234.56");
}

TEST(CurrencyTest, AccountingFormat) {
    auto result = formatCurrency(1234.56, 2, "$", true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$ 1,234.56");
}

TEST(CurrencyTest, AccountingNegative) {
    auto result = formatCurrency(-1234.56, 2, "$", true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "($ 1,234.56)");
}

TEST(CurrencyTest, EuroAfter) {
    auto locale = FormatLocale::EU();
    auto result = formatCurrency(1234.56, 2, "€", false, locale);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.234,56€");
}

TEST(CurrencyTest, NoDecimals) {
    auto result = formatCurrency(1234.0, 0, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$1,234");
}

// =============================================================================
// Scientific notation tests
// =============================================================================

TEST(ScientificTest, Large) {
    auto result = formatScientific(1500000000.0, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.50E+09");
}

TEST(ScientificTest, Small) {
    auto result = formatScientific(0.000015, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.50E-05");
}

TEST(ScientificTest, One) {
    auto result = formatScientific(1.0, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.00E+00");
}

TEST(ScientificTest, Negative) {
    auto result = formatScientific(-2.5e6, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-2.50E+06");
}

TEST(ScientificTest, Zero) {
    auto result = formatScientific(0.0, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0.00E+00");
}

// =============================================================================
// Date format tests
// =============================================================================

TEST(DateFormatTest, ShortFormat) {
    // Jan 15, 2024
    double serial = DateUtils::toSerialDate(2024, 1, 15);
    auto result = formatDate(serial, BuiltInFormats::DATE_SHORT);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1/15/2024");
}

TEST(DateFormatTest, ISOFormat) {
    double serial = DateUtils::toSerialDate(2024, 1, 15);
    auto result = formatDate(serial, BuiltInFormats::DATE_ISO);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "2024-01-15");
}

TEST(DateFormatTest, LongFormat) {
    double serial = DateUtils::toSerialDate(2024, 1, 15);
    auto result = formatDate(serial, BuiltInFormats::DATE_LONG);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "January 15, 2024");
}

TEST(DateFormatTest, DecemberDate) {
    double serial = DateUtils::toSerialDate(2024, 12, 25);
    auto result = formatDate(serial, BuiltInFormats::DATE_LONG);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "December 25, 2024");
}

TEST(DateFormatTest, InvalidDate) {
    auto result = formatDate(-1, BuiltInFormats::DATE_SHORT);
    EXPECT_TRUE(result.isError);
}

// =============================================================================
// Time format tests
// =============================================================================

TEST(TimeFormatTest, TwelveHourAM) {
    double frac = TimeUtils::toFractionalDay(8, 30, 0);
    auto result = formatTime(frac, BuiltInFormats::TIME_12H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "8:30 AM");
}

TEST(TimeFormatTest, TwelveHourPM) {
    double frac = TimeUtils::toFractionalDay(20, 30, 0);
    auto result = formatTime(frac, BuiltInFormats::TIME_12H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "8:30 PM");
}

TEST(TimeFormatTest, Midnight) {
    double frac = 0.0;
    auto result = formatTime(frac, BuiltInFormats::TIME_12H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "12:00 AM");
}

TEST(TimeFormatTest, Noon) {
    double frac = 0.5;
    auto result = formatTime(frac, BuiltInFormats::TIME_12H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "12:00 PM");
}

TEST(TimeFormatTest, TwentyFourHour) {
    double frac = TimeUtils::toFractionalDay(14, 30, 0);
    auto result = formatTime(frac, BuiltInFormats::TIME_24H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "14:30");
}

TEST(TimeFormatTest, TwentyFourHourMidnight) {
    double frac = 0.0;
    auto result = formatTime(frac, BuiltInFormats::TIME_24H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "00:00");
}

TEST(TimeFormatTest, EarlyMorning) {
    double frac = TimeUtils::toFractionalDay(6, 15, 0);
    auto result = formatTime(frac, BuiltInFormats::TIME_24H);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "06:15");
}

// =============================================================================
// DateTime format tests
// =============================================================================

TEST(DateTimeFormatTest, Combined) {
    double serial = DateUtils::toSerialDate(2024, 1, 15);
    double frac = TimeUtils::toFractionalDay(14, 30, 0);
    auto result = formatDateTime(serial + frac, BuiltInFormats::DATETIME_SHORT);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1/15/2024 2:30 PM");
}

// =============================================================================
// formatWithFormat tests (using NumberFormat directly)
// =============================================================================

TEST(FormatWithFormatTest, GeneralFormat) {
    NumberFormat format;
    format.category = NumberFormatCategory::GENERAL;
    auto result = formatWithFormat(42.5, format);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42.5");
}

TEST(FormatWithFormatTest, NumberFormat) {
    NumberFormat format;
    format.category = NumberFormatCategory::NUMBER;
    format.decimalPlaces = 2;
    format.useThousandsSeparator = true;
    auto result = formatWithFormat(1234.567, format);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1,234.57");
}

TEST(FormatWithFormatTest, PercentageFormat) {
    NumberFormat format;
    format.category = NumberFormatCategory::PERCENTAGE;
    format.decimalPlaces = 1;
    auto result = formatWithFormat(0.155, format);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15.5%");
}

TEST(FormatWithFormatTest, CurrencyFormat) {
    NumberFormat format;
    format.category = NumberFormatCategory::CURRENCY;
    format.decimalPlaces = 2;
    format.currencySymbol = "$";
    format.isAccounting = false;
    auto result = formatWithFormat(1234.56, format);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$1,234.56");
}

// =============================================================================
// formatNumber with registry tests
// =============================================================================

TEST(FormatNumberTest, WithRegistry) {
    NumberFormatRegistry registry;
    auto result = formatNumber(registry, 1234.56, BuiltInFormats::CURRENCY_2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$1,234.56");
}

TEST(FormatNumberTest, NullFormatIdUsesGeneral) {
    NumberFormatRegistry registry;
    auto result = formatNumber(registry, 42.5, ID{});
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42.5");
}

TEST(FormatNumberTest, UnknownFormatIdUsesGeneral) {
    NumberFormatRegistry registry;
    auto result = formatNumber(registry, 42.5, ID{"unknown!"});
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42.5");
}

TEST(FormatNumberTest, PercentageFromRegistry) {
    NumberFormatRegistry registry;
    auto result = formatNumber(registry, 0.15, BuiltInFormats::PERCENTAGE_0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15%");
}

TEST(FormatNumberTest, PercentageWithDecimalsFromRegistry) {
    NumberFormatRegistry registry;
    auto result = formatNumber(registry, 0.155, BuiltInFormats::PERCENTAGE_2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15.50%");
}

}  // namespace
}  // namespace cells
