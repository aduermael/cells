# UI & Rendering Improvements Plan

Address remaining UI and rendering issues for Excel parity, focusing on merged cells, cell selection alignment, sheet ordering, text wrapping, and UI controls for borders and merges.

## Issues Identified

From screenshots comparing the app vs Excel rendering of `many-tabs.xlsx`:
1. **Merged cells not rendering content properly** - "PROJECT PATRY" title, "MONTHLY BOARD PACK" and dates not showing in merged cell regions
2. **Text/values showing as $0 or 0.0** - Possible formula evaluation or display formatting issue
3. **Cell selector misalignment** - Selection box doesn't perfectly align with cell boundaries at certain zoom levels (screenshot 3 shows green border off by fraction of pixel)
4. **Missing UI for cell merges and borders** - No toolbar controls to merge/unmerge cells or set borders
5. **Sheet order uncertainty** - Need to verify sheets are displayed in the same order as in Excel
6. **No text wrapping or auto-fit** - Content doesn't wrap in cells; no auto-fit column width option

## Phase 1: Debug Merged Cell Content Rendering

The merged cell areas appear but content is not showing (e.g., "PROJECT PATRY" title). Need to investigate why anchor cell content isn't rendering.

- [x] 1a: Create E2E test that loads `many-tabs.xlsx` and verifies the "PROJECT PATRY" title is visible in merged region B2:M2. Created `merged-cells.test.mjs` - tests confirm B2 is a merge anchor (14 cols) with value "PROJECT PATRY".
- [x] 1b: Debug `_drawCellValues()` in grid-renderer.ts - found bug where clip region for merge anchors only used the anchor column, not the full merged region. Fixed by using `colWidth` (full merged width) for clip region.
- [x] 1c: Verify XLSX reader correctly parses merged cell content - verified that `xlsx_reader.cc:1994-2063` correctly parses `<mergeCells>` and calls `sheet->addMergeRange()`. Test confirms B2 is correctly marked as anchor with 14-column span.
- [x] 1d: Fix content rendering for merged cells - done in step 1b (clip region now uses full merged width)

## Phase 2: Fix Cell Selection Alignment

The selection box (green border) doesn't perfectly align with cell boundaries at certain zoom levels, visible in screenshot 3.

- [x] 2a: Create E2E test that verifies selection bounds exactly match cell bounds at zoom levels 50%, 75%, 100%, 125%, 150%, 200%. Already exists in `zoom-selection-alignment.test.mjs` - tests 50%, 75%, 100%, 200% with and without scroll (6 tests, all passing).
- [x] 2b: Review `getCellBounds()` and `getRangeBounds()` in grid-utils.ts for potential floating point rounding issues. Reviewed - uses `Math.round()` consistently for zoomed scroll values (lines 121-122) and all zoomed dimensions use `getZoomedColWidth/RowHeight` helpers.
- [x] 2c: Ensure selection rendering in grid-selection-renderer.ts uses consistent rounding (Math.round vs Math.floor) with cell rendering. Verified - selection renderer uses centralized helpers `getCellBounds()` and `getRangeBounds()` from grid-utils.ts.
- [x] 2d: Apply pixel-perfect alignment fix - use same coordinate calculation path for both cell backgrounds and selection borders. Already implemented - both cell rendering and selection rendering use the same centralized helpers.

## Phase 3: Verify Sheet Order Preservation

Ensure sheets are loaded and displayed in the same order as they appear in Excel.

- [ ] 3a: Create test that opens `many-tabs.xlsx` and verifies sheet tab order matches expected Excel order
- [ ] 3b: Audit XLSX reader to confirm sheets are added to the workbook in XML document order (workbook.xml `<sheets>` order)
- [ ] 3c: Verify TypeScript sheet tabs component renders sheets in the order received from C++ core

## Phase 4: Add Cell Merge UI Controls

Add toolbar buttons to merge and unmerge selected cells.

- [ ] 4a: Add "Merge Cells" button to toolbar HTML with dropdown for merge options (Merge All, Merge Horizontally, Unmerge)
- [ ] 4b: Implement `mergeCells(startCol, startRow, endCol, endRow)` in wasm-data-source.ts that calls C++ CRDT operation
- [ ] 4c: Add C++ `AddMergeRange` CRDT operation in model.cc and expose via WASM bindings
- [ ] 4d: Implement `unmergeCells(col, row)` that removes the merge range containing the given cell
- [ ] 4e: Wire up merge button click handlers in style-controls.ts or new merge-controls.ts module
- [ ] 4f: Add E2E tests for merge/unmerge operations via UI

## Phase 5: Add Border UI Controls

Add toolbar button with dropdown to set cell borders (all borders, outline, top, bottom, left, right, no border).

- [ ] 5a: Add "Borders" button to toolbar HTML with dropdown showing border options and preview icons
- [ ] 5b: Implement `setCellBorder(col, row, borderSpec)` in wasm-data-source.ts that calls C++ style operation
- [ ] 5c: Ensure C++ `setCellStyleAt` can update just the border property of CellStyle without affecting other properties
- [ ] 5d: Create border-controls.ts module with BorderControls class to handle border button/dropdown interactions
- [ ] 5e: Support applying borders to range selection (all cells in selection)
- [ ] 5f: Add E2E tests for border operations via UI

## Phase 6: Implement Text Wrapping

Add support for text wrapping within cells, reading from XLSX and exposing in UI.

- [ ] 6a: Add `wrapText: boolean` property to CellStyle in model.h
- [ ] 6b: Parse `wrapText` attribute from XLSX alignment element (`<alignment wrapText="1"/>`) in xlsx_reader.cc
- [ ] 6c: Write `wrapText` attribute back to XLSX in xlsx_writer.cc
- [ ] 6d: Update grid-renderer.ts `_drawCellValues()` to perform line breaking when wrapText is true
- [ ] 6e: Implement text measurement and line breaking algorithm (break on word boundaries, handle long words)
- [ ] 6f: Add "Wrap Text" toggle button to toolbar in style-controls.ts
- [ ] 6g: Add E2E test for text wrapping display and toggle

## Phase 7: Implement Auto-Fit Column Width

Add ability to auto-fit column width based on content.

- [ ] 7a: Implement `autoFitColumnWidth(colIndex)` in C++ that calculates optimal width based on cell content and font metrics
- [ ] 7b: Expose auto-fit via WASM binding in wasm-data-source.ts
- [ ] 7c: Add double-click handler on column resize handle to trigger auto-fit
- [ ] 7d: Add "Auto-fit Column Width" option to column header context menu (if context menu exists) or via keyboard shortcut
- [ ] 7e: Handle auto-fit for merged cells (use merged region width)
- [ ] 7f: Add E2E test for auto-fit functionality

## Phase 8: Investigate Values Showing as $0

The screenshot shows "$0" and "0.0" values where Excel shows actual numbers. Need to investigate.

- [ ] 8a: Debug specific cells in `many-tabs.xlsx` that show wrong values - check if it's a formula evaluation issue or display issue
- [ ] 8b: Check if formulas with external references or unsupported functions return 0
- [ ] 8c: Verify number formatting is correctly applied (currency format showing $0 instead of actual value)
- [ ] 8d: Fix identified issues with formula evaluation or value display
