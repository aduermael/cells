# CRDT Resurrection E2E Test Suite

Create dedicated E2E collaboration tests for CRDT resurrection scenarios - where a DELETE operation arrives before/after a SET operation, and the entity should be resurrected based on LWW (Last-Writer-Wins) semantics.

## Background

### The Resurrection Problem

In a distributed system with eventual consistency, operations can arrive out of order:

1. **Peer A**: DELETE cell at t=1000
2. **Peer B**: SET cell with value+style at t=2000
3. **Arrival order on Peer C**: DELETE arrives first, then SET

With LWW semantics, the SET (t=2000) should win, resurrecting the cell with its properties.

### Current Implementation Concern

The current sparse update approach only includes changed properties in SET operations:
- `CELL_SET {t: "n", v: "100"}` - only value, no style
- If a resurrecting SET doesn't include style/format, those properties may be lost

This test suite will:
1. Document current resurrection behavior
2. Identify property loss scenarios
3. Provide regression tests for future fixes

## Phase 1: E2E Test Infrastructure

- [ ] 1a: Create `collab-resurrection.test.mjs` with test harness setup
- [ ] 1b: Add helper functions for simulating out-of-order operations

## Phase 2: Cell Resurrection Tests

- [ ] 2a: Test basic cell resurrection (SET after DELETE, same peer)
- [ ] 2b: Test cell resurrection with style preservation
- [ ] 2c: Test cell resurrection with format preservation
- [ ] 2d: Test concurrent DELETE and SET from different peers (network delay simulation)

## Phase 3: Axis Resurrection Tests

- [ ] 3a: Test column resurrection after delete
- [ ] 3b: Test row resurrection after delete
- [ ] 3c: Test axis resurrection preserves size/hidden/style properties

## Phase 4: Range Resurrection Tests

- [ ] 4a: Test styled range resurrection after delete
- [ ] 4b: Test range resurrection preserves corner references and flags

## Phase 5: Complex Resurrection Scenarios

- [ ] 5a: Test formula cell resurrection (formula + dependencies)
- [ ] 5b: Test cascading resurrection (delete col, resurrect cell in that col)
- [ ] 5c: Test multi-entity resurrection ordering

## Expected Findings

This test suite will help determine if the current sparse-update approach causes property loss during resurrection, informing the decision on whether to switch to full-state SET operations.
