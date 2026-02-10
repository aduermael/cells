// =============================================================================
// Built-in Cell Style Presets
// =============================================================================
//
// Named style presets matching Excel's "Cell Styles" gallery (Home → Cell Styles).
// Presets are theme-aware: accent-based styles use theme color references
// (bgThemeIndex + tint) so they update automatically when the workbook theme changes.
//
// Categories:
//   - Good, Bad and Neutral: Normal, Bad, Good, Neutral
//   - Data and Model: Calculation, Check Cell, Explanatory Text, Input,
//                     Linked Cell, Note, Output, Warning Text
//   - Titles and Headings: Heading 1-4, Title, Total
//   - Themed Cell Styles: 20%/40%/60% accent fills for Accent 1-6
//   - Number Format: Comma, Comma [0], Currency, Currency [0], Percent
//
// =============================================================================

#ifndef CELLS_CELL_STYLE_PRESETS_H_
#define CELLS_CELL_STYLE_PRESETS_H_

#include <string>
#include <vector>

#include "core/cells/style_types.h"
#include "core/cells/theme.h"

namespace cells {

// A named cell style preset with category grouping
struct CellStylePreset {
    std::string name;      // Display name (e.g. "Good", "Accent 1")
    std::string category;  // Category for gallery grouping
    CellStyle style;       // The preset's style definition

    // Optional: format code for number format presets (empty = no format change)
    std::string formatCode;
};

// Category names (constants to avoid typos)
namespace PresetCategory {
inline constexpr const char* kGoodBadNeutral = "Good, Bad and Neutral";
inline constexpr const char* kDataAndModel = "Data and Model";
inline constexpr const char* kTitlesAndHeadings = "Titles and Headings";
inline constexpr const char* kThemedCellStyles = "Themed Cell Styles";
inline constexpr const char* kNumberFormat = "Number Format";
}  // namespace PresetCategory

// ---------------------------------------------------------------------------
// Helper: build a CellStyle with theme background color
// ---------------------------------------------------------------------------
inline CellStyle makeThemeBgStyle(int8_t themeIndex, double tint = 0.0) {
    CellStyle s;
    s.bgThemeIndex = themeIndex;
    s.bgThemeTint = tint;
    s.setDefined(DEFINED_BGCOLOR);
    return s;
}

// ---------------------------------------------------------------------------
// Helper: build a CellStyle with theme text color
// ---------------------------------------------------------------------------
inline CellStyle makeThemeTextStyle(int8_t themeIndex, double tint = 0.0) {
    CellStyle s;
    s.textThemeIndex = themeIndex;
    s.textThemeTint = tint;
    s.setDefined(DEFINED_TEXTCOLOR);
    return s;
}

// ---------------------------------------------------------------------------
// Get all built-in cell style presets
// ---------------------------------------------------------------------------
inline std::vector<CellStylePreset> getBuiltinCellStylePresets() {
    std::vector<CellStylePreset> presets;
    presets.reserve(50);

    // ========================================================================
    // Good, Bad and Neutral
    // ========================================================================

    // Normal — default style (empty, no formatting)
    {
        CellStylePreset p;
        p.name = "Normal";
        p.category = PresetCategory::kGoodBadNeutral;
        presets.push_back(std::move(p));
    }

    // Bad — red text on light red bg
    {
        CellStylePreset p;
        p.name = "Bad";
        p.category = PresetCategory::kGoodBadNeutral;
        p.style.bgColor = "#FFC7CE";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#9C0006";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        presets.push_back(std::move(p));
    }

    // Good — dark green text on light green bg
    {
        CellStylePreset p;
        p.name = "Good";
        p.category = PresetCategory::kGoodBadNeutral;
        p.style.bgColor = "#C6EFCE";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#006100";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        presets.push_back(std::move(p));
    }

    // Neutral — dark yellow text on light yellow bg
    {
        CellStylePreset p;
        p.name = "Neutral";
        p.category = PresetCategory::kGoodBadNeutral;
        p.style.bgColor = "#FFEB9C";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#9C5700";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // Data and Model
    // ========================================================================

    // Calculation — dark orange text on light orange bg, thin borders
    {
        CellStylePreset p;
        p.name = "Calculation";
        p.category = PresetCategory::kDataAndModel;
        p.style.bgColor = "#F2F2F2";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#FA7D00";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.border.top = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        p.style.border.left = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_LEFT);
        p.style.border.right = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_RIGHT);
        presets.push_back(std::move(p));
    }

    // Check Cell — white text on dark bg (themed dk1), thin borders
    {
        CellStylePreset p;
        p.name = "Check Cell";
        p.category = PresetCategory::kDataAndModel;
        p.style.bgColor = "#A5A5A5";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#FFFFFF";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.border.top = BorderEdge(BorderStyle::DOUBLE, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom = BorderEdge(BorderStyle::DOUBLE, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        p.style.border.left = BorderEdge(BorderStyle::DOUBLE, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_LEFT);
        p.style.border.right = BorderEdge(BorderStyle::DOUBLE, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_RIGHT);
        presets.push_back(std::move(p));
    }

    // Explanatory Text — gray italic text
    {
        CellStylePreset p;
        p.name = "Explanatory Text";
        p.category = PresetCategory::kDataAndModel;
        p.style.textColor = "#7F7F7F";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.italic = true;
        p.style.setDefined(DEFINED_ITALIC);
        presets.push_back(std::move(p));
    }

    // Input — dark blue text on light beige bg, thin borders
    {
        CellStylePreset p;
        p.name = "Input";
        p.category = PresetCategory::kDataAndModel;
        p.style.bgColor = "#FFCC99";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#3F3F76";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.border.top = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        p.style.border.left = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_LEFT);
        p.style.border.right = BorderEdge(BorderStyle::THIN, "#7F7F7F");
        p.style.setDefined(DEFINED_BORDER_RIGHT);
        presets.push_back(std::move(p));
    }

    // Linked Cell — orange text, thin bottom border
    {
        CellStylePreset p;
        p.name = "Linked Cell";
        p.category = PresetCategory::kDataAndModel;
        p.style.textColor = "#FA7D00";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.border.bottom = BorderEdge(BorderStyle::DOUBLE, "#FF8001");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        presets.push_back(std::move(p));
    }

    // Note — light yellow bg, thin borders
    {
        CellStylePreset p;
        p.name = "Note";
        p.category = PresetCategory::kDataAndModel;
        p.style.bgColor = "#FFFFCC";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.border.top = BorderEdge(BorderStyle::THIN, "#B2B2B2");
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom = BorderEdge(BorderStyle::THIN, "#B2B2B2");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        p.style.border.left = BorderEdge(BorderStyle::THIN, "#B2B2B2");
        p.style.setDefined(DEFINED_BORDER_LEFT);
        p.style.border.right = BorderEdge(BorderStyle::THIN, "#B2B2B2");
        p.style.setDefined(DEFINED_BORDER_RIGHT);
        presets.push_back(std::move(p));
    }

    // Output — dark gray text on light gray bg, thin borders
    {
        CellStylePreset p;
        p.name = "Output";
        p.category = PresetCategory::kDataAndModel;
        p.style.bgColor = "#F2F2F2";
        p.style.setDefined(DEFINED_BGCOLOR);
        p.style.textColor = "#3F3F3F";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.border.top = BorderEdge(BorderStyle::THIN, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom = BorderEdge(BorderStyle::THIN, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        p.style.border.left = BorderEdge(BorderStyle::THIN, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_LEFT);
        p.style.border.right = BorderEdge(BorderStyle::THIN, "#3F3F3F");
        p.style.setDefined(DEFINED_BORDER_RIGHT);
        presets.push_back(std::move(p));
    }

    // Warning Text — red text
    {
        CellStylePreset p;
        p.name = "Warning Text";
        p.category = PresetCategory::kDataAndModel;
        p.style.textColor = "#FF0000";
        p.style.setDefined(DEFINED_TEXTCOLOR);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // Titles and Headings
    // ========================================================================

    // Title — theme dk1 color, 18pt, bold
    {
        CellStylePreset p;
        p.name = "Title";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.textThemeIndex = 1;  // dk1 (usually black)
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.fontSize = 18;
        p.style.setDefined(DEFINED_FONTSIZE);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        presets.push_back(std::move(p));
    }

    // Heading 1 — theme dk1, 15pt, bold, bottom border in accent color
    {
        CellStylePreset p;
        p.name = "Heading 1";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.textThemeIndex = 1;  // dk1
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.fontSize = 15;
        p.style.setDefined(DEFINED_FONTSIZE);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        // Bottom border in accent1 color
        p.style.border.bottom.style = BorderStyle::THICK;
        p.style.border.bottom.themeIndex = 4;  // accent1
        p.style.border.bottom.themeTint = 0.0;
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        presets.push_back(std::move(p));
    }

    // Heading 2 — theme dk1, 13pt, bold, bottom border in accent1 at 50%
    {
        CellStylePreset p;
        p.name = "Heading 2";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.textThemeIndex = 1;  // dk1
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.fontSize = 13;
        p.style.setDefined(DEFINED_FONTSIZE);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.border.bottom.style = BorderStyle::THICK;
        p.style.border.bottom.themeIndex = 4;  // accent1
        p.style.border.bottom.themeTint = 0.499984;
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        presets.push_back(std::move(p));
    }

    // Heading 3 — theme dk1, 11pt, bold, bottom border thin in accent1 at 40%
    {
        CellStylePreset p;
        p.name = "Heading 3";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.textThemeIndex = 1;  // dk1
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.border.bottom.style = BorderStyle::MEDIUM;
        p.style.border.bottom.themeIndex = 4;  // accent1
        p.style.border.bottom.themeTint = 0.399975;
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        presets.push_back(std::move(p));
    }

    // Heading 4 — theme dk1 text, italic
    {
        CellStylePreset p;
        p.name = "Heading 4";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.textThemeIndex = 1;  // dk1
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.italic = true;
        p.style.setDefined(DEFINED_ITALIC);
        presets.push_back(std::move(p));
    }

    // Total — bold, top+bottom double borders in accent1
    {
        CellStylePreset p;
        p.name = "Total";
        p.category = PresetCategory::kTitlesAndHeadings;
        p.style.bold = true;
        p.style.setDefined(DEFINED_BOLD);
        p.style.textThemeIndex = 1;  // dk1
        p.style.textThemeTint = 0.0;
        p.style.setDefined(DEFINED_TEXTCOLOR);
        p.style.border.top.style = BorderStyle::THIN;
        p.style.border.top.themeIndex = 4;  // accent1
        p.style.border.top.themeTint = 0.0;
        p.style.setDefined(DEFINED_BORDER_TOP);
        p.style.border.bottom.style = BorderStyle::DOUBLE;
        p.style.border.bottom.themeIndex = 4;  // accent1
        p.style.border.bottom.themeTint = 0.0;
        p.style.setDefined(DEFINED_BORDER_BOTTOM);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // Themed Cell Styles — Accent 1-6 with tint variations
    // ========================================================================
    // Theme indices: accent1=4, accent2=5, accent3=6, accent4=7, accent5=8, accent6=9

    struct AccentDef {
        int8_t themeIndex;
        const char* label;  // e.g. "Accent 1"
    };
    const AccentDef accents[] = {
        {4, "Accent 1"}, {5, "Accent 2"}, {6, "Accent 3"},
        {7, "Accent 4"}, {8, "Accent 5"}, {9, "Accent 6"},
    };

    struct TintVariation {
        const char* prefix;  // e.g. "20% - ", "40% - ", "60% - "
        double bgTint;       // Tint for background
        bool whiteText;      // Whether to use white text (for dark fills)
    };

    // For each accent: full color, then 60%, 40%, 20% lighter variations
    for (const auto& accent : accents) {
        // Full accent color (100%) — white text on accent bg
        {
            CellStylePreset p;
            p.name = accent.label;
            p.category = PresetCategory::kThemedCellStyles;
            p.style.bgThemeIndex = accent.themeIndex;
            p.style.bgThemeTint = 0.0;
            p.style.setDefined(DEFINED_BGCOLOR);
            // White text for contrast
            p.style.textThemeIndex = 0;  // lt1 (white)
            p.style.textThemeTint = 0.0;
            p.style.setDefined(DEFINED_TEXTCOLOR);
            presets.push_back(std::move(p));
        }

        // 60% accent (medium tint) — white text
        {
            CellStylePreset p;
            p.name = std::string("60% - ") + accent.label;
            p.category = PresetCategory::kThemedCellStyles;
            p.style.bgThemeIndex = accent.themeIndex;
            p.style.bgThemeTint = 0.399975;
            p.style.setDefined(DEFINED_BGCOLOR);
            p.style.textThemeIndex = 0;  // lt1 (white)
            p.style.textThemeTint = 0.0;
            p.style.setDefined(DEFINED_TEXTCOLOR);
            presets.push_back(std::move(p));
        }

        // 40% accent (lighter tint) — dark text
        {
            CellStylePreset p;
            p.name = std::string("40% - ") + accent.label;
            p.category = PresetCategory::kThemedCellStyles;
            p.style.bgThemeIndex = accent.themeIndex;
            p.style.bgThemeTint = 0.599993;
            p.style.setDefined(DEFINED_BGCOLOR);
            p.style.textThemeIndex = 1;  // dk1
            p.style.textThemeTint = 0.0;
            p.style.setDefined(DEFINED_TEXTCOLOR);
            presets.push_back(std::move(p));
        }

        // 20% accent (lightest tint) — dark text
        {
            CellStylePreset p;
            p.name = std::string("20% - ") + accent.label;
            p.category = PresetCategory::kThemedCellStyles;
            p.style.bgThemeIndex = accent.themeIndex;
            p.style.bgThemeTint = 0.799981;
            p.style.setDefined(DEFINED_BGCOLOR);
            p.style.textThemeIndex = 1;  // dk1
            p.style.textThemeTint = 0.0;
            p.style.setDefined(DEFINED_TEXTCOLOR);
            presets.push_back(std::move(p));
        }
    }

    // ========================================================================
    // Number Format
    // ========================================================================

    // Comma — #,##0.00
    {
        CellStylePreset p;
        p.name = "Comma";
        p.category = PresetCategory::kNumberFormat;
        p.formatCode = "#,##0.00";
        presets.push_back(std::move(p));
    }

    // Comma [0] — #,##0
    {
        CellStylePreset p;
        p.name = "Comma [0]";
        p.category = PresetCategory::kNumberFormat;
        p.formatCode = "#,##0";
        presets.push_back(std::move(p));
    }

    // Currency — $#,##0.00
    {
        CellStylePreset p;
        p.name = "Currency";
        p.category = PresetCategory::kNumberFormat;
        p.formatCode = "$#,##0.00";
        presets.push_back(std::move(p));
    }

    // Currency [0] — $#,##0
    {
        CellStylePreset p;
        p.name = "Currency [0]";
        p.category = PresetCategory::kNumberFormat;
        p.formatCode = "$#,##0";
        presets.push_back(std::move(p));
    }

    // Percent — 0%
    {
        CellStylePreset p;
        p.name = "Percent";
        p.category = PresetCategory::kNumberFormat;
        p.formatCode = "0%";
        presets.push_back(std::move(p));
    }

    return presets;
}

// ---------------------------------------------------------------------------
// Resolve theme color references in a preset to hex preview colors.
// This creates a copy of the style with theme refs resolved for UI display.
// The original theme references are preserved in the returned CellStyle
// (bgThemeIndex/bgThemeTint are still set) — the bgColor/textColor fields
// are filled in with the resolved hex values for preview rendering.
// ---------------------------------------------------------------------------
inline CellStyle resolvePresetPreviewColors(const CellStyle& style, const Theme* theme) {
    CellStyle resolved = style;

    if (resolved.hasBgThemeColor()) {
        resolved.bgColor = resolveThemeColor(theme, resolved.bgThemeIndex, resolved.bgThemeTint);
        if (!resolved.isDefined(DEFINED_BGCOLOR)) {
            resolved.setDefined(DEFINED_BGCOLOR);
        }
    }
    if (resolved.hasTextThemeColor()) {
        resolved.textColor =
            resolveThemeColor(theme, resolved.textThemeIndex, resolved.textThemeTint);
        if (!resolved.isDefined(DEFINED_TEXTCOLOR)) {
            resolved.setDefined(DEFINED_TEXTCOLOR);
        }
    }
    if (resolved.hasFontTheme()) {
        std::string fontName = resolveThemeFont(theme, resolved.fontThemeIndex);
        if (!fontName.empty()) {
            resolved.fontFamily = fontName;
            if (!resolved.isDefined(DEFINED_FONTFAMILY)) {
                resolved.setDefined(DEFINED_FONTFAMILY);
            }
        }
    }

    // Resolve border theme colors
    auto resolveBorderEdge = [&](BorderEdge& edge) {
        if (edge.hasThemeColor()) {
            edge.color = resolveThemeColor(theme, edge.themeIndex, edge.themeTint);
        } else if (edge.hasIndexedColor()) {
            edge.color = resolveIndexedColor(edge.indexedColor);
        }
    };
    resolveBorderEdge(resolved.border.top);
    resolveBorderEdge(resolved.border.right);
    resolveBorderEdge(resolved.border.bottom);
    resolveBorderEdge(resolved.border.left);

    return resolved;
}

}  // namespace cells

#endif  // CELLS_CELL_STYLE_PRESETS_H_
