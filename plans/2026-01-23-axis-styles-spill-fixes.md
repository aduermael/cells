# Plan: Axis Styles, Default Alignment UI, and Spill Blocking Fixes

This plan addresses three related issues:

1. **UI doesn't reflect default alignment for numbers**: Numbers render right-aligned (via "GENERAL" alignment), but the toolbar shows left-alignment as active when no explicit style is set
2. **Cannot set axis (column/row) styles via UI**: The architecture supports axis styles, but there's no UI or binding to set them
3. **Spill functions blocked by style-only cells**: When setting a style on a spilled cell, the cell is created with an empty string value which blocks the spill

## Issue Analysis

### Issue 1: Default Alignment Not Reflected in UI

**Current behavior:**
- When a cell has no explicit alignment, `getEffectiveCellStyle()` returns `hAlign: undefined` (GENERAL)
- `style-controls.ts:620` defaults to "left": `const hAlign = style.hAlign || "left"`
- Numbers render right-aligned in the grid (grid-renderer handles GENERAL correctly)
- But the toolbar incorrectly shows left-alignment button as active

**Expected behavior (Excel approach):**
- When no alignment is explicitly set at any level (cell, range, axis), no alignment button should be active
- The renderer handles GENERAL alignment based on content type (right for numbers, left for text)
- Alignment buttons only show active when user has explicitly set an alignment

**Root cause:** The UI defaults to "left" when alignment is undefined, instead of showing no selection.

### Issue 2: No UI for Axis Styles

**Current state:**
- Axis struct has `HAS_STYLE` flag (model.h:331)
- CRDT operation `AXIS_SET_STYLE` exists (crdt_axis.cc)
- Workbook's `_entityStyles` map supports axis IDs
- Style resolution in viewport correctly checks `col->hasStyle()` and `row->hasStyle()`

**Missing:**
- No `setAxisStyle()` / `setColumnStyle()` / `setRowStyle()` API in bindings
- No UI to apply styles to entire columns/rows

**Excel precedence:** Based on research, Excel doesn't have explicit row vs column style precedence rules since it uses conditional formatting order. For this implementation, we'll use **column > row** (column styles override row styles) since columns are the primary organizational unit in spreadsheets.

### Issue 3: Style-Only Cells Block Spills

**Current behavior (bindings_format.cc:1384):**
```cpp
// When cell doesn't exist and we want to set style:
std::string cellPayload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"...
```
This creates a cell with `type=STRING, value=""`.

**Problem:** `checkSpillBlocked()` in formula_recalc.cc:554-574 checks if cell has a value:
- Empty strings don't block (line 558: `if (!cell->value.raw.empty())`)
- But the cell's existence with `type=STRING` is detected, potentially causing rendering issues

**Actual root cause from screenshots:** When user changes alignment on B2 (a spilled cell), a new cell is created for style storage. The spill system sees this new cell as blocking. The spilled value then doesn't display because the cell at B2 now exists with its own (empty) value instead of showing the spilled value.

**Fix needed:** Empty cells (no hardcoded value, no formula) should not block spills. A cell can have style/format metadata and still be considered empty.

---

## Phase 1: Fix UI Alignment Button State for GENERAL Alignment

When no alignment is explicitly set, no alignment button should be active (Excel behavior).

- [x] 1a: Update `style-controls.ts::setDisplayedStyle()` to handle undefined/GENERAL alignment
  - Removed the fallback to "left", now passes undefined through to updateHAlignButtons()

- [x] 1b: Update `updateHAlignButtons()` to show no active button when alignment is undefined
  - Added `noActive` check for when alignment is undefined or mixed

- [x] 1c: Apply same fix to vertical alignment in `updateVAlignButtons()`
  - Applied same pattern: no button active when vAlign is undefined

- [x] 1d: Add test verifying:
  - Created `alignment-ui.test.mjs` E2E test with comprehensive alignment button tests

## Phase 2: Add API and UI for Axis (Column/Row) Styles

Expose axis styles through the WASM bindings and add UI to apply them.

- [ ] 2a: Add `setColumnStyle(colPosition, styleJson)` to CellsEngine in bindings_format.cc
  - Find column by position
  - Create `AXIS_SET_STYLE` operation
  - Apply and return success/error

- [ ] 2b: Add `setRowStyle(rowPosition, styleJson)` similarly

- [ ] 2c: Add `getColumnStyle(colPosition)` and `getRowStyle(rowPosition)` to read axis styles

- [ ] 2d: Add TypeScript bindings in cells.d.ts, worker-types.ts, worker-handlers.ts

- [ ] 2e: Add `setColumnStyle()` and `setRowStyle()` methods to CellsClient and WasmDataSource

- [ ] 2f: Update style-controls.ts to detect when an entire column/row is selected
  - When column header clicked → apply style to column axis
  - When row header clicked → apply style to row axis

- [ ] 2g: Add tests for column/row style application and inheritance

## Phase 3: Fix Spill Blocking by Empty Cells

Empty cells (no value, no formula) should not block spills. They can have style/format metadata.

- [ ] 3a: Update `checkSpillBlocked()` in formula_recalc.cc to correctly identify empty cells
  - A cell is empty if: `!cell->isFormula() && cell->value.raw.empty()`
  - Empty cells should NOT block spills (regardless of style/format)
  - Review current logic - it may already handle empty strings but miss edge cases

- [ ] 3b: Ensure `setCellStyleAt()` creates truly empty cells when no value exists
  - Current code creates cell with `type=STRING, value=""`
  - This should be recognized as empty by spill blocking logic
  - May need to use a different type or ensure the check in 3a covers this case

- [ ] 3c: Ensure spilled cells can be styled without breaking the spill
  - When user styles B2 (a spilled cell from B1's UNIQUE formula):
  - The empty cell is created to store style
  - The spilled value should still display (empty cell doesn't block)
  - The style should apply to the display of the spilled value

- [ ] 3d: Add tests for:
  - Setting style on a spilled cell doesn't break the spill
  - Setting alignment on a spilled number cell shows correct alignment
  - Spill still works after removing the style

## Phase 4: Verify Style Priority Order

Ensure the style hierarchy is correctly implemented and documented.

- [ ] 4a: Verify style resolution order in `computeEffectiveStyleAt()`:
  - Current order: cell > range > column > row (cell highest priority)
  - This matches the design doc and Excel patterns

- [ ] 4b: Add test cases for style priority:
  - Row style + column style at intersection → column wins (column > row)
  - Cell style + column style → cell wins
  - Range style + column style → range wins

- [ ] 4c: Document style priority in code comments

---

## Architecture Notes

### Style Priority Order (highest to lowest)
1. **Cell style** - Individual cell formatting (always wins)
2. **Range styles** - Applied via RANGE_STYLE ranges (merged in application order)
3. **Column style** - Default formatting for entire column
4. **Row style** - Default formatting for entire row

When column and row styles both exist at an intersection, **column style takes precedence** (column > row). This is a design decision for consistency.

### Empty Cells and Spills

Empty cells (no value, no formula) can coexist with spills:
- An empty cell can have style/format metadata
- Spill blocking logic should ignore empty cells
- When rendering a spilled position that also has an empty cell with style, combine the spilled value with the cell's style
