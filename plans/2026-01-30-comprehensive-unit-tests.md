# Comprehensive C++ Unit Test Coverage Plan

This plan systematically expands unit test coverage for all supported operations. Each phase focuses on a specific domain, adding tests and fixing any bugs surfaced. Tests will be split into separate files when they exceed ~800 lines.

**Important:** All checks (`bazel run :check`) must pass at the end of each phase. This includes unit tests, linting (clang-tidy), type-checking, and formatting. If pre-existing issues are discovered, fix them as part of the phase.

---

## Phase 1: Style Operations Testing

New file: `style_operations_test.cc`

- [x] 1a: Test applying styles to individual cells via CELL_SET (7 tests)
- [x] 1b: Test applying styles to columns/rows via COL_SET/ROW_SET (5 tests)
- [x] 1c: Test applying styles to ranges via RANGE_SET with STYLE flag (3 tests)
- [x] 1d: Test style inheritance hierarchy (cell > range > row > column > sheet) (5 tests)
- [x] 1e: Test concurrent style changes from multiple peers (CRDT convergence) (4 tests)
- [x] 1f: Test all style properties (bold, italic, underline, strikethrough, font, size, colors, alignment, borders) (27 tests)

---

## Phase 2: Format Operations Testing

New file: `format_operations_test.cc`

- [x] 2a: Test applying formats to individual cells via CELL_SET (7 tests)
- [x] 2b: Test applying formats to columns/rows via COL_SET/ROW_SET (5 tests)
- [x] 2c: Test applying formats to ranges via RANGE_SET with FORMAT flag (3 tests)
- [x] 2d: Test format inheritance hierarchy (cell > range > row > column) (5 tests)
- [x] 2e: Test all format categories (number, currency, percentage, date, time, text, custom) (17 tests)
- [x] 2f: Test format display rendering for edge cases (large numbers, negative, zero, text in number cells) (17 tests + 4 CRDT convergence tests = 21 tests)

---

## Phase 3: Axis Insert/Delete Operations

New file: `axis_insert_delete_test.cc`

- [x] 3a: Test column insert at beginning, middle, end positions (8 tests)
- [x] 3b: Test row insert at beginning, middle, end positions (8 tests)
- [x] 3c: Test column delete with cell cascade cleanup (8 tests)
- [x] 3d: Test row delete with cell cascade cleanup (8 tests)
- [x] 3e: Test insert/delete effects on formula references (4 tests)
- [x] 3f: Test insert/delete effects on range boundaries (10 tests)
- [x] 3g: Test concurrent insert/delete from multiple peers (9 tests)

---

## Phase 4: Axis Move/Resize Operations

New file: `axis_move_resize_test.cc`

- [x] 4a: Test column move (position swap) with formula reference updates (7 tests)
- [x] 4b: Test row move (position swap) with formula reference updates (7 tests)
- [x] 4c: Test column resize preserving cell data (5 tests)
- [x] 4d: Test row resize preserving cell data (5 tests)
- [x] 4e: Test moving axis that is part of a range boundary (4 tests)
- [x] 4f: Test concurrent move operations from multiple peers (6 tests)
- [x] 4g: Test axis hidden/shown toggle with formula visibility (10 tests)
- [x] Fix: Pre-existing clang-tidy `modernize-use-auto` errors in `crdt_axis.cc`

---

## Phase 5: Range Expansion/Shrinking

New file: `range_boundary_test.cc`

- [x] 5a: Test range expands when column inserted inside range (6 tests)
- [x] 5b: Test range expands when row inserted inside range (6 tests)
- [x] 5c: Test range shrinks when column deleted from inside range (5 tests)
- [x] 5d: Test range shrinks when row deleted from inside range (5 tests)
- [x] 5e: Test range survives when boundary axis deleted (moves to next) (5 tests)
- [x] 5f: Test range deleted when all axes removed (7 tests)
- [x] 5g: Test all range flags during boundary changes (MERGE, STYLE, FORMAT, etc.) (12 tests + 5 edge case tests)

---

## Phase 6: Merged Cells Operations

New file: `merged_cells_test.cc`

- [x] 6a: Test creating merged cell ranges (8 tests)
- [x] 6b: Test merged cell anchor (top-left) holds value, others empty (7 tests)
- [x] 6c: Test inserting column/row inside merged range (expands merge) (4 tests)
- [x] 6d: Test deleting column/row inside merged range (shrinks merge) (8 tests)
- [x] 6e: Test formulas referencing merged cells (return anchor value) (4 tests)
- [x] 6f: Test unmerging cells preserves anchor value (4 tests)
- [x] 6g: Test concurrent merge operations from multiple peers (7 tests + 4 edge case tests)

---

## Phase 7: Formula Error Handling

New file: `formula_error_test.cc`

- [ ] 7a: Test #VALUE! error from incompatible types (text in math operation)
- [ ] 7b: Test #REF! error from deleted reference
- [ ] 7c: Test #DIV/0! error from division by zero
- [ ] 7d: Test #NAME? error from unknown function/named range
- [ ] 7e: Test #NUM! error from invalid numeric operations
- [ ] 7f: Test #N/A error from lookup failures
- [ ] 7g: Test error propagation through formula chains
- [ ] 7h: Test formula recovery after fixing source cell (auto-recalculate)

---

## Phase 8: Formula Recalculation on Data Changes

New file: `formula_data_change_test.cc`

- [ ] 8a: Test formula recalculates when direct dependency changes
- [ ] 8b: Test formula recalculates when indirect dependency changes (chain)
- [ ] 8c: Test formula recalculates when cell format changes affect type coercion
- [ ] 8d: Test formula recalculates when referenced cell deleted then recreated
- [ ] 8e: Test shared formula group recalculates correctly
- [ ] 8f: Test array formula spill recalculates on source change
- [ ] 8g: Test circular reference detection remains stable during changes

---

## Phase 9: Cross-Sheet Operations

New file: `cross_sheet_operations_test.cc`

- [ ] 9a: Test cross-sheet formula references (Sheet2!A1)
- [ ] 9b: Test cross-sheet reference updates when target sheet renamed
- [ ] 9c: Test cross-sheet reference becomes #REF! when target sheet deleted
- [ ] 9d: Test cross-sheet range references
- [ ] 9e: Test cross-sheet named range references
- [ ] 9f: Test copying formulas between sheets updates references correctly
- [ ] 9g: Test concurrent cross-sheet edits from multiple peers

---

## Phase 10: Named Ranges Operations

New file: `named_ranges_operations_test.cc`

- [ ] 10a: Test creating workbook-scoped named ranges
- [ ] 10b: Test creating sheet-scoped named ranges
- [ ] 10c: Test named range in formulas (=SUM(MyRange))
- [ ] 10d: Test named range boundaries update on insert/delete
- [ ] 10e: Test named range deletion causes #NAME? in formulas
- [ ] 10f: Test renaming named ranges updates formula display
- [ ] 10g: Test scope precedence (sheet scope overrides workbook scope)

---

## Phase 11: Cell Type Coercion

New file: `cell_type_coercion_test.cc`

- [ ] 11a: Test number stored in text-formatted cell
- [ ] 11b: Test text stored in number-formatted cell
- [ ] 11c: Test boolean stored in various format types
- [ ] 11d: Test date/time value coercion
- [ ] 11e: Test formula referencing mixed types
- [ ] 11f: Test VALUE(), TEXT(), NUMBERVALUE() coercion functions
- [ ] 11g: Test implicit coercion in arithmetic operations

---

## Phase 12: Spill Range Operations

New file: `spill_operations_test.cc`

- [ ] 12a: Test array formula creates spill range
- [ ] 12b: Test spill blocked by existing data (shows #SPILL!)
- [ ] 12c: Test spill range updates when array size changes
- [ ] 12d: Test inserting rows/columns in spill range
- [ ] 12e: Test deleting rows/columns in spill range
- [ ] 12f: Test overlapping spill ranges detection
- [ ] 12g: Test spill master cell deletion clears spill range

---

## Phase 13: Complex Operation Sequences

New file: `operation_sequence_test.cc`

- [ ] 13a: Test rapid insert/delete/move sequences maintain consistency
- [ ] 13b: Test style + format + value changes in single operation batch
- [ ] 13c: Test concurrent operations from 3+ peers converge correctly
- [ ] 13d: Test undo/redo style operation sequences (if supported)
- [ ] 13e: Test copy-paste operation sequences with formulas
- [ ] 13f: Test fill operation sequences (fill down, fill right)

---

## Phase 14: Edge Cases and Boundary Conditions

New file: `edge_cases_test.cc`

- [ ] 14a: Test operations on first column (A / position 0)
- [ ] 14b: Test operations on first row (1 / position 0)
- [ ] 14c: Test operations at large positions (column ZZ, row 10000)
- [ ] 14d: Test empty cell references in formulas
- [ ] 14e: Test very long text values
- [ ] 14f: Test very large and very small numbers
- [ ] 14g: Test formula with maximum nesting depth
- [ ] 14h: Test range spanning entire columns/rows

---

## Phase 15: CRDT Conflict Resolution Verification

Extend: `crdt_test.cc` (or new `crdt_conflict_test.cc` if too large)

- [ ] 15a: Test Last-Writer-Wins for concurrent cell edits
- [ ] 15b: Test concurrent axis operations resolve deterministically
- [ ] 15c: Test concurrent range modifications merge correctly
- [ ] 15d: Test HLC ordering guarantees across peers
- [ ] 15e: Test operation replay produces identical state
- [ ] 15f: Test late-joining peer catches up correctly

---

## Summary

| Phase | Focus Area | New/Extended File | Est. Tests |
|-------|------------|-------------------|------------|
| 1 | Style Operations | style_operations_test.cc | 20-25 |
| 2 | Format Operations | format_operations_test.cc | 20-25 |
| 3 | Axis Insert/Delete | axis_insert_delete_test.cc | 25-30 |
| 4 | Axis Move/Resize | axis_move_resize_test.cc | 25-30 |
| 5 | Range Boundaries | range_boundary_test.cc | 20-25 |
| 6 | Merged Cells | merged_cells_test.cc | 20-25 |
| 7 | Formula Errors | formula_error_test.cc | 25-30 |
| 8 | Formula Recalc | formula_data_change_test.cc | 25-30 |
| 9 | Cross-Sheet | cross_sheet_operations_test.cc | 20-25 |
| 10 | Named Ranges | named_ranges_operations_test.cc | 20-25 |
| 11 | Type Coercion | cell_type_coercion_test.cc | 20-25 |
| 12 | Spill Ranges | spill_operations_test.cc | 20-25 |
| 13 | Op Sequences | operation_sequence_test.cc | 15-20 |
| 14 | Edge Cases | edge_cases_test.cc | 25-30 |
| 15 | CRDT Conflicts | crdt_conflict_test.cc | 20-25 |

**Total New Tests:** ~300-370
