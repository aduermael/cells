# Styled Ranges Fixes

Fix three issues with styled ranges:
1. Remove redundant `sheet_id` from RANGE_ADD operation payload
2. Remove redundant `sheet_id` from RANGE_SET_STYLE operation payload
3. Fix collaboration sync so styled ranges appear on peer 2

## Analysis

### Issues 1 & 2: Redundant sheet_id in Payloads

The `sheet_id` is included in both RANGE_ADD and RANGE_SET_STYLE payloads, but it's redundant because:
- RANGE_ADD already specifies `start_col_id`, `start_row_id`, `end_col_id`, `end_row_id`
- Columns and rows belong to a single sheet, so the sheet can be derived from any of these IDs
- RANGE_SET_STYLE operates on an existing range (by `range_id`), which already knows its sheet

**Files that create these payloads:**
- `apps/wasm/bindings_format.cc:2005-2010, 2017-2018, 2035, 2047-2048`
- `apps/wasm/bindings_core.cc:1703`

**Files that parse these payloads:**
- `core/cells/crdt_range.cc:56-100` (applyRangeAdd)
- `core/cells/crdt_range.cc:240-270` (applyRangeSetStyle)

### Issue 3: Styled Ranges Don't Sync in Collaboration

The `bootstrapOpLog()` function in `core/cells/crdt.cc:617-847` generates bootstrap operations for columns, rows, cells, formats, styles, and named ranges—but **not for ranges**. When peer 2 joins, it never receives RANGE_ADD or RANGE_SET_STYLE operations.

**Solution:** Add range bootstrapping to emit RANGE_ADD and RANGE_SET_STYLE operations for all existing ranges.

## Phase 1: Remove sheet_id from RANGE_ADD Payload

- [x] 1a: Update `applyRangeAdd` in `crdt_range.cc` to derive sheet from column/row IDs instead of extracting from payload. Used `workbook.findAxisSheet(startColId)` to derive sheet from start column ID.
- [x] 1b: Update RANGE_ADD payload creation in `bindings_format.cc` to not include sheet_id. Updated two locations (split ranges and main range creation).
- [x] 1c: Update RANGE_ADD payload creation in `bindings_core.cc` to not include sheet_id. Updated merge range creation.

## Phase 2: Remove sheet_id from RANGE_SET_STYLE Payload

- [x] 2a: Update `applyRangeSetStyle` in `crdt_range.cc` to get sheet from the existing range instead of extracting from payload. Now uses `workbook.getRange()` to find range, then derives sheet from `range->startColId`.
- [x] 2b: Update RANGE_SET_STYLE payload creation in `bindings_format.cc` to not include sheet_id. Updated two locations (split range style and main range style).

## Phase 3: Add Range Bootstrap for Collaboration Sync

- [x] 3a: Add RANGE_ADD operations to `bootstrapOpLog()` for all existing ranges. Iterates `sheet->getRangeIds()` and generates RANGE_ADD operations with the new payload format (no sheet_id).
- [x] 3b: Add RANGE_SET_STYLE operations to `bootstrapOpLog()` for ranges that have styles. Checks `workbook.getRangeStyleId(range->id)` and emits RANGE_SET_STYLE if not null.

## Phase 4: Fix Local Style Application Bug

**Problem**: After collaboration starts, new styled ranges don't show on the peer creating them. STYLE_DEFINE operations exist in the oplog but styles don't exist in the workbook.

**Analysis**: In `applyOperation()` (crdt.cc:383-384), operations are added to the oplog "regardless of result":
```cpp
// Add to OpLog regardless of result (for history/sync)
oplog->addOperation(op);
```

This is wrong. The correct flow should be:
- **Local ops**: Create → Apply → On SUCCESS, add to oplog → Broadcast
- **Remote ops**: Receive (pending) → Apply → On SUCCESS, add to oplog

**Fix**: Only add to oplog after successful application.

- [x] 4a: Modify `applyOperation()` to only add to oplog on successful apply (SUCCESS, SUPERSEDED, RESURRECTED). Changed `crdt.cc` to check result before adding to oplog.
- [x] 4b: Review all callers of `applyOperation()` to ensure they handle failure cases appropriately. Reviewed - existing callers either check result explicitly or are local ops that should succeed.
- [x] 4c: Add tests for the scenario: bootstrap → create styled range → verify style exists in workbook. Added 4 tests to `crdt_test.cc`: StyleDefineCreatesStyleInWorkbook, StyleDefineAddedToOpLogOnSuccess, DuplicateStyleDefineNotAddedToOpLog, FailedOperationNotAddedToOpLog.

## Phase 5: Testing

- [ ] 5a: Build and verify no compilation errors
- [ ] 5b: Manual test: create styled range, export file, verify no sheet_id in RANGE_ADD/RANGE_SET_STYLE payloads
- [ ] 5c: Manual test: create styled range, share collaboration link, verify peer 2 sees the styled range

## Phase 6: Fix All Lint Warnings, Checks, and Tests

- [ ] 6a: Run C++ linter and fix all warnings (including pre-existing)
- [ ] 6b: Run TypeScript/JavaScript linter and fix all warnings (including pre-existing)
- [ ] 6c: Run all unit tests and fix any failures
- [ ] 6d: Run all e2e/collaboration tests and fix any failures
