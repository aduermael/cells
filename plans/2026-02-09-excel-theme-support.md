# Excel Theme Support

Add first-class theme support and indexed color preservation for Excel-compatible roundtrips. Themes provide a workbook-level color palette (12 colors) and font scheme (2 fonts) that cells can reference instead of using direct values. Indexed colors (legacy 0-63 palette) are also preserved. Direct properties always override theme/indexed references.

**Status: In Progress — Phase 2.**

## Design

### Color Reference Types

Every color slot in the model (bgColor, textColor, border colors) can hold one of three kinds:

1. **Direct RGB**: `#RRGGBB` (current behavior, unchanged)
2. **Theme reference**: theme index (0-11) + tint (-1.0 to 1.0)
3. **Indexed reference**: palette index (0-65)

All three resolve to a final hex color at render time. Theme references look up the workbook's theme palette and apply tint. Indexed references look up the fixed 64-color legacy palette (plus indices 64/65 for system fg/bg).

### Theme Entity

A workbook has at most one theme. The theme stores:
- **Color scheme**: 12 named color slots (lt1, dk1, lt2, dk2, accent1-6, hlink, folHlink) as `#RRGGBB`
- **Font scheme**: major font (headings) and minor font (body) names

### Override Model

Color references are just another way to express a color value — they participate in the normal style merge cascade (column → row → range → cell). A cell with a direct `#FF0000` overrides a column's `theme:4` the same way it would override a column's direct `#0000FF`.

The rule at each level is simply: whatever color reference type is set (direct, theme, or indexed), that's the color. There's no "theme layer" separate from the style cascade.

### Storage: StyleBuffer Encoding

Colors need to encode which kind of reference they are. The approach: use the existing color slot (3 bytes for RGB) but add new flag bits to indicate the reference type. When a theme/indexed flag is set, the 3 bytes are reinterpreted:

**Direct RGB** (current, unchanged):
- Flag: `STYLE_FLAG_BGCOLOR` (or `TEXTCOLOR`)
- Data: R (1 byte) + G (1 byte) + B (1 byte) = 3 bytes

**Theme color reference**:
- Flag: `STYLE_FLAG_BG_THEME` (or `TEXT_THEME`)
- Data: index (1 byte) + tint (2 bytes int16 fixed-point × 1000) = 3 bytes

**Indexed color reference**:
- Flag: `STYLE_FLAG_BG_INDEXED` (or `TEXT_INDEXED`)
- Data: palette index (1 byte) + 2 padding bytes = 3 bytes

All three use the same 3-byte slot size. Only one flag per color slot is set at a time. Setting a direct color clears theme/indexed flags and vice versa. This keeps the content-addressed identity working — same bytes = same style.

**Border colors**: same approach. The border encoding already has 4 bytes per side (1 style + 3 color). Add per-side bits in the border side mask to indicate theme/indexed. The side mask byte currently uses bits 0-3 (top/right/bottom/left presence). Extend to use bits 4-7 for color-type indicators per side, or add a second mask byte for color types.

### Font Scheme References

The font scheme has two slots: major (0, headings) and minor (1, body text). A new flag `STYLE_FLAG_FONT_THEME` indicates fontFamily is a theme font reference. The stored byte is the font scheme index (0 or 1). Resolution looks up the theme's font scheme to get the actual font name.

### Extended Flag Byte

The current flag space uses bits 0-14 (2 bytes), with bit 15 reserved. New flags needed:
- `STYLE_FLAG_BG_THEME`, `STYLE_FLAG_BG_INDEXED`
- `STYLE_FLAG_TEXT_THEME`, `STYLE_FLAG_TEXT_INDEXED`
- `STYLE_FLAG_FONT_THEME`
- Border color type bits (in border encoding, not top-level flags)

Use bit 15 as "extended flags present" indicator. When set, a third flag byte follows. The third byte holds the theme/indexed flags. This is backward compatible: old 2-byte buffers never have bit 15 set.

---

## Phase 1: Theme Data Model

- [x] 1a: Add `Theme` struct to a new `core/cells/theme.h`. Created ThemeColorScheme, ThemeFontScheme, Theme structs, applyTint/resolveThemeColor/resolveIndexedColor/resolveThemeFont helpers, kIndexedColors palette, and added `_theme` to Workbook with getter/setter.
  - ThemeColorScheme: 12 `std::string` color slots (#RRGGBB) with named accessors (lt1, dk1, accent1, etc.)
  - ThemeFontScheme: majorFont and minorFont (std::string names)
  - Theme struct combining both + a name string
  - Add `resolveThemeColor(index, tint)` helper that looks up palette + applies tint
  - Add `resolveIndexedColor(index)` static helper (the fixed 64-color palette + sys fg/bg)
  - Workbook gets `std::unique_ptr<Theme> _theme` with getter/setter

- [x] 1b: Add theme/indexed color fields to `CellStyle` and `BorderEdge` in `style_types.h`. Added bgThemeIndex/bgThemeTint/bgIndexedColor, textThemeIndex/textThemeTint/textIndexedColor, fontThemeIndex to CellStyle, and themeIndex/themeTint/indexedColor to BorderEdge. Updated equality and hash.
  - CellStyle: add `int8_t bgThemeIndex{-1}`, `double bgThemeTint{0.0}`, `int8_t bgIndexedColor{-1}`
  - CellStyle: add `int8_t textThemeIndex{-1}`, `double textThemeTint{0.0}`, `int8_t textIndexedColor{-1}`
  - CellStyle: add `int8_t fontThemeIndex{-1}` (-1 = direct, 0 = major, 1 = minor)
  - BorderEdge: add `int8_t themeIndex{-1}`, `double themeTint{0.0}`, `int8_t indexedColor{-1}`
  - Update equality operators and hash functions
  - Add corresponding DEFINED flags

- [x] 1c: Extend `StyleBuffer` with theme/indexed color support. Added bit 15 as extended flags indicator with 3rd flag byte, theme/indexed setters/getters for bg/text colors and borders, mutual exclusion between direct/theme/indexed, updated merge/toJSON/fromCellStyle/toCellStyle, border color type byte for per-side theme/indexed tracking.
  - Add bit 15 as "extended flags" indicator, third flag byte for new bits
  - New flags: `STYLE_FLAG_BG_THEME`, `STYLE_FLAG_BG_INDEXED`, `STYLE_FLAG_TEXT_THEME`, `STYLE_FLAG_TEXT_INDEXED`, `STYLE_FLAG_FONT_THEME`
  - Theme color data: index (1 byte) + tint (2 bytes int16 × 1000) = 3 bytes
  - Indexed color data: palette index (1 byte) + 2 padding = 3 bytes
  - Setting direct color clears theme/indexed flags and vice versa (mutual exclusion)
  - Update `merge()`: theme/indexed colors merge like direct colors
  - Update `toJSON()` / `fromJSON()`: e.g. `"bgColor": {"theme": 1, "tint": 0.4}` or `"bgColor": {"indexed": 5}`
  - Update `toCellStyle()` / `fromCellStyle()` roundtrip
  - Update border encoding: add per-side color type bits

- [x] 1d: Unit tests for theme color encoding in StyleBuffer. Added 27 new tests covering theme/indexed roundtrip, mutual exclusion, merge behavior, JSON output, CellStyle conversion, border theme/indexed, backward compat, and tint precision. All 338 unit + 338 E2E tests pass.
  - Test theme color set/get roundtrip
  - Test indexed color set/get roundtrip
  - Test mutual exclusion (setting direct clears theme, etc.)
  - Test merge behavior with mixed color types
  - Test JSON serialization/deserialization of theme refs
  - Test backward compat: old 2-byte-flag buffers still parse correctly

## Phase 2: XLSX Import

- [x] 2a: Import theme entity from `xl/theme/theme1.xml`. Changed `parseThemeXml()` to return `cells::Theme` with color scheme + font scheme (major/minor latin typeface) + theme name. Added `themeColorsFromTheme()` bridge to keep existing `XLSXThemeColors` pipeline working. Theme is stored on workbook via `setTheme()` after creation.
  - Parse color scheme (12 slots) and font scheme (major/minor) into `Theme`
  - Store on workbook via `setTheme()`
  - Keep existing `XLSXThemeColors` parsing working (it feeds the theme entity now)

- [x] 2b: Preserve theme color references in imported styles. Added `ColorRef` struct. Changed `resolveColor()` to return `ColorRef` with hex + themeIndex/tint + indexedColor. Extended `XLSXFont`, `XLSXFill`, `XLSXBorderEdge` with color ref fields. Updated `getCellStyle()` to copy refs through to `CellStyle`. The existing `fromCellStyle()` in StyleBuffer already handles theme/indexed → binary encoding.
  - Change `resolveColor()` to return a struct: `{ string hex; int8_t themeIndex; double tint; int8_t indexedColor; }`
  - When color is `<color theme="1" tint="0.3"/>`, store the theme ref in CellStyle/StyleBuffer
  - When color is `<color indexed="5"/>`, store the indexed ref
  - When color is `<color rgb="FF..."/>`, store as direct RGB (current behavior)
  - Pass refs through: XML → XLSXFont/XLSXFill/XLSXBorder → CellStyle → StyleBuffer → workbook

- [x] 2c: Preserve font scheme references. Parse `<scheme val="major"/>` / `<scheme val="minor"/>` element in XLSX font entries. Added `fontSchemeIndex` to `XLSXFont`, copied to `CellStyle.fontThemeIndex` in `getCellStyle()`. All 338 unit tests pass.
  - Detect when a font name matches the theme's major or minor font
  - XLSX may use `<name val="Calibri"/>` with `<scheme val="minor"/>` — check for `<scheme>` element
  - Store as fontThemeIndex in CellStyle/StyleBuffer

## Phase 3: XLSX Export

- [ ] 3a: Write workbook theme to `xl/theme/theme1.xml`
  - If workbook has a theme, serialize its color and font schemes as DrawingML XML
  - If no theme, keep the current minimal placeholder

- [ ] 3b: Write theme/indexed color references in styles
  - When a style has theme color: output `<color theme="N" tint="T"/>` (omit tint if 0.0)
  - When indexed: output `<color indexed="N"/>`
  - When direct RGB: output `<color rgb="FFRRGGBB"/>` (current behavior)
  - Applies to: font colors, fill fgColor/bgColor, border edge colors
  - When font has theme font ref: output `<scheme val="major"/>` or `<scheme val="minor"/>` in font element

## Phase 4: Comparator & Verification

- [ ] 4a: Fix C# comparator to resolve theme and indexed colors to hex
  - Load theme palette from workbook, resolve `theme:N` → `#RRGGBB`
  - Resolve `indexed:N` → `#RRGGBB` using the fixed palette
  - Apply tint when present
  - Both files now produce comparable hex values

- [ ] 4b: Expose theme to frontend via WASM bindings
  - `getTheme()` → JSON with color scheme and font scheme
  - `getEffectiveCellStyle()` resolves theme/indexed colors to hex before returning
  - Frontend receives resolved colors (no theme awareness in TS needed initially)

- [ ] 4c: Run roundtrip tests
  - `./run-test.sh math-basic` — verify theme color roundtrip
  - Test with indexed-color test file once provided
  - Fix any remaining style mismatches

## Technical Notes

### Theme Index Mapping (OOXML)

| Index | Slot | Meaning |
|-------|------|---------|
| 0 | lt1 | Background 1 (usually white) |
| 1 | dk1 | Text 1 (usually black) |
| 2 | lt2 | Background 2 |
| 3 | dk2 | Text 2 |
| 4-9 | accent1-6 | Accent colors |
| 10 | hlink | Hyperlink |
| 11 | folHlink | Followed hyperlink |

Note: the DrawingML XML order is dk1, lt1, dk2, lt2 but OOXML spreadsheet theme indices swap them to lt1=0, dk1=1, lt2=2, dk2=3.

### Indexed Color Palette

Fixed 64-color palette from Excel 97 (indices 0-63). Indices 64 and 65 are "system foreground" (black) and "system background" (white). These are historical and always resolve to the same values.

### Tint Fixed-Point Encoding

Tint ranges -1.0 to +1.0. Stored as int16 × 1000 (e.g. 0.399 → 399, -0.25 → -250). Precision: 0.001, more than sufficient.

### Key Files

| File | Change |
|------|--------|
| `core/cells/theme.h` | New — Theme struct, color resolution helpers |
| `core/cells/style_types.h` | Theme/indexed fields in CellStyle, BorderEdge |
| `core/cells/style_buffer.h/.cc` | Extended flags, theme/indexed encode/decode |
| `core/cells/xlsx_reader.cc` | Import theme, preserve color refs in styles |
| `core/cells/xlsx_writer.cc` | Export theme XML, write theme/indexed color refs |
| `core/cells/model.h` | Workbook::_theme member |
| `apps/wasm/bindings.cc` | getTheme(), resolve colors for frontend |
| `tests/excel-roundtrips/evaluator/Program.cs` | Resolve colors in comparator |
