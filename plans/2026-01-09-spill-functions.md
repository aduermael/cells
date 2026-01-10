Status: READY
Created At: 2026-01-09 19:40 UTC
Updated At: 2026-01-10 21:15 UTC
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

## Phase 7: Unify Cell Flags (Structural Refactor)

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

- [ ] 7d: Add spill flags to runtime tracking
  - When registering spill range, set `isSpillMaster` flag on master cell
  - When populating spilled positions, set `isSpilledFrom` flag (for virtual cells, track in map only)
  - Flags allow O(1) check "is this cell involved in spill?" before map lookup

- [ ] 7e: Update serialization
  - Flags are runtime-only, not persisted to ZCD
  - On load: rebuild flags from relationships (shared formula refs, spill recalc)
  - Ensure XLSX reader/writer still works with new structure

- [ ] 7f: Update tests
  - Verify Cell size reduction
  - Test shared formula behavior unchanged
  - Test spill behavior unchanged
  - Test flag consistency after operations

---

## Phase 8: UI Rendering (TypeScript)

UI layer only renders based on spill data queried from C++ via WASM bindings.

- [ ] 8a: Highlight spill range on cell selection
  - Call `getSpillRangeForCell()` from C++ to get spill bounds
  - If cell is part of a spill range, draw border around entire range
  - Use a distinct color (e.g., blue border like Excel)

- [ ] 8b: Gray out formula bar for spilled cells
  - Check `isSpilled` flag from `getCell()` response (from C++)
  - If spilled (non-master): show master's formula grayed out, disable editing
  - If master: normal editable behavior

- [ ] 8c: Prevent editing spilled cells
  - Check `isSpilled` flag before allowing edit
  - If user tries to type in spilled cell, show message
  - Deleting master cell clears entire spill (handled in C++)

- [ ] 8d: Add E2E tests for spill UI behavior
  - Test spill range highlighting
  - Test formula bar ghosting
  - Test edit prevention
  - Test #SPILL! error display when blocked

---

## Phase 9: Integration & Polish

- [ ] 9a: Update formula display for spill references
  - FormulaDisplayConverter handles SpillRangeRefNode
  - Display as `A1#` in formula bar

- [ ] 9b: Handle edge cases
  - Spill into merged cells → #SPILL!
  - Spill across sheet boundaries → truncate at edge
  - Circular spill dependencies → detect and error
  - Very large spill ranges → performance limits?

- [ ] 9c: Documentation
  - Add spill functions to any function documentation
  - Document the `#` operator

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
