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

    BorderEdge() = default;
    explicit BorderEdge(BorderStyle s, std::string c = "") : style(s), color(std::move(c)) {}

    [[nodiscard]] bool hasValue() const { return style != BorderStyle::NONE; }

    bool operator==(const BorderEdge& other) const {
        return style == other.style && color == other.color;
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

// Cell style properties for formatting
// Each property is optional - empty string or 0 means "use default"
// Colors use CSS hex format: "#RRGGBB" or "" for transparent/default
struct CellStyle {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string bgColor;                   // Background color (hex, e.g. "#FF0000")
    std::string textColor;                 // Text color (hex, e.g. "#000000")
    std::string fontFamily;                // Font name (e.g. "Arial"), empty = system default
    uint8_t fontSize{0};                   // Font size in points, 0 = default (11pt)
    TextAlign hAlign{TextAlign::GENERAL};  // GENERAL = content-type-aware alignment
    VerticalAlign vAlign{VerticalAlign::BOTTOM};
    CellBorder border;  // Cell borders (top, right, bottom, left)

    CellStyle() = default;

    // Check if style has any non-default values
    [[nodiscard]] bool isEmpty() const {
        return !bold && !italic && !underline && bgColor.empty() && textColor.empty() &&
               fontFamily.empty() && fontSize == 0 && hAlign == TextAlign::GENERAL &&
               vAlign == VerticalAlign::BOTTOM && !border.hasValue();
    }

    // Equality comparison
    bool operator==(const CellStyle& other) const {
        return bold == other.bold && italic == other.italic && underline == other.underline &&
               bgColor == other.bgColor && textColor == other.textColor &&
               fontFamily == other.fontFamily && fontSize == other.fontSize &&
               hAlign == other.hAlign && vAlign == other.vAlign && border == other.border;
    }

    bool operator!=(const CellStyle& other) const { return !(*this == other); }

    // Content-based hash for deduplication
    // Only hashes non-default values (sparse representation)
    // Each property is tagged with a unique identifier to distinguish them
    [[nodiscard]] size_t hash() const {
        size_t h = 0;
        auto hashCombine = [&h](size_t value) { h ^= value + 0x9e3779b9 + (h << 6) + (h >> 2); };

        // Property tags to distinguish different properties
        constexpr size_t TAG_BOLD = 1;
        constexpr size_t TAG_ITALIC = 2;
        constexpr size_t TAG_UNDERLINE = 3;
        constexpr size_t TAG_BGCOLOR = 4;
        constexpr size_t TAG_TEXTCOLOR = 5;
        constexpr size_t TAG_FONTFAMILY = 6;
        constexpr size_t TAG_FONTSIZE = 7;
        constexpr size_t TAG_HALIGN = 8;
        constexpr size_t TAG_VALIGN = 9;
        constexpr size_t TAG_BORDER_TOP = 10;
        constexpr size_t TAG_BORDER_RIGHT = 11;
        constexpr size_t TAG_BORDER_BOTTOM = 12;
        constexpr size_t TAG_BORDER_LEFT = 13;

        // Hash non-default values with their property tags
        if (bold) {
            hashCombine(TAG_BOLD);
            hashCombine(std::hash<bool>{}(bold));
        }
        if (italic) {
            hashCombine(TAG_ITALIC);
            hashCombine(std::hash<bool>{}(italic));
        }
        if (underline) {
            hashCombine(TAG_UNDERLINE);
            hashCombine(std::hash<bool>{}(underline));
        }
        if (!bgColor.empty()) {
            hashCombine(TAG_BGCOLOR);
            hashCombine(std::hash<std::string>{}(bgColor));
        }
        if (!textColor.empty()) {
            hashCombine(TAG_TEXTCOLOR);
            hashCombine(std::hash<std::string>{}(textColor));
        }
        if (!fontFamily.empty()) {
            hashCombine(TAG_FONTFAMILY);
            hashCombine(std::hash<std::string>{}(fontFamily));
        }
        if (fontSize != 0) {
            hashCombine(TAG_FONTSIZE);
            hashCombine(std::hash<uint8_t>{}(fontSize));
        }
        if (hAlign != TextAlign::GENERAL) {
            hashCombine(TAG_HALIGN);
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(hAlign)));
        }
        if (vAlign != VerticalAlign::BOTTOM) {
            hashCombine(TAG_VALIGN);
            hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(vAlign)));
        }
        if (border.hasValue()) {
            // Hash each border edge that has a value
            if (border.top.hasValue()) {
                hashCombine(TAG_BORDER_TOP);
                hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.top.style)));
                if (!border.top.color.empty()) {
                    hashCombine(std::hash<std::string>{}(border.top.color));
                }
            }
            if (border.right.hasValue()) {
                hashCombine(TAG_BORDER_RIGHT);
                hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.right.style)));
                if (!border.right.color.empty()) {
                    hashCombine(std::hash<std::string>{}(border.right.color));
                }
            }
            if (border.bottom.hasValue()) {
                hashCombine(TAG_BORDER_BOTTOM);
                hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.bottom.style)));
                if (!border.bottom.color.empty()) {
                    hashCombine(std::hash<std::string>{}(border.bottom.color));
                }
            }
            if (border.left.hasValue()) {
                hashCombine(TAG_BORDER_LEFT);
                hashCombine(std::hash<uint8_t>{}(static_cast<uint8_t>(border.left.style)));
                if (!border.left.color.empty()) {
                    hashCombine(std::hash<std::string>{}(border.left.color));
                }
            }
        }
        return h;
    }
};

}  // namespace cells

#endif  // CELLS_STYLE_TYPES_H_
