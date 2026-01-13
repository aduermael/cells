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

- [ ] 3a: Change cell background rendering order: draw backgrounds edge-to-edge (no 1px inset), then draw grid lines on top
- [ ] 3b: Ensure merged cell backgrounds span the full merged region without gaps
- [ ] 3c: Add visual regression test or E2E test that checks for gaps in adjacent filled cells

## Phase 4: Blue Section Width Fix

Blue header sections in LBO model don't fill expected width - likely related to merged cell handling or column width accumulation.

- [ ] 4a: Debug the specific cells with blue backgrounds in the LBO model to identify why they're not spanning the expected width
- [ ] 4b: Fix merged cell background width calculation to include all spanned columns
- [ ] 4c: Add test case for merged cell backgrounds spanning multiple columns

## Phase 5: Accounting Number Format

The Accounting format (aligned currency symbol, negatives in parentheses) is partially implemented but not available in the UI and may not be applied from XLSX files correctly.

- [ ] 5a: Add Accounting format option to the number format dropdown in the toolbar UI (style-controls.ts and HTML)
- [ ] 5b: Review XLSX format code parsing for accounting formats (format codes with `_*` alignment characters and `_(` negative patterns)
- [ ] 5c: Update `mapNumFmtIdToFormatId` to correctly identify and map accounting format IDs (37-44 in Excel's built-in formats)
- [ ] 5d: Add unit test for accounting format display (e.g., `$ 2.29` with aligned symbol, `($108.30)` for negatives)
- [ ] 5e: Add E2E test that applies Accounting format from UI and verifies display

## Phase 6: EOMONTH Function

The EOMONTH function is not implemented but is used in financial models.

- [ ] 6a: Implement `fn_EOMONTH(start_date, months)` in fn_datetime.cc that returns the serial date of the last day of the month N months from start_date
- [ ] 6b: Register EOMONTH in registerDateTimeFunctions
- [ ] 6c: Add unit tests for EOMONTH with various inputs (positive months, negative months, edge cases like month-end inputs)

## Phase 7: Dynamic Web Font Loading

When a font like Calibri is specified but not available, dynamically load it from Google Fonts if possible.

- [ ] 7a: Create a font loader module that checks if a font is available via `document.fonts.check()` and loads from Google Fonts API if not
- [ ] 7b: Build a mapping of common fonts (Calibri, Arial, Times New Roman, etc.) to their Google Fonts equivalents or fallbacks
- [ ] 7c: Integrate font loader into the renderer - before rendering a cell, ensure its font is loaded or use fallback
- [ ] 7d: Cache loaded fonts to avoid repeated network requests
- [ ] 7e: Add test verifying font loading fallback behavior
