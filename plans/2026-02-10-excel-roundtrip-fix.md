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

### 5. Formula text differences (intentional — our engine normalizes formulas)
- `10^(-307)` becomes `10^-307` (parentheses stripped by AST round-trip)
- `9.99999999999999E+307` becomes `1e+308` (number literal re-serialized with different precision)
- **Not a bug** — our engine intentionally normalizes formula text for optimization. The comparison tool should ignore formula text.

### 6. Computed value precision differences
- `10^(-307)`: Excel computes `1.0000000000000001E-307` (`0x1.1fa182c40c60e`), we compute `9.9999999999999991E-308` (`0x1.1fa182c40c60d`) — 1 ULP difference
- Root cause: `std::pow(10.0, -307.0)` on our platform differs by 1 ULP from Excel's result at extreme exponents. These are genuinely different IEEE754 bit patterns.

---

## Phase 1: Export Column Widths and Row Heights

The reader already stores column widths and row heights in `Axis::size`. The writer ignores them.

- [ ] 1a: Write column widths in `xlsx_writer.cc` — when `writeDimensions` is true and a column has a non-default size, emit `width` and `customWidth="1"` attributes in `<col>` elements. Also trigger `<cols>` element when any column has non-default width (not just hidden/styled). Convert pixels back to Excel character-width units (reverse of the `* 7.5` import conversion).
- [ ] 1b: Write row heights in `xlsx_writer.cc` — when `writeDimensions` is true and a row has a non-default size, emit `ht` and `customHeight="1"` attributes on `<row>` elements. Convert pixels back to points (reverse of the `* 96/72` import conversion). Also emit rows that have custom height even if they have no cells.
- [ ] 1c: Mark imported sizes with `setSizeSet(true)` in `xlsx_reader.cc` — so the writer can distinguish "explicitly set by user" from "default size". Use `sizeSet()` in the writer to decide whether to emit width/height.

## Phase 2: Preserve Sheet-Level Properties (defaultRowHeight, pageMargins)

These properties are not yet in the data model. Add typed fields so they can be used later.

- [ ] 2a: Add typed fields to `Sheet` in `model.h` — add `double defaultRowHeight{0}` (in points, 0 = not set) and a `PageMargins` struct with `left`, `right`, `top`, `bottom`, `header`, `footer` (all `double`, in inches, matching Excel's representation). Add a `bool hasPageMargins{false}` flag.
- [ ] 2b: Read `<sheetFormatProperties>` and `<pageMargins>` in xlsx_reader — parse the XML attributes into the new typed fields on `Sheet`.
- [ ] 2c: Write the properties in xlsx_writer — emit `<sheetFormatProperties defaultRowHeight="..."/>` and `<pageMargins .../>` elements from the typed fields at the correct positions in the worksheet XML.

## Phase 3: Fix Theme Font Name Preservation

- [ ] 3a: Store font family name alongside theme index in `StyleBuffer` — when `fontThemeIndex >= 0`, store both the theme index AND the original font name (currently only stores the index, losing the name).
- [ ] 3b: Resolve theme font name in xlsx_writer — when writing fonts, if `fontFamily` is empty but `fontThemeIndex >= 0`, resolve the name from the workbook's `Theme::fontScheme` (major/minor font). Fall back to "Calibri"/"Calibri Light" only if no theme is present.

## Phase 4: Fix Fill bgColor Export

- [ ] 4a: Write `<bgColor>` in solid fills — in `xlsx_writer.cc`, when writing a `<patternFill patternType="solid">`, also emit `<bgColor indexed="64"/>` after the `<fgColor>` element. This matches Excel's behavior for solid fills.

## Phase 5: Fix Number Value Formatting (uppercase E)

- [ ] 5a: Use uppercase `E` in scientific notation for XLSX export — in `xlsx_writer.cc`, when writing numeric cell values (`<v>` elements), convert lowercase `e` to uppercase `E` in the output string. This matches Excel's convention. Only apply this to the XLSX writer, not to the internal `CellValue::raw` representation.

## Phase 6: Add Ignore Flags to Comparison Tool

Formula text differences are intentional (our engine normalizes formulas). Add an ignore flag to the comparison infrastructure.

- [ ] 6a: Add `--ignore-formula-text` flag to the C# comparator (`Program.cs`) — when set, skip the `formula` field when comparing cells. The cell values are still compared, just not the formula text.
- [ ] 6b: Wire the flag through `compare.sh` and `run-test.sh` — pass `--ignore-formula-text` from `run-test.sh` to `compare.sh` to the Docker evaluator.

## Phase 7: Fix Extreme Value Computation Precision

Our `std::pow` produces results that differ from Excel by 1 ULP at extreme exponents. We should match Excel's results exactly.

- [ ] 7a: Investigate which `std::pow` calls produce different results — the known case is `10^(-307)` where we get `0x1.1fa182c40c60d` but Excel gets `0x1.1fa182c40c60e`. Profile other extreme exponent cases from the test data to determine the scope.
- [ ] 7b: Fix the power operator precision — likely need to use a more accurate power implementation for the `^` operator and `POWER()` function. Options include: compensated `pow` algorithms, using `exp(y * log(x))` with extended precision intermediates, or a lookup/correction table for `10^n` specifically. The fix should be in `formula_eval.cc` (line 396) and `fn_math.cc` (line 168) where `std::pow` is called.

## Design Notes

**Phase 2**: Using typed fields on `Sheet` for `defaultRowHeight` and `pageMargins`. These will be usable by the app later (e.g., for print preview, page layout). Values use Excel's native units (points for row height, inches for margins) to avoid lossy conversions.
