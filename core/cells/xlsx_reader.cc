#include "core/cells/xlsx_reader.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "core/cells/formula_parser.h"
#include "core/cells/id.h"
#include "core/cells/named_ranges.h"
#include "core/cells/types.h"

#include "miniz.h"
#include "pugixml.hpp"

namespace {

// Debug timing - set via environment variable
bool debugTiming() {
    static const bool enabled = std::getenv("CELLS_DEBUG_TIMING") != nullptr;
    return enabled;
}

void logTiming(const char* stage, std::chrono::steady_clock::time_point start) {
    if (debugTiming()) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        std::cerr << "[timing] " << stage << ": " << duration.count() / 1000.0 << "ms\n";
    }
}

// Parse cell reference "A1" -> (col=0, row=0)
void parseCellRef(const char* ref, int& col, int& row) {
    col = 0;
    row = 0;

    // Parse column letters (A-Z, AA-ZZ, etc.)
    while (*ref >= 'A' && *ref <= 'Z') {
        col = col * 26 + (*ref - 'A' + 1);
        ref++;
    }
    col--;  // Convert to 0-indexed

    // Parse row number
    while (*ref >= '0' && *ref <= '9') {
        row = row * 10 + (*ref - '0');
        ref++;
    }
    row--;  // Convert to 0-indexed
}

// Map XML cell type to our enum
int mapCellType(const char* type) {
    if (type == nullptr || *type == '\0') {
        return 2;  // Default is number
    }

    switch (type[0]) {
        case 's':
            return 1;  // Shared string -> STRING
        case 'b':
            return 3;  // Boolean
        case 'e':
            return 4;  // Error
        case 'n':
            return 2;  // Number
        case 'd':
            return 5;  // Date
        case 'i':
            // "inlineStr" - inline string
            return 1;  // STRING
        default:
            return 2;  // Default to number
    }
}

// ---------------------------------------------------------------------------
// XLSX Style parsing helpers
// ---------------------------------------------------------------------------

// Parsed font from styles.xml
struct XLSXFont {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string name;   // Font family name
    double size{0};     // Font size in points
    std::string color;  // Text color as #RRGGBB
};

// Parsed fill (background) from styles.xml
struct XLSXFill {
    std::string fgColor;  // Foreground color as #RRGGBB (used for solid fills)
    std::string bgColor;  // Background color as #RRGGBB
};

// Parsed border edge from styles.xml
struct XLSXBorderEdge {
    cells::BorderStyle style{cells::BorderStyle::NONE};
    std::string color;  // Color as #RRGGBB
};

// Parsed border from styles.xml (complete cell border)
struct XLSXBorder {
    XLSXBorderEdge left;
    XLSXBorderEdge right;
    XLSXBorderEdge top;
    XLSXBorderEdge bottom;

    [[nodiscard]] bool hasValue() const {
        return left.style != cells::BorderStyle::NONE || right.style != cells::BorderStyle::NONE ||
               top.style != cells::BorderStyle::NONE || bottom.style != cells::BorderStyle::NONE;
    }
};

// Parsed alignment from styles.xml
struct XLSXAlignment {
    cells::TextAlign horizontal{cells::TextAlign::LEFT};
    cells::VerticalAlign vertical{cells::VerticalAlign::BOTTOM};
};

// Cell format record (cellXfs entry) - combines font, fill, alignment, border
struct XLSXCellFormat {
    int fontId{0};
    int fillId{0};
    int borderId{0};
    int numFmtId{0};
    bool applyFont{false};
    bool applyFill{false};
    bool applyBorder{false};
    bool applyAlignment{false};
    XLSXAlignment alignment;
};

// Convert ARGB hex "FFRRGGBB" to "#RRGGBB"
std::string argbToRgb(const char* argb) {
    if (argb == nullptr || argb[0] == '\0') {
        return {};
    }
    // XLSX colors can be ARGB (8 chars) or RGB (6 chars)
    const size_t len = std::strlen(argb);
    if (len == 8) {
        // Skip alpha, take RGB
        return "#" + std::string(argb + 2, 6);
    }
    if (len == 6) {
        return "#" + std::string(argb, 6);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Theme Color Support
// ---------------------------------------------------------------------------

// Excel theme colors - the 12 standard colors from theme.xml
// Theme index mapping:
// 0: lt1 (background 1), 1: dk1 (text 1), 2: lt2 (background 2), 3: dk2 (text 2)
// 4-9: accent1-6, 10: hlink, 11: folHlink
struct XLSXThemeColors {
    std::string colors[12];  // RGB values as "#RRGGBB"

    [[nodiscard]] std::string getColor(int index) const {
        if (index >= 0 && index < 12) {
            return colors[index];
        }
        return {};
    }
};

// Apply tint to a color
// tint < 0: darken toward black (tint = -1.0 is fully black)
// tint > 0: lighten toward white (tint = 1.0 is fully white)
// tint = 0: no change
std::string applyTint(const std::string& color, double tint) {
    if (color.empty() || color.length() != 7 || color[0] != '#') {
        return color;
    }
    if (tint == 0.0) {
        return color;
    }

    // Parse RGB components (not const because they're modified in HSL-to-RGB conversion)
    // NOLINTNEXTLINE(misc-const-correctness)
    int r = std::stoi(color.substr(1, 2), nullptr, 16);
    // NOLINTNEXTLINE(misc-const-correctness)
    int g = std::stoi(color.substr(3, 2), nullptr, 16);
    // NOLINTNEXTLINE(misc-const-correctness)
    int b = std::stoi(color.substr(5, 2), nullptr, 16);

    // Convert RGB to HSL
    const double rd = r / 255.0;
    const double gd = g / 255.0;
    const double bd = b / 255.0;

    const double maxVal = std::max({rd, gd, bd});
    const double minVal = std::min({rd, gd, bd});
    double h = 0;
    double s = 0;
    double l = (maxVal + minVal) / 2.0;

    if (maxVal != minVal) {
        const double d = maxVal - minVal;
        s = l > 0.5 ? d / (2.0 - maxVal - minVal) : d / (maxVal + minVal);
        if (maxVal == rd) {
            h = (gd - bd) / d + (gd < bd ? 6.0 : 0.0);
        } else if (maxVal == gd) {
            h = (bd - rd) / d + 2.0;
        } else {
            h = (rd - gd) / d + 4.0;
        }
        h /= 6.0;
    }

    // Apply tint to lightness
    // Excel's tint algorithm based on ECMA-376 documentation
    if (tint < 0) {
        // Darken: L' = L * (1 + tint)
        l = l * (1.0 + tint);
    } else {
        // Lighten: L' = L * (1 - tint) + tint
        l = l * (1.0 - tint) + tint;
    }
    l = std::max(0.0, std::min(1.0, l));

    // Convert HSL back to RGB
    auto hueToRgb = [](double p, double q, double t) {
        if (t < 0) {
            t += 1;
        }
        if (t > 1) {
            t -= 1;
        }
        if (t < 1.0 / 6.0) {
            return p + (q - p) * 6.0 * t;
        }
        if (t < 0.5) {
            return q;
        }
        if (t < 2.0 / 3.0) {
            return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        }
        return p;
    };

    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;

    if (s == 0) {
        r = g = b = static_cast<int>(std::round(l * 255));
    } else {
        r = static_cast<int>(std::round(hueToRgb(p, q, h + 1.0 / 3.0) * 255));
        g = static_cast<int>(std::round(hueToRgb(p, q, h) * 255));
        b = static_cast<int>(std::round(hueToRgb(p, q, h - 1.0 / 3.0) * 255));
    }

    // Clamp values
    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));

    // Format result
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

// Parse theme.xml to extract the 12 theme colors
XLSXThemeColors parseThemeXml(const std::string& content) {
    XLSXThemeColors theme;

    if (content.empty()) {
        return theme;
    }

    pugi::xml_document doc;
    if (!doc.load_buffer(content.data(), content.size())) {
        return theme;
    }

    // Navigate to color scheme: theme/themeElements/clrScheme
    auto themeNode = doc.child("a:theme");
    auto themeElements = themeNode.child("a:themeElements");
    auto clrScheme = themeElements.child("a:clrScheme");

    // Helper to extract color from a color scheme element
    auto extractColor = [](pugi::xml_node node) -> std::string {
        if (!node) {
            return {};
        }

        // Check for srgbClr (direct RGB value)
        auto srgbClr = node.child("a:srgbClr");
        if (srgbClr) {
            const char* val = srgbClr.attribute("val").value();
            if (val && val[0] != '\0') {
                return "#" + std::string(val);
            }
        }

        // Check for sysClr (system color with lastClr fallback)
        auto sysClr = node.child("a:sysClr");
        if (sysClr) {
            const char* lastClr = sysClr.attribute("lastClr").value();
            if (lastClr && lastClr[0] != '\0') {
                return "#" + std::string(lastClr);
            }
        }

        return {};
    };

    // Extract the 12 theme colors in Excel's index order
    // Index 0: lt1 (background 1)
    // Index 1: dk1 (text 1)
    // Index 2: lt2 (background 2)
    // Index 3: dk2 (text 2)
    // Index 4-9: accent1-6
    // Index 10: hlink
    // Index 11: folHlink
    theme.colors[0] = extractColor(clrScheme.child("a:lt1"));
    theme.colors[1] = extractColor(clrScheme.child("a:dk1"));
    theme.colors[2] = extractColor(clrScheme.child("a:lt2"));
    theme.colors[3] = extractColor(clrScheme.child("a:dk2"));
    theme.colors[4] = extractColor(clrScheme.child("a:accent1"));
    theme.colors[5] = extractColor(clrScheme.child("a:accent2"));
    theme.colors[6] = extractColor(clrScheme.child("a:accent3"));
    theme.colors[7] = extractColor(clrScheme.child("a:accent4"));
    theme.colors[8] = extractColor(clrScheme.child("a:accent5"));
    theme.colors[9] = extractColor(clrScheme.child("a:accent6"));
    theme.colors[10] = extractColor(clrScheme.child("a:hlink"));
    theme.colors[11] = extractColor(clrScheme.child("a:folHlink"));

    return theme;
}

// Excel's 64 standard indexed colors (indices 0-63)
// Based on ECMA-376 Part 1 Section 18.8.27
// clang-format off
const char* const kIndexedColors[64] = {
    "#000000",  // 0: Black
    "#FFFFFF",  // 1: White
    "#FF0000",  // 2: Red
    "#00FF00",  // 3: Bright Green
    "#0000FF",  // 4: Blue
    "#FFFF00",  // 5: Yellow
    "#FF00FF",  // 6: Pink
    "#00FFFF",  // 7: Turquoise
    "#000000",  // 8: Black
    "#FFFFFF",  // 9: White
    "#FF0000",  // 10: Red
    "#00FF00",  // 11: Bright Green
    "#0000FF",  // 12: Blue
    "#FFFF00",  // 13: Yellow
    "#FF00FF",  // 14: Pink
    "#00FFFF",  // 15: Turquoise
    "#800000",  // 16: Dark Red
    "#008000",  // 17: Green
    "#000080",  // 18: Dark Blue
    "#808000",  // 19: Dark Yellow (Olive)
    "#800080",  // 20: Violet
    "#008080",  // 21: Teal
    "#C0C0C0",  // 22: Silver (25% Gray)
    "#808080",  // 23: Gray (50% Gray)
    "#9999FF",  // 24: Periwinkle
    "#993366",  // 25: Plum
    "#FFFFCC",  // 26: Ivory
    "#CCFFFF",  // 27: Light Turquoise
    "#660066",  // 28: Dark Purple
    "#FF8080",  // 29: Coral
    "#0066CC",  // 30: Ocean Blue
    "#CCCCFF",  // 31: Ice Blue
    "#000080",  // 32: Dark Blue
    "#FF00FF",  // 33: Pink
    "#FFFF00",  // 34: Yellow
    "#00FFFF",  // 35: Turquoise
    "#800080",  // 36: Violet
    "#800000",  // 37: Dark Red
    "#008080",  // 38: Teal
    "#0000FF",  // 39: Blue
    "#00CCFF",  // 40: Sky Blue
    "#CCFFFF",  // 41: Light Turquoise
    "#CCFFCC",  // 42: Light Green
    "#FFFF99",  // 43: Light Yellow
    "#99CCFF",  // 44: Pale Blue
    "#FF99CC",  // 45: Rose
    "#CC99FF",  // 46: Lavender
    "#FFCC99",  // 47: Tan
    "#3366FF",  // 48: Light Blue
    "#33CCCC",  // 49: Aqua
    "#99CC00",  // 50: Lime
    "#FFCC00",  // 51: Gold
    "#FF9900",  // 52: Light Orange
    "#FF6600",  // 53: Orange
    "#666699",  // 54: Blue-Gray
    "#969696",  // 55: Gray (40%)
    "#003366",  // 56: Dark Teal
    "#339966",  // 57: Sea Green
    "#003300",  // 58: Dark Green
    "#333300",  // 59: Olive Green
    "#993300",  // 60: Brown
    "#993366",  // 61: Plum
    "#333399",  // 62: Indigo
    "#333333",  // 63: Gray (80%)
};
// clang-format on

// Get indexed color by index (returns empty string for invalid indices)
std::string getIndexedColor(int index) {
    if (index >= 0 && index < 64) {
        return kIndexedColors[index];
    }
    // Special indexed values:
    // 64: System foreground (use system text color) - default to black
    // 65: System background (use system background color) - default to white
    if (index == 64) {
        return "#000000";
    }
    if (index == 65) {
        return "#FFFFFF";
    }
    return {};
}

// Resolve a color from an XML color node (handles rgb, theme+tint, indexed)
// colorNode is an element like <color rgb="FF000000"/> or <color theme="1" tint="0.5"/>
// or <color indexed="5"/>
std::string resolveColor(pugi::xml_node colorNode, const XLSXThemeColors& theme) {
    if (!colorNode) {
        return {};
    }

    // First check for direct RGB color
    const char* rgb = colorNode.attribute("rgb").value();
    if (rgb && rgb[0] != '\0') {
        return argbToRgb(rgb);
    }

    // Check for theme color
    auto themeAttr = colorNode.attribute("theme");
    if (themeAttr) {
        const int themeIndex = themeAttr.as_int(-1);
        std::string baseColor = theme.getColor(themeIndex);
        if (!baseColor.empty()) {
            // Apply tint if present
            const double tint = colorNode.attribute("tint").as_double(0.0);
            if (tint != 0.0) {
                return applyTint(baseColor, tint);
            }
            return baseColor;
        }
    }

    // Check for indexed color
    auto indexedAttr = colorNode.attribute("indexed");
    if (indexedAttr) {
        const int colorIndex = indexedAttr.as_int(-1);
        std::string indexedColor = getIndexedColor(colorIndex);
        if (!indexedColor.empty()) {
            return indexedColor;
        }
    }

    // Check for auto color (maps to black)
    auto autoAttr = colorNode.attribute("auto");
    if (autoAttr && autoAttr.as_bool()) {
        return "#000000";
    }

    return {};
}

// Parse horizontal alignment string to enum
// XLSX uses "general" for content-type-aware alignment (right for numbers, left for text)
// When no alignment is specified or the value is empty/null, return GENERAL
cells::TextAlign parseHorizontalAlign(const char* align) {
    if (align == nullptr || align[0] == '\0' || std::strcmp(align, "general") == 0) {
        return cells::TextAlign::GENERAL;
    }
    if (std::strcmp(align, "left") == 0) {
        return cells::TextAlign::LEFT;
    }
    if (std::strcmp(align, "center") == 0 || std::strcmp(align, "centerContinuous") == 0) {
        return cells::TextAlign::CENTER;
    }
    if (std::strcmp(align, "right") == 0) {
        return cells::TextAlign::RIGHT;
    }
    if (std::strcmp(align, "justify") == 0 || std::strcmp(align, "distributed") == 0) {
        return cells::TextAlign::JUSTIFY;
    }
    // Unknown alignment - default to general
    return cells::TextAlign::GENERAL;
}

// Parse vertical alignment string to enum
cells::VerticalAlign parseVerticalAlign(const char* align) {
    if (align == nullptr) {
        return cells::VerticalAlign::BOTTOM;
    }
    if (std::strcmp(align, "top") == 0) {
        return cells::VerticalAlign::TOP;
    }
    if (std::strcmp(align, "center") == 0) {
        return cells::VerticalAlign::MIDDLE;
    }
    // Default is bottom
    return cells::VerticalAlign::BOTTOM;
}

// Parse border style string to enum
// XLSX border styles: thin, medium, thick, dashed, dotted, double, hair,
// mediumDashed, dashDot, mediumDashDot, dashDotDot, mediumDashDotDot, slantDashDot
cells::BorderStyle parseBorderStyle(const char* style) {
    if (style == nullptr || style[0] == '\0' || std::strcmp(style, "none") == 0) {
        return cells::BorderStyle::NONE;
    }
    if (std::strcmp(style, "thin") == 0) {
        return cells::BorderStyle::THIN;
    }
    if (std::strcmp(style, "medium") == 0) {
        return cells::BorderStyle::MEDIUM;
    }
    if (std::strcmp(style, "thick") == 0) {
        return cells::BorderStyle::THICK;
    }
    if (std::strcmp(style, "dashed") == 0) {
        return cells::BorderStyle::DASHED;
    }
    if (std::strcmp(style, "dotted") == 0) {
        return cells::BorderStyle::DOTTED;
    }
    if (std::strcmp(style, "double") == 0) {
        return cells::BorderStyle::DOUBLE;
    }
    if (std::strcmp(style, "hair") == 0) {
        return cells::BorderStyle::HAIR;
    }
    if (std::strcmp(style, "mediumDashed") == 0) {
        return cells::BorderStyle::MEDIUM_DASHED;
    }
    if (std::strcmp(style, "dashDot") == 0) {
        return cells::BorderStyle::DASH_DOT;
    }
    if (std::strcmp(style, "mediumDashDot") == 0) {
        return cells::BorderStyle::MEDIUM_DASH_DOT;
    }
    if (std::strcmp(style, "dashDotDot") == 0) {
        return cells::BorderStyle::DASH_DOT_DOT;
    }
    if (std::strcmp(style, "mediumDashDotDot") == 0) {
        return cells::BorderStyle::MEDIUM_DASH_DOT_DOT;
    }
    if (std::strcmp(style, "slantDashDot") == 0) {
        return cells::BorderStyle::SLANT_DASH_DOT;
    }
    // Default to thin for any unrecognized non-empty style
    return cells::BorderStyle::THIN;
}

// Parse a single border edge element (left, right, top, or bottom)
XLSXBorderEdge parseBorderEdge(pugi::xml_node edgeNode, const XLSXThemeColors& theme) {
    XLSXBorderEdge edge;
    if (!edgeNode) {
        return edge;
    }

    // Get style attribute
    const char* style = edgeNode.attribute("style").value();
    edge.style = parseBorderStyle(style);

    // Get color (from <color> child element)
    auto colorNode = edgeNode.child("color");
    if (colorNode) {
        edge.color = resolveColor(colorNode, theme);
    }

    return edge;
}

// Container for all parsed styles from styles.xml
struct XLSXStyles {
    std::vector<XLSXFont> fonts;
    std::vector<XLSXFill> fills;
    std::vector<XLSXBorder> borders;
    std::vector<XLSXCellFormat> cellFormats;                // cellXfs entries
    std::unordered_map<int, std::string> customNumFormats;  // numFmtId -> format code

    // Convert an XLSX cell format index to our CellStyle
    // Returns true if any non-default style properties were found
    bool getCellStyle(int styleIndex, cells::CellStyle& outStyle) const {
        if (styleIndex < 0 || styleIndex >= static_cast<int>(cellFormats.size())) {
            return false;
        }

        const XLSXCellFormat& xf = cellFormats[styleIndex];
        bool hasStyle = false;

        // Apply font properties
        if (xf.applyFont && xf.fontId >= 0 && xf.fontId < static_cast<int>(fonts.size())) {
            const XLSXFont& font = fonts[xf.fontId];
            if (font.bold) {
                outStyle.bold = true;
                hasStyle = true;
            }
            if (font.italic) {
                outStyle.italic = true;
                hasStyle = true;
            }
            if (font.underline) {
                outStyle.underline = true;
                hasStyle = true;
            }
            if (!font.name.empty()) {
                outStyle.fontFamily = font.name;
                hasStyle = true;
            }
            if (font.size > 0) {
                outStyle.fontSize = static_cast<uint8_t>(font.size);
                hasStyle = true;
            }
            if (!font.color.empty()) {
                outStyle.textColor = font.color;
                hasStyle = true;
            }
        }

        // Apply fill properties (background color)
        if (xf.applyFill && xf.fillId >= 0 && xf.fillId < static_cast<int>(fills.size())) {
            const XLSXFill& fill = fills[xf.fillId];
            if (!fill.fgColor.empty()) {
                outStyle.bgColor = fill.fgColor;
                hasStyle = true;
            }
        }

        // Apply border properties
        if (xf.applyBorder && xf.borderId >= 0 && xf.borderId < static_cast<int>(borders.size())) {
            const XLSXBorder& border = borders[xf.borderId];
            if (border.hasValue()) {
                // Copy top border
                if (border.top.style != cells::BorderStyle::NONE) {
                    outStyle.border.top.style = border.top.style;
                    outStyle.border.top.color = border.top.color;
                }
                // Copy right border
                if (border.right.style != cells::BorderStyle::NONE) {
                    outStyle.border.right.style = border.right.style;
                    outStyle.border.right.color = border.right.color;
                }
                // Copy bottom border
                if (border.bottom.style != cells::BorderStyle::NONE) {
                    outStyle.border.bottom.style = border.bottom.style;
                    outStyle.border.bottom.color = border.bottom.color;
                }
                // Copy left border
                if (border.left.style != cells::BorderStyle::NONE) {
                    outStyle.border.left.style = border.left.style;
                    outStyle.border.left.color = border.left.color;
                }
                hasStyle = true;
            }
        }

        // Apply alignment
        if (xf.applyAlignment) {
            if (xf.alignment.horizontal != cells::TextAlign::GENERAL) {
                outStyle.hAlign = xf.alignment.horizontal;
                hasStyle = true;
            }
            if (xf.alignment.vertical != cells::VerticalAlign::BOTTOM) {
                outStyle.vAlign = xf.alignment.vertical;
                hasStyle = true;
            }
        }

        return hasStyle;
    }
};

// Create a unique key string from a CellStyle (for deduplication)
std::string cellStyleToKey(const cells::CellStyle& style) {
    std::ostringstream oss;
    oss << (style.bold ? "B" : "b") << (style.italic ? "I" : "i") << (style.underline ? "U" : "u")
        << "|" << style.bgColor << "|" << style.textColor << "|" << style.fontFamily << "|"
        << static_cast<int>(style.fontSize) << "|" << static_cast<int>(style.hAlign) << "|"
        << static_cast<int>(style.vAlign);
    // Add border info to key
    oss << "|B:" << static_cast<int>(style.border.top.style) << "," << style.border.top.color << ":"
        << static_cast<int>(style.border.right.style) << "," << style.border.right.color << ":"
        << static_cast<int>(style.border.bottom.style) << "," << style.border.bottom.color << ":"
        << static_cast<int>(style.border.left.style) << "," << style.border.left.color;
    return oss.str();
}

// ---------------------------------------------------------------------------
// Named Range parsing helpers
// ---------------------------------------------------------------------------

// Raw defined name from XLSX - needs to be resolved after sheets are loaded
struct RawDefinedName {
    std::string name;       // e.g., "Company_Name"
    std::string reference;  // e.g., "'Sheet1'!$D$7" or "'Sheet1'!$A$1:$N$249"
    int localSheetId{-1};   // -1 for workbook scope, 0-indexed sheet index for sheet scope
};

// Parse the reference part of a defined name (e.g., "'Sheet1'!$D$7")
// Returns: sheet name, start ref (col, row, colAbs, rowAbs), and optionally end ref for ranges
struct ParsedDefinedNameRef {
    std::string sheetName;

    // Start cell (or single cell)
    int startCol{-1};
    int startRow{-1};
    bool startColAbsolute{false};
    bool startRowAbsolute{false};

    // End cell for ranges (or -1 if single cell)
    int endCol{-1};
    int endRow{-1};
    bool endColAbsolute{false};
    bool endRowAbsolute{false};

    bool valid{false};
    bool isRange{false};
};

// Parse a cell reference like "$D$7" or "D7" into column and row indices
// Returns false if parsing fails
bool parseCellRefWithAbsolute(const char* ref, int& col, int& row, bool& colAbsolute,
                              bool& rowAbsolute) {
    col = 0;
    row = 0;
    colAbsolute = false;
    rowAbsolute = false;

    // Check for column absolute marker
    if (*ref == '$') {
        colAbsolute = true;
        ref++;
    }

    // Parse column letters (A-Z, AA-ZZ, etc.)
    if (*ref < 'A' || *ref > 'Z') {
        return false;
    }
    while (*ref >= 'A' && *ref <= 'Z') {
        col = col * 26 + (*ref - 'A' + 1);
        ref++;
    }
    col--;  // Convert to 0-indexed

    // Check for row absolute marker
    if (*ref == '$') {
        rowAbsolute = true;
        ref++;
    }

    // Parse row number
    if (*ref < '0' || *ref > '9') {
        return false;
    }
    while (*ref >= '0' && *ref <= '9') {
        row = row * 10 + (*ref - '0');
        ref++;
    }
    row--;  // Convert to 0-indexed

    return true;
}

// Parse a defined name reference like "'Sheet1'!$D$7" or "'Sheet1'!$A$1:$N$249"
ParsedDefinedNameRef parseDefinedNameRef(const std::string& refStr) {
    ParsedDefinedNameRef result;

    if (refStr.empty()) {
        return result;
    }

    // Find the sheet reference separator '!'
    const size_t exclamPos = refStr.find('!');
    if (exclamPos == std::string::npos) {
        return result;  // No sheet reference
    }

    // Parse sheet name
    std::string sheetPart = refStr.substr(0, exclamPos);
    const std::string cellPart = refStr.substr(exclamPos + 1);

    // Sheet name may be quoted (e.g., "'Sheet 1'")
    if (!sheetPart.empty() && sheetPart[0] == '\'') {
        // Remove surrounding single quotes
        if (sheetPart.size() >= 2 && sheetPart.back() == '\'') {
            result.sheetName = sheetPart.substr(1, sheetPart.size() - 2);
            // Handle escaped single quotes ('' -> ')
            size_t pos = 0;
            while ((pos = result.sheetName.find("''", pos)) != std::string::npos) {
                result.sheetName.replace(pos, 2, "'");
                pos++;
            }
        } else {
            return result;  // Invalid quoted sheet name
        }
    } else {
        result.sheetName = sheetPart;
    }

    // Check for range (contains ':')
    const size_t colonPos = cellPart.find(':');
    if (colonPos != std::string::npos) {
        // It's a range like "$A$1:$N$249"
        result.isRange = true;
        const std::string startPart = cellPart.substr(0, colonPos);
        const std::string endPart = cellPart.substr(colonPos + 1);

        if (!parseCellRefWithAbsolute(startPart.c_str(), result.startCol, result.startRow,
                                      result.startColAbsolute, result.startRowAbsolute)) {
            return result;
        }
        if (!parseCellRefWithAbsolute(endPart.c_str(), result.endCol, result.endRow,
                                      result.endColAbsolute, result.endRowAbsolute)) {
            return result;
        }
    } else {
        // It's a single cell like "$D$7"
        if (!parseCellRefWithAbsolute(cellPart.c_str(), result.startCol, result.startRow,
                                      result.startColAbsolute, result.startRowAbsolute)) {
            return result;
        }
    }

    result.valid = true;
    return result;
}

// Parse xl/styles.xml into XLSXStyles struct
XLSXStyles parseStylesXml(const std::string& content, const XLSXThemeColors& theme) {
    XLSXStyles styles;

    if (content.empty()) {
        return styles;
    }

    pugi::xml_document doc;
    if (!doc.load_buffer(content.data(), content.size())) {
        return styles;
    }

    auto styleSheet = doc.child("styleSheet");

    // Parse custom number formats (numFmtId >= 164)
    auto numFmtsNode = styleSheet.child("numFmts");
    for (auto numFmtNode : numFmtsNode.children("numFmt")) {
        const int numFmtId = numFmtNode.attribute("numFmtId").as_int(-1);
        const char* formatCode = numFmtNode.attribute("formatCode").value();
        if (numFmtId >= 0 && formatCode != nullptr && formatCode[0] != '\0') {
            styles.customNumFormats[numFmtId] = formatCode;
        }
    }

    // Parse fonts
    auto fontsNode = styleSheet.child("fonts");
    for (auto fontNode : fontsNode.children("font")) {
        XLSXFont font;

        // Bold: <b/> or <b val="true"/>
        auto bNode = fontNode.child("b");
        if (bNode) {
            const char* val = bNode.attribute("val").value();
            font.bold = (val == nullptr || val[0] == '\0' || std::strcmp(val, "1") == 0 ||
                         std::strcmp(val, "true") == 0);
        }

        // Italic: <i/> or <i val="true"/>
        auto iNode = fontNode.child("i");
        if (iNode) {
            const char* val = iNode.attribute("val").value();
            font.italic = (val == nullptr || val[0] == '\0' || std::strcmp(val, "1") == 0 ||
                           std::strcmp(val, "true") == 0);
        }

        // Underline: <u/> or <u val="single"/>
        auto uNode = fontNode.child("u");
        if (uNode) {
            const char* val = uNode.attribute("val").value();
            // Any underline value counts as underline (single, double, etc.)
            font.underline = (val == nullptr || val[0] == '\0' || std::strcmp(val, "none") != 0);
        }

        // Font name: <name val="Arial"/>
        auto nameNode = fontNode.child("name");
        if (nameNode) {
            font.name = nameNode.attribute("val").value();
        }

        // Font size: <sz val="11"/>
        auto szNode = fontNode.child("sz");
        if (szNode) {
            font.size = szNode.attribute("val").as_double(0);
        }

        // Font color: <color rgb="FF000000"/> or <color theme="1"/>
        auto colorNode = fontNode.child("color");
        if (colorNode) {
            font.color = resolveColor(colorNode, theme);
        }

        styles.fonts.push_back(font);
    }

    // Parse fills
    auto fillsNode = styleSheet.child("fills");
    for (auto fillNode : fillsNode.children("fill")) {
        XLSXFill fill;

        auto patternFill = fillNode.child("patternFill");
        if (patternFill) {
            const char* patternType = patternFill.attribute("patternType").value();
            // Only extract color for solid fills
            if (patternType && std::strcmp(patternType, "solid") == 0) {
                auto fgColorNode = patternFill.child("fgColor");
                if (fgColorNode) {
                    fill.fgColor = resolveColor(fgColorNode, theme);
                }
            }
        }

        styles.fills.push_back(fill);
    }

    // Parse borders
    auto bordersNode = styleSheet.child("borders");
    for (auto borderNode : bordersNode.children("border")) {
        XLSXBorder border;
        border.left = parseBorderEdge(borderNode.child("left"), theme);
        border.right = parseBorderEdge(borderNode.child("right"), theme);
        border.top = parseBorderEdge(borderNode.child("top"), theme);
        border.bottom = parseBorderEdge(borderNode.child("bottom"), theme);
        styles.borders.push_back(border);
    }

    // Parse cellXfs (cell format records)
    auto cellXfsNode = styleSheet.child("cellXfs");
    for (auto xfNode : cellXfsNode.children("xf")) {
        XLSXCellFormat xf;

        xf.fontId = xfNode.attribute("fontId").as_int(0);
        xf.fillId = xfNode.attribute("fillId").as_int(0);
        xf.borderId = xfNode.attribute("borderId").as_int(0);
        xf.numFmtId = xfNode.attribute("numFmtId").as_int(0);

        // Check apply* attributes
        xf.applyFont = xfNode.attribute("applyFont").as_bool(false);
        xf.applyFill = xfNode.attribute("applyFill").as_bool(false);
        xf.applyBorder = xfNode.attribute("applyBorder").as_bool(false);
        xf.applyAlignment = xfNode.attribute("applyAlignment").as_bool(false);

        // If fontId > 0 but applyFont is not explicitly set, still apply font
        // Many XLSX files omit applyFont when font should be applied
        if (xf.fontId > 0 && !xf.applyFont) {
            xf.applyFont = true;
        }
        if (xf.fillId > 0 && !xf.applyFill) {
            xf.applyFill = true;
        }
        if (xf.borderId > 0 && !xf.applyBorder) {
            xf.applyBorder = true;
        }

        // Parse alignment
        auto alignmentNode = xfNode.child("alignment");
        if (alignmentNode) {
            xf.applyAlignment = true;
            xf.alignment.horizontal =
                parseHorizontalAlign(alignmentNode.attribute("horizontal").value());
            xf.alignment.vertical = parseVerticalAlign(alignmentNode.attribute("vertical").value());
        }

        styles.cellFormats.push_back(xf);
    }

    return styles;
}

// Map XLSX numFmtId to Cells format ID
// Returns empty string for General format or if format cannot be mapped
// For custom formats (numFmtId >= 164), returns the format code string which
// can be registered as a custom format in the workbook
//
// Excel built-in format IDs (from ECMA-376 Part 1, Section 18.8.30):
// 0: General
// 1: 0
// 2: 0.00
// 3: #,##0
// 4: #,##0.00
// 9: 0%
// 10: 0.00%
// 11: 0.00E+00
// 14: mm-dd-yy (date)
// 15: d-mmm-yy
// 16: d-mmm
// 17: mmm-yy
// 18: h:mm AM/PM
// 19: h:mm:ss AM/PM
// 20: h:mm
// 21: h:mm:ss
// 22: m/d/yy h:mm
// 37-40: Accounting formats
// 45-48: Time formats
// 49: @ (Text)
cells::ID mapNumFmtIdToFormatId(int numFmtId, const XLSXStyles& styles) {
    // Handle built-in Excel formats (0-163)
    switch (numFmtId) {
        case 0:  // General
            return {};

        case 1:                            // 0 (integer)
            return cells::ID("FMT_N000");  // NUMBER_0
        case 2:                            // 0.00
            return cells::ID("FMT_N002");  // NUMBER_2
        case 3:                            // #,##0
            return cells::ID("FMT_NS00");  // NUMBER_SEP
        case 4:                            // #,##0.00
            return cells::ID("FMT_NS02");  // NUMBER_SEP2

        // Currency formats (5-8) - USD with various decimal places
        case 5:                            // $#,##0_);($#,##0)
        case 6:                            // $#,##0_);[Red]($#,##0)
        case 37:                           // #,##0_);(#,##0)
        case 38:                           // #,##0_);[Red](#,##0)
            return cells::ID("CUSD_000");  // CURRENCY_USD_0
        case 7:                            // $#,##0.00_);($#,##0.00)
        case 8:                            // $#,##0.00_);[Red]($#,##0.00)
        case 39:                           // #,##0.00_);(#,##0.00)
        case 40:                           // #,##0.00_);[Red](#,##0.00)
        case 44:                           // _("$"* #,##0.00_);_("$"* \(#,##0.00\)
            return cells::ID("CUSD_002");  // CURRENCY_USD_2

        // Percentage formats
        case 9:                            // 0%
            return cells::ID("FMT_P000");  // PERCENTAGE_0
        case 10:                           // 0.00%
            return cells::ID("FMT_P002");  // PERCENTAGE_2

        // Scientific notation
        case 11:                           // 0.00E+00
        case 48:                           // ##0.0E+0
            return cells::ID("FMT_SCI2");  // SCIENTIFIC_2

        // Fraction formats (12-13) - we don't have direct support, use general
        case 12:  // # ?/?
        case 13:  // # ??/??
            return {};

        // Date formats (14-17)
        case 14:                           // mm-dd-yy or m/d/yyyy
        case 15:                           // d-mmm-yy
        case 16:                           // d-mmm
        case 17:                           // mmm-yy
            return cells::ID("FMT_DSHT");  // DATE_SHORT

        // Time formats (18-21, 45-47)
        case 18:                           // h:mm AM/PM
        case 19:                           // h:mm:ss AM/PM
            return cells::ID("FMT_T12H");  // TIME_12H
        case 20:                           // h:mm
        case 21:                           // h:mm:ss
        case 45:                           // mm:ss
        case 46:                           // [h]:mm:ss
        case 47:                           // mmss.0
            return cells::ID("FMT_T24H");  // TIME_24H

        // DateTime (22)
        case 22:                           // m/d/yy h:mm
            return cells::ID("FMT_DTSH");  // DATETIME_SHORT

        // Text format
        case 49:                           // @
            return cells::ID("FMT_TEXT");  // TEXT

        default:
            break;
    }

    // Handle custom formats (numFmtId >= 164)
    // Look up the format code and try to map it
    auto it = styles.customNumFormats.find(numFmtId);
    if (it == styles.customNumFormats.end()) {
        return {};  // Custom format not found
    }

    const std::string& formatCode = it->second;

    // Try to detect format type from format code and map to built-in format
    // Check for percentage (contains %)
    if (formatCode.find('%') != std::string::npos) {
        // Count decimal places by looking for pattern like ".00%"
        const size_t percentPos = formatCode.find('%');
        const size_t dotPos = formatCode.rfind('.', percentPos);
        if (dotPos != std::string::npos) {
            int decimals = 0;
            for (size_t i = dotPos + 1; i < percentPos; ++i) {
                if (formatCode[i] == '0' || formatCode[i] == '#') {
                    ++decimals;
                }
            }
            if (decimals >= 0 && decimals <= 15) {
                char formatId[9];
                std::snprintf(formatId, sizeof(formatId), "FMT_P0%02d", decimals);
                return cells::ID(formatId);
            }
        }
        return cells::ID("FMT_P000");  // Default percentage
    }

    // Check for currency (contains $, €, £, ¥, or currency locale patterns)
    bool hasCurrency = false;
    std::string currencyCode = "USD";
    if (formatCode.find('$') != std::string::npos || formatCode.find("[$") != std::string::npos) {
        hasCurrency = true;
        currencyCode = "USD";
    } else if (formatCode.find("€") != std::string::npos ||
               formatCode.find("[$€") != std::string::npos) {
        hasCurrency = true;
        currencyCode = "EUR";
    } else if (formatCode.find("£") != std::string::npos ||
               formatCode.find("[$£") != std::string::npos) {
        hasCurrency = true;
        currencyCode = "GBP";
    } else if (formatCode.find("¥") != std::string::npos ||
               formatCode.find("[$¥") != std::string::npos) {
        hasCurrency = true;
        // Could be JPY or CNY - default to JPY
        currencyCode = "JPY";
    }

    if (hasCurrency) {
        // Count decimal places
        int decimals = 0;
        const size_t dotPos = formatCode.find('.');
        if (dotPos != std::string::npos) {
            for (size_t i = dotPos + 1; i < formatCode.size(); ++i) {
                if (formatCode[i] == '0' || formatCode[i] == '#') {
                    ++decimals;
                } else {
                    break;  // Stop at first non-digit placeholder
                }
            }
        }
        decimals = std::min(decimals, 15);
        char formatId[9];
        std::snprintf(formatId, sizeof(formatId), "C%s_0%02d", currencyCode.c_str(), decimals);
        return cells::ID(formatId);
    }

    // Check for date patterns (contains y, m, d in date context)
    bool hasDatePattern = false;
    {
        std::string lower = formatCode;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // Look for date patterns - must have year or day indicators
        if ((lower.find("yy") != std::string::npos || lower.find("dd") != std::string::npos) &&
            lower.find("mm") != std::string::npos) {
            hasDatePattern = true;
        }
        // Or month name patterns
        if (lower.find("mmm") != std::string::npos) {
            hasDatePattern = true;
        }
    }

    if (hasDatePattern) {
        // Check if it also has time
        std::string lower = formatCode;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find("h:") != std::string::npos || lower.find(":mm") != std::string::npos) {
            return cells::ID("FMT_DTSH");  // DATETIME_SHORT
        }
        return cells::ID("FMT_DSHT");  // DATE_SHORT
    }

    // Check for time patterns (h:mm or similar)
    {
        std::string lower = formatCode;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find("h:") != std::string::npos || lower.find(":mm") != std::string::npos ||
            lower.find(":ss") != std::string::npos) {
            if (lower.find("am") != std::string::npos || lower.find("pm") != std::string::npos) {
                return cells::ID("FMT_T12H");  // TIME_12H
            }
            return cells::ID("FMT_T24H");  // TIME_24H
        }
    }

    // Check for scientific notation (contains E)
    if (formatCode.find('E') != std::string::npos || formatCode.find('e') != std::string::npos) {
        return cells::ID("FMT_SCI2");  // SCIENTIFIC_2
    }

    // Plain number format - check for thousands separator and decimals
    const bool hasThousandsSep =
        formatCode.find("#,##") != std::string::npos || formatCode.find(',') != std::string::npos;

    // Count decimal places
    int decimals = 0;
    const size_t dotPos = formatCode.find('.');
    if (dotPos != std::string::npos) {
        for (size_t i = dotPos + 1; i < formatCode.size(); ++i) {
            if (formatCode[i] == '0' || formatCode[i] == '#') {
                ++decimals;
            } else {
                break;
            }
        }
    }
    decimals = std::min(decimals, 15);

    char formatId[9];
    if (hasThousandsSep) {
        std::snprintf(formatId, sizeof(formatId), "FMT_NS%02d", decimals);
    } else {
        std::snprintf(formatId, sizeof(formatId), "FMT_N0%02d", decimals);
    }
    return cells::ID(formatId);
}

}  // namespace

namespace cells {

// Internal ZIP reader class (needs to be in cells namespace to be used by XLSXReader)
namespace detail {

class ZipReader {
public:
    ZipReader() = default;

    ~ZipReader() {
        if (opened_) {
            mz_zip_reader_end(&archive_);
        }
    }

    bool open(const std::string& path) {
        if (mz_zip_reader_init_file(&archive_, path.c_str(), 0) == 0) {
            return false;
        }
        opened_ = true;
        return true;
    }

    bool openFromMemory(const char* data, size_t size) {
        if (mz_zip_reader_init_mem(&archive_, data, size, 0) == 0) {
            lastError_ = mz_zip_get_last_error(&archive_);
            return false;
        }
        opened_ = true;
        return true;
    }

    [[nodiscard]] mz_zip_error getLastError() const { return lastError_; }

    // Read entire file from archive into string
    std::string readFile(const std::string& name) {
        const int index = mz_zip_reader_locate_file(&archive_, name.c_str(), nullptr, 0);
        if (index < 0) {
            return {};
        }

        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&archive_, index, &stat) == 0) {
            return {};
        }

        std::string content;
        content.resize(stat.m_uncomp_size);

        if (mz_zip_reader_extract_to_mem(&archive_, index, content.data(), content.size(), 0) ==
            0) {
            return {};
        }

        return content;
    }

private:
    mz_zip_archive archive_{};
    bool opened_{false};
    mz_zip_error lastError_{MZ_ZIP_NO_ERROR};
};

}  // namespace detail

// ============================================================================
// XLSXReadError implementation
// ============================================================================

std::string XLSXReadError::toString() const {
    std::ostringstream oss;
    oss << message;
    if (!sheet.empty()) {
        oss << " (sheet: " << sheet;
        if (row > 0 && col > 0) {
            oss << ", row: " << row << ", col: " << col;
        }
        oss << ")";
    }
    return oss.str();
}

// ============================================================================
// XLSXReader implementation
// ============================================================================

XLSXReader::XLSXReader() = default;

XLSXReader::XLSXReader(XLSXReadOptions options) : options_(std::move(options)) {}

void XLSXReader::addWarning(const std::string& msg) {
    warnings_.push_back(msg);
}

void XLSXReader::reset() {
    warnings_.clear();
}

// Static helper to parse XLSX from an already-opened ZipReader
static XLSXReadResult parseXLSXFromZip(detail::ZipReader& zip, const XLSXReadOptions& options,
                                       std::vector<std::string>& warnings) {
    auto totalStart = std::chrono::steady_clock::now();
    XLSXReadResult result;
    auto start = std::chrono::steady_clock::now();

    // Progress tracking
    size_t cellsLoaded = 0;
    size_t totalCellEstimate = 0;
    size_t lastProgressReport = 0;

    // Helper lambda to add warnings
    auto addWarning = [&warnings](const std::string& msg) { warnings.push_back(msg); };

    // Parse workbook relationships to get sheet paths
    start = std::chrono::steady_clock::now();
    std::string relsContent = zip.readFile("xl/_rels/workbook.xml.rels");
    std::unordered_map<std::string, std::string> sheetPaths;  // rId -> path

    if (!relsContent.empty()) {
        pugi::xml_document relsDoc;
        if (relsDoc.load_buffer(relsContent.data(), relsContent.size())) {
            for (auto rel : relsDoc.child("Relationships").children("Relationship")) {
                const char* id = rel.attribute("Id").value();
                const char* target = rel.attribute("Target").value();
                if (id && target) {
                    // Target can be absolute (/xl/worksheets/sheet1.xml) or relative
                    // (worksheets/sheet1.xml)
                    std::string fullPath;
                    if (target[0] == '/') {
                        // Absolute path - strip leading slash
                        fullPath = std::string(target + 1);
                    } else {
                        // Relative path - prepend xl/
                        fullPath = "xl/" + std::string(target);
                    }
                    sheetPaths[id] = fullPath;
                }
            }
        }
    }
    logTiming("parse rels", start);

    // Parse workbook to get sheet names and rIds
    start = std::chrono::steady_clock::now();
    std::string workbookContent = zip.readFile("xl/workbook.xml");
    std::vector<std::pair<std::string, std::string>> sheetInfo;  // name, path

    if (workbookContent.empty()) {
        result.error = XLSXReadError("Failed to read workbook.xml");
        return result;
    }

    pugi::xml_document wbDoc;
    if (!wbDoc.load_buffer(workbookContent.data(), workbookContent.size())) {
        result.error = XLSXReadError("Failed to parse workbook.xml");
        return result;
    }

    for (auto sheet : wbDoc.child("workbook").child("sheets").children("sheet")) {
        const char* name = sheet.attribute("name").value();
        const char* rId = sheet.attribute("r:id").value();
        if (name && rId) {
            auto it = sheetPaths.find(rId);
            if (it != sheetPaths.end()) {
                sheetInfo.emplace_back(name, it->second);
            }
        }
    }

    if (sheetInfo.empty()) {
        result.error = XLSXReadError("No sheets found in workbook");
        return result;
    }

    // Parse defined names (named ranges) from workbook.xml
    // These will be resolved after all sheets are loaded
    std::vector<RawDefinedName> rawDefinedNames;
    for (auto defName : wbDoc.child("workbook").child("definedNames").children("definedName")) {
        const char* name = defName.attribute("name").value();
        const char* refText = defName.text().get();
        if (name && name[0] != '\0' && refText && refText[0] != '\0') {
            RawDefinedName rawName;
            rawName.name = name;
            rawName.reference = refText;
            // localSheetId is 0-indexed sheet index for sheet-scoped names
            // If not present, it's workbook-scoped
            if (static_cast<bool>(defName.attribute("localSheetId"))) {
                rawName.localSheetId = defName.attribute("localSheetId").as_int(-1);
            }
            rawDefinedNames.push_back(rawName);
        }
    }
    logTiming("parse workbook", start);

    // Parse shared strings
    start = std::chrono::steady_clock::now();
    std::vector<std::string> sharedStrings;
    std::string ssContent = zip.readFile("xl/sharedStrings.xml");

    if (!ssContent.empty()) {
        pugi::xml_document ssDoc;
        if (ssDoc.load_buffer(ssContent.data(), ssContent.size())) {
            for (auto si : ssDoc.child("sst").children("si")) {
                // Handle both <t> and <r> (rich text) elements
                auto t = si.child("t");
                if (t) {
                    sharedStrings.emplace_back(t.text().get());
                } else {
                    // Rich text: concatenate all <t> elements within <r> elements
                    std::string text;
                    for (auto r : si.children("r")) {
                        auto rt = r.child("t");
                        if (rt) {
                            text += rt.text().get();
                        }
                    }
                    sharedStrings.push_back(text);
                }
            }
        }
    }
    logTiming("parse sharedStrings", start);

    // Parse theme.xml for theme colors
    start = std::chrono::steady_clock::now();
    XLSXThemeColors themeColors;
    if (options.readStyles) {
        const std::string themeContent = zip.readFile("xl/theme/theme1.xml");
        if (!themeContent.empty()) {
            themeColors = parseThemeXml(themeContent);
        }
    }
    logTiming("parse theme", start);

    // Parse styles if requested
    start = std::chrono::steady_clock::now();
    XLSXStyles xlsxStyles;
    // Map from CellStyle to our style ID (for deduplication)
    std::unordered_map<std::string, ID> styleToId;

    if (options.readStyles) {
        const std::string stylesContent = zip.readFile("xl/styles.xml");
        if (!stylesContent.empty()) {
            xlsxStyles = parseStylesXml(stylesContent, themeColors);
        }
    }
    logTiming("parse styles", start);

    // Create workbook
    auto workbook = std::make_unique<Workbook>(generate_id(), "Imported");

    // Style application helper - gets or creates a style ID for an XLSX style index
    // Defined here to be usable for both cell styles and axis default styles
    auto getOrCreateStyleId = [&](int xlsxStyleIndex) -> ID {
        if (!options.readStyles || xlsxStyleIndex <= 0) {
            return {};  // Null ID - no style or default style
        }

        CellStyle cellStyle;
        if (!xlsxStyles.getCellStyle(xlsxStyleIndex, cellStyle)) {
            return {};  // Failed to convert style
        }

        // Check if this style already exists (deduplication)
        const std::string styleKey = cellStyleToKey(cellStyle);
        auto it = styleToId.find(styleKey);
        if (it != styleToId.end()) {
            return it->second;
        }

        // Register new style
        const ID styleId = generate_id();
        workbook->registerStyle(styleId, cellStyle);
        styleToId[styleKey] = styleId;
        return styleId;
    };

    // Format ID helper - maps XLSX style index to Cells format ID
    // Cache to avoid recomputing format IDs for the same style index
    std::unordered_map<int, ID> styleIndexToFormatId;
    auto getFormatId = [&](int xlsxStyleIndex) -> ID {
        if (!options.readStyles || xlsxStyleIndex < 0) {
            return {};  // No format
        }

        // Check cache
        auto cacheIt = styleIndexToFormatId.find(xlsxStyleIndex);
        if (cacheIt != styleIndexToFormatId.end()) {
            return cacheIt->second;
        }

        // Get numFmtId from cell format
        if (xlsxStyleIndex >= static_cast<int>(xlsxStyles.cellFormats.size())) {
            styleIndexToFormatId[xlsxStyleIndex] = {};
            return {};
        }

        const int numFmtId = xlsxStyles.cellFormats[xlsxStyleIndex].numFmtId;
        const ID formatId = mapNumFmtIdToFormatId(numFmtId, xlsxStyles);
        styleIndexToFormatId[xlsxStyleIndex] = formatId;
        return formatId;
    };

    // Process each sheet
    for (const auto& [sheetName, sheetPath] : sheetInfo) {
        // Filter sheets if specific sheet requested
        if (!options.sheetName.empty() && sheetName != options.sheetName) {
            continue;
        }

        start = std::chrono::steady_clock::now();
        std::string sheetContent = zip.readFile(sheetPath);
        if (sheetContent.empty()) {
            addWarning("Failed to read sheet: " + sheetName);
            continue;
        }
        logTiming("read sheet XML", start);

        start = std::chrono::steady_clock::now();
        pugi::xml_document sheetDoc;
        if (!sheetDoc.load_buffer(sheetContent.data(), sheetContent.size())) {
            addWarning("Failed to parse sheet: " + sheetName);
            continue;
        }
        logTiming("parse sheet XML", start);

        // Create our Sheet
        auto sheet = std::make_unique<Sheet>(generate_id(), sheetName);

        // Parse sheet view properties (grid lines, zoom, etc.)
        auto worksheetNode = sheetDoc.child("worksheet");
        auto sheetViewsNode = worksheetNode.child("sheetViews");
        if (sheetViewsNode) {
            auto sheetViewNode = sheetViewsNode.child("sheetView");
            if (sheetViewNode) {
                // showGridLines: default is "1" (true), "0" means hidden
                auto showGridLinesAttr = sheetViewNode.attribute("showGridLines");
                if (showGridLinesAttr) {
                    sheet->showGridLines = showGridLinesAttr.as_bool(true);
                }

                // zoomScale: default is 100, valid range 10-400
                auto zoomScaleAttr = sheetViewNode.attribute("zoomScale");
                if (zoomScaleAttr) {
                    int zoom = zoomScaleAttr.as_int(100);
                    // Clamp to valid range
                    if (zoom < 10) {
                        zoom = 10;
                    }
                    if (zoom > 400) {
                        zoom = 400;
                    }
                    sheet->zoomScale = static_cast<uint16_t>(zoom);
                }

                // Parse freeze panes from <pane> element
                // XLSX uses xSplit/ySplit to indicate frozen columns/rows when state="frozen"
                auto paneNode = sheetViewNode.child("pane");
                if (paneNode) {
                    auto stateAttr = paneNode.attribute("state");
                    const std::string state = stateAttr ? stateAttr.as_string() : "";
                    // Only handle frozen panes, not split panes
                    if (state == "frozen" || state == "frozenSplit") {
                        const int xSplit = paneNode.attribute("xSplit").as_int(0);
                        const int ySplit = paneNode.attribute("ySplit").as_int(0);
                        // xSplit = number of frozen columns, ySplit = number of frozen rows
                        if (xSplit > 0) {
                            sheet->freezeCol = static_cast<uint16_t>(xSplit);
                        }
                        if (ySplit > 0) {
                            sheet->freezeRow = static_cast<uint16_t>(ySplit);
                        }
                    }
                }
            }
        }

        // Parse column properties (<cols> element) - includes hidden, width, style, etc.
        // XLSX cols use 1-based column indices and can specify ranges (min/max)
        // Width is in Excel "character width" units (approx 7 pixels per unit)
        start = std::chrono::steady_clock::now();
        std::unordered_map<int, bool> columnHidden;      // 0-indexed col -> hidden
        std::unordered_map<int, int> columnStyleIndex;   // 0-indexed col -> XLSX style index
        std::unordered_map<int, uint32_t> columnWidths;  // 0-indexed col -> width in pixels
        auto colsNode = worksheetNode.child("cols");
        if (colsNode) {
            for (auto colNode : colsNode.children("col")) {
                const int minCol = colNode.attribute("min").as_int(1) - 1;  // Convert to 0-indexed
                const int maxColRange = colNode.attribute("max").as_int(1) - 1;
                const bool hidden = colNode.attribute("hidden").as_bool(false);
                const int styleIndex = colNode.attribute("style").as_int(0);
                // Width attribute: character width units (default Excel column is ~8.43 chars)
                // Only parse if readDimensions is enabled
                const double widthAttr =
                    options.readDimensions ? colNode.attribute("width").as_double(0.0) : 0.0;
                for (int c = minCol; c <= maxColRange; ++c) {
                    if (hidden) {
                        columnHidden[c] = true;
                    }
                    if (styleIndex > 0) {
                        columnStyleIndex[c] = styleIndex;
                    }
                    if (widthAttr > 0.0) {
                        // Convert Excel character-width units to pixels
                        // Excel's default column width is 8.43 chars = ~64 pixels
                        // So: pixels = width * 7.5 (rounded)
                        const auto widthPx = static_cast<uint32_t>(std::lround(widthAttr * 7.5));
                        columnWidths[c] = widthPx > 0 ? widthPx : DEFAULT_COLUMN_WIDTH;
                    }
                }
            }
        }
        logTiming("parse cols element", start);

        // First pass: find dimensions
        start = std::chrono::steady_clock::now();
        int maxRow = 0, maxCol = 0;
        auto sheetData = sheetDoc.child("worksheet").child("sheetData");

        // Also track hidden rows, row styles, and row heights as we scan
        std::unordered_map<int, bool> rowHidden;       // 0-indexed row -> hidden
        std::unordered_map<int, int> rowStyleIndex;    // 0-indexed row -> XLSX style index
        std::unordered_map<int, uint32_t> rowHeights;  // 0-indexed row -> height in pixels

        for (auto row : sheetData.children("row")) {
            const int rowNum = row.attribute("r").as_int() - 1;  // 0-indexed
            if (rowNum >= maxRow) {
                maxRow = rowNum + 1;
            }

            // Check if row is hidden
            if (row.attribute("hidden").as_bool(false)) {
                rowHidden[rowNum] = true;
            }

            // Check for row default style (s attribute)
            const int styleIndex = row.attribute("s").as_int(0);
            if (styleIndex > 0) {
                rowStyleIndex[rowNum] = styleIndex;
            }

            // Parse row height (ht attribute) - in points (1pt = 1.33px approx)
            // Only parse if readDimensions is enabled
            if (options.readDimensions) {
                const double htAttr = row.attribute("ht").as_double(0.0);
                if (htAttr > 0.0) {
                    // Convert points to pixels: 1pt = 96/72 = 1.333... px
                    const auto heightPx = static_cast<uint32_t>(std::lround(htAttr * 96.0 / 72.0));
                    rowHeights[rowNum] = heightPx > 0 ? heightPx : DEFAULT_ROW_HEIGHT;
                }
            }

            for (auto cell : row.children("c")) {
                int col = 0, r = 0;
                parseCellRef(cell.attribute("r").value(), col, r);
                if (col >= maxCol) {
                    maxCol = col + 1;
                }
            }
        }
        logTiming("find dimensions", start);

        // Create columns and rows
        start = std::chrono::steady_clock::now();
        std::vector<ID> columnIds;
        std::vector<ID> rowIds;
        columnIds.reserve(maxCol);
        rowIds.reserve(maxRow);

        for (int c = 0; c < maxCol; ++c) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(c);
            // Apply column width from XLSX if available, otherwise use default
            auto widthIt = columnWidths.find(c);
            col->size = (widthIt != columnWidths.end()) ? widthIt->second : DEFAULT_COLUMN_WIDTH;
            col->hidden = columnHidden.count(c) > 0;
            // Apply column default style if present
            auto styleIt = columnStyleIndex.find(c);
            if (styleIt != columnStyleIndex.end()) {
                col->defaultStyleId = getOrCreateStyleId(styleIt->second);
            }
            columnIds.push_back(col->id);
            sheet->addColumn(std::move(col));
        }

        for (int r = 0; r < maxRow; ++r) {
            auto rowAxis = std::make_unique<Axis>(generate_id(), false);
            rowAxis->position = static_cast<uint32_t>(r);
            // Apply row height from XLSX if available, otherwise use default
            auto heightIt = rowHeights.find(r);
            rowAxis->size = (heightIt != rowHeights.end()) ? heightIt->second : DEFAULT_ROW_HEIGHT;
            rowAxis->hidden = rowHidden.count(r) > 0;
            // Apply row default style if present
            auto styleIt = rowStyleIndex.find(r);
            if (styleIt != rowStyleIndex.end()) {
                rowAxis->defaultStyleId = getOrCreateStyleId(styleIt->second);
            }
            rowIds.push_back(rowAxis->id);
            sheet->addRow(std::move(rowAxis));
        }
        logTiming("create rows/cols", start);

        // Second pass: create cells
        start = std::chrono::steady_clock::now();
        int cellCount = 0;

        // Count cells for reservation
        for (auto row : sheetData.children("row")) {
            for ([[maybe_unused]] auto c : row.children("c")) {
                cellCount++;
            }
        }
        sheet->reserveCells(cellCount);
        totalCellEstimate += cellCount;

        // Track shared formulas: si index -> master cell
        std::unordered_map<int, Cell*> sharedFormulaMasters;
        // Track subscribers that need to be linked: si index -> list of subscriber cells
        std::unordered_map<int, std::vector<Cell*>> sharedFormulaSubscribers;

        // Progress reporting helper
        auto reportProgress = [&]() {
            if (options.progressCallback &&
                cellsLoaded - lastProgressReport >= options.progressInterval) {
                options.progressCallback(cellsLoaded, totalCellEstimate);
                lastProgressReport = cellsLoaded;
            }
        };

        for (auto row : sheetData.children("row")) {
            for (auto cellNode : row.children("c")) {
                int col = 0, rowNum = 0;
                parseCellRef(cellNode.attribute("r").value(), col, rowNum);

                if (col < 0 || col >= maxCol || rowNum < 0 || rowNum >= maxRow) {
                    continue;
                }

                // Get value
                std::string value;
                const char* type = cellNode.attribute("t").value();
                auto vNode = cellNode.child("v");

                if (vNode) {
                    const char* rawValue = vNode.text().get();

                    if (type && type[0] == 's') {
                        // Shared string
                        const int idx = std::atoi(rawValue);
                        if (idx >= 0 && idx < static_cast<int>(sharedStrings.size())) {
                            value = sharedStrings[idx];
                        }
                    } else {
                        value = rawValue;
                    }
                } else if (type && std::strcmp(type, "inlineStr") == 0) {
                    // Inline string: <c t="inlineStr"><is><t>text</t></is></c>
                    auto isNode = cellNode.child("is");
                    if (isNode) {
                        auto tNode = isNode.child("t");
                        if (tNode) {
                            value = tNode.text().get();
                        } else {
                            // Rich text: concatenate all <t> elements within <r> elements
                            for (auto rNode : isNode.children("r")) {
                                auto rtNode = rNode.child("t");
                                if (rtNode) {
                                    value += rtNode.text().get();
                                }
                            }
                        }
                    }
                }

                // Skip empty cells that have no style
                // We need to keep empty cells if they have styling (e.g., background color)
                // as they may be part of styled header rows
                const int styleIndexForSkip = cellNode.attribute("s").as_int(0);
                if (value.empty() && !cellNode.child("f") && styleIndexForSkip == 0) {
                    continue;
                }

                // Create cell
                auto cell = std::make_unique<Cell>(generate_id(), columnIds[col], rowIds[rowNum]);

                // Apply style if present
                const int styleIndex = cellNode.attribute("s").as_int(0);
                const ID styleId = getOrCreateStyleId(styleIndex);
                if (!styleId.isNull()) {
                    cell->styleId = styleId;
                }

                // Apply number format if present
                const ID formatId = getFormatId(styleIndex);
                if (!formatId.isNull()) {
                    cell->formatId = formatId;
                }

                // Parse value based on type (type was read earlier)
                const int cellType = mapCellType(type);

                switch (cellType) {
                    case 2:  // Number
                        if (!value.empty()) {
                            // Use strtod instead of std::stod to avoid exceptions
                            cell->value = CellValue(strtod(value.c_str(), nullptr));
                        }
                        break;
                    case 3:  // Boolean
                        cell->value = CellValue(value == "1" || value == "true");
                        break;
                    case 4:  // Error
                        cell->value = CellValue(stringToError(value));
                        break;
                    default:  // String
                        cell->value = CellValue(value);
                        break;
                }

                // Read formula if present and requested
                if (options.readFormulas) {
                    auto fNode = cellNode.child("f");
                    if (fNode) {
                        const char* formulaType = fNode.attribute("t").value();
                        const bool isShared =
                            (formulaType != nullptr) && std::strcmp(formulaType, "shared") == 0;

                        if (isShared) {
                            const int si = fNode.attribute("si").as_int(-1);
                            const char* ref = fNode.attribute("ref").value();
                            const char* formulaText = fNode.text().get();

                            if (si >= 0) {
                                // Master cell has ref attribute and formula text
                                if (ref && ref[0] != '\0' && formulaText &&
                                    formulaText[0] != '\0') {
                                    if (options.readFormulaText) {
                                        // Parse formula text to create AST
                                        const std::string fullFormula =
                                            "=" + std::string(formulaText);
                                        cells::FormulaParser parser(fullFormula);
                                        std::unique_ptr<cells::ASTNode> ast = parser.parse();
                                        auto* formula = new cells::Formula();
                                        formula->ast = ast.release();
                                        formula->dirty = true;
                                        cell->setFormula(formula);
                                    } else {
                                        // Empty formula placeholder
                                        auto* formula = new cells::Formula();
                                        formula->dirty = true;
                                        cell->setFormula(formula);
                                    }
                                    Cell* rawPtr = cell.get();
                                    sheet->addCell(std::move(cell));
                                    sharedFormulaMasters[si] = rawPtr;
                                    cellsLoaded++;
                                    reportProgress();
                                    continue;
                                }
                                // Subscriber cell has only si attribute (rawPtr used for
                                // setSharedFormulaRef)
                                Cell* rawPtr = cell.get();  // NOLINT(misc-const-correctness)
                                sheet->addCell(std::move(cell));
                                sharedFormulaSubscribers[si].push_back(rawPtr);
                                cellsLoaded++;
                                reportProgress();
                                continue;
                            }
                        }

                        // Regular formula (not shared)
                        if (options.readFormulaText) {
                            const std::string formulaTextStr = fNode.text().get();
                            const std::string fullFormula = "=" + formulaTextStr;
                            cells::FormulaParser parser(fullFormula);
                            std::unique_ptr<cells::ASTNode> ast = parser.parse();
                            auto* formula = new cells::Formula();
                            formula->ast = ast.release();
                            formula->dirty = true;
                            cell->setFormula(formula);
                        } else {
                            // Empty formula placeholder
                            auto* formula = new cells::Formula();
                            formula->dirty = true;
                            cell->setFormula(formula);
                        }
                    }
                }

                sheet->addCell(std::move(cell));
                cellsLoaded++;
                reportProgress();
            }
        }

        // Link shared formula subscribers to their masters using Sheet-level tracking
        for (const auto& [si, subscribers] : sharedFormulaSubscribers) {
            auto masterIt = sharedFormulaMasters.find(si);
            if (masterIt != sharedFormulaMasters.end()) {
                const Cell* master = masterIt->second;

                // Collect subscriber IDs
                std::vector<ID> subscriberIds;
                subscriberIds.reserve(subscribers.size());
                for (Cell* subscriber : subscribers) {
                    subscriberIds.push_back(subscriber->id);
                    // Mark cell as a shared formula subscriber
                    subscriber->setSharedFormulaSubscriber(true);
                }

                // Register the shared formula group at Sheet level
                sheet->registerSharedFormulaGroup(master->id, subscriberIds);
            } else {
                // Master not found - add warning and leave subscriber without formula
                addWarning("Shared formula master not found for si=" + std::to_string(si));
            }
        }
        logTiming("create cells", start);

        // Parse merged cells (<mergeCells> element in worksheet XML)
        // Format: <mergeCells><mergeCell ref="A2:E2"/></mergeCells>
        // Note: Merged cells may span columns/rows that don't have cells.
        // We need to ensure those columns/rows exist before adding the merge.
        start = std::chrono::steady_clock::now();
        auto mergeCellsNode = worksheetNode.child("mergeCells");
        if (mergeCellsNode) {
            for (auto mergeNode : mergeCellsNode.children("mergeCell")) {
                const char* refAttr = mergeNode.attribute("ref").value();
                if (refAttr == nullptr || refAttr[0] == '\0') {
                    continue;
                }

                // Parse range reference (e.g., "A2:E2")
                const std::string refStr(refAttr);
                const size_t colonPos = refStr.find(':');
                if (colonPos == std::string::npos) {
                    // Single cell, not a valid merge
                    continue;
                }

                int startCol = 0, startRow = 0, endCol = 0, endRow = 0;
                parseCellRef(refStr.substr(0, colonPos).c_str(), startCol, startRow);
                parseCellRef(refStr.substr(colonPos + 1).c_str(), endCol, endRow);

                // Basic sanity check
                if (startCol < 0 || startRow < 0 || endCol < startCol || endRow < startRow) {
                    addWarning("Invalid merged cell range: " + refStr);
                    continue;
                }

                // Ensure columns exist up to endCol
                while (static_cast<int>(columnIds.size()) <= endCol) {
                    const ID colId = generate_id();
                    auto col = std::make_unique<Axis>(colId, true);
                    col->position = static_cast<uint32_t>(columnIds.size());
                    col->size = DEFAULT_COLUMN_WIDTH;
                    columnIds.push_back(colId);
                    sheet->addColumn(std::move(col));
                }

                // Ensure rows exist up to endRow
                while (static_cast<int>(rowIds.size()) <= endRow) {
                    const ID rowId = generate_id();
                    auto row = std::make_unique<Axis>(rowId, false);
                    row->position = static_cast<uint32_t>(rowIds.size());
                    row->size = DEFAULT_ROW_HEIGHT;
                    rowIds.push_back(rowId);
                    sheet->addRow(std::move(row));
                }

                // Calculate spans
                const int colSpan = endCol - startCol + 1;
                const int rowSpan = endRow - startRow + 1;

                if (colSpan < 1 || rowSpan < 1 || (colSpan == 1 && rowSpan == 1)) {
                    // Invalid merge
                    continue;
                }

                // Get anchor column and row IDs
                const ID& anchorColId = columnIds[startCol];
                const ID& anchorRowId = rowIds[startRow];

                // Add merge range to sheet
                sheet->addMergeRange(anchorColId, anchorRowId, static_cast<uint16_t>(colSpan),
                                     static_cast<uint16_t>(rowSpan));
            }
        }
        logTiming("parse merged cells", start);

        workbook->addSheet(std::move(sheet));
    }

    // Check if requested sheet was found
    if (!options.sheetName.empty() && workbook->sheets.empty()) {
        result.error = XLSXReadError("Sheet \"" + options.sheetName + "\" not found");
        return result;
    }

    // Resolve named ranges now that all sheets and cells are loaded
    start = std::chrono::steady_clock::now();
    for (const auto& rawName : rawDefinedNames) {
        // Skip reserved names (like _xlnm.Print_Area, _xlnm.Print_Titles)
        if (rawName.name.find("_xlnm.") == 0) {
            continue;
        }

        const ParsedDefinedNameRef parsed = parseDefinedNameRef(rawName.reference);
        if (!parsed.valid) {
            addWarning("Failed to parse named range reference: " + rawName.name + " = " +
                       rawName.reference);
            continue;
        }

        // Find the target sheet
        Sheet* targetSheet = workbook->getSheetByName(parsed.sheetName);
        if (targetSheet == nullptr) {
            addWarning("Named range references unknown sheet: " + rawName.name + " -> " +
                       parsed.sheetName);
            continue;
        }

        // Get the cell(s) by position
        const Axis* startCol =
            targetSheet->getColumnByPosition(static_cast<uint32_t>(parsed.startCol));
        const Axis* startRow =
            targetSheet->getRowByPosition(static_cast<uint32_t>(parsed.startRow));

        if (startCol == nullptr || startRow == nullptr) {
            addWarning("Named range references out-of-bounds cell: " + rawName.name);
            continue;
        }

        const Cell* startCell = targetSheet->getCellAt(startCol->id, startRow->id);

        // Determine scope
        ID scopeSheetId;
        if (rawName.localSheetId >= 0 &&
            rawName.localSheetId < static_cast<int>(workbook->sheetCount())) {
            // Sheet-scoped: use the sheet at localSheetId index
            const Sheet* scopeSheet =
                workbook->getSheetByIndex(static_cast<size_t>(rawName.localSheetId));
            if (scopeSheet != nullptr) {
                scopeSheetId = scopeSheet->id;
            }
        }

        NamedRangeRegistry* registry = workbook->getNamedRanges();
        if (registry == nullptr) {
            continue;
        }

        if (parsed.isRange) {
            // Range reference
            const Axis* endCol =
                targetSheet->getColumnByPosition(static_cast<uint32_t>(parsed.endCol));
            const Axis* endRow =
                targetSheet->getRowByPosition(static_cast<uint32_t>(parsed.endRow));

            if (endCol == nullptr || endRow == nullptr) {
                addWarning("Named range references out-of-bounds end cell: " + rawName.name);
                continue;
            }

            const Cell* endCell = targetSheet->getCellAt(endCol->id, endRow->id);

            // Create or get the corner cells
            // For ranges, we need both start and end cells to exist
            // If they don't exist, we need to handle this case
            ID startCellId;
            ID endCellId;

            if (startCell != nullptr) {
                startCellId = startCell->id;
            } else {
                // Cell doesn't exist at this position - create a virtual target
                // For now, just skip (cell must exist)
                addWarning("Named range start cell does not exist: " + rawName.name);
                continue;
            }

            if (endCell != nullptr) {
                endCellId = endCell->id;
            } else {
                // End cell doesn't exist - create a virtual target
                addWarning("Named range end cell does not exist: " + rawName.name);
                continue;
            }

            const NamedRangeTarget target =
                NamedRangeTarget::range(startCellId, endCellId, targetSheet->id);

            if (scopeSheetId.isNull()) {
                registry->defineWorkbook(rawName.name, target);
            } else {
                registry->defineSheet(rawName.name, scopeSheetId, target);
            }
        } else {
            // Single cell reference
            if (startCell == nullptr) {
                // Cell doesn't exist at this position
                addWarning("Named range cell does not exist: " + rawName.name);
                continue;
            }

            const NamedRangeTarget target = NamedRangeTarget::cell(startCell->id, targetSheet->id);

            if (scopeSheetId.isNull()) {
                registry->defineWorkbook(rawName.name, target);
            } else {
                registry->defineSheet(rawName.name, scopeSheetId, target);
            }
        }
    }
    logTiming("resolve named ranges", start);

    // Final progress report (100% complete)
    if (options.progressCallback && cellsLoaded > lastProgressReport) {
        options.progressCallback(cellsLoaded, cellsLoaded);
    }

    logTiming("TOTAL", totalStart);

    result.workbook = std::move(workbook);
    result.warnings = std::move(warnings);
    return result;
}

// ============================================================================
// XLSXReader public methods
// ============================================================================

XLSXReadResult XLSXReader::readFile(const std::string& path) {
    reset();
    detail::ZipReader zip;
    if (!zip.open(path)) {
        XLSXReadResult result;
        result.error = XLSXReadError("Failed to open XLSX file: " + path);
        return result;
    }
    return parseXLSXFromZip(zip, options_, warnings_);
}

XLSXReadResult XLSXReader::readFromMemory(const char* data, size_t size) {
    reset();
    detail::ZipReader zip;
    if (!zip.openFromMemory(data, size)) {
        XLSXReadResult result;
        std::string errorMsg = "Failed to read XLSX data from memory";
        errorMsg += " (size=" + std::to_string(size) + ", miniz error: ";
        errorMsg += mz_zip_get_error_string(zip.getLastError());
        errorMsg += ")";
        result.error = XLSXReadError(errorMsg);
        return result;
    }
    return parseXLSXFromZip(zip, options_, warnings_);
}

// ============================================================================
// Convenience functions
// ============================================================================

XLSXReadResult readXLSX(const std::string& path) {
    XLSXReader reader;
    return reader.readFile(path);
}

XLSXReadResult readXLSX(const std::string& path, const XLSXReadOptions& options) {
    XLSXReader reader(options);
    return reader.readFile(path);
}

XLSXReadResult readXLSXFromMemory(const char* data, size_t size) {
    XLSXReader reader;
    return reader.readFromMemory(data, size);
}

XLSXReadResult readXLSXFromMemory(const char* data, size_t size, const XLSXReadOptions& options) {
    XLSXReader reader(options);
    return reader.readFromMemory(data, size);
}

}  // namespace cells
