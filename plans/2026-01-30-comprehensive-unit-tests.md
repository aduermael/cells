# Comprehensive C++ Unit Test Coverage Plan

This plan systematically expands unit test coverage for all supported operations. Each phase focuses on a specific domain, adding tests and fixing any bugs surfaced. Tests will be split into separate files when they exceed ~800 lines.

**Important:** All checks (`bazel run :check`) must pass at the end of each phase. This includes unit tests, linting (clang-tidy), type-checking, and formatting. If pre-existing issues are discovered, fix them as part of the phase.

---

## Testing Philosophy

**The purpose of unit tests is to discover bugs, not to document limitations.**

### Core Principles

1. **No Workarounds in Tests**: Tests must NEVER use workarounds that hide engine issues. Examples of forbidden patterns:
   - Setting `formula->dirty = true` to force re-evaluation
   - Manually calling internal functions to "fix up" state
   - Skipping assertions because "the engine doesn't support this yet"
   - Catching and ignoring errors that should not occur

2. **Tests Reflect Reality**: If a test fails, either:
   - The test expectation is wrong (fix the test)
   - The engine has a bug (fix the engine)
   - Never add workarounds to make a failing test pass

3. **Bugs Surface Fixes**: When tests reveal bugs:
   - Document the bug clearly in the plan
   - Create a dedicated phase to fix the bug if needed
   - Fix the engine, then verify the test passes naturally

4. **UUID-Based Architecture**: Everything should be indexed by UUID:
   - Sheets are identified by UUID, not name
   - Renaming a sheet should only update the name-to-UUID index
   - Sheet renames must have ZERO impact on formula resolution or evaluation

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

- [x] 7a: Test #VALUE! error from incompatible types (text in math operation) (10 tests)
- [x] 7b: Test #REF! error from deleted reference (6 tests)
- [x] 7c: Test #DIV/0! error from division by zero (7 tests)
- [x] 7d: Test #NAME? error from unknown function/named range (5 tests)
- [x] 7e: Test #NUM! error from invalid numeric operations (6 tests)
- [x] 7f: Test #N/A error from lookup failures (7 tests)
- [x] 7g: Test error propagation through formula chains (12 tests)
- [x] 7h: Test formula recovery after fixing source cell (auto-recalculate) (6 tests + 5 edge case tests = 11 tests)

---

## Phase 8: Formula Recalculation on Data Changes

New file: `formula_data_change_test.cc`

- [x] 8a: Test formula recalculates when direct dependency changes (9 tests)
- [x] 8b: Test formula recalculates when indirect dependency changes (chain) (7 tests)
- [x] 8c: Test formula recalculates when cell format changes affect type coercion (3 tests)
- [x] 8d: Test formula recalculates when referenced cell deleted then recreated (4 tests)
- [x] 8e: Test shared formula group recalculates correctly (3 tests)
- [x] 8f: Test array formula spill recalculates on source change (3 tests)
- [x] 8g: Test circular reference detection remains stable during changes (7 tests + 4 edge case tests)

---

## Phase 9: Cross-Sheet Operations

New file: `cross_sheet_operations_test.cc`

### Completed Tests
- [x] 9a: Test cross-sheet formula references (Sheet2!A1) - 9 tests
- [x] 9b: Test cross-sheet reference display updates when target sheet renamed - 7 tests (includes evaluation verification after Phase 16 fix)
- [x] 9c: Test cross-sheet reference becomes #REF! when target sheet deleted - 7 tests
  - Tests exposed two engine bugs that were fixed:
    1. `removeSheet` wasn't removing child entities (cells, columns, rows, ranges) from workbook storage
    2. `removeSheet` wasn't marking dependent formulas as dirty (using transitive marking via dependency graph)
  - Also fixed `evaluateCellRef` and `evaluateRangeRef` to return #REF! when resolved cellId is not found

### Remaining Steps
- [x] 9d: Test cross-sheet range references - 17 tests covering SUM/AVERAGE/MIN/MAX/COUNT functions, multi-column ranges, recalculation, sheet rename, sheet delete, and edge cases
- [x] 9e: Test cross-sheet named range references - 10 tests covering workbook-scoped named ranges pointing to other sheets, SUM/AVERAGE functions, recalculation, sheet rename, sheet/named-range delete, multi-column ranges, and usage from multiple sheets
  - Fixed 3 engine bugs discovered by tests:
    1. `evaluateNamedRef()` didn't set `targetSheet` on range results for cross-sheet ranges
    2. `Sheet::setCellFormula()` didn't pass named range registry to dependency graph
    3. `evaluateNamedRef()` didn't return #REF! when target sheet was deleted
- [x] 9f: Test copying formulas between sheets updates references correctly - 9 tests covering cross-sheet ref preservation during adjustment, range refs, absolute refs not adjusted, mixed absolute/relative, mixed local/cross-sheet refs, negative offsets becoming #REF!, and evaluation after adjustment
- [x] 9g: Test concurrent cross-sheet edits from multiple peers - 7 tests covering CRDT convergence (last-writer-wins), formula recalculation after remote ops, range formula updates, sheet rename preserves formula evaluation, multiple formulas updating together, chained formula recalculation, and bidirectional cross-sheet references

---

## Phase 10: Named Ranges Operations

New file: `named_ranges_operations_test.cc`

- [x] 10a: Test creating workbook-scoped named ranges - 4 tests (cell reference, range reference, cross-sheet access, multi-column range)
- [x] 10b: Test creating sheet-scoped named ranges - 3 tests (cell reference, not accessible from other sheet, same name different sheets)
- [x] 10c: Test named range in formulas (=SUM(MyRange)) - 7 tests (SUM, AVERAGE, MIN, MAX, COUNT, mixed with cell refs, arithmetic expressions)
- [x] 10d: Test named range boundaries update on insert/delete - 5 tests (insert column, insert row, delete cell, delete middle cell, delete both boundaries)
- [x] 10e: Test named range deletion causes #NAME? in formulas - 3 tests (workbook-scoped cell, workbook-scoped range, sheet-scoped)
- [x] 10f: Test renaming named ranges updates formula display - 3 tests (rename workbook-scoped, display shows name, display in function)
- [x] 10g: Test scope precedence (sheet scope overrides workbook scope) - 4 tests (sheet shadows workbook, workbook visible from other sheet, remove sheet falls back, range references)

---

## Phase 11: Cell Type Coercion

New file: `cell_type_coercion_test.cc`

- [x] 11a: Test number stored in text-formatted cell - 7 tests (numeric strings, implicit coercion, scientific notation, etc.)
- [x] 11b: Test text stored in number-formatted cell - 6 tests (text in number context, concat, empty string handling)
- [x] 11c: Test boolean stored in various format types - 8 tests (boolean coercion to number/string, in SUM/AVERAGE)
- [x] 11d: Test date/time value coercion - 5 tests (date serial arithmetic, time serial, date+time combined)
- [x] 11e: Test formula referencing mixed types - 8 tests (SUM/COUNT/COUNTA/AVERAGE/MIN/MAX with mixed types, error propagation)
- [x] 11f: Test VALUE(), TEXT(), NUMBERVALUE() coercion functions - 9 tests (VALUE with currency/percentage/invalid, TEXT with formats)
- [x] 11g: Test implicit coercion in arithmetic operations - 21 tests (add/multiply/divide with numbers/booleans/strings, comparisons, edge cases)

---

## Phase 12: Spill Range Operations

New file: `spill_operations_test.cc`

- [x] 12a: Test array formula creates spill range - 7 tests (SEQUENCE vertical/horizontal/2D, UNIQUE, SORT, FILTER, TRANSPOSE)
- [x] 12b: Test spill blocked by existing data (shows #SPILL!) - 5 tests (blocked by value/formula/string/2D, not blocked by same spill)
- [x] 12c: Test spill range updates when array size changes - 6 tests (expands, shrinks, changes shape, becomes blocked/unblocked, to single value)
- [x] 12d: Test inserting rows/columns in spill range - 2 tests (insert row/column clears spill for recalculation)
- [x] 12e: Test deleting rows/columns in spill range - 3 tests (delete row/column clears spill, delete master cell row)
- [x] 12f: Test overlapping spill ranges detection - 4 tests (non-overlapping, blocked by first spill, adjacent spills, horizontal non-overlapping)
- [x] 12g: Test spill master cell deletion clears spill range - 3 tests (delete master clears spill, clear formula removes spill, clear all removes multiple)
- [x] Additional tests: Spill reference (A1#) tests - 2 tests, edge cases - 4 tests (empty array, single element, sheet edge, mixed types, position reservation)

**Note:** Spill references (A1#) return ranges, but range iteration currently only looks at actual Cell objects. Spilled values (stored in SpillInfo) are virtual and don't create Cell objects. This is a known limitation documented in the tests.

---

## Phase 13: Complex Operation Sequences

New file: `operation_sequence_test.cc`

- [x] 13a: Test rapid insert/delete/move sequences maintain consistency - 8 tests
- [x] 13b: Test style + format + value changes in single operation batch - 5 tests
- [x] 13c: Test concurrent operations from 3+ peers converge correctly - 5 tests
- [x] 13d: Test undo/redo style operation sequences (if supported) - SKIPPED (undo/redo not implemented)
- [x] 13e: Test copy-paste operation sequences with formulas - 6 tests
- [x] 13f: Test fill operation sequences (fill down, fill right) - 8 tests

---

## Phase 14: Edge Cases and Boundary Conditions

New file: `edge_cases_test.cc`

- [x] 14a: Test operations on first column (A / position 0) - 7 tests (set value, formula, range sum, delete, insert before, style, cross-ref)
- [x] 14b: Test operations on first row (1 / position 0) - 7 tests (set value, formula, range sum, delete, insert before, style, cross-ref)
- [x] 14c: Test operations at large positions (column ZZ, row 10000) - 7 tests (large column/row positions, cell at large position, column name conversions)
- [x] 14d: Test empty cell references in formulas - 9 tests (arithmetic, SUM, AVERAGE, COUNT, COUNTA, multiplication, division, concatenation, IF)
- [x] 14e: Test very long text values - 8 tests (storage, concatenation, LEN, LEFT, RIGHT, MID, Unicode, empty string)
- [x] 14f: Test very large and very small numbers - 13 tests (large/small/negative numbers, max double, min positive, arithmetic, overflow, underflow, NaN, infinity)
  - **Bug fixed:** `CellValue(double)` constructor used `std::to_string()` which only has ~6 decimal digit precision. Very small numbers like 1e-15 were being rounded to 0. Fixed to use `snprintf("%.17g")` for full IEEE 754 double precision.
- [x] 14g: Test formula with maximum nesting depth - 6 tests (nested parentheses, nested functions, nested IF, formula chain, complex arithmetic, multiple arguments)
- [x] 14h: Test range spanning entire columns/rows - 8 tests (sum column, sum row, count, max, min, average, multi-column/row range, empty range)

---

## Phase 15: CRDT Conflict Resolution Verification

New file: `crdt_conflict_test.cc` (25 tests total)

- [x] 15a: Test Last-Writer-Wins for concurrent cell edits - 6 tests (higher wall_time wins, higher logical counter wins, node_id breaks tie, order independence, duplicate rejection)
- [x] 15b: Test concurrent axis operations resolve deterministically - 4 tests (column resize, row resize, column position, hide/show toggle)
- [x] 15c: Test concurrent range modifications merge correctly - 3 tests (style update LWW, creation with different IDs, delete vs modify LWW)
- [x] 15d: Test HLC ordering guarantees across peers - 4 tests (monotonic within peer, update on receive, total ordering, causal ordering)
- [x] 15e: Test operation replay produces identical state - 3 tests (same order, different order, mixed operation types)
- [x] 15f: Test late-joining peer catches up correctly - 6 tests (receives all operations, operations since HLC, concurrent edits while joining, three peers staggered join, structure creation ops, empty oplog)

---

## Phase 16: Fix Cross-Sheet Evaluation Bug

**Priority: HIGH** - This bug breaks fundamental UUID-based architecture guarantees.

Files to modify:
- `core/cells/formula_eval.cc`

### Bug Summary
Sheet renames break formula evaluation because `evaluateCellRef()` checks `sheetName` (from parsing) before checking `cellId` (from resolution). The fix is to prioritize `cellId` for resolved formulas.

### Steps
- [x] 16a: Fix `evaluateCellRef()` in `formula_eval.cc` to check `cellId` first
  - Changed priority order: cellId (UUID) → sheetId → sheetName (fallback)
- [x] 16b: Fix `evaluateRangeRef()` similarly (uses topLeft/bottomRight cell refs)
  - Same priority change for range reference evaluation
- [x] 16c: Verify all 16 existing cross-sheet tests pass without workarounds
- [x] 16d: Remove the temporary `SheetRename_FormulaBecomesRefError` test (it documents the bug, not correct behavior)
- [x] 16e: Add proper test: `SheetRename_EvaluationStillWorks` that passes naturally
  - Renamed and updated test to verify 77.0 result after sheet rename
- [x] 16f: Run full test suite to ensure no regressions (66 unit tests, 331 E2E tests pass)

### Verification
After this phase:
- Renaming a sheet must have ZERO impact on formula evaluation
- All cross-sheet formulas must resolve via UUID, not name
- The display system already works correctly (it uses `findCell` properly)

---

## Phase 17: Audit All Tests for Workarounds

**Purpose:** Ensure no tests added in this plan contain workarounds that hide engine bugs.

### Forbidden Patterns to Search For
```cpp
// Pattern 1: Forcing dirty flag
formula->dirty = true;

// Pattern 2: Manual state manipulation
cell->value = CellValue(...);  // Before evaluation to "fix" state

// Pattern 3: Ignoring expected failures
// EXPECT_... commented out with "known issue" notes

// Pattern 4: Try-catch hiding errors
try { ... } catch (...) { /* ignore */ }
```

### Files to Audit
- [x] 17a: `style_operations_test.cc` - ✅ No forbidden patterns found
- [x] 17b: `format_operations_test.cc` - ✅ No forbidden patterns found
- [x] 17c: `axis_insert_delete_test.cc` - ✅ No forbidden patterns found
- [x] 17d: `axis_move_resize_test.cc` - ✅ No forbidden patterns found
- [x] 17e: `range_boundary_test.cc` - ✅ No forbidden patterns found
- [x] 17f: `merged_cells_test.cc` - ✅ No forbidden patterns found
- [x] 17g: `formula_error_test.cc` - ✅ No forbidden patterns found
- [x] 17h: `formula_data_change_test.cc` - ✅ No forbidden patterns found
- [x] 17i: `cross_sheet_operations_test.cc` - ⚠️ See findings below
- [x] 17j: Other test files - See findings below

### Audit Findings

**Files with `->dirty = true` usage (10 instances in 6 files):**

#### Acceptable: Initialization Pattern (3 files)
These helper functions create raw formulas outside the normal sheet workflow, mirroring what `Sheet::setCellFormula` does internally:
- `serializer_test.cc:30` - `createFormula()` helper
- `xlsx_writer_test.cc:26` - `createFormula()` helper
- `formula_integration_test.cc:25` - `createFormula()` helper

#### Acceptable: Testing Recalculation Mechanics (1 file)
- `formula_recalc_test.cc:353` - Tests that volatile functions (NOW) can be manually triggered for re-evaluation

#### FIXED: Named Range Deletion (2 files, 7 instances)
~~These tests force `dirty = true` because **named range deletion doesn't mark dependent formulas dirty**~~ - **FIXED IN PHASE 18**

These workarounds have been removed. The dependency graph now tracks which formulas depend on which named ranges (by name), and `NamedRangeRegistry` uses a callback to notify `Workbook` when a named range is removed, which then marks dependent formulas dirty automatically.

The tests have been updated to:
1. Set `dirty = false` before removal
2. Assert `EXPECT_TRUE(formula->dirty)` after removal
3. Continue with the evaluation test

### Other Patterns Searched (None Found)
- ❌ `cell->value = CellValue(...)` - Manual state manipulation: NOT FOUND
- ❌ Commented-out EXPECT_ macros with "known issue" notes: NOT FOUND
- ❌ `try { ... } catch (...) { /* ignore */ }` patterns: NOT FOUND

### Action for Each File
1. Search for forbidden patterns
2. If found: determine if it's hiding a bug
3. If hiding a bug: create issue/phase to fix the engine
4. Remove the workaround and let the test fail until engine is fixed

---

## Phase 18: Named Range Dependency Tracking

**Priority:** LOW - Architectural improvement, not a critical bug
**Status:** COMPLETE

### Problem Statement

When a named range is deleted via `NamedRangeRegistry::removeWorkbook()` or `removeSheet()`, formulas that reference the named range are not automatically marked dirty. This means:

1. The formula continues to use its cached value until something else triggers re-evaluation
2. Tests must manually set `formula->dirty = true` to verify the expected #NAME! error behavior
3. In production, users might see stale values after deleting a named range

### Root Cause Analysis

The dependency graph (`DependencyGraph`) resolves named ranges at tracking time:

```cpp
// In dependency_graph.cc - ReferenceExtractor::extract()
case ASTNodeType::NAMED_REF: {
    const NamedRange* range = namedRegistry_->resolve(namedRef->name, sheetId_);
    if (range) {
        // Converts to underlying CELL or RANGE dependency
        // The named range NAME is not stored, only the resolved cell IDs
    }
}
```

This means:
- `=MyRange` is tracked as a dependency on cell `A1:B5` (whatever MyRange resolves to)
- The string "MyRange" is NOT stored in the dependency graph
- When "MyRange" is deleted, there's no way to find formulas that reference it by name

### Proposed Solution

Add a reverse mapping from named range names to dependent formula cell IDs.

#### Option A: Extend DependencyGraph (Recommended)

Files to modify:
- `core/cells/dependency_graph.h`
- `core/cells/dependency_graph.cc`
- `core/cells/named_ranges.h`
- `core/cells/named_ranges.cc`

Steps:
- [x] 18a: Add `namedRangeDependents_` map to DependencyGraph - Added map and reverse mapping `cellNamedRangeDeps_` for cleanup
- [x] 18b: In `addFormula()`, when processing NAMED_REF nodes, also record the name → cellId mapping - ReferenceExtractor now tracks named range keys during extraction
- [x] 18c: In `removeFormula()`, clean up named range dependencies - Both maps are properly cleaned up
- [x] 18d: Add `getDependentsForWorkbookNamedRange()` and `getDependentsForSheetNamedRange()` methods - Separate methods for each scope
- [x] 18e: Add removal callback to NamedRangeRegistry - Workbook sets callback in constructor to mark dependent formulas dirty
- [x] 18f: Update tests to remove manual `dirty = true` workarounds - 7 instances updated in 2 files, now verify automatic dirty marking
- [x] 18g: Add dedicated tests for named range deletion triggering automatic recalculation - 6 new tests added

#### Option B: Store in NamedRangeRegistry

Alternative approach - store dependents directly in the named range registry:
- Pros: Simpler, all named range logic in one place
- Cons: Duplicates some dependency tracking logic, harder to keep in sync

#### Complexity Considerations

1. **Scope handling**: Sheet-scoped names can shadow workbook-scoped names, so the mapping needs to account for `(name, sheetId)` pairs
2. **Rename handling**: Renaming a named range should transfer dependencies to the new name
3. **CRDT operations**: Named range operations come through CRDT, so dirty marking must happen at the right point in the apply flow

### Updated Tests

The manual `dirty = true` workarounds have been removed and replaced with assertions verifying automatic dirty marking:
- `cross_sheet_operations_test.cc`: Lines 1227, 1263 - Now verify `EXPECT_TRUE(b1->getFormula()->dirty)` after named range deletion
- `named_ranges_operations_test.cc`: Lines 733, 767, 799, 844, 977 - Same pattern

### New Tests Added (6 tests in named_ranges_operations_test.cc)

1. `AutomaticDirtyMarking_WorkbookScope_SingleFormula` - Single formula dirty marking
2. `AutomaticDirtyMarking_WorkbookScope_MultipleFormulas` - Multiple formulas referencing same name
3. `AutomaticDirtyMarking_SheetScope_OnlyAffectsCorrectSheet` - Sheet-scoped isolation
4. `AutomaticDirtyMarking_RangeReference` - Range references (SUM, etc.)
5. `AutomaticDirtyMarking_RemoveAllForSheet` - Bulk removal marks all dependents dirty
6. `AutomaticDirtyMarking_NoEffectOnUnrelatedFormulas` - Unrelated formulas not affected

### Verification

After this phase:
- ✅ Deleting a named range automatically marks all dependent formulas dirty
- ✅ Re-evaluating those formulas returns #NAME! without manual intervention
- ✅ No tests require `formula->dirty = true` workarounds for named range scenarios
- ✅ 72 unit tests pass, 331 E2E tests pass

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
| **16** | **Fix Cross-Sheet Bug** | **formula_eval.cc** | **Engine fix** |
| **17** | **Audit for Workarounds** | **All test files** | **Audit** |
| **18** | **Named Range Dependencies** | **dependency_graph.cc, named_ranges.cc, model.cc** | **Engine fix + 6 tests** |

**Total New Tests:** ~300-370

**Bugs Discovered:**
- Phase 9 (9a/9b): Cross-sheet evaluation uses `sheetName` instead of `cellId` for resolved formulas, breaking sheet rename (fixed in Phase 16)
- Phase 9 (9c): `removeSheet` wasn't removing child entities from workbook storage - cells, columns, rows, and ranges belonging to the deleted sheet remained in workbook-level maps (fixed in 9c)
- Phase 9 (9c): `removeSheet` wasn't marking dependent formulas as dirty - formulas referencing deleted cells would return stale cached values (fixed with transitive dependency marking in 9c)
- Phase 9 (9c): `evaluateCellRef` and `evaluateRangeRef` would return 0 instead of #REF! when a resolved cellId was not found (cell was deleted) (fixed in 9c)
- Phase 17 (audit): Named range deletion (`NamedRangeRegistry::removeWorkbook/removeSheet`) doesn't mark dependent formulas dirty - tests use manual `dirty = true` workaround (architectural limitation, fix planned in Phase 18)
