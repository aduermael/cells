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

**Expected behavior:**
- When viewing a number cell with GENERAL alignment, the right-align button should be active
- The UI should know the cell's content type to determine effective alignment

**Root cause:** The UI layer doesn't know the cell's value type when determining button state.

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

**Fix needed:** Cells that exist only for style/format metadata (no user-entered value or formula) should not block spills. The spill system should check if a cell is "content-less" (only has style/format, no actual value).

---

## Phase 1: Fix UI Alignment Button State for GENERAL Alignment

Make the toolbar show the effective visual alignment based on content type.

- [ ] 1a: Extend `getEffectiveCellStyle()` API to also return cell value type (or visual alignment)
  - Option A: Add `valueType` field to response indicating "number", "string", "boolean", "date", "formula_number", etc.
  - Option B: Add `visualHAlign` field that resolves GENERAL to "right" for numbers, "left" for text
  - **Chosen approach:** Option B - compute `visualHAlign` in C++ and return it alongside raw `hAlign`

- [ ] 1b: Update `bindings_format.cc::getEffectiveCellStyle()` to include `visualHAlign` in JSON response
  - When `hAlign == GENERAL`: check cell value type, return "right" for numbers/dates, "left" for text
  - When `hAlign != GENERAL`: return the explicit alignment

- [ ] 1c: Update `bindings_format.cc::getEffectiveStyleForRange()` similarly for multi-cell selections

- [ ] 1d: Update TypeScript `CellStyle` type to include optional `visualHAlign` field

- [ ] 1e: Update `style-controls.ts::setDisplayedStyle()` to use `visualHAlign` when present
  - Change line 620 to use `style.visualHAlign || style.hAlign || "left"`

- [ ] 1f: Add test verifying number cells show right-align button active

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

## Phase 3: Fix Spill Blocking by Style-Only Cells

Allow spills to overwrite cells that only contain style/format metadata.

- [ ] 3a: Define "style-only cell" criteria in checkSpillBlocked():
  - Cell exists but has no formula
  - Cell has `type=STRING` with empty value (`raw.empty()`)
  - OR cell has `type=FORMULA_EMPTY`
  - These cells exist only for style/format storage and should not block spills

- [ ] 3b: Update `checkSpillBlocked()` in formula_recalc.cc to allow style-only cells
  - A cell is style-only if: `!cell->isFormula() && (cell->value.raw.empty() || cell->value.type == FORMULA_EMPTY)`
  - Style-only cells should NOT block spills

- [ ] 3c: Update `setCellStyleAt()` to not create cells when styling a spilled position
  - Check if position is within a spill range (using `sheet->getSpillMaster()`)
  - If spilled: store style differently (maybe on the spill master or in a separate index)
  - Alternative: Allow the cell creation but ensure it doesn't block spill in step 3b

- [ ] 3d: Ensure spilled cells can still be styled without breaking the spill
  - When user styles B2 (a spilled cell from B1's UNIQUE formula):
  - The style should be stored
  - The spilled value should still display
  - The style should apply to the display of the spilled value

- [ ] 3e: Add tests for:
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

### Spill-Safe Style Storage

For spilled cells, we have two options:

**Option A (Simpler):** Allow style-only cells to coexist with spills
- Spill blocking logic ignores cells that have no actual content (style-only)
- The cell still stores the style
- When rendering, spilled value + cell style are combined

**Option B (Cleaner):** Store spill cell styles on the master cell
- No separate cells created for spilled positions
- Master cell stores map of relative position → style
- More complex but cleaner data model

**Chosen approach:** Option A - simpler to implement and maintains the current architecture.
