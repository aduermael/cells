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

- [x] 2a: Create E2E test that verifies selection bounds exactly match cell bounds at zoom levels 50%, 75%, 100%, 125%, 150%, 200%. Tests in `zoom-selection-alignment.test.mjs` - now 7 tests including 72% non-standard zoom.
- [x] 2b: Review `getCellBounds()` and `getRangeBounds()` in grid-utils.ts for potential floating point rounding issues. Found issue: was zooming each column width individually causing rounding accumulation errors at non-standard zoom levels (e.g., 72%).
- [x] 2c: Ensure selection rendering in grid-selection-renderer.ts uses consistent rounding (Math.round vs Math.floor) with cell rendering. Fixed all coordinate functions in grid-utils.ts to use "sum unzoomed first, zoom once" approach matching the cell renderer.
- [x] 2d: Apply pixel-perfect alignment fix - use same coordinate calculation path for both cell backgrounds and selection borders. Fixed `gridToScreen`, `getRangeBounds`, `getColAtX`, `getRowAtY`, `getResizeHandleCol/Row`, `getDropTargetCol/Row` to avoid rounding accumulation.

## Phase 3: Verify Sheet Order Preservation

Ensure sheets are loaded and displayed in the same order as they appear in Excel.

- [x] 3a: Create test that opens `many-tabs.xlsx` and verifies sheet tab order matches expected Excel order. Created `sheet-order.test.mjs` with 5 tests verifying all 42 sheets are in correct order from workbook.xml.
- [x] 3b: Audit XLSX reader to confirm sheets are added to the workbook in XML document order (workbook.xml `<sheets>` order). Verified: `xlsx_reader.cc:1433` iterates sheets in XML order, adds to vector via `push_back`, and `getSheetByIndex()` returns them in insertion order.
- [x] 3c: Verify TypeScript sheet tabs component renders sheets in the order received from C++ core. Verified: `worker-handlers.ts:handleGetSheets()` iterates by index, and `sheet-tabs.ts:renderSheetTabs()` uses `forEach` maintaining order.

## Phase 4: Add Cell Merge UI Controls

Add toolbar buttons to merge and unmerge selected cells.

- [x] 4a: Add "Merge Cells" button to toolbar HTML with dropdown for merge options (Merge All, Merge Horizontally, Unmerge). Added merge dropdown after bg-color in style-controls-row2 with SVG icons.
- [x] 4b: Implement `mergeCells()` method in `wasm-data-source.ts` that calls the C++ merge function. Added mergeCells() and unmergeCells() to WasmDataSource.
- [x] 4c: Add C++ `AddMergeRange` CRDT operation to `bindings_core.cc` that creates a merge range from position coordinates. Added addMergeRange() and removeMergeRange() to CellsEngine with EMSCRIPTEN bindings, worker handlers, and client methods.
- [x] 4d: Implement `unmergeCells()` in `wasm-data-source.ts` that removes an existing merge range. Implemented alongside 4b/4c.
- [x] 4e: Wire up merge button click handlers to call `mergeCells()` with current selection range. Created MergeControls class in merge-controls.ts and integrated into init-components.ts.
- [x] 4f: Add E2E tests for merge/unmerge operations. Added 4 new tests to merged-cells.test.mjs covering merge all, unmerge, merge horizontally, and dropdown behavior.

## Phase 5: Design Range-Based Operations for CRDT

**Problem Statement:** The current cell merge implementation (Phase 4) revealed a fundamental design challenge: how to efficiently represent and handle range-based operations (merges, background colors, borders) in a CRDT context where cells can be moved concurrently by different users.

**Current Approach (Cell-by-Cell):**
- Select A1:C3, set background red → creates/updates 9 individual cells with background color
- 9 CRDT operations, each cell stores its own style
- Simple, but inefficient for large ranges and doesn't handle "column-wide" or "row-wide" styles

**Key Design Questions:**

1. **Efficiency:** Can we represent "set background red on A1:C3" as a single CRDT operation instead of 9?

2. **Range Identity:** If we use corner UUIDs (UUIDA1, UUIDC3) to define a range:
   - What if A1 or C3 don't exist yet? Create them?
   - What if one corner is deleted? Does the range collapse?

3. **Concurrent Movement Conflicts:**
   - User A: selects A1:C3, sets background red
   - User B: simultaneously drags B2 to E5
   - What happens to B2's background? And to the A1:C3 range?

4. **Column/Row-Wide Styles (Excel behavior):**
   - In Excel: color entire column A, then drag cell A5 to D5 → A5 loses the color, A column still has color
   - This suggests styles can be "attached to positions" not just "attached to cells"
   - But positions change when columns/rows are inserted/deleted

5. **Merge-Specific Issues:**
   - A1 has value, A2 is empty → merge A1:A2 → should A2 be created?
   - If merge is defined by corner UUIDs (A1, C3), what if B2 inside is moved out?
   - Should the merge "follow" the cells or "stay at the position"?

**Design Alternatives to Explore:**

A. **Cell-by-Cell (current):** Each cell stores its own complete style. Range operations expand to N individual cell operations. Merges store anchor cell UUID + span.
   - Pro: Simple, cells carry their styles when moved
   - Con: Inefficient for large ranges, no column/row-wide styles

B. **Range Records with Position Anchors:** Ranges defined by (startCol, startRow, endCol, endRow) positions, not UUIDs. Styles apply to positions.
   - Pro: Efficient, natural for column/row-wide styles
   - Con: When cells move, they lose range styles; conflicts with cell movement harder to resolve

C. **Range Records with UUID Corners:** Ranges defined by (startCellUUID, endCellUUID). Range "stretches" between corners.
   - Pro: Range can track when corners move
   - Con: What happens to interior cells that move? What if corners are deleted?

D. **Hybrid: Position Ranges + Cell Overrides:** Store range styles by position, but when a cell moves OUT of a range, it "inherits" the style as a cell-level override. Holes appear in ranges.
   - Pro: Efficient storage, handles movement gracefully
   - Con: Complexity in tracking overrides and holes

E. **Layers (like Photoshop):** Separate layer for range styles, separate layer for cell styles. Cell styles override range styles. Movement detaches cells from range layers.
   - Pro: Clean separation of concerns
   - Con: Complexity, unclear CRDT semantics for layers

**Phase 5 Steps:**

- [ ] 5a: Document current merge implementation: how MergeRange stores anchor UUID + span, how viewport looks up merge info, why it fails for newly created cells
- [ ] 5b: Analyze how Excel handles the "drag cell out of colored range" scenario - does cell keep color? Does range get a hole?
- [ ] 5c: Analyze how Excel handles "drag cell out of merged region" - is it allowed? What happens?
- [ ] 5d: Prototype Design A fix: ensure merge anchor cell exists and has correct column/row UUIDs when merge is created
- [ ] 5e: Evaluate if Design A (cell-by-cell) is sufficient for MVP, or if range-based design is required
- [ ] 5f: If range-based design needed, prototype Design D (position ranges + cell overrides) for background colors
- [ ] 5g: Design CRDT operations for range-based styles (e.g., `SetRangeStyle` operation)
- [ ] 5h: Document chosen design and update affected phases

## Phase 6: Add Border UI Controls

Add toolbar button with dropdown to set cell borders (all borders, outline, top, bottom, left, right, no border).

- [ ] 6a: Add "Borders" button to toolbar HTML with dropdown showing border options and preview icons
- [ ] 6b: Implement `setCellBorder(col, row, borderSpec)` in wasm-data-source.ts that calls C++ style operation
- [ ] 6c: Ensure C++ `setCellStyleAt` can update just the border property of CellStyle without affecting other properties
- [ ] 6d: Create border-controls.ts module with BorderControls class to handle border button/dropdown interactions
- [ ] 6e: Support applying borders to range selection (all cells in selection)
- [ ] 6f: Add E2E tests for border operations via UI

## Phase 7: Implement Text Wrapping

Add support for text wrapping within cells, reading from XLSX and exposing in UI.

- [ ] 7a: Add `wrapText: boolean` property to CellStyle in model.h
- [ ] 7b: Parse `wrapText` attribute from XLSX alignment element (`<alignment wrapText="1"/>`) in xlsx_reader.cc
- [ ] 7c: Write `wrapText` attribute back to XLSX in xlsx_writer.cc
- [ ] 7d: Update grid-renderer.ts `_drawCellValues()` to perform line breaking when wrapText is true
- [ ] 7e: Implement text measurement and line breaking algorithm (break on word boundaries, handle long words)
- [ ] 7f: Add "Wrap Text" toggle button to toolbar in style-controls.ts
- [ ] 7g: Add E2E test for text wrapping display and toggle

## Phase 8: Implement Auto-Fit Column Width

Add ability to auto-fit column width based on content.

- [ ] 8a: Implement `autoFitColumnWidth(colIndex)` in C++ that calculates optimal width based on cell content and font metrics
- [ ] 8b: Expose auto-fit via WASM binding in wasm-data-source.ts
- [ ] 8c: Add double-click handler on column resize handle to trigger auto-fit
- [ ] 8d: Add "Auto-fit Column Width" option to column header context menu (if context menu exists) or via keyboard shortcut
- [ ] 8e: Handle auto-fit for merged cells (use merged region width)
- [ ] 8f: Add E2E test for auto-fit functionality

## Phase 9: Investigate Values Showing as $0

The screenshot shows "$0" and "0.0" values where Excel shows actual numbers. Need to investigate.

- [ ] 9a: Debug specific cells in `many-tabs.xlsx` that show wrong values - check if it's a formula evaluation issue or display issue
- [ ] 9b: Check if formulas with external references or unsupported functions return 0
- [ ] 9c: Verify number formatting is correctly applied (currency format showing $0 instead of actual value)
- [ ] 9d: Fix identified issues with formula evaluation or value display
