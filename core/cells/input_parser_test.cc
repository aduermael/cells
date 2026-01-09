#include "core/cells/input_parser.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

// =============================================================================
// ParsedInput factory tests
// =============================================================================

TEST(ParsedInputTest, NumberFactory) {
    auto result = ParsedInput::number(42.5, BuiltInFormats::NUMBER_2, NumberFormatCategory::NUMBER);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.numericValue, 42.5);
    EXPECT_EQ(result.valueType, CellValueType::NUMBER);
    EXPECT_EQ(result.formatId, BuiltInFormats::NUMBER_2);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::NUMBER);
}

TEST(ParsedInputTest, TextFactory) {
    auto result = ParsedInput::text("hello");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stringValue, "hello");
    EXPECT_EQ(result.valueType, CellValueType::STRING);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TEXT);
}

TEST(ParsedInputTest, ErrorFactory) {
    auto result = ParsedInput::error("Something went wrong");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorMessage, "Something went wrong");
}

// =============================================================================
// Percentage parsing tests
// =============================================================================

TEST(PercentageParsingTest, SimplePercentage) {
    auto result = parsePercentage("15%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.15);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(result.formatId, BuiltInFormats::PERCENTAGE_0);
}

TEST(PercentageParsingTest, PercentageWithSpace) {
    auto result = parsePercentage("15 %");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.15);
}

TEST(PercentageParsingTest, NegativePercentage) {
    auto result = parsePercentage("-25%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, -0.25);
}

TEST(PercentageParsingTest, DecimalPercentage) {
    // 15.5% has 1 decimal place, so should use PERCENTAGE_1
    auto result = parsePercentage("15.5%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.155);
    EXPECT_EQ(result.formatId, BuiltInFormats::PERCENTAGE_1);
}

TEST(PercentageParsingTest, TwoDecimalPercentage) {
    // 15.50% has 2 decimal places, so should use PERCENTAGE_2
    auto result = parsePercentage("15.50%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.155);
    EXPECT_EQ(result.formatId, BuiltInFormats::PERCENTAGE_2);
}

TEST(PercentageParsingTest, ThreeDecimalPercentage) {
    // 15.123% has 3 decimal places, so should use PERCENTAGE_3
    auto result = parsePercentage("15.123%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.15123);
    EXPECT_EQ(result.formatId, BuiltInFormats::PERCENTAGE_3);
}

TEST(PercentageParsingTest, ZeroPercentage) {
    auto result = parsePercentage("0%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.0);
}

TEST(PercentageParsingTest, HundredPercentage) {
    auto result = parsePercentage("100%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1.0);
}

TEST(PercentageParsingTest, NotPercentage) {
    auto result = parsePercentage("15");
    EXPECT_FALSE(result.success);
}

TEST(PercentageParsingTest, PercentageWithCommas) {
    auto result = parsePercentage("1,234.56%");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 12.3456);
}

// =============================================================================
// Currency parsing tests
// =============================================================================

TEST(CurrencyParsingTest, SimpleDollar) {
    auto result = parseCurrency("$1234");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.0);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::CURRENCY);
    EXPECT_EQ(result.formatId, BuiltInFormats::CURRENCY_0);
}

TEST(CurrencyParsingTest, DollarWithDecimals) {
    auto result = parseCurrency("$1,234.56");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.56);
    EXPECT_EQ(result.formatId, BuiltInFormats::CURRENCY_2);
}

TEST(CurrencyParsingTest, NegativeDollar) {
    auto result = parseCurrency("-$500");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, -500.0);
}

TEST(CurrencyParsingTest, DollarNegative) {
    auto result = parseCurrency("$-500");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, -500.0);
}

TEST(CurrencyParsingTest, NoCurrencySymbol) {
    auto result = parseCurrency("1234");
    EXPECT_FALSE(result.success);
}

TEST(CurrencyParsingTest, OneDecimalPlace) {
    // $99.9 has 1 decimal place, so should use CURRENCY_1
    auto result = parseCurrency("$99.9");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 99.9);
    EXPECT_EQ(result.formatId, BuiltInFormats::CURRENCY_1);
}

TEST(CurrencyParsingTest, ThreeDecimalPlaces) {
    // $99.999 has 3 decimal places, so should use CURRENCY_3
    auto result = parseCurrency("$99.999");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 99.999);
    EXPECT_EQ(result.formatId, BuiltInFormats::CURRENCY_3);
}

// =============================================================================
// Date parsing tests
// =============================================================================

TEST(DateParsingTest, SlashFormat) {
    auto result = parseDate("1/15/2024");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
    EXPECT_EQ(result.formatId, BuiltInFormats::DATE_SHORT);
    // Jan 15, 2024 should be a specific serial date
    EXPECT_GT(result.numericValue, 45000);  // After 2023
}

TEST(DateParsingTest, ISOFormat) {
    auto result = parseDate("2024-01-15");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
    EXPECT_EQ(result.formatId, BuiltInFormats::DATE_ISO);
}

TEST(DateParsingTest, SlashAndISOSameValue) {
    auto slash = parseDate("1/15/2024");
    auto iso = parseDate("2024-01-15");
    EXPECT_TRUE(slash.success);
    EXPECT_TRUE(iso.success);
    EXPECT_DOUBLE_EQ(slash.numericValue, iso.numericValue);
}

TEST(DateParsingTest, InvalidMonth) {
    auto result = parseDate("13/15/2024");
    EXPECT_FALSE(result.success);
}

TEST(DateParsingTest, InvalidDay) {
    auto result = parseDate("2/30/2024");  // Feb 30 doesn't exist
    EXPECT_FALSE(result.success);
}

TEST(DateParsingTest, LeapYear) {
    auto result = parseDate("2/29/2024");  // 2024 is a leap year
    EXPECT_TRUE(result.success);
}

TEST(DateParsingTest, NotLeapYear) {
    auto result = parseDate("2/29/2023");  // 2023 is not a leap year
    EXPECT_FALSE(result.success);
}

// =============================================================================
// 2-digit year parsing tests
// =============================================================================

TEST(DateParsingTest, TwoDigitYear00) {
    // 00 -> 2000
    auto result = parseDate("1/15/00");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2000);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, TwoDigitYear25) {
    // 25 -> 2025
    auto result = parseDate("12/12/25");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 12);
    EXPECT_EQ(day, 12);
}

TEST(DateParsingTest, TwoDigitYear29) {
    // 29 -> 2029 (boundary)
    auto result = parseDate("6/1/29");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2029);
}

TEST(DateParsingTest, TwoDigitYear30) {
    // 30 -> 1930 (boundary)
    auto result = parseDate("6/1/30");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 1930);
}

TEST(DateParsingTest, TwoDigitYear99) {
    // 99 -> 1999
    auto result = parseDate("12/31/99");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 1999);
}

// =============================================================================
// Text month name parsing tests
// =============================================================================

TEST(DateParsingTest, MonthNameDayYear) {
    // "Jan 15, 2025"
    auto result = parseDate("Jan 15, 2025");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, FullMonthNameDayYear) {
    // "January 15, 2025"
    auto result = parseDate("January 15, 2025");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, MonthNameDayYearNoComma) {
    // "Jan 15 2025" (without comma)
    auto result = parseDate("Jan 15 2025");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, DayMonthNameYearSpaces) {
    // "15 Jan 2025"
    auto result = parseDate("15 Jan 2025");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, DayMonthNameYearDashes) {
    // "15-Jan-2025"
    auto result = parseDate("15-Jan-2025");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(year, 2025);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateParsingTest, AllMonthNames) {
    // Test all 12 months
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        std::string input = std::string(months[i]) + " 1, 2025";
        auto result = parseDate(input);
        EXPECT_TRUE(result.success) << "Failed for " << months[i];
        int year = 0, month = 0, day = 0;
        DateUtils::fromSerialDate(result.numericValue, year, month, day);
        EXPECT_EQ(month, i + 1) << "Failed for " << months[i];
    }
}

// =============================================================================
// Short date format tests (no year)
// =============================================================================

TEST(DateParsingTest, ShortSlashFormat) {
    // "1/15" -> January 15 of current year
    auto result = parseDate("1/15");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
    // Year should be current year (can't check exact value as it changes)
    EXPECT_GE(year, 2020);  // Reasonable lower bound
}

TEST(DateParsingTest, ShortMonthNameFormat) {
    // "Jan 15" -> January 15 of current year
    auto result = parseDate("Jan 15");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
    EXPECT_GE(year, 2020);
}

TEST(DateParsingTest, ShortFullMonthNameFormat) {
    // "December 25" -> December 25 of current year
    auto result = parseDate("December 25");
    EXPECT_TRUE(result.success);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(result.numericValue, year, month, day);
    EXPECT_EQ(month, 12);
    EXPECT_EQ(day, 25);
}

// =============================================================================
// Time parsing tests
// =============================================================================

TEST(TimeParsingTest, TwelveHourAM) {
    auto result = parseTime("8:30 AM");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TIME);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_12H);
    // 8:30 AM = 8.5/24 = 0.354166...
    EXPECT_NEAR(result.numericValue, 8.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, TwelveHourPM) {
    auto result = parseTime("8:30 PM");
    EXPECT_TRUE(result.success);
    // 8:30 PM = 20:30 = 20.5/24 = 0.854166...
    EXPECT_NEAR(result.numericValue, 20.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, TwentyFourHour) {
    auto result = parseTime("14:30");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_24H);
    // 14:30 = 14.5/24 = 0.604166...
    EXPECT_NEAR(result.numericValue, 14.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, Midnight) {
    auto result = parseTime("12:00 AM");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 0.0, 0.0001);
}

TEST(TimeParsingTest, Noon) {
    auto result = parseTime("12:00 PM");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 0.5, 0.0001);
}

TEST(TimeParsingTest, WithSeconds) {
    auto result = parseTime("14:30:45");
    EXPECT_TRUE(result.success);
}

TEST(TimeParsingTest, InvalidHours) {
    auto result = parseTime("25:00");
    EXPECT_FALSE(result.success);
}

TEST(TimeParsingTest, InvalidMinutes) {
    auto result = parseTime("12:60");
    EXPECT_FALSE(result.success);
}

// =============================================================================
// Time format variations (Phase 5)
// =============================================================================

// 5a: Time without seconds (already supported)
TEST(TimeParsingTest, WithoutSeconds24H) {
    auto result = parseTime("9:30");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_24H);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, WithoutSeconds12H) {
    auto result = parseTime("9:30 AM");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_12H);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

// 5b: Lowercase am/pm
TEST(TimeParsingTest, LowercaseAM) {
    auto result = parseTime("9:30 am");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_12H);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, LowercasePM) {
    auto result = parseTime("9:30 pm");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 21.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, LowercaseNoSpace) {
    auto result = parseTime("9:30pm");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 21.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, MixedCaseAm) {
    auto result = parseTime("9:30 Am");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

// 5c: Period notation (a.m. / p.m.)
TEST(TimeParsingTest, PeriodNotationAM) {
    auto result = parseTime("9:30 a.m.");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatId, BuiltInFormats::TIME_12H);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, PeriodNotationPM) {
    auto result = parseTime("9:30 p.m.");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 21.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, PeriodNotationNoSpace) {
    auto result = parseTime("9:30p.m.");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 21.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, PeriodNotationUppercase) {
    auto result = parseTime("9:30 A.M.");
    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.numericValue, 9.5 / 24.0, 0.0001);
}

TEST(TimeParsingTest, PeriodNotationWithSeconds) {
    auto result = parseTime("9:30:45 p.m.");
    EXPECT_TRUE(result.success);
    // 21:30:45 = (21*3600 + 30*60 + 45) / 86400
    double expected = (21 * 3600.0 + 30 * 60.0 + 45.0) / 86400.0;
    EXPECT_NEAR(result.numericValue, expected, 0.0001);
}

// =============================================================================
// Scientific notation tests
// =============================================================================

TEST(ScientificParsingTest, PositiveExponent) {
    auto result = parseScientific("1.5E+10");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1.5e10);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::SCIENTIFIC);
}

TEST(ScientificParsingTest, NegativeExponent) {
    auto result = parseScientific("1.5e-5");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1.5e-5);
}

TEST(ScientificParsingTest, LowercaseE) {
    auto result = parseScientific("2.5e3");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 2500.0);
}

TEST(ScientificParsingTest, NoExponent) {
    auto result = parseScientific("1234");
    EXPECT_FALSE(result.success);
}

// =============================================================================
// Plain number parsing tests
// =============================================================================

TEST(NumberParsingTest, Integer) {
    auto result = parseNumber("1234");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.0);
}

TEST(NumberParsingTest, Decimal) {
    auto result = parseNumber("1234.56");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.56);
    EXPECT_EQ(result.formatId, BuiltInFormats::NUMBER_2);
}

TEST(NumberParsingTest, WithThousandsSeparator) {
    auto result = parseNumber("1,234");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.0);
    EXPECT_EQ(result.formatId, BuiltInFormats::NUMBER_SEP);
}

TEST(NumberParsingTest, WithThousandsSeparatorAndDecimals) {
    auto result = parseNumber("1,234.56");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.56);
    EXPECT_EQ(result.formatId, BuiltInFormats::NUMBER_SEP2);
}

TEST(NumberParsingTest, Negative) {
    auto result = parseNumber("-123.45");
    EXPECT_TRUE(result.success);
    EXPECT_DOUBLE_EQ(result.numericValue, -123.45);
}

TEST(NumberParsingTest, NotANumber) {
    auto result = parseNumber("hello");
    EXPECT_FALSE(result.success);
}

// =============================================================================
// Auto-detection tests (parseUserInput)
// =============================================================================

TEST(AutoDetectTest, DetectsPercentage) {
    auto result = parseUserInput("15%");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::PERCENTAGE);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.15);
}

TEST(AutoDetectTest, DetectsCurrency) {
    auto result = parseUserInput("$1,234.56");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::CURRENCY);
}

TEST(AutoDetectTest, DetectsDate) {
    auto result = parseUserInput("1/15/2024");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
}

TEST(AutoDetectTest, DetectsDateTwoDigitYear) {
    auto result = parseUserInput("12/12/25");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
}

TEST(AutoDetectTest, DetectsDateWithMonthName) {
    auto result = parseUserInput("Jan 15, 2025");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
}

TEST(AutoDetectTest, DetectsDateShortFormat) {
    auto result = parseUserInput("1/15");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
}

TEST(AutoDetectTest, DetectsDateShortMonthName) {
    auto result = parseUserInput("Jan 15");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::DATE);
}

TEST(AutoDetectTest, DetectsTime) {
    auto result = parseUserInput("12:30 PM");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TIME);
}

TEST(AutoDetectTest, DetectsTimeLowercase) {
    auto result = parseUserInput("9:30 am");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TIME);
}

TEST(AutoDetectTest, DetectsTimePeriodNotation) {
    auto result = parseUserInput("9:30 p.m.");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TIME);
}

TEST(AutoDetectTest, DetectsScientific) {
    auto result = parseUserInput("1.5E+10");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::SCIENTIFIC);
}

TEST(AutoDetectTest, PlainNumberUsesGeneral) {
    auto result = parseUserInput("1234");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::GENERAL);
    EXPECT_DOUBLE_EQ(result.numericValue, 1234.0);
}

TEST(AutoDetectTest, TextFallback) {
    auto result = parseUserInput("hello world");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.valueType, CellValueType::STRING);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::TEXT);
    EXPECT_EQ(result.stringValue, "hello world");
}

TEST(AutoDetectTest, EmptyInput) {
    auto result = parseUserInput("");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.valueType, CellValueType::STRING);
    EXPECT_EQ(result.stringValue, "");
}

TEST(AutoDetectTest, WhitespaceOnly) {
    auto result = parseUserInput("   ");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stringValue, "");
}

TEST(AutoDetectTest, LeadingTrailingWhitespace) {
    auto result = parseUserInput("  15%  ");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.formatCategory, NumberFormatCategory::PERCENTAGE);
    EXPECT_DOUBLE_EQ(result.numericValue, 0.15);
}

// =============================================================================
// Date utility tests
// =============================================================================

TEST(DateUtilsTest, IsLeapYear) {
    EXPECT_TRUE(DateUtils::isLeapYear(2024));   // Divisible by 4
    EXPECT_FALSE(DateUtils::isLeapYear(2023));  // Not divisible by 4
    EXPECT_FALSE(DateUtils::isLeapYear(1900));  // Divisible by 100 but not 400
    EXPECT_TRUE(DateUtils::isLeapYear(2000));   // Divisible by 400
}

TEST(DateUtilsTest, DaysInMonth) {
    EXPECT_EQ(DateUtils::daysInMonth(2024, 1), 31);   // January
    EXPECT_EQ(DateUtils::daysInMonth(2024, 2), 29);   // Feb in leap year
    EXPECT_EQ(DateUtils::daysInMonth(2023, 2), 28);   // Feb in non-leap year
    EXPECT_EQ(DateUtils::daysInMonth(2024, 4), 30);   // April
    EXPECT_EQ(DateUtils::daysInMonth(2024, 12), 31);  // December
}

TEST(DateUtilsTest, SerialDateRoundtrip) {
    // Test roundtrip for a known date
    double serial = DateUtils::toSerialDate(2024, 1, 15);
    int year = 0, month = 0, day = 0;
    DateUtils::fromSerialDate(serial, year, month, day);
    EXPECT_EQ(year, 2024);
    EXPECT_EQ(month, 1);
    EXPECT_EQ(day, 15);
}

TEST(DateUtilsTest, ExcelEpoch) {
    // Excel serial date 1 = January 1, 1900
    double serial = DateUtils::toSerialDate(1900, 1, 1);
    EXPECT_EQ(serial, 1.0);
}

// =============================================================================
// Time utility tests
// =============================================================================

TEST(TimeUtilsTest, ToFractionalDay) {
    EXPECT_DOUBLE_EQ(TimeUtils::toFractionalDay(0, 0, 0), 0.0);       // Midnight
    EXPECT_DOUBLE_EQ(TimeUtils::toFractionalDay(12, 0, 0), 0.5);      // Noon
    EXPECT_NEAR(TimeUtils::toFractionalDay(6, 0, 0), 0.25, 0.0001);   // 6 AM
    EXPECT_NEAR(TimeUtils::toFractionalDay(18, 0, 0), 0.75, 0.0001);  // 6 PM
}

TEST(TimeUtilsTest, FromFractionalDay) {
    int h = 0, m = 0, s = 0;
    TimeUtils::fromFractionalDay(0.5, h, m, s);
    EXPECT_EQ(h, 12);
    EXPECT_EQ(m, 0);
    EXPECT_EQ(s, 0);
}

TEST(TimeUtilsTest, Roundtrip) {
    double frac = TimeUtils::toFractionalDay(14, 30, 45);
    int h = 0, m = 0, s = 0;
    TimeUtils::fromFractionalDay(frac, h, m, s);
    EXPECT_EQ(h, 14);
    EXPECT_EQ(m, 30);
    EXPECT_EQ(s, 45);
}

}  // namespace
}  // namespace cells
