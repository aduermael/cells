# Unified CRDT Operations

Simplify CRDT operation types from 26 to ~12 by using SET + DELETE pattern.

**Note: No backward compatibility needed.** App is not released, so we prioritize clean code over legacy support.

## Motivation

1. **CRDT resurrection semantics**: A SET after DELETE should revive the entity. Having INSERT as separate breaks this.
2. **Fewer operation types**: Easier to maintain, less code.
3. **Optional properties**: Only store what's explicitly set (e.g., no size unless user resizes).
4. **Unified property updates**: No need for separate CELL_SET_VALUE, CELL_SET_STYLE, CELL_SET_FORMAT.

## New Operation Types

| Type | Description |
|------|-------------|
| `CELL_SET` | Create/update cell (col, row, t, v, sty, fmt) |
| `CELL_DELETE` | Delete/clear cell |
| `COL_SET` | Create/update column (pos, size, name, sty, fmt, hidden) |
| `COL_DELETE` | Delete column |
| `ROW_SET` | Create/update row (pos, size, sty, fmt, hidden) |
| `ROW_DELETE` | Delete row |
| `SHEET_SET` | Create/update sheet (name, pos) |
| `SHEET_DELETE` | Delete sheet |
| `RANGE_SET` | Create/update range (corners, flags, sty, fmt) |
| `RANGE_DELETE` | Delete range |
| `WORKBOOK_SET` | Update workbook properties (name) |
| `NAMED_RANGE_SET` | Create/update named range |
| `NAMED_RANGE_DELETE` | Delete named range |

**Removed**: COL_INSERT, ROW_INSERT, COL_MOVE, ROW_MOVE, COL_RESIZE, ROW_RESIZE, COL_RENAME, AXIS_SET_HIDDEN, AXIS_SET_STYLE, AXIS_SET_FORMAT, CELL_SET_VALUE, CELL_SET_STYLE, CELL_SET_FORMAT, CELL_CLEAR, SHEET_CREATE, SHEET_RENAME, WORKBOOK_RENAME, NAMED_RANGE_DEFINE, RANGE_ADD, RANGE_REMOVE, RANGE_UPDATE_CORNERS, RANGE_UPDATE_FLAGS, RANGE_SET_STYLE, RANGE_SET_FORMAT

## Payload Conventions

- Only include properties being set (sparse updates)
- Shortened keys for common properties: `col`, `row`, `t` (type), `v` (value), `sty`, `fmt`, `pos`
- Empty string or null to clear a property

## Phase 1: Define New Operation Types
- [x] 1a: Update `OpType` enum in `operation.h` with new types
- [x] 1b: Update `opTypeToString` and `stringToOpType` in `operation.cc`
- [x] ~~1c: Legacy type mapping~~ (skipped - no backward compatibility needed)

## Phase 2: Update Operation Makers
- [x] 2a: Replace cell ops with unified `makeCellSetOp`
- [x] 2b: Replace col ops with unified `makeColSetOp`
- [x] 2c: Replace row ops with `makeRowSetOp`
- [x] 2d: Replace sheet ops with `makeSheetSetOp`
- [x] 2e: Replace range ops with unified `makeRangeSetOp`
- [x] 2f: Update workbook rename → `makeWorkbookSetOp`
- [x] 2g: Update named range → `makeNamedRangeSetOp`

## Phase 3: Update Operation Application
- [x] 3a: Implement `applyCellSet` - create cell if needed, update provided properties
- [x] 3b: Implement `applyColSet` - create column if needed, update provided properties
- [x] 3c: Implement `applyRowSet` - same pattern
- [x] 3d: Implement `applySheetSet` - create/update sheet
- [x] 3e: Implement `applyRangeSet` - create/update range
- [x] 3f: Update delete handlers to mark as deleted (allow resurrection)
- [x] 3g: Remove old apply functions

## Phase 4: Update Callers
- [x] 4a: Update `bindings.cc` to use new operation makers
- [x] 4b: Update `luau_api.cc` and `luau_types.cc`
- [x] 4c: Update test files to use new operation types

## Phase 5: Test Cleanup
- [x] 5a: Update `crdt_test.cc` - replace `CELL_SET_VALUE` with `CELL_SET`, fix payloads to use new format
- [x] 5b: Update `sync_manager_test.cc` - check for `CELL_SET` not `CELL_SET_VALUE`, fix payloads
- [x] 5c: Update `workbook_benchmark_test.cc` - use new operation types and payload formats
- [x] 5d: Update `crdt_range_test.cc` - adapt tests for unified SET semantics
- [x] 5e: Update `sync_formula_test.cc` - fix all payload formats
- [x] 5f: Update `fill_range.cc` - fix all payload formats
- [x] 5g: Run full test suite - all 331 E2E tests pass, all unit tests pass

## Phase 6: Final Cleanup
- [x] 6a: All code uses new unified operation types and payload formats
- [x] 6b: `bazel run :check` passes (all tests + format check)

## Summary

Successfully migrated from 26 operation types to ~12 unified SET + DELETE operations:
- Simplified payload format: `t`, `v`, `col`, `row`, `sty`, `fmt`, `pos`
- All tests pass (331 E2E + all unit tests)
- No backward compatibility needed (app not released)
