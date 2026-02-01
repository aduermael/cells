# CRDT Full-State SET Operations & Resurrection Tests

Implement full-state SET operations for CRDT to ensure resurrection correctness, then verify with dedicated E2E tests.

## Background

### The Resurrection Problem

In a distributed system with eventual consistency, operations can arrive out of order:

1. **Peer A**: DELETE cell at t=1000
2. **Peer B**: SET cell with value+style at t=2000
3. **Arrival order on Peer C**: DELETE arrives first, then SET

With LWW semantics, the SET (t=2000) should win, resurrecting the cell with all its properties.

### Current Issue: Sparse Updates Lose Properties

The current sparse update approach only includes changed properties:
- `CELL_SET {t: "n", v: "100"}` - only value, no style/format
- If a resurrecting SET doesn't include style/format, those properties are **lost**

### Solution: Full-State SET Operations

Every SET operation includes all current properties of the entity:
- `CELL_SET {col, row, t, v, sty, fmt}` - always complete
- Self-sufficient operations that can correctly resurrect entities
- Content-addressed styles/formats keep payloads compact (~4-80 extra chars)

## Phase 1: Full-State CELL_SET

- [x] 1a: Update `makeCellSetOp` to always include current style/format
- [x] 1b: Update `bootstrapOpLog` cell serialization (should already be full-state)
- [x] 1c: Add unit test verifying CELL_SET payloads include all properties

## Phase 2: Full-State Axis SET

- [x] 2a: Update `makeColSetOp` to include all axis properties (pos, size if set, style, format, hidden)
- [x] 2b: Update `makeRowSetOp` similarly
- [x] 2c: Add unit tests for axis SET payload completeness

## Phase 3: Full-State Range SET

- [x] 3a: Update `makeRangeSetOp` to always include corners, flags, style, format
- [x] 3b: Add unit test for range SET payload completeness

## Phase 4: E2E Resurrection Test Suite

- [x] 4a: Create `collab-resurrection.test.mjs` with test harness
- [x] 4b: Test cell resurrection preserves value + style + format
- [x] 4c: Test axis resurrection preserves size/hidden/style
- [x] 4d: Test range resurrection preserves all properties
- [x] 4e: Test formula cell resurrection with dependencies

## Bug Fixes During Implementation

- Fixed: `applyRemoteOperation` now uses `STRUCTURE_CHANGED` notification type for axis/sheet operations (was always using `CELL_CHANGED`, causing viewport not to refresh after remote axis operations)
- Fixed: `resizeColumn`, `resizeColumnByPos`, `resizeRow`, `resizeRowByPos` now call `broadcastPendingOperations()` to sync axis resize operations to peers
