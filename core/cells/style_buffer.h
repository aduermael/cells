// =============================================================================
// StyleBuffer - Content-Addressed Binary Style Encoding
// =============================================================================
//
// Binary format for cell styles where the content IS the identity.
// Same properties = same bytes = same style. No separate style ID needed.
//
// Binary Format:
// +--------+--------+--------+--------+...
// | Flags  | Flags  | Prop   | Prop   |...
// | Byte 0 | Byte 1 | Data   | Data   |
// +--------+--------+--------+--------+...
//
// Flag bytes (2 bytes minimum):
// - Bits 0-1 of byte 0: Number of flag bytes indicator (reserved, always 0b00 = 2 bytes)
// - Remaining bits: Property presence flags (presence only, NOT values)
//
// Property data follows flags in order of flag bits.
// Only present if the corresponding flag is set.
//
// =============================================================================

#ifndef CELLS_STYLE_BUFFER_H_
#define CELLS_STYLE_BUFFER_H_

#include <cstdint>

#include <optional>
#include <string>
#include <vector>

#include "core/cells/style_types.h"

namespace cells {

// =============================================================================
// Flag Bit Positions
// =============================================================================
//
// Flag Byte 0 (bits 0-7):
//   Bits 0-1: Reserved for flag byte count (always 0b00 = 2 bytes for now)
//   Bit 2: bold present
//   Bit 3: italic present
//   Bit 4: underline present
//   Bit 5: strikethrough present
//   Bit 6: bgColor present
//   Bit 7: textColor present
//
// Flag Byte 1 (bits 8-15):
//   Bit 0 (8): fontSize present
//   Bit 1 (9): fontFamily present
//   Bit 2 (10): horizontalAlign present
//   Bit 3 (11): verticalAlign present
//   Bit 4 (12): textWrap present
//   Bit 5 (13): numberFormat present
//   Bit 6 (14): border present
//   Bit 7 (15): extended flags byte present (3rd flag byte follows)
//
// Extended Flag Byte (byte 2, only present when bit 15 is set):
//   Bit 0: bgColor is theme reference (3 bytes: index, tint_hi, tint_lo)
//   Bit 1: bgColor is indexed reference (3 bytes: index, 0, 0)
//   Bit 2: textColor is theme reference
//   Bit 3: textColor is indexed reference
//   Bit 4: fontFamily is theme font reference (1 byte: scheme index)
//   Bit 5: border has theme/indexed color types (extra byte in border section)
//   Bits 6-7: reserved
//

// Flag byte 0 bits
constexpr uint16_t STYLE_FLAG_BOLD = 1 << 2;
constexpr uint16_t STYLE_FLAG_ITALIC = 1 << 3;
constexpr uint16_t STYLE_FLAG_UNDERLINE = 1 << 4;
constexpr uint16_t STYLE_FLAG_STRIKETHROUGH = 1 << 5;
constexpr uint16_t STYLE_FLAG_BGCOLOR = 1 << 6;
constexpr uint16_t STYLE_FLAG_TEXTCOLOR = 1 << 7;

// Flag byte 1 bits (offset by 8)
constexpr uint16_t STYLE_FLAG_FONTSIZE = 1 << 8;
constexpr uint16_t STYLE_FLAG_FONTFAMILY = 1 << 9;
constexpr uint16_t STYLE_FLAG_HALIGN = 1 << 10;
constexpr uint16_t STYLE_FLAG_VALIGN = 1 << 11;
constexpr uint16_t STYLE_FLAG_TEXTWRAP = 1 << 12;
constexpr uint16_t STYLE_FLAG_NUMBERFORMAT = 1 << 13;
constexpr uint16_t STYLE_FLAG_BORDER = 1 << 14;
constexpr uint16_t STYLE_FLAG_EXTENDED = 1 << 15;  // Extended flags byte follows

// Extended flags byte bit positions (in byte 2, only present when STYLE_FLAG_EXTENDED is set)
constexpr uint8_t STYLE_EXT_BG_THEME = 1 << 0;      // bgColor slot is theme reference
constexpr uint8_t STYLE_EXT_BG_INDEXED = 1 << 1;    // bgColor slot is indexed reference
constexpr uint8_t STYLE_EXT_TEXT_THEME = 1 << 2;    // textColor slot is theme reference
constexpr uint8_t STYLE_EXT_TEXT_INDEXED = 1 << 3;  // textColor slot is indexed reference
constexpr uint8_t STYLE_EXT_FONT_THEME = 1 << 4;    // fontFamily slot is theme font ref
constexpr uint8_t STYLE_EXT_BORDER_THEME = 1 << 5;  // border has theme/indexed color types

// Mask for all boolean flags (bold, italic, underline, strikethrough, textWrap)
constexpr uint16_t STYLE_FLAG_BOOLEANS = STYLE_FLAG_BOLD | STYLE_FLAG_ITALIC |
                                         STYLE_FLAG_UNDERLINE | STYLE_FLAG_STRIKETHROUGH |
                                         STYLE_FLAG_TEXTWRAP;

// Boolean byte bit positions (within the packed boolean byte)
constexpr uint8_t BOOL_BIT_BOLD = 0;
constexpr uint8_t BOOL_BIT_ITALIC = 1;
constexpr uint8_t BOOL_BIT_UNDERLINE = 2;
constexpr uint8_t BOOL_BIT_STRIKETHROUGH = 3;
constexpr uint8_t BOOL_BIT_TEXTWRAP = 4;

// Border side mask bits
constexpr uint8_t BORDER_SIDE_TOP = 1 << 0;
constexpr uint8_t BORDER_SIDE_RIGHT = 1 << 1;
constexpr uint8_t BORDER_SIDE_BOTTOM = 1 << 2;
constexpr uint8_t BORDER_SIDE_LEFT = 1 << 3;

// =============================================================================
// StyleBuffer Class
// =============================================================================

class StyleBuffer {
public:
    // Default constructor - creates an empty style
    StyleBuffer();

    // Construct from existing binary data
    explicit StyleBuffer(const std::vector<uint8_t>& data);
    explicit StyleBuffer(std::vector<uint8_t>&& data);

    // =========================================================================
    // Flag accessors
    // =========================================================================

    // Get all flags as a 16-bit value
    [[nodiscard]] uint16_t getFlags() const;

    // Check if a specific flag is set
    [[nodiscard]] bool hasFlag(uint16_t flag) const;

    // Check if style has any defined properties
    [[nodiscard]] bool isEmpty() const;

    // Check if extended flags byte is present (for testing/debugging)
    [[nodiscard]] bool hasExtendedFlags() const;

    // =========================================================================
    // Boolean property setters
    // =========================================================================

    // Set bold (presence indicates the property is defined)
    void setBold(bool value);
    void clearBold();

    // Set italic
    void setItalic(bool value);
    void clearItalic();

    // Set underline
    void setUnderline(bool value);
    void clearUnderline();

    // Set strikethrough
    void setStrikethrough(bool value);
    void clearStrikethrough();

    // Set text wrap
    void setTextWrap(bool value);
    void clearTextWrap();

    // =========================================================================
    // Boolean property getters
    // =========================================================================

    [[nodiscard]] bool hasBold() const { return hasFlag(STYLE_FLAG_BOLD); }
    [[nodiscard]] bool hasItalic() const { return hasFlag(STYLE_FLAG_ITALIC); }
    [[nodiscard]] bool hasUnderline() const { return hasFlag(STYLE_FLAG_UNDERLINE); }
    [[nodiscard]] bool hasStrikethrough() const { return hasFlag(STYLE_FLAG_STRIKETHROUGH); }
    [[nodiscard]] bool hasTextWrap() const { return hasFlag(STYLE_FLAG_TEXTWRAP); }

    [[nodiscard]] bool getBold() const;
    [[nodiscard]] bool getItalic() const;
    [[nodiscard]] bool getUnderline() const;
    [[nodiscard]] bool getStrikethrough() const;
    [[nodiscard]] bool getTextWrap() const;

    // =========================================================================
    // Color property setters (RGB, 3 bytes each)
    // Setting direct color clears any theme/indexed reference, and vice versa.
    // =========================================================================

    void setBgColor(uint8_t r, uint8_t g, uint8_t b);
    void setBgColorHex(const std::string& hex);  // "#RRGGBB" format
    void clearBgColor();

    void setTextColor(uint8_t r, uint8_t g, uint8_t b);
    void setTextColorHex(const std::string& hex);  // "#RRGGBB" format
    void clearTextColor();

    // =========================================================================
    // Theme/indexed color references
    // These use the same 3-byte slot as direct colors but reinterpret the bytes.
    // Theme: [index(1), tint_hi(1), tint_lo(1)] where tint = int16/1000
    // Indexed: [index(1), 0, 0]
    // Setting a theme/indexed ref clears any direct color, and vice versa.
    // =========================================================================

    // Background theme/indexed color
    void setBgThemeColor(uint8_t themeIndex, double tint);
    void setBgIndexedColor(uint8_t paletteIndex);
    [[nodiscard]] bool hasBgThemeColor() const;
    [[nodiscard]] bool hasBgIndexedColor() const;
    [[nodiscard]] uint8_t getBgThemeIndex() const;
    [[nodiscard]] double getBgThemeTint() const;
    [[nodiscard]] uint8_t getBgIndexedColorIndex() const;

    // Text theme/indexed color
    void setTextThemeColor(uint8_t themeIndex, double tint);
    void setTextIndexedColor(uint8_t paletteIndex);
    [[nodiscard]] bool hasTextThemeColor() const;
    [[nodiscard]] bool hasTextIndexedColor() const;
    [[nodiscard]] uint8_t getTextThemeIndex() const;
    [[nodiscard]] double getTextThemeTint() const;
    [[nodiscard]] uint8_t getTextIndexedColorIndex() const;

    // Font theme reference (0 = major/headings, 1 = minor/body)
    // fontName preserves the resolved font name alongside the theme index
    void setFontTheme(uint8_t schemeIndex, const std::string& fontName = "");
    void clearFontTheme();
    [[nodiscard]] bool hasFontTheme() const;
    [[nodiscard]] uint8_t getFontThemeIndex() const;

    // =========================================================================
    // Color property getters
    // =========================================================================

    // hasBgColor/hasTextColor return true for ANY color type (direct, theme, or indexed)
    [[nodiscard]] bool hasBgColor() const { return hasFlag(STYLE_FLAG_BGCOLOR); }
    [[nodiscard]] bool hasTextColor() const { return hasFlag(STYLE_FLAG_TEXTCOLOR); }

    // These return raw RGB bytes. For theme/indexed refs, the bytes are the encoded ref data.
    void getBgColor(uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getTextColor(uint8_t& r, uint8_t& g, uint8_t& b) const;

    // These return hex color. For theme/indexed refs, returns "#000000" (not meaningful).
    [[nodiscard]] std::string getBgColorHex() const;
    [[nodiscard]] std::string getTextColorHex() const;

    // =========================================================================
    // Font size (1 byte, stored as value - 6, supports 6-261pt)
    // =========================================================================

    void setFontSize(uint8_t size);  // Size in points (6-255 practical)
    void clearFontSize();

    [[nodiscard]] bool hasFontSize() const { return hasFlag(STYLE_FLAG_FONTSIZE); }
    [[nodiscard]] uint8_t getFontSize() const;  // Returns 0 if not set

    // =========================================================================
    // Font family (length-prefixed string, max 255 chars)
    // =========================================================================

    void setFontFamily(const std::string& family);
    void clearFontFamily();

    [[nodiscard]] bool hasFontFamily() const { return hasFlag(STYLE_FLAG_FONTFAMILY); }
    [[nodiscard]] std::string getFontFamily() const;

    // =========================================================================
    // Alignment (packed into 1 byte: 3 bits hAlign, 3 bits vAlign)
    // =========================================================================

    void setHAlign(TextAlign align);
    void clearHAlign();

    void setVAlign(VerticalAlign align);
    void clearVAlign();

    [[nodiscard]] bool hasHAlign() const { return hasFlag(STYLE_FLAG_HALIGN); }
    [[nodiscard]] bool hasVAlign() const { return hasFlag(STYLE_FLAG_VALIGN); }

    [[nodiscard]] TextAlign getHAlign() const;
    [[nodiscard]] VerticalAlign getVAlign() const;

    // =========================================================================
    // Number format (8 bytes format ID reference)
    // =========================================================================

    void setNumberFormat(uint64_t formatId);
    void clearNumberFormat();

    [[nodiscard]] bool hasNumberFormat() const { return hasFlag(STYLE_FLAG_NUMBERFORMAT); }
    [[nodiscard]] uint64_t getNumberFormat() const;

    // =========================================================================
    // Border (variable length: 1 sides-mask byte + 4 bytes per side)
    // =========================================================================

    void setBorderTop(BorderStyle style, uint8_t r, uint8_t g, uint8_t b);
    void setBorderRight(BorderStyle style, uint8_t r, uint8_t g, uint8_t b);
    void setBorderBottom(BorderStyle style, uint8_t r, uint8_t g, uint8_t b);
    void setBorderLeft(BorderStyle style, uint8_t r, uint8_t g, uint8_t b);

    void setBorderTopHex(BorderStyle style, const std::string& colorHex);
    void setBorderRightHex(BorderStyle style, const std::string& colorHex);
    void setBorderBottomHex(BorderStyle style, const std::string& colorHex);
    void setBorderLeftHex(BorderStyle style, const std::string& colorHex);

    void clearBorderTop();
    void clearBorderRight();
    void clearBorderBottom();
    void clearBorderLeft();
    void clearBorder();  // Clear all border sides

    [[nodiscard]] bool hasBorder() const { return hasFlag(STYLE_FLAG_BORDER); }
    [[nodiscard]] bool hasBorderTop() const;
    [[nodiscard]] bool hasBorderRight() const;
    [[nodiscard]] bool hasBorderBottom() const;
    [[nodiscard]] bool hasBorderLeft() const;

    [[nodiscard]] BorderStyle getBorderTopStyle() const;
    [[nodiscard]] BorderStyle getBorderRightStyle() const;
    [[nodiscard]] BorderStyle getBorderBottomStyle() const;
    [[nodiscard]] BorderStyle getBorderLeftStyle() const;

    void getBorderTopColor(uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getBorderRightColor(uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getBorderBottomColor(uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getBorderLeftColor(uint8_t& r, uint8_t& g, uint8_t& b) const;

    [[nodiscard]] std::string getBorderTopColorHex() const;
    [[nodiscard]] std::string getBorderRightColorHex() const;
    [[nodiscard]] std::string getBorderBottomColorHex() const;
    [[nodiscard]] std::string getBorderLeftColorHex() const;

    // Border theme/indexed color setters
    void setBorderTopThemeColor(BorderStyle style, uint8_t themeIndex, double tint);
    void setBorderRightThemeColor(BorderStyle style, uint8_t themeIndex, double tint);
    void setBorderBottomThemeColor(BorderStyle style, uint8_t themeIndex, double tint);
    void setBorderLeftThemeColor(BorderStyle style, uint8_t themeIndex, double tint);

    void setBorderTopIndexedColor(BorderStyle style, uint8_t paletteIndex);
    void setBorderRightIndexedColor(BorderStyle style, uint8_t paletteIndex);
    void setBorderBottomIndexedColor(BorderStyle style, uint8_t paletteIndex);
    void setBorderLeftIndexedColor(BorderStyle style, uint8_t paletteIndex);

    // Border color type queries (per side: 0=direct, 1=theme, 2=indexed)
    [[nodiscard]] uint8_t getBorderSideColorType(uint8_t sideBit) const;
    [[nodiscard]] uint8_t getBorderTopThemeIndex() const;
    [[nodiscard]] double getBorderTopThemeTint() const;
    [[nodiscard]] uint8_t getBorderTopIndexedColorIndex() const;
    [[nodiscard]] uint8_t getBorderRightThemeIndex() const;
    [[nodiscard]] double getBorderRightThemeTint() const;
    [[nodiscard]] uint8_t getBorderRightIndexedColorIndex() const;
    [[nodiscard]] uint8_t getBorderBottomThemeIndex() const;
    [[nodiscard]] double getBorderBottomThemeTint() const;
    [[nodiscard]] uint8_t getBorderBottomIndexedColorIndex() const;
    [[nodiscard]] uint8_t getBorderLeftThemeIndex() const;
    [[nodiscard]] double getBorderLeftThemeTint() const;
    [[nodiscard]] uint8_t getBorderLeftIndexedColorIndex() const;

    // =========================================================================
    // Serialization
    // =========================================================================

    // Encode to base64 (standard RFC 4648)
    [[nodiscard]] std::string toBase64() const;

    // Decode from base64 (returns nullopt on invalid input)
    [[nodiscard]] static std::optional<StyleBuffer> fromBase64(const std::string& b64);

    // Get raw binary data
    [[nodiscard]] const std::vector<uint8_t>& data() const { return _data; }

    // =========================================================================
    // JSON conversion (for debugging and export)
    // =========================================================================

    // Convert to JSON object string
    [[nodiscard]] std::string toJSON() const;

    // Parse from JSON object string (returns nullopt on invalid input)
    [[nodiscard]] static std::optional<StyleBuffer> fromJSON(const std::string& json);

    // =========================================================================
    // Conversion from/to CellStyle
    // =========================================================================

    // Convert from existing CellStyle
    static StyleBuffer fromCellStyle(const CellStyle& style);

    // Convert to CellStyle
    [[nodiscard]] CellStyle toCellStyle() const;

    // =========================================================================
    // Style merging (for computing effective styles)
    // =========================================================================

    // Merge another style into this one.
    // Properties from 'other' override properties in this style.
    void merge(const StyleBuffer& other);

    // Check if this style has any property collision with another style.
    // Returns true if both styles define the same property.
    [[nodiscard]] bool hasCollision(const StyleBuffer& other) const;

    // Compute effective style by merging multiple StyleBuffers.
    // The order defines priority: later styles override earlier ones.
    // Typical usage: getEffectiveStyle({columnStyle, rowStyle, rangeStyles..., cellStyle})
    // Returns the merged result.
    [[nodiscard]] static StyleBuffer getEffectiveStyle(
        const std::vector<const StyleBuffer*>& styles);

    // Convenience overload for common case: column, row, and cell styles.
    // rangeStyles are merged in order (first range has lowest priority).
    [[nodiscard]] static StyleBuffer getEffectiveStyle(
        const StyleBuffer* columnStyle, const StyleBuffer* rowStyle,
        const std::vector<const StyleBuffer*>& rangeStyles, const StyleBuffer* cellStyle);

    // =========================================================================
    // Equality and comparison
    // =========================================================================

    bool operator==(const StyleBuffer& other) const { return _data == other._data; }
    bool operator!=(const StyleBuffer& other) const { return _data != other._data; }

private:
    // Binary data storage: flag bytes + property data
    // Minimum 2 bytes (just flags), grows as properties are added
    std::vector<uint8_t> _data;

    // =========================================================================
    // Internal helpers
    // =========================================================================

    // Ensure minimum data size (at least 2 bytes for flags)
    void ensureMinSize();

    // Set/clear a flag bit
    void setFlag(uint16_t flag);
    void clearFlag(uint16_t flag);

    // Find the byte offset where a property's data starts
    // Returns the offset after the flag bytes, accounting for
    // all properties whose flags come before this one
    [[nodiscard]] size_t findPropertyOffset(uint16_t flag) const;

    // Get the size of a property's data
    [[nodiscard]] size_t getPropertySize(uint16_t flag) const;

    // Insert data at a specific offset
    void insertDataAt(size_t offset, const uint8_t* data, size_t size);

    // Remove data at a specific offset
    void removeDataAt(size_t offset, size_t size);

    // Helper for boolean byte position and manipulation
    [[nodiscard]] bool hasBooleanByte() const;
    [[nodiscard]] size_t booleanByteOffset() const;
    void ensureBooleanByte();
    void removeBooleanByteIfEmpty();
    [[nodiscard]] uint8_t getBooleanByte() const;
    void setBooleanBit(uint8_t bit, bool value);
    [[nodiscard]] bool getBooleanBit(uint8_t bit) const;

    // Color helpers
    static bool parseHexColor(const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b);
    static std::string formatHexColor(uint8_t r, uint8_t g, uint8_t b);

    // Border internal helpers
    [[nodiscard]] uint8_t getBorderSideMask() const;
    void setBorderSide(uint8_t sideBit, BorderStyle style, uint8_t r, uint8_t g, uint8_t b);
    void clearBorderSide(uint8_t sideBit);
    [[nodiscard]] BorderStyle getBorderSideStyle(uint8_t sideBit) const;
    void getBorderSideColor(uint8_t sideBit, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Extended flags helpers
    [[nodiscard]] uint8_t getExtFlags() const;
    void setExtFlag(uint8_t flag);
    void clearExtFlag(uint8_t flag);
    void ensureExtendedFlags();
    void removeExtendedFlagsIfEmpty();

    // Number of flag bytes (2 without extended, 3 with extended)
    [[nodiscard]] size_t flagByteCount() const;

    // Border theme/indexed color type byte helpers
    [[nodiscard]] uint8_t getBorderColorTypeByte() const;
    void setBorderColorTypeByte(uint8_t value);
    void ensureBorderColorTypeByte();
    void removeBorderColorTypeByteIfEmpty();

    // Border theme/indexed internal setters/getters
    void setBorderSideThemeColorImpl(uint8_t sideBit, BorderStyle style, uint8_t themeIndex,
                                     double tint);
    void setBorderSideIndexedColorImpl(uint8_t sideBit, BorderStyle style, uint8_t paletteIndex);
    [[nodiscard]] uint8_t getBorderSideThemeIndexImpl(uint8_t sideBit) const;
    [[nodiscard]] double getBorderSideThemeTintImpl(uint8_t sideBit) const;
    [[nodiscard]] uint8_t getBorderSideIndexedColorIndexImpl(uint8_t sideBit) const;

    // Tint encoding: double -> int16 (x1000), stored as 2 bytes big-endian
public:
    static void encodeTint(double tint, uint8_t& hi, uint8_t& lo);
    static double decodeTint(uint8_t hi, uint8_t lo);
};

}  // namespace cells

#endif  // CELLS_STYLE_BUFFER_H_
