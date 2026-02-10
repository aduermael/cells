// =============================================================================
// Cell Style Types
// =============================================================================
//
// Data structures for cell styling: CellStyle, TextAlign, VerticalAlign, etc.
// Extracted to a separate header to break circular dependencies between
// model.h and style_registry.h.
//
// =============================================================================

#ifndef CELLS_STYLE_TYPES_H_
#define CELLS_STYLE_TYPES_H_

#include <cstdint>

#include <functional>
#include <string>

namespace cells {

// Horizontal text alignment within cell
// GENERAL means content-type-aware: right for numbers/dates, left for text
enum class TextAlign : std::uint8_t { LEFT = 0, CENTER = 1, RIGHT = 2, JUSTIFY = 3, GENERAL = 4 };

// Vertical text alignment within cell
enum class VerticalAlign : std::uint8_t { TOP = 0, MIDDLE = 1, BOTTOM = 2 };

// Border style for cell edges
enum class BorderStyle : std::uint8_t {
    NONE = 0,
    THIN = 1,
    MEDIUM = 2,
    THICK = 3,
    DASHED = 4,
    DOTTED = 5,
    DOUBLE = 6,
    HAIR = 7,
    MEDIUM_DASHED = 8,
    DASH_DOT = 9,
    MEDIUM_DASH_DOT = 10,
    DASH_DOT_DOT = 11,
    MEDIUM_DASH_DOT_DOT = 12,
    SLANT_DASH_DOT = 13
};

// Single border edge definition
struct BorderEdge {
    BorderStyle style{BorderStyle::NONE};
    std::string color;  // Hex color "#RRGGBB" or empty for default black

    // Theme/indexed color references (-1 = not set, use direct color)
    int8_t themeIndex{-1};    // Theme color index (0-11), -1 = direct
    double themeTint{0.0};    // Tint modifier for theme color (-1.0 to 1.0)
    int8_t indexedColor{-1};  // Indexed color palette index (0-65), -1 = direct

    BorderEdge() = default;
    explicit BorderEdge(BorderStyle s, std::string c = "") : style(s), color(std::move(c)) {}

    [[nodiscard]] bool hasValue() const { return style != BorderStyle::NONE; }
    [[nodiscard]] bool hasThemeColor() const { return themeIndex >= 0; }
    [[nodiscard]] bool hasIndexedColor() const { return indexedColor >= 0; }

    bool operator==(const BorderEdge& other) const {
        return style == other.style && color == other.color && themeIndex == other.themeIndex &&
               themeTint == other.themeTint && indexedColor == other.indexedColor;
    }
    bool operator!=(const BorderEdge& other) const { return !(*this == other); }
};

// Complete cell border (all four edges)
struct CellBorder {
    BorderEdge top;
    BorderEdge right;
    BorderEdge bottom;
    BorderEdge left;

    CellBorder() = default;

    [[nodiscard]] bool hasValue() const {
        return top.hasValue() || right.hasValue() || bottom.hasValue() || left.hasValue();
    }

    bool operator==(const CellBorder& other) const {
        return top == other.top && right == other.right && bottom == other.bottom &&
               left == other.left;
    }
    bool operator!=(const CellBorder& other) const { return !(*this == other); }
};

// Defined flags bitfield - tracks which properties have been explicitly set
// The defined flag is the source of truth for merges, not whether value equals default
constexpr uint16_t DEFINED_BOLD = 1 << 0;
constexpr uint16_t DEFINED_ITALIC = 1 << 1;
constexpr uint16_t DEFINED_UNDERLINE = 1 << 2;
constexpr uint16_t DEFINED_WRAPTEXT = 1 << 3;
constexpr uint16_t DEFINED_BGCOLOR = 1 << 4;
constexpr uint16_t DEFINED_TEXTCOLOR = 1 << 5;
constexpr uint16_t DEFINED_FONTFAMILY = 1 << 6;
constexpr uint16_t DEFINED_FONTSIZE = 1 << 7;
constexpr uint16_t DEFINED_HALIGN = 1 << 8;
constexpr uint16_t DEFINED_VALIGN = 1 << 9;
constexpr uint16_t DEFINED_BORDER_TOP = 1 << 10;
constexpr uint16_t DEFINED_BORDER_RIGHT = 1 << 11;
constexpr uint16_t DEFINED_BORDER_BOTTOM = 1 << 12;
constexpr uint16_t DEFINED_BORDER_LEFT = 1 << 13;
// bits 14-15 reserved for future use

// Cell style properties for formatting
// Each property is optional - empty string or 0 means "use default"
// Colors use CSS hex format: "#RRGGBB" or "" for transparent/default
// The `defined` bitfield tracks which properties have been explicitly set.
struct CellStyle {
    // Fixed-size fields grouped for better memory alignment
    uint16_t defined{0};                   // Bitfield: which properties are explicitly set
    uint8_t fontSize{0};                   // Font size in points, 0 = default (11pt)
    TextAlign hAlign{TextAlign::GENERAL};  // GENERAL = content-type-aware alignment
    VerticalAlign vAlign{VerticalAlign::BOTTOM};
    bool bold{false};
    bool italic{false};
    bool underline{false};
    bool wrapText{false};  // Wrap text within cell

    // Variable-size strings (each ~24-32 bytes with SSO)
    std::string bgColor;     // Background color (hex, e.g. "#FF0000")
    std::string textColor;   // Text color (hex, e.g. "#000000")
    std::string fontFamily;  // Font name (e.g. "Arial"), empty = system default

    // Theme/indexed color references for bgColor (-1 = not set, use direct color)
    int8_t bgThemeIndex{-1};    // Theme color index (0-11)
    double bgThemeTint{0.0};    // Tint modifier (-1.0 to 1.0)
    int8_t bgIndexedColor{-1};  // Indexed color palette index (0-65)

    // Theme/indexed color references for textColor (-1 = not set, use direct color)
    int8_t textThemeIndex{-1};    // Theme color index (0-11)
    double textThemeTint{0.0};    // Tint modifier (-1.0 to 1.0)
    int8_t textIndexedColor{-1};  // Indexed color palette index (0-65)

    // Theme font reference (-1 = direct, 0 = major/headings, 1 = minor/body)
    int8_t fontThemeIndex{-1};

    // Border (nested struct)
    CellBorder border;  // Cell borders (top, right, bottom, left)

    CellStyle() = default;

    // Helper methods for defined flags
    [[nodiscard]] bool isDefined(uint16_t prop) const { return (defined & prop) != 0; }
    void setDefined(uint16_t prop) { defined |= prop; }
    void clearDefined(uint16_t prop) { defined &= ~prop; }

    // Check if style has any defined properties
    // The defined flag is the source of truth, not whether values equal defaults
    [[nodiscard]] bool isEmpty() const { return defined == 0; }

    // Theme/indexed helpers
    [[nodiscard]] bool hasBgThemeColor() const { return bgThemeIndex >= 0; }
    [[nodiscard]] bool hasBgIndexedColor() const { return bgIndexedColor >= 0; }
    [[nodiscard]] bool hasTextThemeColor() const { return textThemeIndex >= 0; }
    [[nodiscard]] bool hasTextIndexedColor() const { return textIndexedColor >= 0; }
    [[nodiscard]] bool hasFontTheme() const { return fontThemeIndex >= 0; }

    // Equality comparison
    bool operator==(const CellStyle& other) const {
        return defined == other.defined && bold == other.bold && italic == other.italic &&
               underline == other.underline && wrapText == other.wrapText &&
               bgColor == other.bgColor && textColor == other.textColor &&
               fontFamily == other.fontFamily && fontSize == other.fontSize &&
               hAlign == other.hAlign && vAlign == other.vAlign && border == other.border &&
               bgThemeIndex == other.bgThemeIndex && bgThemeTint == other.bgThemeTint &&
               bgIndexedColor == other.bgIndexedColor &&
               textThemeIndex == other.textThemeIndex && textThemeTint == other.textThemeTint &&
               textIndexedColor == other.textIndexedColor &&
               fontThemeIndex == other.fontThemeIndex;
    }

    bool operator!=(const CellStyle& other) const { return !(*this == other); }

    // Content-based hash for deduplication
    // Hashes the defined flags and values of defined properties
    // The defined flag determines which properties participate in the hash
    [[nodiscard]] size_t hash() const {
        // Empty style (no defined properties) has hash 0
        if (defined == 0) {
            return 0;
        }

        size_t h = 0;
        auto hashCombine = [&h](size_t value) { h ^= value + 0x9e3779b9 + (h << 6) + (h >> 2); };

        // Always hash the defined flags first - they're the source of truth
        hashCombine(std::hash<uint16_t>{}(defined));

        // Hash values of defined properties
        if (isDefined(DEFINED_BOLD)) {
            hashCombine(std::hash<bool>{}(bold));
        }
        if (isDefined(DEFINED_ITALIC)) {
            hashCombine(std::hash<bool>{}(italic));
        }
        if (isDefined(DEFINED_UNDERLINE)) {
            hashCombine(std::hash<bool>{}(underline));
        }
        if (isDefined(DEFINED_WRAPTEXT)) {
            hashCombine(std::hash<bool>{}(wrapText));
        }
        if (isDefined(DEFINED_BGCOLOR)) {
            hashCombine(std::hash<std::string>{}(bgColor));
            hashCombine(std::hash<int8_t>{}(bgThemeIndex));
            if (bgThemeIndex >= 0) {
                hashCombine(std::hash<double>{}(bgThemeTint));
            }
            hashCombine(std::hash<int8_t>{}(bgIndexedColor));
        }
        if (isDefined(DEFINED_TEXTCOLOR)) {
            hashCombine(std::hash<std::string>{}(textColor));
            hashCombine(std::hash<int8_t>{}(textThemeIndex));
            if (textThemeIndex >= 0) {
                hashCombine(std::hash<double>{}(textThemeTint));
            }
            hashCombine(std::hash<int8_t>{}(textIndexedColor));
        }
        if (isDefined(DEFINED_FONTFAMILY)) {
            hashCombine(std::hash<std::string>{}(fontFamily));
            hashCombine(std::hash<int8_t>{}(fontThemeIndex));
        }
        if (isDefined(DEFINED_FONTSIZE)) {
            hashCombine(std::hash<uint8_t>{}(fontSize));
        }
        if (isDefined(DEFINED_HALIGN)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(hAlign)));
        }
        if (isDefined(DEFINED_VALIGN)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(vAlign)));
        }
        if (isDefined(DEFINED_BORDER_TOP)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.top.style)));
            hashCombine(std::hash<std::string>{}(border.top.color));
            hashCombine(std::hash<int8_t>{}(border.top.themeIndex));
            hashCombine(std::hash<int8_t>{}(border.top.indexedColor));
        }
        if (isDefined(DEFINED_BORDER_RIGHT)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.right.style)));
            hashCombine(std::hash<std::string>{}(border.right.color));
            hashCombine(std::hash<int8_t>{}(border.right.themeIndex));
            hashCombine(std::hash<int8_t>{}(border.right.indexedColor));
        }
        if (isDefined(DEFINED_BORDER_BOTTOM)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.bottom.style)));
            hashCombine(std::hash<std::string>{}(border.bottom.color));
            hashCombine(std::hash<int8_t>{}(border.bottom.themeIndex));
            hashCombine(std::hash<int8_t>{}(border.bottom.indexedColor));
        }
        if (isDefined(DEFINED_BORDER_LEFT)) {
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.left.style)));
            hashCombine(std::hash<std::string>{}(border.left.color));
            hashCombine(std::hash<int8_t>{}(border.left.themeIndex));
            hashCombine(std::hash<int8_t>{}(border.left.indexedColor));
        }
        return h;
    }
};

}  // namespace cells

#endif  // CELLS_STYLE_TYPES_H_
