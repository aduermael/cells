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

- [ ] 1a: Update `applyRangeAdd` in `crdt_range.cc` to derive sheet from column/row IDs instead of extracting from payload
- [ ] 1b: Update RANGE_ADD payload creation in `bindings_format.cc` to not include sheet_id
- [ ] 1c: Update RANGE_ADD payload creation in `bindings_core.cc` to not include sheet_id

## Phase 2: Remove sheet_id from RANGE_SET_STYLE Payload

- [ ] 2a: Update `applyRangeSetStyle` in `crdt_range.cc` to get sheet from the existing range instead of extracting from payload
- [ ] 2b: Update RANGE_SET_STYLE payload creation in `bindings_format.cc` to not include sheet_id

## Phase 3: Add Range Bootstrap for Collaboration Sync

- [ ] 3a: Add RANGE_ADD operations to `bootstrapOpLog()` for all existing ranges
- [ ] 3b: Add RANGE_SET_STYLE operations to `bootstrapOpLog()` for ranges that have styles

## Phase 4: Testing

- [ ] 4a: Build and verify no compilation errors
- [ ] 4b: Manual test: create styled range, export file, verify no sheet_id in RANGE_ADD/RANGE_SET_STYLE payloads
- [ ] 4c: Manual test: create styled range, share collaboration link, verify peer 2 sees the styled range
