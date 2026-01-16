# UI, Style, and Formula Bugs

Address several bugs related to border/style application, cross-sheet references, formula editing UX, and column/row-level styling.

## Issues

1. **Border + Bold bug**: When setting a border for a range, then clicking "bold" for the same range, the bold button becomes disabled and not all cells get bold styling. Root cause: when borders are applied, the range system splits overlapping ranges (Phase J of range-system-design). The bold operation then doesn't recognize the original selection as a single range anymore.

2. **Cross-sheet reference parsing**: Formulas like `=Sheet2!B27` don't work. The parser supports cross-sheet references syntactically, but the issue is likely in A1-to-UUID resolution when the sheet name contains certain characters or the parsing flow.

3. **Formula editing sheet switch**: When editing a formula, clicking another sheet tab loses focus and cancels the edit. Should allow selecting cells from other sheets while maintaining formula edit mode.

4. **Column/row-wide styling**: Can't set a style for an entire column or row via the UI. The backend supports this via `Axis.defaultStyleId` (Phase E4 of range-system-design), but no UI exposes it.

## Phase 1: Investigate and Fix Border + Bold Bug

The bug is architectural: when `setRangeStyle()` is called with a border, it checks for conflicting properties with existing ranges. Borders are treated as atomic (if both have "border" property, they conflict), which causes splitting even when only applying to the exact same range. After splitting, subsequent operations may not find the original unified range.

**Root cause hypothesis**: The toolbar's `applyStyleToSelection()` may be checking effective style of only the first cell in the range, not recognizing the selection as having a unified style after range splitting.

- [ ] 1a: Create E2E test reproducing the bug: apply border to B2:D4, then apply bold to B2:D4, verify all cells are bold
- [ ] 1b: Debug `StyleControls.applyStyleToSelection()` to understand how it handles range selections after border-induced splitting
- [ ] 1c: Fix the issue - when applying style to a selection, should apply to the entire selected range regardless of underlying range fragmentation
- [ ] 1d: Verify bold button state reflects effective style correctly for multi-cell selections (uses `getEffectiveStyleForRange`)

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

When editing a formula, the user should be able to:
1. Click a different sheet tab to switch views
2. Click cells in that sheet to insert references
3. Have the formula bar maintain the edit state

Current behavior: `uiStateMachine.reset()` is called on sheet switch, which transitions to IDLE and loses edit state.

- [ ] 3a: Create E2E test: start editing formula in Sheet1, click Sheet2 tab, click cell B5, verify formula shows `=Sheet2!B5`
- [ ] 3b: Modify sheet tab click handler to detect formula editing mode
- [ ] 3c: When in formula edit mode, sheet switch should NOT reset UI state machine
- [ ] 3d: Update cell click handler to insert cross-sheet reference when editing formula from different sheet
- [ ] 3e: Add visual indicator showing which sheet the formula is being edited in (current anchor cell's sheet)
- [ ] 3f: Handle Enter/Escape to commit/cancel and return to the original sheet

## Phase 4: Column/Row-Wide Style UI

The backend already supports column/row default styles via `Axis.defaultStyleId` with `AXIS_SET_STYLE` CRDT operation. The Luau API exposes `setColumnStyle()`/`setRowStyle()`. Need to add UI.

**Design**: When user selects entire column (click header) or row (click row number), style operations should apply to the axis default style, not create cell-level or range-level styles.

- [ ] 4a: Create E2E test: select entire column A, apply bold, verify new cells in column A inherit bold
- [ ] 4b: Add `selectEntireColumn(colIndex)` and `selectEntireRow(rowIndex)` methods to selection system
- [ ] 4c: Update column/row header click to use new selection methods (may already exist partially)
- [ ] 4d: Detect "entire column" or "entire row" selection in `StyleControls.applyStyleToSelection()`
- [ ] 4e: When entire column/row selected, call `setColumnStyle()`/`setRowStyle()` instead of `setStyleForRange()`
- [ ] 4f: Update effective style display to include column/row default styles in mixed state calculation
- [ ] 4g: Add E2E test: set column style, then override single cell, verify cell shows override while others show column style

## Architecture Notes

### Range System Recap (from 2026-01-14-range-system-design.md)
- Ranges use UUID corners (startColId, startRowId, endColId, endRowId)
- Flags bitmask allows single range to serve multiple purposes (MERGE | STYLE)
- R-tree spatial index for O(log n) queries
- Style inheritance: default → column → row → range → cell (CSS-like cascade)
- Rectangle splitting (Phase J) handles overlapping ranges with same property

### Style Resolution Order
1. Cell's own style (highest priority)
2. Range styles (CSS-like merge of overlapping ranges)
3. Column default style
4. Row default style
5. Default/null style

### Existing Infrastructure
- `getEffectiveCellStyle(col, row)` - computes merged style for single cell
- `getEffectiveStyleForRange(col1, row1, col2, row2)` - returns style + mixed flags
- `Axis.defaultStyleId` - column/row default styling
- `AXIS_SET_STYLE` CRDT operation for column/row styles
- Formula parser supports `SheetName!A1` syntax (needs verification)

## Testing Strategy

- E2E tests for each bug scenario
- Unit tests for formula parsing edge cases
- Manual testing for UX flow (sheet switching during formula edit)
