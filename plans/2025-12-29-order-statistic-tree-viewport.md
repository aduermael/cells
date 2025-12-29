Status: IN_PROGRESS
Created At: 2025-12-29 21:08 UTC
Updated At: 2025-12-29 22:30 UTC
Following plan management guidelines defined in AGENTS.md

# Replace Quadtree with Order-Statistic Tree for Viewport Queries

## Problem Statement

The current quadtree implementation has several issues:

1. **Full rebuild on every change**: `rebuildQuadtree()` is called 25+ times after various operations, each doing O(n) work
2. **Fixed logical coordinates**: The quadtree indexes by (col.position, row.position) which are logical indices, not pixel offsets
3. **No support for variable sizes**: Row/column pixel sizes are tracked separately in TypeScript with O(n) cumulative sums
4. **Memory inefficient for sparse grids**: Quadtree subdivides space even when mostly empty

## Solution: Order-Statistic Trees

Replace the quadtree with two Order-Statistic Trees (augmented red-black trees):
- **Column tree**: Maps cumulative pixel widths → column UUIDs
- **Row tree**: Maps cumulative pixel heights → row UUIDs

### Key Operations (all O(log n))

| Operation | Description |
|-----------|-------------|
| `pixelToColumn(x)` | Find which column contains pixel offset x |
| `pixelToRow(y)` | Find which row contains pixel offset y |
| `columnToPixel(colId)` | Get pixel offset of column's left edge |
| `rowToPixel(rowId)` | Get pixel offset of row's top edge |
| `resizeColumn(colId, newWidth)` | Update column width |
| `resizeRow(rowId, newHeight)` | Update row height |
| `insertColumn(colId, position, width)` | Insert column at position |
| `insertRow(rowId, position, height)` | Insert row at position |
| `deleteColumn(colId)` | Remove column |
| `deleteRow(rowId)` | Remove row |

### Memory Estimate

Per-node: ~48 bytes (UUID, size, subtree_total, 3 pointers, balance)
- 1M rows: ~48 MB
- 16K columns: ~768 KB
- Total: ~49 MB (acceptable for desktop/web app)

### Architecture Change

```
BEFORE:
  UI (TypeScript)                      WASM (C++)
  ┌─────────────────┐                 ┌─────────────────┐
  │ O(n) cumulative │  logical coords │ Quadtree        │
  │ sum loops       │ ──────────────► │ (rebuilt fully) │
  └─────────────────┘                 └─────────────────┘

AFTER:
  UI (TypeScript)                      WASM (C++)
  ┌─────────────────┐                 ┌─────────────────────────┐
  │ Direct pixel    │  pixel coords   │ Order-Statistic Trees   │
  │ coordinates     │ ──────────────► │ (incremental updates)   │
  └─────────────────┘                 │                         │
                                      │ Cells: HashMap by UUID  │
                                      └─────────────────────────┘
```

## Phases

### Phase 1: Core Order-Statistic Tree Implementation ✅

Implement the generic augmented red-black tree in C++.

- [x] 1a: Create `core/cells/ostree.h` with node struct and tree class declaration
- [x] 1b: Implement red-black tree insertion with subtree_total maintenance
- [x] 1c: Implement red-black tree deletion with subtree_total maintenance
- [x] 1d: Implement `findByOffset(pixel)` → returns node containing that pixel offset
- [x] 1e: Implement `getOffset(node)` → returns pixel offset of node's start
- [x] 1f: Implement `updateSize(node, newSize)` → update size and bubble up totals
- [x] 1g: Create `core/cells/ostree_test.cc` with comprehensive unit tests

### Phase 2: Axis Index Using Order-Statistic Tree

Create AxisIndex class that wraps the OS tree for column/row indexing.

- [ ] 2a: Create `core/cells/axis_index.h` with AxisIndex class
- [ ] 2b: Implement `insert(axisId, position, size)` - maintains order by position
- [ ] 2c: Implement `remove(axisId)`
- [ ] 2d: Implement `pixelToAxis(offset)` → returns (axisId, offsetWithinAxis)
- [ ] 2e: Implement `axisToPixel(axisId)` → returns pixel offset of axis start
- [ ] 2f: Implement `resize(axisId, newSize)`
- [ ] 2g: Implement `move(axisId, newPosition)` - reorder axis
- [ ] 2h: Create `core/cells/axis_index_test.cc` with unit tests

### Phase 3: ViewportIndex Replacing Quadtree

Create ViewportIndex that uses two AxisIndex instances (cols, rows) plus cell HashMap.

- [ ] 3a: Create `core/cells/viewport_index.h` with ViewportIndex class
- [ ] 3b: Implement `build(sheet)` - populate from sheet data
- [ ] 3c: Implement `queryViewport(x1, y1, x2, y2)` using pixel coordinates
- [ ] 3d: Implement incremental `onCellAdded/Removed/Changed`
- [ ] 3e: Implement incremental `onAxisInserted/Deleted/Resized/Moved`
- [ ] 3f: Create `core/cells/viewport_index_test.cc` with unit tests
- [ ] 3g: Verify tests pass: `bazel test //core/cells:viewport_index_test`

### Phase 4: Integrate into WASM Bindings

Replace quadtree usage in bindings.cc with ViewportIndex.

- [ ] 4a: Replace `Quadtree _quadtree` with `ViewportIndex _viewportIndex`
- [ ] 4b: Update `queryViewport()` to use pixel coordinates
- [ ] 4c: Replace `rebuildQuadtree()` calls with incremental updates where possible
- [ ] 4d: Keep `rebuildViewportIndex()` for full rebuilds (file load, sheet switch)
- [ ] 4e: Update WASM API to expose axis pixel queries (for TypeScript)
- [ ] 4f: Verify WASM builds: `bazel build //apps/wasm:cells_wasm`

### Phase 5: Update TypeScript Rendering

Remove O(n) cumulative sum loops, use WASM pixel queries instead.

- [ ] 5a: Add `getColumnPixelOffset(colPos)` and `getRowPixelOffset(rowPos)` to client API
- [ ] 5b: Update `grid-renderer.ts` to use pixel offsets from WASM
- [ ] 5c: Update `grid-utils.ts` helper functions
- [ ] 5d: Update `app-events.ts` coordinate conversion
- [ ] 5e: Update `cell-editor.ts` positioning
- [ ] 5f: Run E2E tests: `cd apps/wasm && npm run test:stable`

### Phase 6: Cleanup and Documentation

Remove quadtree code and document new architecture.

- [ ] 6a: Remove `core/cells/quadtree.h`, `quadtree.cc`, `quadtree_test.cc`
- [ ] 6b: Update BUILD file to remove quadtree targets
- [ ] 6c: Update `docs/rendering.md` with new viewport indexing architecture
- [ ] 6d: Final test pass: `bazel test //core/...` and `npm run test:stable`

## Design Decisions

### Why Red-Black Tree over AVL?
- Slightly faster insertions/deletions (fewer rotations)
- Good enough for lookups (both are O(log n))
- More common in standard libraries (easier to reference implementations)

### Why Not B+ Tree?
- Simpler implementation
- Memory overhead difference (~16 bytes/entry vs ~48 bytes) is negligible at our scale
- B+ tree advantages (cache locality, range scans) less important for point queries

### Cell Lookup Strategy
- Cells remain in `Sheet::cells` HashMap indexed by cell UUID
- ViewportIndex stores cell pointers, not copies
- No redundant cell storage

### Incremental vs Full Rebuild
- Most operations (cell edit, single resize) → incremental O(log n)
- Structural operations (insert/delete row/col) → incremental O(log n)
- File load, sheet switch → full rebuild O(n log n)
- This eliminates the 25+ `rebuildQuadtree()` calls that currently do O(n) each

## Testing Strategy

1. **Unit tests**: Each component (OS tree, AxisIndex, ViewportIndex) has dedicated tests
2. **Integration tests**: WASM bindings tested via existing E2E suite
3. **Performance tests**: Benchmark with 1M rows, 16K columns to verify O(log n) behavior
4. **Regression tests**: Existing E2E tests (`npm run test:stable`) must pass

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Red-black tree bugs | Extensive unit tests, compare with reference implementation |
| TypeScript coordinate mismatch | Keep old code path initially, A/B test |
| Performance regression | Benchmark before/after on large sheets |
| Memory increase | Acceptable at ~49MB; monitor in production |
