Status: READY
Created At: 2026-01-09 19:40 UTC
Updated At: 2026-01-09 19:40 UTC
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

- [ ] 1a: Add `SpillInfo` struct and spill tracking to Sheet
  - `SpillInfo { ID masterCellId; vector<pair<ID,ID>> spilledPositions; }`
  - `Sheet::spillMasters` map: masterCellId → SpillInfo
  - `Sheet::spilledFrom` map: (colId,rowId) → masterCellId (reverse lookup)
  - These are runtime-only, not serialized

- [ ] 1b: Add `EvalResult::ARRAY` type for multi-value results
  - New type in `EvalResult`: `ARRAY` with `vector<vector<EvalResult>> arrayValue`
  - Add `isArray()`, `getArray()`, `getArrayRows()`, `getArrayCols()` methods
  - Arrays are row-major: `arrayValue[row][col]`

- [ ] 1c: Add `CellError::SPILL` error type
  - Add `SPILL` to `CellError` enum in `types.h`
  - Add `#SPILL!` string representation in error formatting

---

## Phase 2: Spill Evaluation Engine

Modify formula evaluation to detect array results and handle spilling.

- [ ] 2a: Add spill range calculation in formula_eval
  - After evaluating a formula that returns ARRAY, calculate target positions
  - `calculateSpillRange(Cell* master, size_t rows, size_t cols)` → vector of (colId, rowId)
  - Creates new columns/rows if needed (auto-extend)

- [ ] 2b: Add spill blocking detection
  - `checkSpillBlocked(Sheet*, masterCellId, spillPositions)` → bool
  - A position is "blocked" if:
    - Cell exists with a value (not empty)
    - Cell has its own formula (not spilled from this master)
    - Cell is spilled from a DIFFERENT master
  - If blocked, master cell gets `#SPILL!` error, no spilling occurs

- [ ] 2c: Implement spill population during recalculation
  - In `recalculateCell()`: if result is ARRAY and not blocked:
    - Clear previous spill range for this master (if any)
    - Register new spill range in `Sheet::spillMasters` and `spilledFrom`
    - Store individual values in spilled positions (runtime cache, not CRDT)
  - If blocked: clear any existing spill, set master to #SPILL!

- [ ] 2d: Add spill range cleanup on master cell deletion/change
  - When master cell is deleted: clear its spill range
  - When master formula changes: recalculate and update spill range
  - When cell in spill range gets a value: trigger master recalc → #SPILL!

---

## Phase 3: Implement UNIQUE Function

First spill-capable function as proof of concept.

- [ ] 3a: Implement UNIQUE function returning ARRAY result
  - Signature: `UNIQUE(array, [by_col], [exactly_once])`
  - `array`: source range to extract unique values from
  - `by_col`: FALSE (default) = compare rows, TRUE = compare columns
  - `exactly_once`: FALSE (default) = all unique, TRUE = only values appearing once
  - Returns `EvalResult::ARRAY` with unique values as single column (or row if by_col)

- [ ] 3b: Add UNIQUE tests
  - Basic: `=UNIQUE(A1:A5)` with duplicates → returns unique values
  - Exactly once: `=UNIQUE(A1:A5,,TRUE)` → only non-duplicated values
  - By column: `=UNIQUE(A1:C1,TRUE)` → unique columns
  - Empty input → empty array
  - Error in input → error propagation

---

## Phase 4: Implement Additional Spill Functions

- [ ] 4a: Implement SORT function
  - Signature: `SORT(array, [sort_index], [sort_order], [by_col])`
  - Returns sorted array (spills)

- [ ] 4b: Implement FILTER function
  - Signature: `FILTER(array, include, [if_empty])`
  - Returns filtered rows/columns matching criteria (spills)

- [ ] 4c: Implement SEQUENCE function
  - Signature: `SEQUENCE(rows, [cols], [start], [step])`
  - Returns generated number sequence (spills)

- [ ] 4d: Implement TRANSPOSE function (upgrade existing if any)
  - Signature: `TRANSPOSE(array)`
  - Returns transposed array (spills)

- [ ] 4e: Add tests for all new functions

---

## Phase 5: Spill Range Operator (#)

- [ ] 5a: Add `#` operator to formula lexer
  - Recognize `A1#` as spill range reference
  - New token type: `SPILL_REF`

- [ ] 5b: Add SpillRangeRef AST node
  - `SpillRangeRefNode { CellRefNode* anchor }`
  - Parser: `cell_ref '#'` → SpillRangeRefNode

- [ ] 5c: Implement SpillRangeRef evaluation
  - Look up anchor cell's spill range from `Sheet::spillMasters`
  - Return RANGE result covering the spill area
  - If anchor has no spill range, return single cell

---

## Phase 6: WASM Bindings

- [ ] 6a: Expose spill info in cell queries
  - `getCell()` response includes: `isSpilled: bool`, `spillMasterId: string | null`
  - `getSpillRange(cellId)` → returns spill range bounds if cell is master

- [ ] 6b: Add spill range query for highlighting
  - `getSpillRangeForCell(colId, rowId)` → returns master cell ID and full spill bounds
  - Used by UI to highlight spill range on selection

---

## Phase 7: UI Integration (TypeScript)

- [ ] 7a: Highlight spill range on cell selection
  - When selecting any cell, check if it's part of a spill range
  - If yes, draw a border around the entire spill range (similar to selection but different style)
  - Use a distinct color (e.g., blue border like Excel)

- [ ] 7b: Gray out formula bar for spilled cells
  - When selecting a spilled (non-master) cell:
    - Show the master's formula in the formula bar
    - Apply "ghosted" styling (gray text, non-editable)
    - Disable editing in formula bar
  - When selecting master cell: normal editable behavior

- [ ] 7c: Prevent editing spilled cells
  - Block typing/editing in spilled cells
  - If user tries to type, show tooltip: "Can't edit spilled cell"
  - Allow deleting entire spill by deleting master cell

- [ ] 7d: Add E2E tests for spill UI behavior
  - Test spill range highlighting
  - Test formula bar ghosting
  - Test edit prevention
  - Test #SPILL! error display when blocked

---

## Phase 8: Integration & Polish

- [ ] 8a: Update formula display for spill references
  - FormulaDisplayConverter handles SpillRangeRefNode
  - Display as `A1#` in formula bar

- [ ] 8b: Handle edge cases
  - Spill into merged cells → #SPILL!
  - Spill across sheet boundaries → truncate at edge
  - Circular spill dependencies → detect and error
  - Very large spill ranges → performance limits?

- [ ] 8c: Documentation
  - Add spill functions to any function documentation
  - Document the `#` operator

---

## Technical Notes

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
