# Zoom and Rendering Fixes

Address remaining zoom and rendering issues discovered after implementing Phase 5 (proper zoom) in the previous plan.

## Phase 1: Zoom-Aware Selection and Cell Editor

The cell/range selection highlight and cell editor positioning use unzoomed dimensions, causing misalignment at non-100% zoom levels.

- [x] 1a: Add zoom factor parameter to selection renderer functions (`drawRangeSelection`, `drawSingleCellSelection`, `drawFillHandle`, etc.) and apply zoom to position calculations using `getZoomedColWidth`/`getZoomedRowHeight`. Added `zoomFactor` to `SelectionRendererState` interface and updated all selection rendering functions.
- [x] 1b: Update cell editor positioning in `CellEditor.positionEditor()` to use zoomed dimensions for cell X/Y and width/height. Added `getZoomFactor` accessor to CellEditor.
- [x] 1c: Add E2E test that verifies selection box aligns with cell at various zoom levels (50%, 100%, 200%). Created `zoom-selection.test.mjs` with 5 tests. Also fixed bug where `getRowAtY` calls in `mouse-events.ts` were missing the zoom factor parameter.

## Phase 2: Zoom-Aware Row/Column Headers

Headers are rendered but positioning may not account for zoom properly, causing misalignment with the grid.

- [x] 2a: Review `drawColumnHeaders` and `drawRowHeaders` in grid-header-renderer.ts to ensure they use zoomed dimensions consistently. Verified that both functions already use `getZoomedHeaderWidth()`, `getZoomedHeaderHeight()`, `getZoomedColWidth()`, `getZoomedRowHeight()`, and `getZoomedFontSize()` via the global zoom factor.
- [x] 2b: Ensure header text font size scales with zoom factor. Confirmed that font size is already scaled using `getZoomedFontSize(12)` in both header rendering functions.
- [x] 2c: Add E2E test verifying header alignment at different zoom levels. Created `zoom-headers.test.mjs` with 7 tests covering header dimensions and cell alignment at 50%, 75%, 100%, 150%, and 200% zoom levels.

## Phase 3: Cell Background Gap Fix

Thin gaps visible between cells when no border exists because background fill leaves 1px margins for grid lines, but grid lines are drawn after backgrounds.

- [x] 3a: Change cell background rendering order: draw backgrounds edge-to-edge (no 1px inset), then draw grid lines on top. Removed the 1px inset from `_drawCellBackgrounds()` in grid-renderer.ts - backgrounds now fill the full cell area and grid lines are drawn on top.
- [x] 3b: Ensure merged cell backgrounds span the full merged region without gaps. Verified that merged cell handling in `_drawCellBackgrounds()` was already correct - it sums up all spanned columns/rows for merge anchors.
- [x] 3c: Add visual regression test or E2E test that checks for gaps in adjacent filled cells. Created `cell-background.test.mjs` with 4 tests that verify pixels near cell boundaries have the correct background color.

## Phase 4: Blue Section Width Fix

Blue header sections in LBO model don't fill expected width - likely related to merged cell handling or column width accumulation.

- [x] 4a: Debug the specific cells with blue backgrounds in the LBO model to identify why they're not spanning the expected width. Root cause: XLSX reader was skipping empty cells that had styling but no content. The blue section headers (e.g., row 5 "General Assumptions:") have 12 cells (B5-M5) all with blue backgrounds, but only B5 had text content - the other 11 were empty styled cells that were being skipped.
- [x] 4b: Fix XLSX reader to not skip empty cells that have styles. Changed the skip condition in xlsx_reader.cc:1815 to check for style index before skipping: `if (value.empty() && !cellNode.child("f") && styleIndexForSkip == 0)` - now empty cells with styles are preserved.
- [x] 4c: Add test case for styled empty cells from XLSX. Created `styled-empty-cells.test.mjs` with 3 tests verifying that blue section headers now have all 12 cells (B5-M5) with proper backgrounds.

## Phase 5: Accounting Number Format

The Accounting format (aligned currency symbol, negatives in parentheses) is partially implemented but not available in the UI and may not be applied from XLSX files correctly.

- [x] 5a: Add Accounting format option to the number format dropdown in the toolbar UI (style-controls.ts and HTML). Added "Accounting" option to format dropdown in index.html and updated `getFormatIdForCategory` in format-controls.ts to return FMT_A002 for ACCOUNTING category.
- [x] 5b: Review XLSX format code parsing for accounting formats (format codes with `_*` alignment characters and `_(` negative patterns). Added detection for accounting format patterns (`_(*`, `_($*`, `_(\"$\"*`) in custom format parsing.
- [x] 5c: Update `mapNumFmtIdToFormatId` to correctly identify and map accounting format IDs (37-44 in Excel's built-in formats). Fixed mapping: 37-40 are number formats with parentheses (not accounting), 41-44 are actual accounting formats mapped to FMT_A000/FMT_A002.
- [x] 5d: Add unit test for accounting format display (e.g., `$ 2.29` with aligned symbol, `($108.30)` for negatives). Added 4 tests in number_formatter_test.cc for AccountingFromRegistry, AccountingNegativeFromRegistry, AccountingZeroFromRegistry, and AccountingNoDecimalsFromRegistry.
- [x] 5e: Add E2E test that applies Accounting format from UI and verifies display. Added 4 E2E tests in format.test.mjs for positive, negative, zero, and large numbers with thousands separator.

## Phase 6: EOMONTH Function

The EOMONTH function is not implemented but is used in financial models.

- [x] 6a: Implement `fn_EOMONTH(start_date, months)` in fn_datetime.cc that returns the serial date of the last day of the month N months from start_date. Implemented with proper handling of month overflow/underflow and leap years (including Excel's 1900 leap year bug).
- [x] 6b: Register EOMONTH in registerDateTimeFunctions. Registered with signature "(start_date, months)" in the Date category.
- [x] 6c: Add unit tests for EOMONTH with various inputs (positive months, negative months, edge cases like month-end inputs). Added 10 tests in formula_functions_test.cc covering: basic (0 months), positive months, negative months, leap year February, non-leap year February, cross-year positive, cross-year negative, starting from end of month, invalid date, and wrong arg count.

## Phase 7: Dynamic Web Font Loading

When a font like Calibri is specified but not available, dynamically load it from Google Fonts if possible.

- [x] 7a: Create a font loader module that checks if a font is available via `document.fonts.check()` and loads from Google Fonts API if not. Created `font-loader.ts` with `ensureFont()`, `onFontLoaded()`, `preloadCommonFonts()`, and canvas-based fallback detection.
- [x] 7b: Build a mapping of common fonts (Calibri, Arial, Times New Roman, etc.) to their Google Fonts equivalents or fallbacks. Mapped Calibri→Carlito, Cambria→Caladea, plus many Google Fonts (Roboto, Open Sans, etc.).
- [x] 7c: Integrate font loader into the renderer - before rendering a cell, ensure its font is loaded or use fallback. Updated `grid-renderer.ts` to use `ensureFont()` and registered font-loaded callback in `init.ts` to trigger re-renders.
- [x] 7d: Cache loaded fonts to avoid repeated network requests. Font loader uses a Map-based cache and tracks loading state to prevent duplicate requests.
- [x] 7e: Add test verifying font loading fallback behavior. Created `font-loading.test.mjs` with 5 tests covering module availability, system fonts, fallback behavior, XLSX loading, and re-render callbacks.

## Phase 8: Comprehensive Zoom Architecture Review

Despite previous zoom fixes, rendering issues persist at non-100% zoom levels. Selection boxes are misaligned with cells, and column resize indicators appear at wrong positions. This phase performs a systematic audit and architectural fix of all zoom-dependent rendering.

### 8.1: Audit and Document Current Zoom Usage

Review all code that uses positions, sizes, or coordinates to identify zoom gaps.

- [ ] 8.1a: Audit `grid-renderer.ts` - document all position/size calculations and whether they properly use zoomed vs unzoomed values
- [ ] 8.1b: Audit `grid-selection-renderer.ts` - verify selection box, fill handle, fill preview all use consistent zoom calculations
- [ ] 8.1c: Audit `grid-header-renderer.ts` - verify resize indicators, drag ghosts, header highlights use zoom
- [ ] 8.1d: Audit `mouse-events.ts` - verify all mouse coordinate translations (screen→grid, grid→screen) use zoom correctly
- [ ] 8.1e: Audit `cell-editor.ts` - verify editor positioning accounts for zoom in all cases
- [ ] 8.1f: Document findings and create fix list for each file

### 8.2: Fix Column/Row Resize Indicators

The green dashed line shown during column/row resize is positioned incorrectly at non-100% zoom.

- [ ] 8.2a: Write E2E test that verifies resize indicator X position matches the column boundary at 50%, 100%, 200% zoom
- [ ] 8.2b: Fix resize indicator positioning in header renderer to use zoomed column positions
- [ ] 8.2c: Verify row resize indicator has same fix applied

### 8.3: Fix Selection Box Alignment

Selection boxes (single cell, range, column, row) don't align with cell boundaries at non-100% zoom.

- [ ] 8.3a: Write E2E test that precisely measures selection box position vs cell position at various zoom levels
- [ ] 8.3b: Identify root cause - likely mismatch between how cell positions and selection positions are calculated
- [ ] 8.3c: Fix selection position calculations to use same zoomed position logic as cell rendering
- [ ] 8.3d: Verify fill handle position is also fixed

### 8.4: Centralize Zoom Coordinate System

If issues stem from inconsistent zoom application, introduce architectural improvements.

- [ ] 8.4a: Create helper functions for coordinate conversion: `screenToGrid(x, y)` and `gridToScreen(x, y)` that handle zoom
- [ ] 8.4b: Create `getCellBounds(col, row)` that returns zoomed {x, y, width, height} for any cell
- [ ] 8.4c: Refactor selection renderer to use `getCellBounds()` instead of manual calculations
- [ ] 8.4d: Refactor resize indicator to use centralized position helpers
- [ ] 8.4e: Ensure all mouse event handlers use `screenToGrid()` for hit testing

### 8.5: Dynamic Zoom Change Handling

Ensure all rendered entities update correctly when zoom level changes.

- [ ] 8.5a: Review zoom change propagation - verify all components are notified when zoom changes
- [ ] 8.5b: Write E2E test that changes zoom while selection is active and verifies selection updates
- [ ] 8.5c: Write E2E test that changes zoom during column resize drag and verifies indicator updates
- [ ] 8.5d: Fix any components that don't respond to zoom changes dynamically

### 8.6: Additional Zoom-Dependent Elements

Review and fix any remaining zoom-dependent UI elements.

- [ ] 8.6a: Verify scrollbar thumb size/position accounts for zoom
- [ ] 8.6b: Verify formula reference highlights (colored boxes during formula editing) use zoom
- [ ] 8.6c: Verify remote presence cursors/selections use zoom
- [ ] 8.6d: Verify drag-and-drop ghost elements use zoom
- [ ] 8.6e: Verify frozen pane dividers (if implemented) use zoom

### 8.7: Regression Test Suite

Create comprehensive test coverage to prevent future zoom regressions.

- [ ] 8.7a: Create `zoom-comprehensive.test.mjs` with tests for all zoom-dependent rendering
- [ ] 8.7b: Add tests at boundary zoom levels (25%, 50%, 75%, 100%, 150%, 200%, 400%)
- [ ] 8.7c: Add tests for zoom + scroll combinations
- [ ] 8.7d: Add tests for zoom + frozen rows/columns (if applicable)
