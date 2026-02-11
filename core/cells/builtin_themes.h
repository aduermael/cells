// =============================================================================
// Built-in Theme Palettes
// =============================================================================
//
// Predefined workbook themes for the "Themes" gallery. Each theme provides a
// 12-color scheme and a font scheme. The first entry ("Office") matches Excel's
// default theme; "Cells" is our own clean default. The rest are modern palettes
// designed for contemporary design aesthetics.
//
// Color slot layout (OOXML convention):
//   0: lt1 (Background 1)    1: dk1 (Text 1)
//   2: lt2 (Background 2)    3: dk2 (Text 2)
//   4-9: accent1-6           10: hlink   11: folHlink
//
// =============================================================================

#ifndef CELLS_BUILTIN_THEMES_H_
#define CELLS_BUILTIN_THEMES_H_

#include <string>
#include <vector>

#include "core/cells/theme.h"

namespace cells {

// A built-in theme entry with its definition
struct BuiltinTheme {
    std::string name;
    Theme theme;
};

// ---------------------------------------------------------------------------
// Helper: construct a Theme from color array + fonts
// ---------------------------------------------------------------------------
inline Theme makeTheme(const std::string& name, const std::string (&colors)[12],
                       const std::string& majorFont, const std::string& minorFont) {
    Theme t;
    t.name = name;
    for (int i = 0; i < 12; ++i) {
        t.colorScheme.colors[i] = colors[i];
    }
    t.fontScheme.majorFont = majorFont;
    t.fontScheme.minorFont = minorFont;
    return t;
}

// ---------------------------------------------------------------------------
// Get all built-in themes
// ---------------------------------------------------------------------------
inline std::vector<BuiltinTheme> getBuiltinThemes() {
    std::vector<BuiltinTheme> themes;
    themes.reserve(12);

    // ========================================================================
    // Office — Excel's default theme (Office 2013+)
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1 - Background 1
            "#000000",  // dk1 - Text 1
            "#E7E6E6",  // lt2 - Background 2
            "#44546A",  // dk2 - Text 2
            "#4472C4",  // accent1 - Blue
            "#ED7D31",  // accent2 - Orange
            "#A5A5A5",  // accent3 - Gray
            "#FFC000",  // accent4 - Gold
            "#5B9BD5",  // accent5 - Light Blue
            "#70AD47",  // accent6 - Green
            "#0563C1",  // hlink
            "#954F72",  // folHlink
        };
        themes.push_back({"Office", makeTheme("Office", colors, "Calibri Light", "Calibri")});
    }

    // ========================================================================
    // Cells — our own clean default (slightly warmer, modern feel)
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1
            "#1A1A2E",  // dk1 - deep navy-black
            "#F0F0F5",  // lt2 - cool light gray
            "#3D3D5C",  // dk2 - muted navy
            "#3A86FF",  // accent1 - vivid blue
            "#FF006E",  // accent2 - hot pink
            "#8338EC",  // accent3 - purple
            "#FFBE0B",  // accent4 - amber
            "#06D6A0",  // accent5 - mint green
            "#FB5607",  // accent6 - tangerine
            "#3A86FF",  // hlink
            "#8338EC",  // folHlink
        };
        themes.push_back({"Cells", makeTheme("Cells", colors, "Inter", "Inter")});
    }

    // ========================================================================
    // Arctic — cool blues and grays, crisp and professional
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1
            "#1B2838",  // dk1 - dark slate blue
            "#E8EDF2",  // lt2 - ice blue-gray
            "#3B5068",  // dk2 - steel blue
            "#2E86AB",  // accent1 - cerulean
            "#A23B72",  // accent2 - plum
            "#5C8A97",  // accent3 - teal gray
            "#E8C547",  // accent4 - gold
            "#63B0CD",  // accent5 - sky blue
            "#3D5A80",  // accent6 - navy
            "#2E86AB",  // hlink
            "#5C8A97",  // folHlink
        };
        themes.push_back({"Arctic", makeTheme("Arctic", colors, "Segoe UI", "Segoe UI")});
    }

    // ========================================================================
    // Sunset — warm oranges, reds, and golds
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFDF7",  // lt1 - warm white
            "#2D1B00",  // dk1 - dark brown
            "#FFF0DB",  // lt2 - warm cream
            "#5C3D1E",  // dk2 - brown
            "#E76F51",  // accent1 - burnt sienna
            "#F4A261",  // accent2 - sandy brown
            "#E9C46A",  // accent3 - gold
            "#264653",  // accent4 - dark teal (contrast)
            "#2A9D8F",  // accent5 - teal
            "#D62828",  // accent6 - crimson
            "#E76F51",  // hlink
            "#D62828",  // folHlink
        };
        themes.push_back({"Sunset", makeTheme("Sunset", colors, "Georgia", "Georgia")});
    }

    // ========================================================================
    // Forest — earthy greens and natural tones
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FAFDF7",  // lt1 - natural white
            "#1B2E1B",  // dk1 - deep forest
            "#E8F0E4",  // lt2 - pale sage
            "#3D5C3D",  // dk2 - forest green
            "#588157",  // accent1 - fern green
            "#A3B18A",  // accent2 - sage
            "#DAD7CD",  // accent3 - light tan
            "#3A5A40",  // accent4 - dark green
            "#84A98C",  // accent5 - eucalyptus
            "#BC6C25",  // accent6 - amber brown
            "#588157",  // hlink
            "#3A5A40",  // folHlink
        };
        themes.push_back(
            {"Forest", makeTheme("Forest", colors, "Palatino Linotype", "Palatino Linotype")});
    }

    // ========================================================================
    // Lavender — soft purples and pinks
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FEFAFF",  // lt1 - lavender white
            "#1F1033",  // dk1 - deep purple
            "#F0E6F6",  // lt2 - light lavender
            "#4A2C6E",  // dk2 - dark purple
            "#7B2D8E",  // accent1 - violet
            "#C77DFF",  // accent2 - light purple
            "#E0AAFF",  // accent3 - soft lavender
            "#9D4EDD",  // accent4 - medium purple
            "#5A189A",  // accent5 - deep purple
            "#FF6D94",  // accent6 - pink
            "#7B2D8E",  // hlink
            "#5A189A",  // folHlink
        };
        themes.push_back({"Lavender", makeTheme("Lavender", colors, "Segoe UI", "Segoe UI")});
    }

    // ========================================================================
    // Midnight — deep blues and teals, dark mode friendly
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1
            "#0D1B2A",  // dk1 - midnight blue
            "#E0E7EE",  // lt2 - soft blue-gray
            "#1B3A5C",  // dk2 - deep blue
            "#00B4D8",  // accent1 - cyan
            "#0077B6",  // accent2 - medium blue
            "#90E0EF",  // accent3 - light cyan
            "#CAF0F8",  // accent4 - ice blue
            "#48CAE4",  // accent5 - sky blue
            "#023E8A",  // accent6 - navy
            "#00B4D8",  // hlink
            "#0077B6",  // folHlink
        };
        themes.push_back({"Midnight", makeTheme("Midnight", colors, "Calibri Light", "Calibri")});
    }

    // ========================================================================
    // Coral — warm pastels, friendly and approachable
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFBF5",  // lt1 - warm white
            "#2B2024",  // dk1 - dark mauve
            "#FFF0EB",  // lt2 - peach cream
            "#5C4047",  // dk2 - dark mauve
            "#FF7F7F",  // accent1 - coral
            "#FFB3B3",  // accent2 - light coral
            "#FFDAB9",  // accent3 - peach
            "#FF6B6B",  // accent4 - red coral
            "#EE9B00",  // accent5 - amber
            "#E07A5F",  // accent6 - terra cotta
            "#FF6B6B",  // hlink
            "#E07A5F",  // folHlink
        };
        themes.push_back({"Coral", makeTheme("Coral", colors, "Segoe UI", "Segoe UI")});
    }

    // ========================================================================
    // Slate — neutral and professional, minimalist
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1
            "#212529",  // dk1 - charcoal
            "#F8F9FA",  // lt2 - near-white
            "#495057",  // dk2 - medium gray
            "#6C757D",  // accent1 - gray
            "#ADB5BD",  // accent2 - light gray
            "#343A40",  // accent3 - dark gray
            "#DEE2E6",  // accent4 - silver
            "#868E96",  // accent5 - medium gray
            "#495057",  // accent6 - darker gray
            "#0D6EFD",  // hlink - blue (for visibility)
            "#6610F2",  // folHlink - purple
        };
        themes.push_back({"Slate", makeTheme("Slate", colors, "Helvetica Neue", "Helvetica Neue")});
    }

    // ========================================================================
    // Neon — high contrast, vibrant and energetic
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFFFF",  // lt1
            "#0A0A0A",  // dk1 - near-black
            "#F5F5F5",  // lt2
            "#333333",  // dk2
            "#00FF87",  // accent1 - neon green
            "#FF00C8",  // accent2 - neon pink
            "#00D4FF",  // accent3 - neon cyan
            "#FFE600",  // accent4 - neon yellow
            "#7B61FF",  // accent5 - electric purple
            "#FF3D00",  // accent6 - neon red-orange
            "#00D4FF",  // hlink
            "#7B61FF",  // folHlink
        };
        themes.push_back({"Neon", makeTheme("Neon", colors, "Consolas", "Consolas")});
    }

    // ========================================================================
    // Sage — muted greens and naturals, calm and organic
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FAFAF5",  // lt1 - warm off-white
            "#2C3227",  // dk1 - dark olive
            "#EAEEE5",  // lt2 - light sage
            "#4A5240",  // dk2 - olive
            "#87986A",  // accent1 - sage green
            "#B5C99A",  // accent2 - light sage
            "#718355",  // accent3 - olive green
            "#E9EDC9",  // accent4 - pale green
            "#CCD5AE",  // accent5 - khaki green
            "#D4A373",  // accent6 - warm tan
            "#718355",  // hlink
            "#87986A",  // folHlink
        };
        themes.push_back({"Sage", makeTheme("Sage", colors, "Garamond", "Garamond")});
    }

    // ========================================================================
    // Rose Gold — warm pinks and metallic golds
    // ========================================================================
    {
        const std::string colors[12] = {
            "#FFFAF8",  // lt1 - blush white
            "#2E1A1E",  // dk1 - dark wine
            "#FFF0EE",  // lt2 - light blush
            "#5C3A42",  // dk2 - mauve
            "#B76E79",  // accent1 - rose gold
            "#E8A0BF",  // accent2 - pink
            "#C9A96E",  // accent3 - gold
            "#F2D7D5",  // accent4 - light pink
            "#D4A373",  // accent5 - warm gold
            "#8B5E3C",  // accent6 - bronze
            "#B76E79",  // hlink
            "#8B5E3C",  // folHlink
        };
        themes.push_back({"Rose Gold", makeTheme("Rose Gold", colors, "Didot", "Didot")});
    }

    return themes;
}

}  // namespace cells

#endif  // CELLS_BUILTIN_THEMES_H_
