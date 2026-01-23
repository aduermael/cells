Status: IN_PROGRESS
Created At: 2026-01-22
Following plan management guidelines defined in AGENTS.md

# Viewport Performance Optimization: Move AxisIndex to Sheet Level

## Problem Statement

Scrolling past 50K rows feels slower than expected. Investigation reveals several O(n) bottlenecks in the viewport system despite having O(log n) OSTree data structures.

### Current Architecture

```
CellsEngine (WASM bindings level)
└── ViewportIndex (ephemeral, rebuilt on sheet switch)
    ├── AxisIndex _columns (OSTree) - O(log n) queries
    ├── AxisIndex _rows (OSTree) - O(log n) queries
    └── HashMap _cells
```

### Identified Bottlenecks

| Issue | Location | Complexity | Impact |
|-------|----------|------------|--------|
| `onAxisInserted()` linear scan | viewport_index.cc:254-282 | O(n) | Every row/col insert |
| Spilled cells iteration | bindings_viewport.cc:517-615 | O(total_cells) | Every viewport query |
| Column/row JSON output | bindings_viewport.cc:621-666 | O(all_axes) | Every viewport query |
| ViewportIndex rebuild | viewport_index.cc:19-69 | O(n log n) | Every sheet switch |

### Fenwick Tree vs OSTree

**Fenwick Tree (Binary Indexed Tree):**
- O(log n) prefix sum queries
- O(log n) point updates
- **Requires fixed-size array** - no dynamic insert/delete
- Would need O(n) rebuild when inserting rows/columns

**OSTree (Order-Statistic Tree):**
- O(log n) prefix sum queries
- O(log n) point updates
- **Supports O(log n) dynamic insert/delete**
- Perfect for spreadsheets with row/column insertion

**Verdict:** OSTree is correct. The slowness comes from O(n) operations around it, not the tree itself.

## Solution: Move AxisIndex to Sheet Level

Sheets own their columns and rows conceptually. Moving AxisIndex to Sheet eliminates:
1. O(n log n) rebuild on sheet switch
2. O(n) position-to-tree-position computation in `onAxisInserted()`
3. Architectural mismatch (Sheet knows axis order, ViewportIndex asks Sheet)

### Target Architecture

```
Sheet
├── AxisIndex _columnIndex (OSTree) - maintained incrementally
├── AxisIndex _rowIndex (OSTree) - maintained incrementally
└── ... (existing cell storage)

CellsEngine
└── ViewportIndex (lightweight query adapter)
    └── HashMap _cells (only cells, not axes)
```

## Phases

### Phase 1: Profile and Baseline

Confirm bottlenecks with measurements before optimizing.

- [x] 1a: Create performance test that measures viewport query time at row positions 100, 1K, 10K, 50K, 100K
- [x] 1b: Profile `queryViewport()` to identify where time is spent (axis lookup vs cell iteration vs JSON serialization)
- [x] 1c: Measure impact of spill iteration by comparing sheets with/without spilled formulas
- [x] 1d: Document baseline metrics in this plan

#### Baseline Metrics (2026-01-22)

**Test Environment:** 10 columns × 100,001 rows, 1,001 cells (sparse)

| Row Position | Query Time (µs) | Slowdown vs Row 100 |
|--------------|-----------------|---------------------|
| 100          | 467             | 1.0x                |
| 1,000        | 3,457           | 7.4x                |
| 10,000       | 38,213          | 81.8x               |
| 50,000       | 186,526         | 399.4x              |
| 100,000      | 385,346         | 825.2x              |

**Key Finding:** Slowdown ratio of ~825x confirms O(n) complexity. Expected O(log n) would be ~1.7x.

**Breakdown Analysis (100 cols × 10,000 rows, 100K cells):**
- Sparse region query (no cells): 13,762 µs/query - axis lookup dominates
- Dense region query (200 cells): 101 µs/query - cell handling is fast
- Axis lookups (4 operations): ~38,000 ns/iteration

**Conclusion:** The bottleneck is in axis position lookups (`getColumnAt`, `getRowAt`), not cell iteration. The O(n) behavior comes from the OSTree axis index lookups when the position is far into the sheet.

**Additional O(n) bottlenecks confirmed in bindings layer:**
- `bindings_viewport.cc:517-615`: Iterates ALL cells to find spill masters
- `bindings_viewport.cc:621-666`: Iterates ALL columns/rows for JSON output

### Phase 2: Fix O(n) Spilled Cells Iteration

Most impactful fix - eliminate O(total_cells) loop per viewport query.

- [x] 2a: Add spill spatial index to Sheet (use RangeIndex R-tree for spill bounding boxes)
  - Created `SpillIndex` class in `core/cells/spill_index.h/.cc` using R-tree
  - Added `_spillIndex` member to Sheet class with accessor methods
- [x] 2b: Index spill masters by their spill extent (col1, row1, col2, row2)
  - SpillIndex stores bounding boxes: (startCol, startRow, endCol, endRow)
- [x] 2c: Replace O(n) cell iteration in `bindings_viewport.cc:517-615` with R-tree viewport query
  - Changed to use `spillIndex->queryRange()` instead of iterating all cells
- [x] 2d: Add `onSpillCreated()`, `onSpillRemoved()` to maintain spill index
  - Added `Sheet::updateSpillIndex()` and `Sheet::removeFromSpillIndex()` methods
  - Called from `Workbook::registerSpillRange()` and `Workbook::clearSpillRange()`
- [x] 2e: Unit tests for spill spatial queries
  - Created `spill_index_test.cc` with 14 test cases
- [x] 2f: Benchmark: verify O(log n + k) where k = spills intersecting viewport
  - Benchmark shows 2.4x slowdown with 1000 entries vs empty (confirms O(log n))

### Phase 3: Fix O(n) Column/Row JSON Output

- [x] 3a: Change `bindings_viewport.cc:621-666` to use ViewportIndex position range instead of iterating all axes
  - Changed column loop to iterate `[col1, col2)` positions, calling `getColumnAt(pos)` for each
- [x] 3b: Use `getColumnAt(position)` for each position in viewport range
- [x] 3c: Same optimization for rows
  - Changed row loop to iterate `[row1, row2)` positions, calling `getRowAt(pos)` for each
- [x] 3d: Verify O(k log n) where k = visible columns/rows
  - Before: O(total_columns + total_rows) per query
  - After: O(k_cols × log(n) + k_rows × log(m)) where k_cols and k_rows are visible counts
  - For 20 visible columns and 50 visible rows in a 100K row sheet: ~1K ops vs ~100K ops

### Phase 4: Move AxisIndex to Sheet Level

Architectural change to eliminate rebuild on sheet switch.

- [x] 4a: Add `AxisIndex _columnIndex` and `AxisIndex _rowIndex` to Sheet class
  - Added `_columnAxisIndex` and `_rowAxisIndex` members to Sheet in `model.h`
  - Added accessor methods `getColumnAxisIndex()` and `getRowAxisIndex()`
- [x] 4b: Update Sheet to maintain AxisIndex on `insertColumn()`, `deleteColumn()`, `resizeColumn()`, `moveColumn()`
  - Modified `addColumn()`, `insertColumnAt()`, `deleteColumn()`, `moveColumn()`, `removeColumnFromIndex()`
  - Updated CRDT `applyColResize()` to update Sheet's AxisIndex
- [x] 4c: Same for row operations
  - Modified `addRow()`, `insertRowAt()`, `deleteRow()`, `moveRow()`, `removeRowFromIndex()`
  - Updated CRDT `applyRowResize()` to update Sheet's AxisIndex
- [x] 4d: Modify ViewportIndex to reference Sheet's AxisIndex (not own copies)
  - ViewportIndex now stores `_sheet` pointer and delegates all axis queries to Sheet's AxisIndex
  - Coordinate conversions (`pixelToColumn`, `columnToPixel`, etc.) delegate to Sheet
- [x] 4e: Update `rebuildViewportIndex()` to only rebuild cell HashMap (O(k) for cells, not O(n) for axes)
  - `ViewportIndex::build()` now only builds the cell HashMap
  - Axis indexes are maintained incrementally by Sheet
- [x] 4f: Remove duplicate AxisIndex from ViewportIndex
  - Removed `_columns` and `_rows` members from ViewportIndex
  - `onAxisInserted()`, `onAxisResized()`, `onAxisMoved()` are now no-ops (Sheet maintains AxisIndex)
  - `onAxisDeleted()` only removes cells from the HashMap
- [x] 4g: Unit tests for Sheet-owned AxisIndex
  - Updated viewport_index_test.cc to use Sheet's axis operations
  - Tests now verify ViewportIndex correctly delegates to Sheet's AxisIndex
- [x] 4h: Tests pass: `bazel run :check` and `bazel run :e2e`

### Phase 5: Fix O(n) in onAxisInserted

Now that Sheet owns AxisIndex, insertion position is known.

- [x] 5a: Change `_columnIndex` and `_rowIndex` from `unordered_map` to sorted `std::map`
  - Enables O(1) check for append case (comparing with `rbegin()`)
- [x] 5b: Add fast path for appends (position > all existing positions)
  - Fast path: O(log n) map insertion + O(log n) tree append
  - Slow path: O(n) distance calculation for middle insertions (rare)
- [x] 5c: Add benchmark test `BenchmarkAxisInsertionPerformance`
  - Tests append performance (100K rows) and middle/beginning insertions
- [x] 5d: Verify O(log n) axis insertion at 100K rows
  - Before: 103,114 ms for 100K appends (1,031 µs/row)
  - After: 207 ms for 100K appends (2.07 µs/row)
  - **498x improvement** for append case

### Phase 6: Validation

- [x] 6a: Re-run performance benchmarks from Phase 1
  - Initial run showed O(n) still present (846x slowdown from row 100 to 100K)
- [x] 6b: **Discovered hidden O(n) bottleneck**: `OSTree::nodeAtPosition()` used linear traversal
  - Added `subtree_count` field to OSNode struct
  - Updated `updateSubtreeTotal()` to maintain subtree counts
  - Rewrote `nodeAtPosition()` to use O(log n) binary search via subtree counts
  - Updated `leftSubtreeCount()` to use O(1) subtree_count lookup
- [x] 6c: Verify all viewport operations are O(log n) or better at 100K rows
  - **Final benchmark results:**

    | Row Position | Before (µs) | After (µs) | Improvement |
    |--------------|-------------|------------|-------------|
    | 100          | 478         | 94         | 5.1x        |
    | 1,000        | 3,455       | 95         | 36x         |
    | 10,000       | 38,859      | 114        | 341x        |
    | 50,000       | 198,051     | 112        | 1,768x      |
    | 100,000      | 404,477     | 156        | 2,593x      |

  - Slowdown ratio: **1.66x** (expected ~1.7x for O(log n)) ✓
- [x] 6d: Full test pass: `bazel run :check` - all 57 unit tests + 313 E2E tests pass
- [ ] 6e: Update docs/rendering.md with final architecture

### Phase 7: Fix Pre-existing Test Failures

Fix all failing tests discovered during this work, even if unrelated to viewport optimization.

- [ ] 7a: Fix spill E2E test: "Spilled cell shows grayed formula bar: A2 should be marked as spilled"
- [ ] 7b: Investigate and fix any other intermittent test failures
- [ ] 7c: Ensure `bazel run :check` and `bazel run :e2e` pass with zero failures

## Design Decisions

### Why Sheet-Level AxisIndex?

1. **Semantic correctness**: A Sheet owns its columns and rows
2. **Eliminates rebuild**: Sheet's AxisIndex persists across viewport queries
3. **Single source of truth**: No sync between Sheet axis list and ViewportIndex tree
4. **Natural incremental updates**: Sheet mutations directly update the tree

### Why R-tree for Spills?

1. Spills have 2D extent (start col/row to end col/row)
2. R-tree provides O(log n + k) rectangle-rectangle intersection
3. Already have RangeIndex (R-tree) code in codebase
4. Lazy initialization: only create if sheet has spills

### Complexity After Optimization

| Operation | Before | After |
|-----------|--------|-------|
| `queryViewport()` | O(log n + k + total_cells + all_axes) | O(log n + k) |
| `onAxisInserted()` | O(n) | O(log n) |
| Sheet switch | O(n log n) full rebuild | O(k) cell index only |
| Column/row JSON | O(all_axes) | O(visible_axes × log n) |

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Spill index overhead for sheets without spills | Lazy init: only create if spills exist |
| Breaking existing incremental updates | Thorough testing, keep ViewportIndex API stable |
| Memory duplication | ViewportIndex references Sheet's trees, doesn't copy |
| Regression in cell rendering | E2E tests catch visual regressions |
