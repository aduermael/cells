# UI, Style, and Formula Bugs

Address several bugs related to border/style application, cross-sheet references, formula editing UX, and column/row-level styling.

## Issues

1. **Border + Bold bug**: When setting a border for a range, then clicking "bold" for the same range, the bold button becomes disabled and not all cells get bold styling. Root cause: when borders are applied, the range system splits overlapping ranges (Phase J of range-system-design). The bold operation then doesn't recognize the original selection as a single range anymore.

2. **Cross-sheet reference parsing**: Formulas like `=Sheet2!B27` don't work. The parser supports cross-sheet references syntactically, but the issue is likely in A1-to-UUID resolution when the sheet name contains certain characters or the parsing flow.

3. **Cross-sheet dependency graph (CRITICAL)**: The dependency graph only tracks dependencies within the same sheet. When a formula references a cell on another sheet, changing that cell does NOT trigger re-evaluation of the dependent formula. Cross-sheet formulas show correct initial values but become stale.

4. **Formula editing sheet switch**: When editing a formula, clicking another sheet tab loses focus and cancels the edit. Should allow selecting cells from other sheets while maintaining formula edit mode.

5. **Column/row-wide styling**: Can't set a style for an entire column or row via the UI. The backend supports this via `Axis.defaultStyleId` (Phase E4 of range-system-design), but no UI exposes it.

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

The parser architecture supports cross-sheet references (verified in formula_parser.cc:316-435). The issue was that the formula storage format used sheet names but the display/resolution needed UUID-based storage for stability.

**Implementation approach**: Store sheet references using UUID format (`!sheetId~~cellId`) similar to how cell references use UUIDs. This provides:
- Stability when sheets are renamed
- Consistency with cell reference storage format
- Clean separation between A1 notation (user-facing) and UUID format (storage)

- [x] 2a: Create E2E test: enter `=Sheet2!B27` formula. Added tests for both simple cell ref and SUM with range.
- [x] 2b: Add unit test for cross-sheet reference parsing. Added `ResolveCrossSheetRef_SheetFound`, `ResolveCrossSheetRef_SheetNotFound`, and `ResolveCrossSheetRange` tests.
- [x] 2c: Debug the formula entry flow. Found that `RefConverter::formulaToUuid` didn't handle sheet prefixes, leading to incorrect storage.
- [x] 2d: Implemented UUID-based sheet reference storage:
  - Added `UUID_SHEET_REF` token type to lexer for `!sheetId` format
  - Added `parseUuidSheetRef()` to parser to handle sheet UUID tokens
  - Added `sheetId` field to AST reference nodes (CellRefNode, ColumnRefNode, etc.)
  - Updated FormulaResolver to set `sheetId` when resolving cross-sheet refs
  - Updated FormulaSerializer to output `!sheetId` prefix for cross-sheet refs
  - Updated `RefConverter::formulaToA1` to convert `!sheetId` back to sheet name
  - Fixed lexer to only match `!` + 8 alphanumeric chars as UUID sheet ref (not break A1 notation)
- [x] 2e: Fixed cross-sheet evaluation in core:
  - Added `getSheetById` method to Workbook for looking up sheets by UUID
  - Updated `evaluateCellRef` to check `sheetId` first, then fall back to `sheetName`
  - Updated `evaluateRangeRef` to resolve target sheet and pass it to range iteration
  - Added `targetSheet` field to EvalResult for cross-sheet ranges
  - Updated `collectRangeValues` to use targetSheet from EvalResult
  - Unit tests pass (CrossSheetCellRef_Evaluates, CrossSheetRange_SUMEvaluates)
- [x] 2f: Fixed cross-sheet range display in formula bar:
  - Modified serializer to output sheet prefix once for ranges (not twice per cell)
  - Fixed `formulaToA1` to keep cross-sheet context for the second cell of a range
  - Formula bar now correctly shows `=SUM(Sheet2!A1:A3)`
- [x] 2g: **ROOT CAUSE FOUND** - CRDT operations always apply to sheets[0]:
  - Unit tests pass because they set up cells directly on Sheet objects
  - E2E tests fail because CRDT operations (`COL_INSERT`, `ROW_INSERT`, `CELL_SET_VALUE`) are hardcoded to apply to `workbook.sheets[0]` (Sheet1), NOT the active sheet
  - The issue is in `core/cells/crdt_axis.cc`:
    - `applyColInsert()` line 41: `Sheet* sheet = workbook.sheets[0].get();`
    - `applyRowInsert()` line 66: `Sheet* sheet = workbook.sheets[0].get();`
    - Similar pattern in other axis operations
  - Impact: When on Sheet2, `getOrCreateCellAt()` creates cells on Sheet1, so:
    1. Cell ID returned is for Sheet1 cell
    2. `updateCellWithFormatDetection()` looks on Sheet2 (active) - "Cell not found"
    3. Cell value never gets set
  - **Fix needed**: Operations need sheet context. Two options:
    - A) Add sheetId to Operation structure (significant refactor)
    - B) Add active sheet parameter to operation handlers (simpler but less clean)
  - This is a structural issue affecting ALL multi-sheet editing via CRDT operations
- [x] 2h: **Fix CRDT multi-sheet bug** - Operations must apply to correct sheet
  - Added `sheetId` field to `Operation` struct with full backwards-compatible serialization
  - Added overloaded `makeColInsertOp`, `makeRowInsertOp`, `makeCellSetValueOp` that accept sheetId
  - Updated `applyColInsert`, `applyRowInsert`, `applyDimInsertAxis` to use sheetId from operation
  - Updated `applyCellSetValue` to use sheetId when creating new cells
  - Updated `bindings_core.cc` to pass active sheet ID in `getOrCreateCellAt`, `setValueAtPosition`, `updateCellWithFormatDetection`
  - Fixed E2E test to use B5 instead of B27 (viewport issue unrelated to CRDT fix)
  - Fixed `RefConverter::formulaToA1` to handle column/row UUID refs (`@~`/`#~` format) - was showing `@~#REF!` for column/row references
  - All 181 E2E tests pass!
- [x] 2i: Test sheet names with spaces (should use `'Sheet Name'!A1` syntax)
  - Added `QUOTED_SHEET_NAME` token type to lexer for `'Sheet Name'` syntax
  - Added `scanQuotedSheetName()` method to lexer with escaped quote support (`''` -> `'`)
  - Updated parser to handle `QUOTED_SHEET_NAME` token followed by `!`
  - Updated `FormulaDisplayConverter::getSheetPrefix()` to quote sheet names with spaces, quotes, `!`, or `[`
  - Added unit tests: `QuotedSheetNameWithSpace`, `QuotedSheetNameWithHyphen`, `QuotedSheetNameWithEscapedQuote`, `QuotedSheetNameRange`
  - Added integration tests: `QuotedSheetNameDisplay`, `QuotedSheetNameWithQuoteDisplay`
  - E2E tests blocked on sheet renaming UI (not yet implemented)
- [x] 2j: Run all tests (unit, E2E) to verify Phase 2 complete
  - All 54 unit tests pass
  - All 181 E2E tests pass
  - Lint, type-check, and format checks pass

## Phase 3: Cross-Sheet Dependency Graph (CRITICAL) ✅ COMPLETE

**Bug**: The dependency graph currently only tracks dependencies within the same sheet. When a formula references a cell on another sheet (e.g., `=Sheet2!A1`), changing the source cell does NOT trigger re-evaluation of the dependent formula.

**Root cause**: Each sheet has its own DependencyGraph. When Sheet1!B1 has formula `=Sheet2!A1`, the dependency was registered in Sheet1's graph, but when Sheet2!A1 changed, only Sheet2's graph was checked for dependents. Sheet2 had no knowledge that Sheet1's formula depended on it.

**Solution implemented**:
1. Added workbook-level cross-sheet dependency tracking (`Workbook::_crossSheetDeps` and `Workbook::_crossSheetRangeDeps`)
2. When a formula with cross-sheet references is created, register the dependency at the workbook level
3. When a cell changes, call `recalculateCrossSheet()` which queries both direct cell dependencies and range dependencies
4. Range dependencies use position-based checking to handle formulas like `=SUM(Sheet2!A1:A3)` where any cell in the range can trigger recalc

- [x] 3a: Create E2E test reproducing the bug:
  - Set Sheet2!A1 = 10
  - In Sheet1!B1, enter formula `=Sheet2!A1`
  - Verify Sheet1!B1 shows 10
  - Change Sheet2!A1 to 20
  - Verify Sheet1!B1 updates to 20 ✅ Now passes
- [x] 3b: Investigate current dependency graph implementation
  - Each sheet has its own `DependencyGraph` in `core/cells/dependency_graph.cc`
  - Dependencies registered via `depGraph->addFormula()` only in the formula's sheet
  - Recalculation called only on the changed cell's sheet
- [x] 3c: Fix dependency registration to include sheetId
  - Added `CrossSheetRef` struct to extract cross-sheet references from AST
  - Added `Workbook::addCrossSheetDep()` and `addCrossSheetRangeDep()`
  - Modified `crdt_cell.cc` to register cross-sheet deps at workbook level
- [x] 3d: Fix dirty propagation to check cross-sheet dependents
  - Added `recalculateCrossSheet()` function in `formula_recalc.cc`
  - Queries workbook cross-sheet index for dependent formulas on other sheets
  - Handles recursive multi-hop dependencies
  - Called from all cell modification paths in `bindings_core.cc`
- [x] 3e: Add E2E test for cross-sheet range dependency ✅
  - Tests `=SUM(Sheet2!A1:A3)` updating when middle cell (A2) changes
- [x] 3f: Add E2E test for multi-hop cross-sheet dependencies ✅
  - Tests Sheet3!A1 → Sheet2!A1 → Sheet1!A1 dependency chain
- [x] 3g: Run all tests to verify cross-sheet reactivity works ✅
  - All 184 E2E tests pass

---

## Known Bugs (discovered during testing, to be addressed)

### Bug A: Cross-sheet formula becomes #VALUE! on re-edit

**Repro**:
1. Enter `=Sheet2!A1` in cell A1 (Sheet1)
2. Observe: correct value from Sheet2!A1 is displayed ✓
3. Click on A1 to edit, don't change anything, press Enter
4. Observe: cell now shows `#VALUE!` instead of the original value

**Hypothesis**: The formula re-parsing or re-resolution during edit-commit may be losing the cross-sheet context (sheetId) or failing to resolve the cell reference properly.

### Bug B: Error formulas display #ERROR! suffix incorrectly

**Repro**:
1. Enter `=B1++` (invalid formula) in a cell
2. Observe: cell shows error as expected
3. Click on the cell to edit
4. Observe: formula bar shows `=B1++#ERROR!` instead of just `=B1++`

**Expected**: The formula bar should show the original formula text `=B1++`, not with `#ERROR!` appended. The AST's `partialText` should preserve the original input for display, but the error marker is being concatenated during display conversion.

---

## Phase 4: Formula Editing Across Sheets ✅ COMPLETE

Excel-like behavior: when editing a formula, the user can navigate to other sheets and click cells to insert cross-sheet references. The formula bar stays active and shows the building formula with proper sheet prefixes.

Current behavior: `uiStateMachine.reset()` is called on sheet switch, which transitions to IDLE and cancels the edit.

**Target UX flow**:
1. User clicks cell A1 in Sheet1, types `=SUM(`
2. User clicks Sheet2 tab → view switches but formula bar stays in edit mode
3. User clicks cell B5 in Sheet2 → formula becomes `=SUM(Sheet2!B5`
4. User clicks Sheet1 tab → view returns to Sheet1
5. User clicks cell C3 → formula becomes `=SUM(Sheet2!B5,C3` (no prefix needed, same sheet as formula)
6. User types `)` and presses Enter → formula commits to A1 in Sheet1

**Implementation summary**:
- Added `originSheetIndex` field to `EditingSession` to track which sheet the formula editing started on
- Modified `SheetTabsManager.switchToSheet()` to preserve edit state when `editingSession.isFormulaEditing()` is true
- Added `getCrossSheetPrefix()` helper in `mouse-events.ts` that returns "SheetName!" or "'Sheet Name'!" when clicking cells on a different sheet than the origin
- Updated `CellEditor.confirmEditing()` to handle cross-sheet formula commits: switch to origin sheet first, then create/update cell
- Updated `CellEditor.cancelEditing()` and `FormulaBarEditor.cancelFormulaBarEdit()` to return to origin sheet on Escape

- [x] 4a: Create E2E test: start editing formula in Sheet1, click Sheet2 tab, click cell B5, verify formula shows `=Sheet2!B5`
- [x] 4b: Track "formula origin sheet" in EditingSession when formula editing starts
- [x] 4c: Modify sheet tab click handler: when in formula edit mode, switch view but preserve edit state
- [x] 4d: Update cell click handler: when editing formula from different sheet, insert `SheetName!` prefix
- [x] 4e: When clicking cells on the formula's origin sheet, insert reference without prefix
- [ ] 4f: Add subtle visual indicator in formula bar showing origin sheet (e.g., "Editing in: Sheet1") - **deferred, optional UX enhancement**
- [x] 4g: Handle Enter to commit formula and return view to origin sheet
- [x] 4h: Handle Escape to cancel and return view to origin sheet
- [x] 4i: Handle clicking outside grid (not on sheet tabs) to commit formula

## Phase 5: Column/Row-Wide Style UI

The backend already supports column/row default styles via `Axis.defaultStyleId` with `AXIS_SET_STYLE` CRDT operation. The Luau API exposes `setColumnStyle()`/`setRowStyle()`. Need to add UI.

**Design**: Auto-detect when user has selected an entire column or row. When clicking a column header, the selection should span all rows (conceptually infinite). Style operations on such selections should apply to the axis default style, not create cell-level or range-level styles.

**Detection logic**:
- "Entire column" = selection starts at row 0 and extends to max row (or a special flag)
- "Entire row" = selection starts at col 0 and extends to max col (or a special flag)
- Could use sentinel values like `startRow = 0, endRow = MAX_ROWS` or a dedicated selection type

- [ ] 5a: Create E2E test: click column A header, apply bold, verify new cells in column A inherit bold
- [ ] 5b: Add selection type or flags to distinguish "entire column/row" from regular range selection
- [ ] 5c: Update column header click to create "entire column" selection
- [ ] 5d: Update row header click to create "entire row" selection
- [ ] 5e: In `StyleControls.applyStyleToSelection()`, detect entire column/row selection
- [ ] 5f: When entire column selected, call `setColumnStyle()` instead of `setStyleForRange()`
- [ ] 5g: When entire row selected, call `setRowStyle()` instead of `setStyleForRange()`
- [ ] 5h: Update effective style display to show column/row default styles correctly
- [ ] 5i: Add E2E test: set column style, then override single cell, verify cell shows override while others show column style
- [ ] 5j: Visual feedback: highlight entire column/row when selected (not just visible cells)

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
