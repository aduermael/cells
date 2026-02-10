// =============================================================================
// Excel Theme Support
// =============================================================================
//
// Theme data model for Excel-compatible workbooks. A theme provides:
// - Color scheme: 12 named color slots (lt1, dk1, lt2, dk2, accent1-6, hlink, folHlink)
// - Font scheme: major font (headings) and minor font (body text)
//
// Theme/indexed color references allow non-destructive roundtrips of Excel
// files by preserving the original reference type instead of resolving to hex.
//
// =============================================================================

#ifndef CELLS_THEME_H_
#define CELLS_THEME_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace cells {

// Excel's 64 standard indexed colors (indices 0-63) plus system fg/bg (64-65)
// Based on ECMA-376 Part 1 Section 18.8.27
inline const char* const kIndexedColors[64] = {
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

// Theme color scheme - 12 named color slots as #RRGGBB
// Index mapping (OOXML spreadsheet convention):
//   0: lt1 (Background 1, usually white)
//   1: dk1 (Text 1, usually black)
//   2: lt2 (Background 2)
//   3: dk2 (Text 2)
//   4-9: accent1-6
//   10: hlink (Hyperlink)
//   11: folHlink (Followed hyperlink)
struct ThemeColorScheme {
    std::string colors[12];

    [[nodiscard]] const std::string& getColor(int index) const {
        static const std::string empty;
        if (index >= 0 && index < 12) {
            return colors[index];
        }
        return empty;
    }

    void setColor(int index, const std::string& color) {
        if (index >= 0 && index < 12) {
            colors[index] = color;
        }
    }

    // Named accessors
    [[nodiscard]] const std::string& lt1() const { return colors[0]; }
    [[nodiscard]] const std::string& dk1() const { return colors[1]; }
    [[nodiscard]] const std::string& lt2() const { return colors[2]; }
    [[nodiscard]] const std::string& dk2() const { return colors[3]; }
    [[nodiscard]] const std::string& accent1() const { return colors[4]; }
    [[nodiscard]] const std::string& accent2() const { return colors[5]; }
    [[nodiscard]] const std::string& accent3() const { return colors[6]; }
    [[nodiscard]] const std::string& accent4() const { return colors[7]; }
    [[nodiscard]] const std::string& accent5() const { return colors[8]; }
    [[nodiscard]] const std::string& accent6() const { return colors[9]; }
    [[nodiscard]] const std::string& hlink() const { return colors[10]; }
    [[nodiscard]] const std::string& folHlink() const { return colors[11]; }

    bool operator==(const ThemeColorScheme& other) const {
        for (int i = 0; i < 12; ++i) {
            if (colors[i] != other.colors[i]) return false;
        }
        return true;
    }
    bool operator!=(const ThemeColorScheme& other) const { return !(*this == other); }
};

// Theme font scheme - major (headings) and minor (body) font names
struct ThemeFontScheme {
    std::string majorFont;  // Headings font (e.g. "Calibri Light")
    std::string minorFont;  // Body font (e.g. "Calibri")

    bool operator==(const ThemeFontScheme& other) const {
        return majorFont == other.majorFont && minorFont == other.minorFont;
    }
    bool operator!=(const ThemeFontScheme& other) const { return !(*this == other); }
};

// Complete theme combining color scheme, font scheme, and name
struct Theme {
    std::string name;
    ThemeColorScheme colorScheme;
    ThemeFontScheme fontScheme;

    bool operator==(const Theme& other) const {
        return name == other.name && colorScheme == other.colorScheme &&
               fontScheme == other.fontScheme;
    }
    bool operator!=(const Theme& other) const { return !(*this == other); }
};

// Apply tint to a hex color string
// tint < 0: darken toward black (tint = -1.0 is fully black)
// tint > 0: lighten toward white (tint = 1.0 is fully white)
// tint = 0: no change
// Uses HSL color space transformation per ECMA-376 spec
inline std::string applyTint(const std::string& color, double tint) {
    if (color.empty() || color.length() != 7 || color[0] != '#') {
        return color;
    }
    if (tint == 0.0) {
        return color;
    }

    int r = std::stoi(color.substr(1, 2), nullptr, 16);
    int g = std::stoi(color.substr(3, 2), nullptr, 16);
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

    // Apply tint to lightness (ECMA-376 algorithm)
    if (tint < 0) {
        l = l * (1.0 + tint);
    } else {
        l = l * (1.0 - tint) + tint;
    }
    l = std::max(0.0, std::min(1.0, l));

    // Convert HSL back to RGB
    auto hueToRgb = [](double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
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

    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));

    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

// Resolve a theme color reference to a hex color string
// Looks up the theme's color scheme by index and applies tint
// Returns empty string if theme is null or index is out of range
inline std::string resolveThemeColor(const Theme* theme, int index, double tint) {
    if (!theme) return {};
    const std::string& base = theme->colorScheme.getColor(index);
    if (base.empty()) return {};
    if (tint == 0.0) return base;
    return applyTint(base, tint);
}

// Resolve an indexed color reference to a hex color string
// Uses the fixed 64-color legacy palette, plus indices 64/65 for system fg/bg
// Returns empty string for invalid indices
inline std::string resolveIndexedColor(int index) {
    if (index >= 0 && index < 64) {
        return kIndexedColors[index];
    }
    if (index == 64) return "#000000";  // System foreground
    if (index == 65) return "#FFFFFF";  // System background
    return {};
}

// Resolve a theme font reference to a font name
// fontThemeIndex: 0 = major (headings), 1 = minor (body)
// Returns empty string if theme is null or index is invalid
inline std::string resolveThemeFont(const Theme* theme, int fontThemeIndex) {
    if (!theme) return {};
    if (fontThemeIndex == 0) return theme->fontScheme.majorFont;
    if (fontThemeIndex == 1) return theme->fontScheme.minorFont;
    return {};
}

}  // namespace cells

#endif  // CELLS_THEME_H_
