# Fix Excel Round-Trip for math-basic

Fix all issues preventing `./run-test.sh math-basic` from passing. The test reads an XLSX file (with formulas, no cached results), evaluates formulas via the CLI, writes a new XLSX, and compares it cell-by-cell against the original (which has Excel-cached results).

## Observed Differences

Running the test produces these categories of differences:

### 1. Sheet properties missing from export
- **Column definitions** (`<cols>` with widths/customWidth) — reader stores in `Axis::size`, writer never writes it back
- **defaultRowHeight** (`<sheetFormatProperties>`) — not in the data model at all
- **pageMargins** — not in the data model at all

### 2. Font name lost on theme fonts
- Original: `"Aptos Narrow"` (the minor theme font) → Our output: `"Calibri"` (hardcoded default)
- Root cause: `StyleBuffer` stores only the theme index (0=major, 1=minor) but discards the actual font name. On write-back, the writer defaults empty `fontFamily` to `"Calibri"` instead of resolving from the theme.

### 3. Fill bgColor missing
- Original has `"bgColor":"indexed:64"` on solid fills → Our output omits it
- Root cause: Writer only emits `<fgColor>` inside `<patternFill>`, never writes the `<bgColor>` element. In Excel, `indexed:64` is the "system foreground" default background for solid fills.

### 4. Number formatting: lowercase `e` vs uppercase `E`
- Original (Excel): `9.9999999999999901E+307` → Our output: `9.9999999999999901e+307`
- Root cause: `CellValue(double)` uses `%.17g` which produces lowercase `e`. Excel uses uppercase `E` in cell values.

### 5. Formula text differences
- `10^(-307)` becomes `10^-307` (parentheses stripped by AST round-trip)
- `9.99999999999999E+307` becomes `1e+308` (number literal re-serialized with different precision)
- Root cause: Formulas go through parse→AST→serialize, losing original text formatting

### 6. Computed value precision differences
- `1.0000000000000001E-307` vs `9.9999999999999991e-308` — different last-digit rounding
- Root cause: Our formula engine produces slightly different floating-point results for extreme values

---

## Phase 1: Export Column Widths and Row Heights

The reader already stores column widths and row heights in `Axis::size`. The writer ignores them.

- [ ] 1a: Write column widths in `xlsx_writer.cc` — when `writeDimensions` is true and a column has a non-default size, emit `width` and `customWidth="1"` attributes in `<col>` elements. Also trigger `<cols>` element when any column has non-default width (not just hidden/styled). Convert pixels back to Excel character-width units (reverse of the `* 7.5` import conversion).
- [ ] 1b: Write row heights in `xlsx_writer.cc` — when `writeDimensions` is true and a row has a non-default size, emit `ht` and `customHeight="1"` attributes on `<row>` elements. Convert pixels back to points (reverse of the `* 96/72` import conversion). Also emit rows that have custom height even if they have no cells.
- [ ] 1c: Mark imported sizes with `setSizeSet(true)` in `xlsx_reader.cc` — so the writer can distinguish "explicitly set by user" from "default size". Use `sizeSet()` in the writer to decide whether to emit width/height.

## Phase 2: Preserve Sheet-Level Properties (defaultRowHeight, pageMargins)

These properties are not in the data model and need a pass-through mechanism.

- [ ] 2a: Add an opaque XML pass-through map to `Sheet` — a `std::map<std::string, std::string>` field (e.g., `Sheet::xlsxProperties`) that stores raw XML snippets keyed by element name. This avoids adding typed fields for every possible Excel property.
- [ ] 2b: Read `<sheetFormatProperties>` and `<pageMargins>` in xlsx_reader — store the raw XML attributes as a serialized string in the pass-through map.
- [ ] 2c: Write pass-through properties in xlsx_writer — emit `<sheetFormatProperties>` and `<pageMargins>` elements from the pass-through map at the correct positions in the worksheet XML.

## Phase 3: Fix Theme Font Name Preservation

- [ ] 3a: Store font family name alongside theme index in `StyleBuffer` — when `fontThemeIndex >= 0`, store both the theme index AND the original font name (currently only stores the index, losing the name).
- [ ] 3b: Resolve theme font name in xlsx_writer — when writing fonts, if `fontFamily` is empty but `fontThemeIndex >= 0`, resolve the name from the workbook's `Theme::fontScheme` (major/minor font). Fall back to "Calibri"/"Calibri Light" only if no theme is present.

## Phase 4: Fix Fill bgColor Export

- [ ] 4a: Write `<bgColor>` in solid fills — in `xlsx_writer.cc`, when writing a `<patternFill patternType="solid">`, also emit `<bgColor indexed="64"/>` after the `<fgColor>` element. This matches Excel's behavior for solid fills.

## Phase 5: Fix Number Value Formatting (uppercase E)

- [ ] 5a: Use uppercase `E` in scientific notation for XLSX export — in `xlsx_writer.cc`, when writing numeric cell values (`<v>` elements), convert lowercase `e` to uppercase `E` in the output string. This matches Excel's convention. Only apply this to the XLSX writer, not to the internal `CellValue::raw` representation.

## Phase 6: Fix Formula Text Preservation

- [ ] 6a: Preserve original formula text during XLSX import — store the raw A1-notation formula text from the XLSX file (before AST round-trip) and use it when writing back to XLSX. This avoids losing parentheses and number precision through the parse→AST→serialize cycle. The formula text is already available in the reader; the issue is that `convertFormulasToUuid` regenerates it from the AST.
- [ ] 6b: Fix formula number literal precision in serializer — ensure `FormulaSerializer` preserves full precision for number literals (e.g., `9.99999999999999E+307` should not become `1e+308`).

## Phase 7: Fix Extreme Value Computation Precision

- [ ] 7a: Investigate and fix the precision difference for `10^(-307)` — Excel computes `1.0000000000000001E-307`, our engine computes `9.9999999999999991E-308`. These are the same value at different precisions but the string representation differs. This may require using `%.15g` or matching Excel's specific formatting rules for cell values.

## Design Considerations

**Phase 2 (pass-through map)**: This is the main design decision. Two approaches:
1. **Typed fields**: Add `defaultRowHeight`, `pageMargins` etc. as typed fields to `Sheet`. Pro: type-safe, usable in the app. Con: must add every possible property, model bloat.
2. **Opaque pass-through**: Store raw XML strings for properties we don't actively use. Pro: handles any property automatically, minimal model changes. Con: not usable by the app logic.

I'm proposing option 2 because these properties (page margins, print settings, etc.) are display/print concerns that our app doesn't need to manipulate — we just need to preserve them for round-trip fidelity.

**Phase 6 (formula preservation)**: The core issue is that formulas go through parse→AST→serialize, which is lossy for formatting. The cleanest fix is to preserve the original A1-notation text and skip re-serialization when writing back to XLSX.
