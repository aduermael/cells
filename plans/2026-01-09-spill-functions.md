Status: IN PROGRESS
Created At: 2026-01-09 19:40 UTC
Updated At: 2026-01-10 22:10 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

---

# Spill Functions (Dynamic Array Formulas)

## Overview

Implement Excel-compatible spill/dynamic array functionality where formulas can return multiple values that automatically "spill" into neighboring cells.

**Key behaviors (per Excel documentation):**
- Formula results that return arrays automatically populate adjacent cells
- Only the "master cell" (top-left) contains the actual formula
- Spilled cells show the formula grayed out in the formula bar (non-editable)
- If any cell in the spill range is blocked (has data), show `#SPILL!` error
- Selecting any cell in the spill range highlights the entire spill boundary
- Spilled values are **not persisted** - computed dynamically on recalculation
- The `#` operator references entire spill ranges (e.g., `D2#` = all spilled values from D2)

**Target functions:** UNIQUE, SORT, FILTER, SEQUENCE, TRANSPOSE, RANDARRAY

## Architecture Principle

**All spill logic lives in C++ (core engine), not TypeScript (UI).**

Even though spill data is runtime-only (not persisted to CRDT/file), the tracking structures and evaluation logic must be in the C++ core:
- `SpillInfo`, `spillMasters`, `spilledFrom` maps → in `Sheet` (C++)
- Spill range calculation, blocking detection → in `formula_eval.cc` / `formula_recalc.cc`
- Spilled cell values → stored in C++ runtime cache, queried via WASM bindings
- TypeScript only handles **rendering** (highlighting, grayed formula bar) based on data from C++

This ensures:
1. Consistency across all platforms (WASM, CLI, native)
2. Single source of truth for spill state
3. Proper integration with dependency graph and recalculation
4. No duplicated logic between C++ and TypeScript

## Research Summary

From [Microsoft Excel Documentation](https://support.microsoft.com/en-us/office/dynamic-array-formulas-and-spilled-array-behavior-205c6b06-03ba-4151-89a1-87a7eb36e531):
- Spill = formula resulted in multiple values placed in neighboring cells
- Excel dynamically sizes the output range on Enter
- #SPILL! error when output range is blocked by existing data
- Only first cell in spill range is editable
- Other cells show "ghosted" formula text in formula bar

---

## Phase 1: Core Data Model for Spill Ranges

Extend the data model to track spill relationships without persisting spilled values.

- [x] 1a: Add `SpillInfo` struct and spill tracking to Sheet
  - `SpillInfo { ID masterCellId; vector<pair<ID,ID>> spilledPositions; }`
  - `Sheet::spillMasters` map: masterCellId → SpillInfo
  - `Sheet::spilledFrom` map: (colId,rowId) → masterCellId (reverse lookup)
  - These are runtime-only, not serialized

- [x] 1b: Add `EvalResult::ARRAY` type for multi-value results
  - New type in `EvalResult`: `ARRAY` with `vector<vector<EvalResult>> arrayValue`
  - Add `isArray()`, `getArray()`, `getArrayRows()`, `getArrayCols()` methods
  - Arrays are row-major: `arrayValue[row][col]`

- [x] 1c: Add `CellError::SPILL` error type
  - Add `SPILL` to `CellError` enum in `types.h`
  - Add `#SPILL!` string representation in error formatting

---

## Phase 2: Spill Evaluation Engine

Modify formula evaluation to detect array results and handle spilling.

- [x] 2a: Add spill range calculation in formula_eval
  - After evaluating a formula that returns ARRAY, calculate target positions
  - `calculateSpillRange(Cell* master, size_t rows, size_t cols)` → vector of (colId, rowId)
  - Creates new columns/rows if needed (auto-extend)

- [x] 2b: Add spill blocking detection
  - `checkSpillBlocked(Sheet*, masterCellId, spillPositions)` → bool
  - A position is "blocked" if:
    - Cell exists with a value (not empty)
    - Cell has its own formula (not spilled from this master)
    - Cell is spilled from a DIFFERENT master
  - If blocked, master cell gets `#SPILL!` error, no spilling occurs

- [x] 2c: Implement spill population during recalculation
  - In `recalculateCell()`: if result is ARRAY and not blocked:
    - Clear previous spill range for this master (if any)
    - Register new spill range in `Sheet::spillMasters` and `spilledFrom`
    - Store individual values in spilled positions (runtime cache, not CRDT)
  - If blocked: clear any existing spill, set master to #SPILL!

- [x] 2d: Add spill range cleanup on master cell deletion/change
  - When master cell is deleted: clear its spill range
  - When master formula changes: recalculate and update spill range
  - When cell in spill range gets a value: trigger master recalc → #SPILL!

---

## Phase 3: Implement UNIQUE Function ✅

First spill-capable function as proof of concept.

- [x] 3a: Implement UNIQUE function returning ARRAY result
  - Signature: `UNIQUE(array, [by_col], [exactly_once])`
  - `array`: source range to extract unique values from
  - `by_col`: FALSE (default) = compare rows, TRUE = compare columns
  - `exactly_once`: FALSE (default) = all unique, TRUE = only values appearing once
  - Returns `EvalResult::ARRAY` with unique values as single column (or row if by_col)

- [x] 3b: Add UNIQUE tests
  - Basic: `=UNIQUE(A1:A5)` with duplicates → returns unique values
  - Exactly once: `=UNIQUE(A1:A5,FALSE,TRUE)` → only non-duplicated values
  - By column: `=UNIQUE(A1:C1,TRUE)` → unique columns
  - Empty input → empty array
  - Error in input → error propagation
  - Multi-column row comparison
  - Mixed types (numbers vs strings)

---

## Phase 4: Implement Additional Spill Functions ✅

- [x] 4a: Implement SORT function
  - Signature: `SORT(array, [sort_index], [sort_order], [by_col])`
  - Returns sorted array (spills)

- [x] 4b: Implement FILTER function
  - Signature: `FILTER(array, include, [if_empty])`
  - Returns filtered rows/columns matching criteria (spills)

- [x] 4c: Implement SEQUENCE function
  - Signature: `SEQUENCE(rows, [cols], [start], [step])`
  - Returns generated number sequence (spills)

- [x] 4d: Implement TRANSPOSE function (upgrade existing if any)
  - Signature: `TRANSPOSE(array)`
  - Returns transposed array (spills)

- [x] 4e: Add tests for all new functions

---

## Phase 5: Spill Range Operator (#)

- [x] 5a: Add `#` operator to formula lexer
  - Recognize `A1#` as spill range reference
  - New token type: `HASH`

- [x] 5b: Add SpillRangeRef AST node
  - `SpillRangeRefNode { CellRefNode* anchor }`
  - Parser: `cell_ref '#'` → SpillRangeRefNode

- [x] 5c: Implement SpillRangeRef evaluation
  - Look up anchor cell's spill range from `Sheet::spillMasters`
  - Return RANGE result covering the spill area
  - If anchor has no spill range, return single cell

---

## Phase 6: WASM Bindings ✅

- [x] 6a: Expose spill info in cell queries
  - `queryViewport()` response includes: `isSpilled: bool`, `spillMasterId: string`, `isSpillMaster: bool`
  - Virtual spilled cells (no actual cell in cells map) are now included in viewport query
  - Spilled values shown with `type: "s"` marker

- [x] 6b: Add spill range query for highlighting
  - `getSpillRangeAt(col, row)` → returns master cell ID and full spill bounds
  - Returns: `masterId`, `masterCol`, `masterRow`, `minCol`, `minRow`, `maxCol`, `maxRow`, `spillCount`
  - Used by UI to highlight spill range on selection

---

## Phase 7: Unify Cell Flags (Structural Refactor) ✅

Unify shared formula and spill tracking by moving shared formula relationships to Sheet level (like spill) and adding a flags byte to Cell for fast runtime checks.

**Rationale:**
- Currently `_spillMasters`/`_spilledFrom` are at Sheet level (good)
- But `sharedFormulaRef`/`_isSharedFormulaMaster` are at Cell level (increases Cell size)
- A pointer (`Cell* sharedFormulaRef`) adds 8 bytes to every Cell
- Moving to Sheet-level maps + a flags byte reduces memory and unifies architecture

**Flags byte layout (runtime-only, not persisted):**
```
bit 0: isSharedFormulaMaster
bit 1: isSharedFormulaSubscriber
bit 2: isSpillMaster
bit 3: isSpilledFrom
bits 4-7: reserved for future use
```

- [x] 7a: Add `uint8_t _flags` field to Cell
  - Replace `bool _isSharedFormulaMaster` with flag bit
  - Add helper methods: `setFlag()`, `clearFlag()`, `hasFlag()`
  - Add public accessors that use flags internally

- [x] 7b: Add shared formula tracking to Sheet
  - Add `_sharedFormulaMasters`: map masterId → SharedFormulaInfo
  - Add `_sharedFormulaFrom`: map subscriberId → masterId
  - Similar structure to `_spillMasters`/`_spilledFrom`

- [x] 7c: Remove `sharedFormulaRef` pointer from Cell
  - Removed 8-byte `sharedFormulaRef` pointer from Cell struct
  - `Cell::getFormula()` now returns own formula only (nullptr for subscribers)
  - Added `Cell::setSharedFormulaSubscriber()` to set the subscriber flag
  - `Cell::isSharedFormula()` now checks SHARED_FORMULA_SUBSCRIBER flag
  - Added `Sheet::getEffectiveFormula(Cell*)` to get formula (follows master reference)
  - Updated `SharedFormulaGroup` to use flags and Sheet-level storage
  - Updated serializer.cc, parser.cc, xlsx_reader.cc, xlsx_writer.cc to use Sheet-level API

- [x] 7d: Add spill flags to runtime tracking
  - When registering spill range, set `isSpillMaster` flag on master cell
  - When populating spilled positions, set `isSpilledFrom` flag (for virtual cells, track in map only)
  - Flags allow O(1) check "is this cell involved in spill?" before map lookup

- [x] 7e: Update serialization
  - Flags are runtime-only, not persisted to ZCD
  - On load: rebuild flags from relationships (shared formula refs, spill recalc)
  - Ensure XLSX reader/writer still works with new structure
  - Verified: `_flags` field initialized to 0, not serialized anywhere
  - Shared formula flags: rebuilt by `registerSharedFormulaGroup()` in parser.cc:904 and xlsx_reader.cc:896
  - Spill flags: rebuilt during recalculation when `registerSpillRange()` is called

- [x] 7f: Update tests
  - Verify Cell size reduction (from Phase 7a-7c: removed 8-byte pointer, added 1-byte flags)
  - Test shared formula behavior unchanged (xlsx_writer_test, serializer_test pass)
  - Test spill behavior unchanged (formula_recalc_test passes)
  - Test flag consistency after operations:
    - SpillMasterFlagSetOnRegister: flag set when spill is registered
    - SpillMasterFlagClearedOnClear: flag cleared when spill is cleared
    - SpillMasterFlagClearedOnClearAll: both masters' flags cleared on clearAll
    - SpillMasterFlagRemainsOnReplace: flag stays set when replacing spill with another
    - SpillMasterFlagClearedOnSingleValue: flag cleared when replaced with 1x1 (no spill)

---

## Phase 8: UI Rendering (TypeScript)

UI layer only renders based on spill data queried from C++ via WASM bindings.

- [x] 8a: Highlight spill range on cell selection
  - Call `getSpillRangeAt()` from C++ to get spill bounds (exposed via WASM)
  - If cell is part of a spill range, draw blue border around entire range
  - Added `SPILL_RANGE_COLOR` constant (blue: #4285f4)
  - Added `SpillRangeHighlight` interface and state to grid renderer
  - Added `drawSpillRangeHighlight()` function in grid-selection-renderer.ts
  - Selection change triggers async spill range query and updates highlight

- [x] 8b: Gray out formula bar for spilled cells
  - Added `masterFormula` to viewport query response for spilled cells
  - Added `isSpilled`, `isSpillMaster`, `spillMasterId`, `masterFormula` to CellData type
  - Formula bar shows master formula when spilled cell is selected
  - Added `.spilled-cell` CSS class with grayed out styling
  - Formula highlights work for spilled cells (shows master's references)

- [x] 8c: Prevent editing spilled cells
  - Added `getCellDataAt` callback to CellEditor for checking spill status
  - Added `isSelectedCellSpilled()` method to CellEditor
  - `startEditing()` returns early if selected cell is spilled
  - `deleteRangeCells()` skips spilled cells (can't delete non-master cells)
  - Formula bar uses `pointer-events: none` via CSS for spilled cells
  - Deleting master cell clears entire spill (handled in C++)

- [x] 8d: Add E2E tests for spill UI behavior
  - Created spill.test.mjs with 7 tests:
    - SEQUENCE creates a spill range
    - Spilled cell shows grayed formula bar
    - Cannot edit spilled cell by typing
    - Cannot edit spilled cell with F2
    - Cannot delete spilled cell with Backspace
    - Master cell can be edited
    - UNIQUE creates spill range with unique values
  - Fixed bug in ref_converter.cc: 8-char function names (like SEQUENCE) were incorrectly treated as cell IDs

---

## Phase 9: Integration & Polish

- [x] 9a: Update formula display for spill references
  - FormulaDisplayConverter handles SpillRangeRefNode (already implemented)
  - Display as `A1#` in formula bar
  - Added tests: DisplayConversion_SpillRangeRef, DisplayConversion_SpillRangeRefAbsolute, DisplayConversion_SpillRangeInFunction
  - Added spill range refs to round-trip test suite

- [x] 9b: Handle edge cases
  - Spill into merged cells → #SPILL! (N/A: merged cells not implemented)
  - Spill across sheet boundaries → auto-extend (columns/rows created as needed)
  - Circular spill dependencies → already handled by formula circular reference detection
  - Very large spill ranges → added MAX_SPILL_CELLS limit (1,000,000 cells)
  - Added tests: SpillSizeLimitExceeded, SpillSizeJustUnderLimit

- [x] 9c: Documentation
  - Updated docs/formula-engine.md implementation status (now "Fully implemented")
  - Added "Dynamic Arrays (Spill Behavior)" section documenting:
    - Key behaviors and spill semantics
    - Spill-capable functions (UNIQUE, SORT, FILTER, SEQUENCE, TRANSPOSE)
    - The `#` spill range operator with examples
    - Spill blocking conditions
    - Limits (MAX_SPILL_CELLS)

---

## Phase 10: Bug Fixes

Post-implementation bug fixes discovered during testing. Each bug should be reproduced with a failing test first, then the test should be made to pass.

- [x] 10a: Improve spill range highlighting behavior
  - **Bug**: Spill range highlighting behavior was inconsistent - highlight not clearing when selecting outside spill
  - **Root cause**: `setSelectedCell` in the AppEventManager config was not triggering `updateSpillRangeHighlight`
  - **Fix**: Modified `setSelectedCell` to call `updateSpillRangeHighlight(cell)` when cell is set, and clear highlight when cell is null
  - **Fix**: Added `render()` call after async highlight update completes to ensure UI reflects changes
  - **Added E2E tests**:
    - "Spill highlight shows when selecting master cell"
    - "Spill highlight shows when selecting spilled cell"
    - "Spill highlight clears when selecting cell outside spill range"

- [x] 10b: Fix `=INDEX(A:A)` returning #REF! error
  - **Bug**: Using whole-column reference in INDEX returns #REF! error
  - **Root cause**: `getRangeDimensions` and `getCellAtPosition` in fn_lookup.cc only handled `CELL_RANGE` type, returning invalid/error for `COLUMN`, `COLUMN_RANGE`, `ROW`, `ROW_RANGE` types
  - **Fix**: Extended both helper functions to handle all range types:
    - `COLUMN`/`COLUMN_RANGE`: Use column IDs, rows start at 0 with "unlimited" max (1M rows)
    - `ROW`/`ROW_RANGE`: Use row IDs, columns start at 0 with "unlimited" max (16K cols)
  - **Tests added**: IndexWholeColumn, IndexWholeColumnWithColArg, IndexColumnRange, IndexWholeRow, IndexRowRange

- [x] 10c: Fix column/row reference highlighting in formula bar
  - **Bug**: Whole-column/row references like `A:A` and `1:1` were not highlighted correctly in the formula bar
  - **Root cause**: AST parser was setting source position to only the first token, not the full reference text
  - **Fix**: Updated `FormulaParser` to compute full source position spanning from start to end of reference:
    - `parseRowRef()` now takes `startPos` parameter and computes `fullPos{startPos.start, endToken.position.end}`
    - `ColumnRefNode` position now spans full `A:A` instead of just first `A`
    - `ColumnRangeRefNode` position now spans full `A:C`
    - `RowRefNode` position now spans full `1:1`
    - `RowRangeRefNode` position now spans full `1:10`
  - **Tests added**:
    - C++ unit tests: WholeRowRefPosition, WholeColumnRefPosition, RowRangeRefPosition, ColumnRangeRefPosition
    - E2E tests: "Column reference is colored in formula bar", "Row reference is colored in formula bar"
  - **Note**: WASM build is currently broken (Luau exception issue), E2E tests pending

- [x] 10d: Allow editing cells that would break existing spill
  - **Bug**: Cannot type into a cell currently occupied by a spill range
  - **Current behavior**: Editing is blocked for spilled cells (too restrictive)
  - **Expected behavior (Excel-compatible)**:
    - User CAN type a value into a spilled cell
    - This causes the spill master to show `#SPILL!` error
    - Deleting the blocking value should dynamically restore the spill
  - **Fix**:
    - Removed edit restriction in `CellEditor.startEditing()` (cell-editor.ts)
    - Added spill master recalculation in `createCell()` and `updateCellWithFormatDetection()` when cell is created/updated at a spill position
    - Added #SPILL! cell recalculation in `deleteCell()`, `deleteCellAt()`, and `updateCellWithFormatDetection()` to restore spills when blocking cells are removed or cleared
    - Changed Delete/Backspace key to always call `deleteRangeCells()` for Excel-compatible clear behavior (keyboard-events.ts)
    - Removed obsolete E2E tests that verified editing was blocked for spilled cells
    - Added two new E2E tests: "Typing into spilled cell blocks spill and shows #SPILL! error" and "Deleting blocking value restores spill"

- [ ] 10e: (Placeholder for additional bugs found during testing)

---

## Technical Notes

### Runtime-Only But Still C++

Spill data is "runtime-only" meaning:
- NOT persisted to .cells files or CRDT operations
- Recomputed on file load / recalculation

But it still lives in C++:
- `Sheet` holds `spillMasters` and `spilledFrom` maps (cleared on load, rebuilt on recalc)
- Spilled cell values stored in a C++ cache structure (not in `Cell::value` for non-master cells)
- All blocking detection, range calculation, cleanup logic in C++
- TypeScript queries this state via WASM, never computes it

### Why Not Persist Spilled Values?

1. **Excel behavior**: Spilled values are computed, not stored
2. **CRDT complexity**: Would need operations for each spilled cell
3. **File size**: Large spill ranges would bloat files
4. **Correctness**: Spill range can change when source data changes

### Spill Resolution Order

During recalculation:
1. Evaluate all formulas (may produce ARRAY results)
2. For each ARRAY result, attempt to spill
3. If blocked, set #SPILL! error
4. Spill blocking is checked against:
   - Cells with values
   - Cells with their own formulas
   - Cells spilled from OTHER masters (not self)

### Performance Considerations

- Large UNIQUE/SORT on big ranges: consider lazy evaluation
- Spill range lookup should be O(1) via reverse map
- Recalculation should batch spill updates

---

## References

- [Microsoft: Dynamic array formulas and spilled array behavior](https://support.microsoft.com/en-us/office/dynamic-array-formulas-and-spilled-array-behavior-205c6b06-03ba-4151-89a1-87a7eb36e531)
- [Exceljet: Dynamic array formulas in Excel](https://exceljet.net/articles/dynamic-array-formulas-in-excel)
- [Excel Campus: Dynamic Array Formulas & Spill Ranges](https://www.excelcampus.com/functions/dynamic-array-formulas-spill-ranges/)
