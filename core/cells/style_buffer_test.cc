// =============================================================================
// StyleBuffer Unit Tests
// =============================================================================

#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Empty Style Tests
// =============================================================================

TEST(StyleBufferTest, EmptyStyle) {
    StyleBuffer s;
    EXPECT_TRUE(s.isEmpty());
    EXPECT_EQ(s.getFlags(), 0);
    EXPECT_EQ(s.data().size(), 2u);  // Just flag bytes
}

TEST(StyleBufferTest, EmptyStyleRoundTrip) {
    StyleBuffer s;
    std::string b64 = s.toBase64();
    EXPECT_EQ(b64, "AAA=");  // Two zero bytes

    auto decoded = StyleBuffer::fromBase64(b64);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isEmpty());
    EXPECT_EQ(decoded->getFlags(), 0);
}

// =============================================================================
// Boolean Property Tests
// =============================================================================

TEST(StyleBufferTest, BoldOnly) {
    StyleBuffer s;
    s.setBold(true);

    EXPECT_TRUE(s.hasBold());
    EXPECT_TRUE(s.getBold());
    EXPECT_FALSE(s.hasItalic());
    EXPECT_FALSE(s.isEmpty());

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->hasBold());
    EXPECT_TRUE(decoded->getBold());
    EXPECT_FALSE(decoded->hasItalic());
}

TEST(StyleBufferTest, BoldFalseExplicit) {
    StyleBuffer s;
    s.setBold(false);  // Explicitly set to false

    EXPECT_TRUE(s.hasBold());   // Flag IS set
    EXPECT_FALSE(s.getBold());  // But value is false

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->hasBold());
    EXPECT_FALSE(decoded->getBold());
}

TEST(StyleBufferTest, AllBooleans) {
    StyleBuffer s;
    s.setBold(true);
    s.setItalic(true);
    s.setUnderline(false);
    s.setStrikethrough(true);
    s.setTextWrap(false);

    EXPECT_TRUE(s.hasBold());
    EXPECT_TRUE(s.hasItalic());
    EXPECT_TRUE(s.hasUnderline());
    EXPECT_TRUE(s.hasStrikethrough());
    EXPECT_TRUE(s.hasTextWrap());

    EXPECT_TRUE(s.getBold());
    EXPECT_TRUE(s.getItalic());
    EXPECT_FALSE(s.getUnderline());
    EXPECT_TRUE(s.getStrikethrough());
    EXPECT_FALSE(s.getTextWrap());

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->getBold());
    EXPECT_TRUE(decoded->getItalic());
    EXPECT_FALSE(decoded->getUnderline());
    EXPECT_TRUE(decoded->getStrikethrough());
    EXPECT_FALSE(decoded->getTextWrap());
}

TEST(StyleBufferTest, ClearBoolean) {
    StyleBuffer s;
    s.setBold(true);
    EXPECT_TRUE(s.hasBold());

    s.clearBold();
    EXPECT_FALSE(s.hasBold());
    EXPECT_FALSE(s.getBold());
}

// =============================================================================
// Color Tests
// =============================================================================

TEST(StyleBufferTest, BgColorRGB) {
    StyleBuffer s;
    s.setBgColor(0xFB, 0xBF, 0x24);

    EXPECT_TRUE(s.hasBgColor());
    uint8_t r, g, b;
    s.getBgColor(r, g, b);
    EXPECT_EQ(r, 0xFB);
    EXPECT_EQ(g, 0xBF);
    EXPECT_EQ(b, 0x24);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    decoded->getBgColor(r, g, b);
    EXPECT_EQ(r, 0xFB);
    EXPECT_EQ(g, 0xBF);
    EXPECT_EQ(b, 0x24);
}

TEST(StyleBufferTest, BgColorHex) {
    StyleBuffer s;
    s.setBgColorHex("#FBBF24");

    EXPECT_TRUE(s.hasBgColor());
    EXPECT_EQ(s.getBgColorHex(), "#FBBF24");
}

TEST(StyleBufferTest, TextColorRGB) {
    StyleBuffer s;
    s.setTextColor(0x00, 0x00, 0xFF);

    EXPECT_TRUE(s.hasTextColor());
    uint8_t r, g, b;
    s.getTextColor(r, g, b);
    EXPECT_EQ(r, 0x00);
    EXPECT_EQ(g, 0x00);
    EXPECT_EQ(b, 0xFF);
}

TEST(StyleBufferTest, BothColors) {
    StyleBuffer s;
    s.setBgColor(0xFF, 0xFF, 0x00);    // Yellow background
    s.setTextColor(0x00, 0x00, 0x00);  // Black text

    EXPECT_TRUE(s.hasBgColor());
    EXPECT_TRUE(s.hasTextColor());

    EXPECT_EQ(s.getBgColorHex(), "#FFFF00");
    EXPECT_EQ(s.getTextColorHex(), "#000000");

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getBgColorHex(), "#FFFF00");
    EXPECT_EQ(decoded->getTextColorHex(), "#000000");
}

TEST(StyleBufferTest, ClearColor) {
    StyleBuffer s;
    s.setBgColor(0xFF, 0x00, 0x00);
    EXPECT_TRUE(s.hasBgColor());

    s.clearBgColor();
    EXPECT_FALSE(s.hasBgColor());
}

// =============================================================================
// Font Size Tests
// =============================================================================

TEST(StyleBufferTest, FontSizeMinimum) {
    StyleBuffer s;
    s.setFontSize(6);  // Minimum

    EXPECT_TRUE(s.hasFontSize());
    EXPECT_EQ(s.getFontSize(), 6);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontSize(), 6);
}

TEST(StyleBufferTest, FontSizeDefault) {
    StyleBuffer s;
    s.setFontSize(11);  // Default

    EXPECT_EQ(s.getFontSize(), 11);
}

TEST(StyleBufferTest, FontSizeLarge) {
    StyleBuffer s;
    s.setFontSize(72);  // Max practical

    EXPECT_EQ(s.getFontSize(), 72);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontSize(), 72);
}

TEST(StyleBufferTest, FontSizeNotSet) {
    StyleBuffer s;
    EXPECT_FALSE(s.hasFontSize());
    EXPECT_EQ(s.getFontSize(), 0);
}

// =============================================================================
// Font Family Tests
// =============================================================================

TEST(StyleBufferTest, FontFamilySimple) {
    StyleBuffer s;
    s.setFontFamily("Arial");

    EXPECT_TRUE(s.hasFontFamily());
    EXPECT_EQ(s.getFontFamily(), "Arial");

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontFamily(), "Arial");
}

TEST(StyleBufferTest, FontFamilyWithSpaces) {
    StyleBuffer s;
    s.setFontFamily("Times New Roman");

    EXPECT_EQ(s.getFontFamily(), "Times New Roman");
}

TEST(StyleBufferTest, FontFamilyUnicode) {
    StyleBuffer s;
    s.setFontFamily("Helvetica Neue");

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontFamily(), "Helvetica Neue");
}

TEST(StyleBufferTest, FontFamilyClear) {
    StyleBuffer s;
    s.setFontFamily("Arial");
    EXPECT_TRUE(s.hasFontFamily());

    s.clearFontFamily();
    EXPECT_FALSE(s.hasFontFamily());
    EXPECT_EQ(s.getFontFamily(), "");
}

// =============================================================================
// Alignment Tests
// =============================================================================

TEST(StyleBufferTest, HAlignCenter) {
    StyleBuffer s;
    s.setHAlign(TextAlign::CENTER);

    EXPECT_TRUE(s.hasHAlign());
    EXPECT_EQ(s.getHAlign(), TextAlign::CENTER);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getHAlign(), TextAlign::CENTER);
}

TEST(StyleBufferTest, VAlignMiddle) {
    StyleBuffer s;
    s.setVAlign(VerticalAlign::MIDDLE);

    EXPECT_TRUE(s.hasVAlign());
    EXPECT_EQ(s.getVAlign(), VerticalAlign::MIDDLE);
}

TEST(StyleBufferTest, BothAlignments) {
    StyleBuffer s;
    s.setHAlign(TextAlign::RIGHT);
    s.setVAlign(VerticalAlign::TOP);

    EXPECT_TRUE(s.hasHAlign());
    EXPECT_TRUE(s.hasVAlign());
    EXPECT_EQ(s.getHAlign(), TextAlign::RIGHT);
    EXPECT_EQ(s.getVAlign(), VerticalAlign::TOP);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getHAlign(), TextAlign::RIGHT);
    EXPECT_EQ(decoded->getVAlign(), VerticalAlign::TOP);
}

TEST(StyleBufferTest, AllAlignmentValues) {
    // Test all horizontal alignments
    for (auto h : {TextAlign::LEFT, TextAlign::CENTER, TextAlign::RIGHT, TextAlign::JUSTIFY,
                   TextAlign::GENERAL}) {
        StyleBuffer s;
        s.setHAlign(h);
        auto decoded = StyleBuffer::fromBase64(s.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->getHAlign(), h);
    }

    // Test all vertical alignments
    for (auto v : {VerticalAlign::TOP, VerticalAlign::MIDDLE, VerticalAlign::BOTTOM}) {
        StyleBuffer s;
        s.setVAlign(v);
        auto decoded = StyleBuffer::fromBase64(s.toBase64());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->getVAlign(), v);
    }
}

// =============================================================================
// Border Tests
// =============================================================================

TEST(StyleBufferTest, BorderSingleSide) {
    StyleBuffer s;
    s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);

    EXPECT_TRUE(s.hasBorder());
    EXPECT_TRUE(s.hasBorderTop());
    EXPECT_FALSE(s.hasBorderRight());
    EXPECT_FALSE(s.hasBorderBottom());
    EXPECT_FALSE(s.hasBorderLeft());

    EXPECT_EQ(s.getBorderTopStyle(), BorderStyle::THIN);
    EXPECT_EQ(s.getBorderTopColorHex(), "#000000");

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->hasBorderTop());
    EXPECT_EQ(decoded->getBorderTopStyle(), BorderStyle::THIN);
}

TEST(StyleBufferTest, BorderAllSides) {
    StyleBuffer s;
    s.setBorderTop(BorderStyle::THIN, 0xFF, 0x00, 0x00);
    s.setBorderRight(BorderStyle::MEDIUM, 0x00, 0xFF, 0x00);
    s.setBorderBottom(BorderStyle::THICK, 0x00, 0x00, 0xFF);
    s.setBorderLeft(BorderStyle::DASHED, 0xFF, 0xFF, 0x00);

    EXPECT_TRUE(s.hasBorderTop());
    EXPECT_TRUE(s.hasBorderRight());
    EXPECT_TRUE(s.hasBorderBottom());
    EXPECT_TRUE(s.hasBorderLeft());

    EXPECT_EQ(s.getBorderTopStyle(), BorderStyle::THIN);
    EXPECT_EQ(s.getBorderRightStyle(), BorderStyle::MEDIUM);
    EXPECT_EQ(s.getBorderBottomStyle(), BorderStyle::THICK);
    EXPECT_EQ(s.getBorderLeftStyle(), BorderStyle::DASHED);

    EXPECT_EQ(s.getBorderTopColorHex(), "#FF0000");
    EXPECT_EQ(s.getBorderRightColorHex(), "#00FF00");
    EXPECT_EQ(s.getBorderBottomColorHex(), "#0000FF");
    EXPECT_EQ(s.getBorderLeftColorHex(), "#FFFF00");

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getBorderTopStyle(), BorderStyle::THIN);
    EXPECT_EQ(decoded->getBorderRightStyle(), BorderStyle::MEDIUM);
    EXPECT_EQ(decoded->getBorderBottomStyle(), BorderStyle::THICK);
    EXPECT_EQ(decoded->getBorderLeftStyle(), BorderStyle::DASHED);
}

TEST(StyleBufferTest, BorderClearSide) {
    StyleBuffer s;
    s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);
    s.setBorderBottom(BorderStyle::THIN, 0x00, 0x00, 0x00);

    EXPECT_TRUE(s.hasBorderTop());
    EXPECT_TRUE(s.hasBorderBottom());

    s.clearBorderTop();
    EXPECT_FALSE(s.hasBorderTop());
    EXPECT_TRUE(s.hasBorderBottom());

    s.clearBorderBottom();
    EXPECT_FALSE(s.hasBorder());
}

TEST(StyleBufferTest, BorderClearAll) {
    StyleBuffer s;
    s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);
    s.setBorderRight(BorderStyle::THIN, 0x00, 0x00, 0x00);

    s.clearBorder();
    EXPECT_FALSE(s.hasBorder());
    EXPECT_FALSE(s.hasBorderTop());
    EXPECT_FALSE(s.hasBorderRight());
}

TEST(StyleBufferTest, BorderWithHex) {
    StyleBuffer s;
    s.setBorderTopHex(BorderStyle::DOUBLE, "#FF5733");

    EXPECT_EQ(s.getBorderTopStyle(), BorderStyle::DOUBLE);
    EXPECT_EQ(s.getBorderTopColorHex(), "#FF5733");
}

// =============================================================================
// Complex Style Tests
// =============================================================================

TEST(StyleBufferTest, ComplexStyle) {
    StyleBuffer s;
    s.setBold(true);
    s.setItalic(true);
    s.setBgColor(0xFF, 0xFF, 0x00);
    s.setTextColor(0x00, 0x00, 0x00);
    s.setFontSize(14);
    s.setFontFamily("Helvetica");
    s.setHAlign(TextAlign::CENTER);
    s.setVAlign(VerticalAlign::MIDDLE);
    s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());

    EXPECT_TRUE(decoded->getBold());
    EXPECT_TRUE(decoded->getItalic());
    EXPECT_FALSE(decoded->hasUnderline());
    EXPECT_EQ(decoded->getFontSize(), 14);
    EXPECT_EQ(decoded->getFontFamily(), "Helvetica");
    EXPECT_EQ(decoded->getHAlign(), TextAlign::CENTER);
    EXPECT_EQ(decoded->getVAlign(), VerticalAlign::MIDDLE);
    EXPECT_TRUE(decoded->hasBorderTop());
    EXPECT_FALSE(decoded->hasBorderBottom());
}

// =============================================================================
// Determinism Tests
// =============================================================================

TEST(StyleBufferTest, Deterministic) {
    StyleBuffer s1, s2;

    // Set in different orders
    s1.setBold(true);
    s1.setBgColor(0xFB, 0xBF, 0x24);

    s2.setBgColor(0xFB, 0xBF, 0x24);  // Different order
    s2.setBold(true);

    // Same result regardless of order
    EXPECT_EQ(s1.toBase64(), s2.toBase64());
    EXPECT_EQ(s1, s2);
}

TEST(StyleBufferTest, ContentIdentity) {
    StyleBuffer s1, s2;
    s1.setBold(true);
    s2.setBold(true);

    // Same content = same base64 = same identity
    EXPECT_EQ(s1.toBase64(), s2.toBase64());
    EXPECT_EQ(s1, s2);

    StyleBuffer s3;
    s3.setBold(false);  // Explicit false is different from no bold

    StyleBuffer s4;  // No bold set at all

    EXPECT_NE(s3.toBase64(), s4.toBase64());
    EXPECT_NE(s3, s4);
}

// =============================================================================
// CellStyle Conversion Tests
// =============================================================================

TEST(StyleBufferTest, FromCellStyle) {
    CellStyle cs;
    cs.bold = true;
    cs.setDefined(DEFINED_BOLD);
    cs.bgColor = "#FF0000";
    cs.setDefined(DEFINED_BGCOLOR);
    cs.fontSize = 16;
    cs.setDefined(DEFINED_FONTSIZE);

    StyleBuffer buf = StyleBuffer::fromCellStyle(cs);

    EXPECT_TRUE(buf.hasBold());
    EXPECT_TRUE(buf.getBold());
    EXPECT_TRUE(buf.hasBgColor());
    EXPECT_EQ(buf.getBgColorHex(), "#FF0000");
    EXPECT_TRUE(buf.hasFontSize());
    EXPECT_EQ(buf.getFontSize(), 16);
}

TEST(StyleBufferTest, ToCellStyle) {
    StyleBuffer buf;
    buf.setBold(true);
    buf.setBgColorHex("#00FF00");
    buf.setFontSize(20);
    buf.setHAlign(TextAlign::CENTER);

    CellStyle cs = buf.toCellStyle();

    EXPECT_TRUE(cs.isDefined(DEFINED_BOLD));
    EXPECT_TRUE(cs.bold);
    EXPECT_TRUE(cs.isDefined(DEFINED_BGCOLOR));
    EXPECT_EQ(cs.bgColor, "#00FF00");
    EXPECT_TRUE(cs.isDefined(DEFINED_FONTSIZE));
    EXPECT_EQ(cs.fontSize, 20);
    EXPECT_TRUE(cs.isDefined(DEFINED_HALIGN));
    EXPECT_EQ(cs.hAlign, TextAlign::CENTER);
}

TEST(StyleBufferTest, CellStyleRoundTrip) {
    CellStyle original;
    original.bold = true;
    original.setDefined(DEFINED_BOLD);
    original.italic = false;
    original.setDefined(DEFINED_ITALIC);
    original.bgColor = "#FBBF24";
    original.setDefined(DEFINED_BGCOLOR);
    original.textColor = "#1F2937";
    original.setDefined(DEFINED_TEXTCOLOR);
    original.fontSize = 14;
    original.setDefined(DEFINED_FONTSIZE);
    original.fontFamily = "Inter";
    original.setDefined(DEFINED_FONTFAMILY);
    original.hAlign = TextAlign::RIGHT;
    original.setDefined(DEFINED_HALIGN);
    original.vAlign = VerticalAlign::TOP;
    original.setDefined(DEFINED_VALIGN);

    StyleBuffer buf = StyleBuffer::fromCellStyle(original);
    CellStyle converted = buf.toCellStyle();

    EXPECT_EQ(converted.bold, original.bold);
    EXPECT_EQ(converted.italic, original.italic);
    EXPECT_EQ(converted.bgColor, original.bgColor);
    EXPECT_EQ(converted.textColor, original.textColor);
    EXPECT_EQ(converted.fontSize, original.fontSize);
    EXPECT_EQ(converted.fontFamily, original.fontFamily);
    EXPECT_EQ(converted.hAlign, original.hAlign);
    EXPECT_EQ(converted.vAlign, original.vAlign);
}

// =============================================================================
// Merge Tests
// =============================================================================

TEST(StyleBufferTest, MergeSimple) {
    StyleBuffer s1;
    s1.setBold(true);

    StyleBuffer s2;
    s2.setItalic(true);

    s1.merge(s2);

    EXPECT_TRUE(s1.hasBold());
    EXPECT_TRUE(s1.getBold());
    EXPECT_TRUE(s1.hasItalic());
    EXPECT_TRUE(s1.getItalic());
}

TEST(StyleBufferTest, MergeOverride) {
    StyleBuffer s1;
    s1.setBold(true);
    s1.setBgColorHex("#FF0000");

    StyleBuffer s2;
    s2.setBold(false);  // Override bold
    s2.setItalic(true);

    s1.merge(s2);

    EXPECT_TRUE(s1.hasBold());
    EXPECT_FALSE(s1.getBold());  // Overridden to false
    EXPECT_TRUE(s1.hasItalic());
    EXPECT_TRUE(s1.hasBgColor());  // Still has bgColor from s1
}

TEST(StyleBufferTest, HasCollision) {
    StyleBuffer s1;
    s1.setBold(true);

    StyleBuffer s2;
    s2.setItalic(true);

    EXPECT_FALSE(s1.hasCollision(s2));  // No overlap

    StyleBuffer s3;
    s3.setBold(false);

    EXPECT_TRUE(s1.hasCollision(s3));  // Both define bold
}

// =============================================================================
// JSON Tests
// =============================================================================

TEST(StyleBufferTest, ToJSONEmpty) {
    StyleBuffer s;
    EXPECT_EQ(s.toJSON(), "{}");
}

TEST(StyleBufferTest, ToJSONBold) {
    StyleBuffer s;
    s.setBold(true);
    EXPECT_EQ(s.toJSON(), "{\"bold\":true}");
}

TEST(StyleBufferTest, ToJSONComplex) {
    StyleBuffer s;
    s.setBold(true);
    s.setBgColorHex("#FF0000");
    s.setFontSize(14);

    std::string json = s.toJSON();
    EXPECT_NE(json.find("\"bold\":true"), std::string::npos);
    EXPECT_NE(json.find("\"bgColor\":\"#FF0000\""), std::string::npos);
    EXPECT_NE(json.find("\"fontSize\":14"), std::string::npos);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(StyleBufferTest, InvalidBase64) {
    auto result = StyleBuffer::fromBase64("not valid base64!!!");
    EXPECT_FALSE(result.has_value());
}

TEST(StyleBufferTest, EmptyBase64) {
    auto result = StyleBuffer::fromBase64("");
    EXPECT_FALSE(result.has_value());
}

TEST(StyleBufferTest, TooShortBase64) {
    // Base64 of a single byte (not enough for flags)
    auto result = StyleBuffer::fromBase64("QQ==");
    EXPECT_FALSE(result.has_value());
}

TEST(StyleBufferTest, NumberFormat) {
    StyleBuffer s;
    s.setNumberFormat(12345678901234567ULL);

    EXPECT_TRUE(s.hasNumberFormat());
    EXPECT_EQ(s.getNumberFormat(), 12345678901234567ULL);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getNumberFormat(), 12345678901234567ULL);
}

TEST(StyleBufferTest, LongFontFamily) {
    StyleBuffer s;
    std::string longName(200, 'A');  // 200 character font name
    s.setFontFamily(longName);

    EXPECT_EQ(s.getFontFamily(), longName);

    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontFamily(), longName);
}

TEST(StyleBufferTest, MaxFontFamily) {
    StyleBuffer s;
    std::string maxName(255, 'X');  // Max length font name
    s.setFontFamily(maxName);

    EXPECT_EQ(s.getFontFamily(), maxName);
}

TEST(StyleBufferTest, TruncateFontFamily) {
    StyleBuffer s;
    std::string tooLong(300, 'Y');  // Over max length
    s.setFontFamily(tooLong);

    EXPECT_EQ(s.getFontFamily().size(), 255u);  // Truncated
}

// =============================================================================
// Order Independence Tests
// =============================================================================

TEST(StyleBufferTest, PropertyOrderIndependence) {
    // Set properties in different orders, should get same binary

    StyleBuffer a;
    a.setBold(true);
    a.setBgColorHex("#AABBCC");
    a.setFontSize(12);

    StyleBuffer b;
    b.setFontSize(12);
    b.setBold(true);
    b.setBgColorHex("#AABBCC");

    StyleBuffer c;
    c.setBgColorHex("#AABBCC");
    c.setFontSize(12);
    c.setBold(true);

    EXPECT_EQ(a.toBase64(), b.toBase64());
    EXPECT_EQ(b.toBase64(), c.toBase64());
}

// =============================================================================
// Update Existing Property Tests
// =============================================================================

TEST(StyleBufferTest, UpdateBgColor) {
    StyleBuffer s;
    s.setBgColorHex("#FF0000");
    EXPECT_EQ(s.getBgColorHex(), "#FF0000");

    s.setBgColorHex("#00FF00");
    EXPECT_EQ(s.getBgColorHex(), "#00FF00");
}

TEST(StyleBufferTest, UpdateFontSize) {
    StyleBuffer s;
    s.setFontSize(12);
    EXPECT_EQ(s.getFontSize(), 12);

    s.setFontSize(24);
    EXPECT_EQ(s.getFontSize(), 24);
}

TEST(StyleBufferTest, UpdateFontFamily) {
    StyleBuffer s;
    s.setFontFamily("Arial");
    EXPECT_EQ(s.getFontFamily(), "Arial");

    s.setFontFamily("Times New Roman");
    EXPECT_EQ(s.getFontFamily(), "Times New Roman");

    // Verify round-trip after update
    auto decoded = StyleBuffer::fromBase64(s.toBase64());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->getFontFamily(), "Times New Roman");
}

TEST(StyleBufferTest, UpdateBorderStyle) {
    StyleBuffer s;
    s.setBorderTop(BorderStyle::THIN, 0xFF, 0x00, 0x00);
    EXPECT_EQ(s.getBorderTopStyle(), BorderStyle::THIN);

    s.setBorderTop(BorderStyle::THICK, 0x00, 0xFF, 0x00);
    EXPECT_EQ(s.getBorderTopStyle(), BorderStyle::THICK);
    EXPECT_EQ(s.getBorderTopColorHex(), "#00FF00");
}

// =============================================================================
// getEffectiveStyle Tests
// =============================================================================

TEST(StyleBufferTest, EffectiveStyleEmptyList) {
    std::vector<const StyleBuffer*> styles;
    StyleBuffer result = StyleBuffer::getEffectiveStyle(styles);
    EXPECT_TRUE(result.isEmpty());
}

TEST(StyleBufferTest, EffectiveStyleSingleStyle) {
    StyleBuffer s;
    s.setBold(true);
    s.setBgColorHex("#FF0000");

    std::vector<const StyleBuffer*> styles = {&s};
    StyleBuffer result = StyleBuffer::getEffectiveStyle(styles);

    EXPECT_TRUE(result.hasBold());
    EXPECT_TRUE(result.getBold());
    EXPECT_TRUE(result.hasBgColor());
    EXPECT_EQ(result.getBgColorHex(), "#FF0000");
}

TEST(StyleBufferTest, EffectiveStyleMergeNonOverlapping) {
    // Two styles with different properties - both should be present in result
    StyleBuffer s1;
    s1.setBold(true);
    s1.setBgColorHex("#FF0000");

    StyleBuffer s2;
    s2.setItalic(true);
    s2.setFontSize(14);

    std::vector<const StyleBuffer*> styles = {&s1, &s2};
    StyleBuffer result = StyleBuffer::getEffectiveStyle(styles);

    // All properties from both styles
    EXPECT_TRUE(result.getBold());
    EXPECT_EQ(result.getBgColorHex(), "#FF0000");
    EXPECT_TRUE(result.getItalic());
    EXPECT_EQ(result.getFontSize(), 14);
}

TEST(StyleBufferTest, EffectiveStyleOverride) {
    // Later styles override earlier ones for the same property
    StyleBuffer s1;
    s1.setBgColorHex("#FF0000");  // Red

    StyleBuffer s2;
    s2.setBgColorHex("#00FF00");  // Green

    std::vector<const StyleBuffer*> styles = {&s1, &s2};
    StyleBuffer result = StyleBuffer::getEffectiveStyle(styles);

    // s2's green overrides s1's red
    EXPECT_EQ(result.getBgColorHex(), "#00FF00");
}

TEST(StyleBufferTest, EffectiveStylePriorityChain) {
    // Column < Row < Range < Cell priority
    StyleBuffer colStyle;
    colStyle.setBgColorHex("#FF0000");  // Column sets red bg
    colStyle.setFontSize(10);

    StyleBuffer rowStyle;
    rowStyle.setBold(true);  // Row adds bold

    StyleBuffer rangeStyle;
    rangeStyle.setBgColorHex("#00FF00");  // Range overrides to green
    rangeStyle.setItalic(true);

    StyleBuffer cellStyle;
    cellStyle.setBgColorHex("#0000FF");  // Cell overrides to blue
    cellStyle.setUnderline(true);

    std::vector<const StyleBuffer*> rangeStyles = {&rangeStyle};
    StyleBuffer result =
        StyleBuffer::getEffectiveStyle(&colStyle, &rowStyle, rangeStyles, &cellStyle);

    // Cell's blue wins for bgColor (highest priority)
    EXPECT_EQ(result.getBgColorHex(), "#0000FF");
    // Column's fontSize is preserved (not overridden)
    EXPECT_EQ(result.getFontSize(), 10);
    // Row's bold is preserved
    EXPECT_TRUE(result.getBold());
    // Range's italic is preserved
    EXPECT_TRUE(result.getItalic());
    // Cell's underline is preserved
    EXPECT_TRUE(result.getUnderline());
}

TEST(StyleBufferTest, EffectiveStyleNullPointers) {
    // Should handle null pointers gracefully
    StyleBuffer cellStyle;
    cellStyle.setBold(true);

    std::vector<const StyleBuffer*> rangeStyles;
    StyleBuffer result = StyleBuffer::getEffectiveStyle(nullptr, nullptr, rangeStyles, &cellStyle);

    EXPECT_TRUE(result.getBold());
    EXPECT_FALSE(result.hasItalic());
}

TEST(StyleBufferTest, EffectiveStyleMultipleRanges) {
    // Multiple overlapping ranges - later ranges have higher priority
    StyleBuffer range1;
    range1.setBgColorHex("#FF0000");
    range1.setBold(true);

    StyleBuffer range2;
    range2.setBgColorHex("#00FF00");  // Overrides range1's bg
    range2.setItalic(true);

    StyleBuffer range3;
    range3.setUnderline(true);  // Adds underline, doesn't touch bg

    std::vector<const StyleBuffer*> rangeStyles = {&range1, &range2, &range3};
    StyleBuffer result = StyleBuffer::getEffectiveStyle(nullptr, nullptr, rangeStyles, nullptr);

    // range2's green wins (later in list)
    EXPECT_EQ(result.getBgColorHex(), "#00FF00");
    // range1's bold preserved
    EXPECT_TRUE(result.getBold());
    // range2's italic preserved
    EXPECT_TRUE(result.getItalic());
    // range3's underline preserved
    EXPECT_TRUE(result.getUnderline());
}

TEST(StyleBufferTest, EffectiveStyleBorderMerge) {
    // Different borders from different styles
    StyleBuffer s1;
    s1.setBorderTop(BorderStyle::THIN, 0xFF, 0x00, 0x00);

    StyleBuffer s2;
    s2.setBorderBottom(BorderStyle::MEDIUM, 0x00, 0xFF, 0x00);

    StyleBuffer s3;
    s3.setBorderTop(BorderStyle::THICK, 0x00, 0x00, 0xFF);  // Overrides s1's top

    std::vector<const StyleBuffer*> styles = {&s1, &s2, &s3};
    StyleBuffer result = StyleBuffer::getEffectiveStyle(styles);

    // s3's thick blue top wins
    EXPECT_EQ(result.getBorderTopStyle(), BorderStyle::THICK);
    EXPECT_EQ(result.getBorderTopColorHex(), "#0000FF");
    // s2's bottom preserved
    EXPECT_EQ(result.getBorderBottomStyle(), BorderStyle::MEDIUM);
    EXPECT_EQ(result.getBorderBottomColorHex(), "#00FF00");
}

}  // namespace
}  // namespace cells
