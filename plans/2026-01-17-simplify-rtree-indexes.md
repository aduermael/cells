# Simplify RTree Indexes

Remove `_mergeTrees` and `_styleTrees` from `RangeIndex`, keeping only the single `_rtree`. Post-retrieval filtering is a cheap O(k) operation where k is the number of ranges at a position, which is rarely large.

## Context

The `RangeIndex` class currently maintains 3 separate RTrees:
- `_rtree` - main tree containing ALL ranges
- `_mergeTrees` - only ranges with MERGE flag
- `_styleTrees` - only ranges with STYLE flag

The rationale was to optimize queries like `queryAt(col, row, RangeFlags::MERGE)` by avoiding post-filtering. However:
1. In practice, there are rarely many overlapping ranges at the same position
2. Post-filtering is O(k) where k is typically 1-5 ranges
3. Maintaining 3 trees adds complexity and memory overhead
4. Every insert/remove/update must touch up to 3 trees

## Phase 1: Remove Flag-Specific RTrees

- [x] 1a: Remove `_mergeTrees` and `_styleTrees` member variables from `RangeIndex` class in `range_index.h`
- [x] 1b: Remove `insertIntoFlagTrees()` and `removeFromFlagTrees()` helper methods
- [x] 1c: Update `insert()` to only insert into `_rtree`
- [x] 1d: Update `removeById()` to only remove from `_rtree`
- [x] 1e: Update `updateBounds()` to only update `_rtree`
- [x] 1f: Update `queryAt(col, row, RangeFlags)` to always use post-filtering (remove fast-path)
- [x] 1g: Update `queryRange(...)` with flags to always use post-filtering (remove fast-path)
- [x] 1h: Update `clear()` to only clear `_rtree` and `_bounds`

## Phase 2: Update Documentation and Tests

- [x] 2a: Update header comment in `range_index.h` to remove mention of flag-specific indices
- [x] 2b: Run existing tests to verify no regressions (`range_index_test.cc`) - All 54 unit tests pass
- [ ] 2c: Run E2E tests to verify viewport/rendering still works correctly
