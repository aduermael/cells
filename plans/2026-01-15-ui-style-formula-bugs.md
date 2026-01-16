# UI, Style, and Formula Bugs

Address several bugs related to border/style application, cross-sheet references, formula editing UX, and column/row-level styling.

## Issues

1. **Border + Bold bug**: When setting a border for a range, then clicking "bold" for the same range, the bold button becomes disabled and not all cells get bold styling. Root cause: when borders are applied, the range system splits overlapping ranges (Phase J of range-system-design). The bold operation then doesn't recognize the original selection as a single range anymore.

2. **Cross-sheet reference parsing**: Formulas like `=Sheet2!B27` don't work. The parser supports cross-sheet references syntactically, but the issue is likely in A1-to-UUID resolution when the sheet name contains certain characters or the parsing flow.

3. **Formula editing sheet switch**: When editing a formula, clicking another sheet tab loses focus and cancels the edit. Should allow selecting cells from other sheets while maintaining formula edit mode.

4. **Column/row-wide styling**: Can't set a style for an entire column or row via the UI. The backend supports this via `Axis.defaultStyleId` (Phase E4 of range-system-design), but no UI exposes it.

## Phase 1: Investigate and Fix Border + Bold Bug

The bug is architectural: when `setRangeStyle()` is called with a border, it checks for conflicting properties with existing ranges. After border application, the range may be split. The subsequent bold operation should create a new range with the bold property for the full selection, which will:
- Split any existing ranges that have bold (same property = split to avoid overlap)
- Layer with ranges that have different properties (border + bold can coexist)
- Merge with exact-match ranges to save space (Phase K smart merging)

**Root cause hypothesis**: The toolbar's `applyStyleToSelection()` may not be applying to the full user selection, or the button state calculation is wrong.

- [x] 1a: Create E2E test reproducing the bug: apply OUTLINE border to B2:D4, then apply bold. Bug confirmed - only interior cells got bold, edge cells with cell-level border styles did not. Tests added to `borders.test.mjs`.
- [x] 1b: Debug `StyleControls.applyStyleToSelection()` - uses `getSelectionRange()` correctly and calls `setStyleForRange()` for range-level styling. Issue was in C++ effective style computation.
- [x] 1c: Found root cause in TWO places:
  - `computeEffectiveStyleAt()` in `bindings_format.cc:2111` - used for toolbar style display
  - `getEffectiveStyle()` in `bindings_viewport.cc:84` - used for cell rendering
  Both had early return when cell had cell-level style, ignoring range styles. Fixed both to merge styles.
- [x] 1d: Bold button state now reflects effective style correctly. After fix, bold from range style merges with border from cell style.
- [x] 1e: Border and bold now coexist correctly - edge cells show both `bold: true` and `border` in both toolbar display AND viewport rendering data. Added viewport-specific test.
- [x] 1f: Fixed toggle bold OFF: `setRangeStyle` was checking if style is empty BEFORE looking for exact-match ranges. Moved exact-match handling FIRST so `{bold: false}` can properly merge with existing `{bold: true}` range. Also added range deletion when merged style is empty.

## Phase 2: Fix Cross-Sheet Reference Parsing

The parser architecture supports cross-sheet references (verified in formula_parser.cc:316-435). The issue may be:
- Sheet name with spaces not quoted properly
- A1 resolution not finding the target sheet
- Lexer not recognizing sheet prefix pattern

- [ ] 2a: Create E2E test: enter `=Sheet2!B27` formula, verify it parses and evaluates correctly
- [ ] 2b: Add unit test for cross-sheet reference parsing in formula_parser_test.cc
- [ ] 2c: Debug the formula entry flow - trace from input to AST to resolution
- [ ] 2d: Fix the identified issue (likely in lexer tokenization or parser sheet prefix handling)
- [ ] 2e: Test sheet names with spaces (should use `'Sheet Name'!A1` syntax)

## Phase 3: Formula Editing Across Sheets

Excel-like behavior: when editing a formula, the user can navigate to other sheets and click cells to insert cross-sheet references. The formula bar stays active and shows the building formula with proper sheet prefixes.

Current behavior: `uiStateMachine.reset()` is called on sheet switch, which transitions to IDLE and cancels the edit.

**Target UX flow**:
1. User clicks cell A1 in Sheet1, types `=SUM(`
2. User clicks Sheet2 tab → view switches but formula bar stays in edit mode
3. User clicks cell B5 in Sheet2 → formula becomes `=SUM(Sheet2!B5`
4. User clicks Sheet1 tab → view returns to Sheet1
5. User clicks cell C3 → formula becomes `=SUM(Sheet2!B5,C3` (no prefix needed, same sheet as formula)
6. User types `)` and presses Enter → formula commits to A1 in Sheet1

- [ ] 3a: Create E2E test: start editing formula in Sheet1, click Sheet2 tab, click cell B5, verify formula shows `=Sheet2!B5`
- [ ] 3b: Track "formula origin sheet" in EditingSession when formula editing starts
- [ ] 3c: Modify sheet tab click handler: when in formula edit mode, switch view but preserve edit state
- [ ] 3d: Update cell click handler: when editing formula from different sheet, insert `SheetName!` prefix
- [ ] 3e: When clicking cells on the formula's origin sheet, insert reference without prefix
- [ ] 3f: Add subtle visual indicator in formula bar showing origin sheet (e.g., "Editing in: Sheet1")
- [ ] 3g: Handle Enter to commit formula and return view to origin sheet
- [ ] 3h: Handle Escape to cancel and return view to origin sheet
- [ ] 3i: Handle clicking outside grid (not on sheet tabs) to commit formula

## Phase 4: Column/Row-Wide Style UI

The backend already supports column/row default styles via `Axis.defaultStyleId` with `AXIS_SET_STYLE` CRDT operation. The Luau API exposes `setColumnStyle()`/`setRowStyle()`. Need to add UI.

**Design**: Auto-detect when user has selected an entire column or row. When clicking a column header, the selection should span all rows (conceptually infinite). Style operations on such selections should apply to the axis default style, not create cell-level or range-level styles.

**Detection logic**:
- "Entire column" = selection starts at row 0 and extends to max row (or a special flag)
- "Entire row" = selection starts at col 0 and extends to max col (or a special flag)
- Could use sentinel values like `startRow = 0, endRow = MAX_ROWS` or a dedicated selection type

- [ ] 4a: Create E2E test: click column A header, apply bold, verify new cells in column A inherit bold
- [ ] 4b: Add selection type or flags to distinguish "entire column/row" from regular range selection
- [ ] 4c: Update column header click to create "entire column" selection
- [ ] 4d: Update row header click to create "entire row" selection
- [ ] 4e: In `StyleControls.applyStyleToSelection()`, detect entire column/row selection
- [ ] 4f: When entire column selected, call `setColumnStyle()` instead of `setStyleForRange()`
- [ ] 4g: When entire row selected, call `setRowStyle()` instead of `setStyleForRange()`
- [ ] 4h: Update effective style display to show column/row default styles correctly
- [ ] 4i: Add E2E test: set column style, then override single cell, verify cell shows override while others show column style
- [ ] 4j: Visual feedback: highlight entire column/row when selected (not just visible cells)

## Architecture Notes

### Range System Recap (from 2026-01-14-range-system-design.md)
- Ranges use UUID corners (startColId, startRowId, endColId, endRowId)
- Flags bitmask allows single range to serve multiple purposes (MERGE | STYLE)
- R-tree spatial index for O(log n) queries
- Style inheritance: default → column → row → range → cell (CSS-like cascade)

### Range Overlap Rules
- **Same property** (e.g., both have bgColor): Rectangle splitting - old range is split to avoid overlap
- **Different properties** (e.g., border + bold): Layering OK - ranges can overlap, styles merge at render
- **Exact same bounds**: Merge into single range to save space (Phase K smart merging)

### Style Resolution Order
1. Cell's own style (highest priority)
2. Range styles (CSS-like merge of overlapping ranges with different properties)
3. Column default style
4. Row default style
5. Default/null style

### Existing Infrastructure
- `getEffectiveCellStyle(col, row)` - computes merged style for single cell
- `getEffectiveStyleForRange(col1, row1, col2, row2)` - returns style + mixed flags
- `Axis.defaultStyleId` - column/row default styling
- `AXIS_SET_STYLE` CRDT operation for column/row styles
- `setColumnStyle(colIndex, style)` / `setRowStyle(rowIndex, style)` - Luau API (needs WASM binding)
- Formula parser supports `SheetName!A1` syntax (needs verification)

## Testing Strategy

- E2E tests for each bug scenario
- Unit tests for formula parsing edge cases
- Manual testing for UX flow (sheet switching during formula edit)
