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

- [ ] 2a: Add spill spatial index to Sheet (use RangeIndex R-tree for spill bounding boxes)
- [ ] 2b: Index spill masters by their spill extent (col1, row1, col2, row2)
- [ ] 2c: Replace O(n) cell iteration in `bindings_viewport.cc:517-615` with R-tree viewport query
- [ ] 2d: Add `onSpillCreated()`, `onSpillRemoved()` to maintain spill index
- [ ] 2e: Unit tests for spill spatial queries
- [ ] 2f: Benchmark: verify O(log n + k) where k = spills intersecting viewport

### Phase 3: Fix O(n) Column/Row JSON Output

- [ ] 3a: Change `bindings_viewport.cc:621-666` to use ViewportIndex position range instead of iterating all axes
- [ ] 3b: Use `getColumnAt(position)` for each position in viewport range
- [ ] 3c: Same optimization for rows
- [ ] 3d: Verify O(k log n) where k = visible columns/rows

### Phase 4: Move AxisIndex to Sheet Level

Architectural change to eliminate rebuild on sheet switch.

- [ ] 4a: Add `AxisIndex _columnIndex` and `AxisIndex _rowIndex` to Sheet class
- [ ] 4b: Update Sheet to maintain AxisIndex on `insertColumn()`, `deleteColumn()`, `resizeColumn()`, `moveColumn()`
- [ ] 4c: Same for row operations
- [ ] 4d: Modify ViewportIndex to reference Sheet's AxisIndex (not own copies)
- [ ] 4e: Update `rebuildViewportIndex()` to only rebuild cell HashMap (O(k) for cells, not O(n) for axes)
- [ ] 4f: Remove duplicate AxisIndex from ViewportIndex
- [ ] 4g: Unit tests for Sheet-owned AxisIndex
- [ ] 4h: Tests pass: `make test` and `npm run test:stable`

### Phase 5: Fix O(n) in onAxisInserted

Now that Sheet owns AxisIndex, insertion position is known.

- [ ] 5a: Modify `onAxisInserted()` to receive tree position directly (Sheet knows it from OSTree insertion)
- [ ] 5b: Remove the O(n) loop that counts axes with smaller positions
- [ ] 5c: Update all callsites
- [ ] 5d: Verify O(log n) axis insertion at 100K rows

### Phase 6: Validation

- [ ] 6a: Re-run performance benchmarks from Phase 1
- [ ] 6b: Verify all viewport operations are O(log n) or better at 100K rows
- [ ] 6c: Full test pass: `make test` and `npm run test:stable`
- [ ] 6d: Update docs/rendering.md with final architecture

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
