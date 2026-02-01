# Omit Default Axis Sizes from CRDT Operations

## Problem

When creating columns/rows, the CRDT operations always include the size field:

```
O 1769967510272.0.83Weacis COL_SET mCIDLa29 {"pos":1,"size":100}
O 1769967510273.0.83Weacis ROW_SET R9zR53NN {"pos":1,"size":24}
```

This conflates two different semantics:
1. **Unset size** - "use your local default" (could differ per client)
2. **Explicitly set to 100** - "this is 100, all clients must use 100"

### Example Scenario

- User A has default column width = 100
- User B has default column width = 200
- User A creates a column (no explicit size) → should show as 100 for A, 200 for B
- User A explicitly sets width to 100 → should show as 100 for both A and B

### Solution

Add a `SIZE_SET` flag to `AxisFlags` (bit 4 is available) to track whether size was explicitly set.

## Phase 1: Add SIZE_SET Flag to AxisFlags

- [x] 1a: Add `SIZE_SET = 1 << 4` to `AxisFlags` enum in `model.h`
- [x] 1b: Add `sizeSet()` and `setSizeSet(bool)` accessor methods to `Axis` struct
- [x] 1c: Fix inconsistent default row height in `crdt_axis.cc` (uses 21, should use `DEFAULT_ROW_HEIGHT` = 24)

## Phase 2: Update CRDT Operation Generation

- [x] 2a: Update `luau_api.cc` to not include size when creating axes (size not explicitly set)
- [x] 2b: Update `fill_range.cc` to not include size when creating axes (size not explicitly set)
- [x] 2c: Update `crdt.cc` `bootstrapOpLog()` to only include size when `sizeSet()` is true

## Phase 3: Update CRDT Operation Application

- [x] 3a: Update `crdt_axis.cc` `applyColSet()` to set `SIZE_SET` flag when payload contains "size"
- [x] 3b: Update `crdt_axis.cc` `applyRowSet()` to set `SIZE_SET` flag when payload contains "size"

## Phase 4: Update Resize Operations

- [x] 4a: Ensure column/row resize UI operations set the `SIZE_SET` flag
- [x] 4b: Ensure resize CRDT operations always include size (since it's explicit)

## Phase 5: Add Tests

- [x] 5a: Test that newly created axes have `sizeSet() == false`
- [x] 5b: Test that explicitly resized axes have `sizeSet() == true`
- [x] 5c: Test that CRDT payloads omit size when `sizeSet() == false`
- [x] 5d: Test that CRDT payloads include size when `sizeSet() == true`
- [x] 5e: Test cross-client scenario: unset size uses local default, explicit size propagates
