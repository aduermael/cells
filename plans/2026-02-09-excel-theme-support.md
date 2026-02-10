# Excel Theme Support

Add first-class theme support for Excel-compatible roundtrips. Themes provide a workbook-level color palette (12 colors) and font scheme (2 fonts) that cells can reference instead of using direct values. Direct properties always override theme references.

**Status: Planning.**

## Design

### Theme Entity

A workbook has at most one theme. The theme stores:
- **Color scheme**: 12 named color slots (lt1, dk1, lt2, dk2, accent1-6, hlink, folHlink) as `#RRGGBB`
- **Font scheme**: major font (headings) and minor font (body) names

### Theme Color References

Colors in styles can be either:
- **Direct**: `#RRGGBB` (current behavior, unchanged)
- **Theme reference**: theme index (0-11) + tint (-1.0 to 1.0)

A theme color reference resolves to a hex color at render time by looking up the theme palette and applying the tint. If the theme changes, all theme-referencing styles update automatically.

### Override Model

Theme references live alongside direct properties — they don't replace them. The rule is:

- If a property has a **direct value** set, use it (ignoring any theme reference)
- If a property has a **theme reference** but no direct value, resolve from theme
- If neither, use default

This means the existing style merge cascade (column → row → range → cell) works unchanged. Theme references are just an alternative way to specify a color or font — they participate in the same merge logic.

### Storage

Theme color references are stored in StyleBuffer as a new encoding for color slots. Instead of 3 bytes RGB, a theme color uses a tag byte + index byte + 2-byte tint:

- **Direct RGB**: tag `0x00` + R + G + B (4 bytes) — current format stays as-is with 3 bytes
- **Theme color**: tag `0x01` + index (uint8) + tint (int16, fixed-point × 1000) = 4 bytes

The tag byte distinguishes the two. Existing 3-byte RGB stays at 3 bytes (no tag needed for backward compat); theme colors use 4 bytes with tag `0x01` as first byte. Detection: if first byte of color data is `0x01` and the remaining bytes form a valid theme reference, it's theme; otherwise it's direct RGB (since no valid RGB starts with R=0x01 in practice... actually that's wrong, R could be 1).

Better approach: extend the flag system. Add new flag bits for theme-based colors:
- `STYLE_FLAG_BG_THEME` — bgColor is a theme reference instead of RGB
- `STYLE_FLAG_TEXT_THEME` — textColor is a theme reference instead of RGB

When the theme flag is set, the color data is: index (1 byte) + tint (2 bytes fixed-point) = 3 bytes (same size as RGB, no format change needed).

Border colors: same approach — a per-side theme flag in the border encoding.

### Font Scheme References

Theme font references are simpler. The font scheme has two slots:
- Major (index 0) — typically for headings
- Minor (index 1) — typically for body text

A new flag `STYLE_FLAG_FONT_THEME` indicates fontFamily is a theme font reference. The stored byte is the font scheme index (0 or 1). When resolving, look up the theme's font scheme.

---

## Phase 1: Theme Data Model

- [ ] 1a: Add `ThemeColorScheme` and `ThemeFontScheme` structs to a new `core/cells/theme.h`
  - ThemeColorScheme: 12 `std::string` color slots (#RRGGBB)
  - ThemeFontScheme: majorFont and minorFont (std::string names)
  - Theme struct: colorScheme + fontScheme + name
  - Workbook gets `std::unique_ptr<Theme> _theme` with getter/setter

- [ ] 1b: Add theme color reference support to `StyleBuffer`
  - Add new flag bits: `STYLE_FLAG_BG_THEME`, `STYLE_FLAG_TEXT_THEME`
  - When theme flag is set, color data is: index (1 byte) + tint (2 bytes int16 fixed-point)
  - Add setters: `setBgThemeColor(uint8_t index, double tint)`, `setTextThemeColor(...)`
  - Add getters: `getBgThemeIndex()`, `getBgThemeTint()`, `isThemeBgColor()`, etc.
  - Direct color setters (`setBgColor`) clear the theme flag; theme setters clear the direct flag
  - Update `merge()` to handle theme color flags (theme colors merge like direct colors)
  - Update `toJSON()` / `fromJSON()` to serialize theme refs (e.g. `"bgColor": {"theme": 1, "tint": 0.4}`)
  - Update `toCellStyle()` / `fromCellStyle()` to handle theme refs

- [ ] 1c: Add theme color fields to `CellStyle` in `style_types.h`
  - Add: `int8_t bgThemeIndex{-1}`, `double bgThemeTint{0.0}` (-1 = not theme-based)
  - Add: `int8_t textThemeIndex{-1}`, `double textThemeTint{0.0}`
  - Add border theme support: extend `BorderEdge` with `int8_t themeIndex{-1}`, `double themeTint{0.0}`
  - Update equality, hash, defined flags

## Phase 2: XLSX Reader — Import Themes

- [ ] 2a: Import theme from `xl/theme/theme1.xml` into `Workbook::_theme`
  - Parse color scheme (12 slots) and font scheme (major/minor)
  - Store as `Theme` entity on the workbook
  - Keep the existing `XLSXThemeColors` parsing, but now also populate the workbook theme

- [ ] 2b: Store theme color references in styles instead of resolving
  - Modify `resolveColor()` to return a struct `XLSXColorResult { string hex; int themeIndex; double tint; }` instead of just a string
  - When a color is `<color theme="1" tint="0.3"/>`, store both the resolved hex AND the theme ref
  - Pass theme refs through to `CellStyle` → `StyleBuffer` during style extraction
  - Font scheme: detect when font name matches theme major/minor font and store as theme font ref

## Phase 3: XLSX Writer — Export Themes

- [ ] 3a: Write workbook theme to `xl/theme/theme1.xml`
  - If workbook has a theme, serialize its color scheme and font scheme
  - Generate proper DrawingML theme XML instead of the current minimal placeholder

- [ ] 3b: Write theme color references in styles
  - When a style has a theme color ref, output `<color theme="N" tint="T"/>` instead of `<color rgb="..."/>`
  - When a font uses theme font scheme, write the font scheme reference
  - Applies to: font colors, fill colors, border colors

## Phase 4: Comparator Fix & Frontend

- [ ] 4a: Fix C# comparator to resolve theme colors to hex for comparison
  - Same change as the reverted commit, but now both sides produce matching output
  - Resolve theme:N and indexed:N to actual hex values using the workbook's theme

- [ ] 4b: Expose theme data to frontend via WASM bindings
  - Add binding: `getTheme()` → JSON with color scheme and font scheme
  - Update `getEffectiveCellStyle()` to resolve theme colors before returning to frontend
  - Frontend receives resolved hex colors (no theme awareness needed in TS initially)

- [ ] 4c: Run roundtrip tests and verify math-basic passes
  - `./run-test.sh math-basic` should show matching theme colors
  - Fix any remaining style differences found during testing

## Technical Notes

### StyleBuffer Binary Encoding for Theme Colors

The current flag space has bits 0-14 used, bit 15 reserved. We need 2-3 new flag bits. Options:
- Use bit 15 as an "extended flags" indicator, adding a third flag byte
- Or repurpose: since bgColor and textColor flags already exist, add theme-specific flags in byte 2

Recommendation: add a third flag byte when needed (bit 15 = "has byte 2"), with theme bits in byte 2.

### Theme Index Mapping (OOXML → Internal)

OOXML spreadsheet theme indices (note the swap for indices 0-3):

| OOXML Index | Color Scheme Slot | Meaning |
|-------------|-------------------|---------|
| 0 | lt1 | Background 1 (usually white) |
| 1 | dk1 | Text 1 (usually black) |
| 2 | lt2 | Background 2 |
| 3 | dk2 | Text 2 |
| 4 | accent1 | Accent color 1 |
| 5 | accent2 | Accent color 2 |
| 6 | accent3 | Accent color 3 |
| 7 | accent4 | Accent color 4 |
| 8 | accent5 | Accent color 5 |
| 9 | accent6 | Accent color 6 |
| 10 | hlink | Hyperlink |
| 11 | folHlink | Followed hyperlink |

### Tint Fixed-Point Encoding

Tint ranges from -1.0 to +1.0. Stored as int16 × 1000 (e.g. 0.399 → 399, -0.25 → -250). This gives 0.001 precision which is more than enough for Excel's ~0.05 increments.

### Key Files

| File | Change |
|------|--------|
| `core/cells/theme.h` | New — Theme struct |
| `core/cells/style_types.h` | Add theme fields to CellStyle, BorderEdge |
| `core/cells/style_buffer.h/.cc` | Theme color flag bits, encode/decode |
| `core/cells/xlsx_reader.cc` | Import theme, preserve theme refs in styles |
| `core/cells/xlsx_writer.cc` | Export theme, write theme color refs |
| `core/cells/model.h` | Workbook theme member |
| `apps/wasm/bindings.cc` | Expose theme to frontend |
| `tests/excel-roundtrips/evaluator/Program.cs` | Resolve theme colors in comparator |
