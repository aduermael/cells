# Dependency Graph Optimization

Status: IN_PROGRESS
Created At: 2025-12-26 06:33 UTC
Updated At: 2025-12-26 19:24 UTC
Following plan management guidelines defined in AGENTS.md

## Overview

Optimize the dependency reverse lookup from O(n*m) to O(1) for cell references and O(log n) for range queries. Currently, `getDependents()` and `getDependentsWithRanges()` scan all formulas on every call, which will be a bottleneck for sheets with 1000+ formulas.

## Problem

Current implementation:
```cpp
// O(n*m) - iterates ALL formulas to find dependents
std::vector<ID> DependencyGraph::getDependents(const ID& cellId) const {
    for (const auto& [formulaCellId, refs] : dependencies_) {
        for (const auto& ref : refs) { ... }
    }
}
```

For a sheet with 10,000 formulas averaging 3 dependencies each:
- Current: ~30,000 comparisons per lookup
- Optimized: 1 hash lookup + 1 R-tree query

## Solution

1. **Reverse index** for cell-to-cell dependencies (O(1) lookup)
2. **R-tree integration** for range dependencies (O(log n) query)

---

## Phase 1: Add Reverse Index for Cell Dependencies

Add a reverse mapping from cells to the formulas that depend on them.

### 1a: Add reverse index data structure

```cpp
// In dependency_graph.h
class DependencyGraph {
private:
    // Existing: formula → [dependencies]
    std::unordered_map<ID, std::vector<DependencyRef>> dependencies_;

    // NEW: cell → [formulas that depend on this cell]
    std::unordered_map<ID, std::vector<ID>> reverseDeps_;
};
```

- [x] 1a: Add `reverseDeps_` member to DependencyGraph

### 1b: Update addFormula() to maintain reverse index

```cpp
void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast) {
    // ... existing code ...

    // NEW: Add to reverse index
    for (const auto& ref : refs) {
        if (ref.type == DependencyRef::Type::CELL) {
            reverseDeps_[ref.cellId].push_back(cellId);
        }
    }
}
```

- [x] 1b: Update addFormula() to populate reverseDeps_

### 1c: Update removeFormula() to maintain reverse index

```cpp
void DependencyGraph::removeFormula(const ID& cellId) {
    // Get old dependencies before removing
    auto it = dependencies_.find(cellId);
    if (it != dependencies_.end()) {
        for (const auto& ref : it->second) {
            if (ref.type == DependencyRef::Type::CELL) {
                auto& vec = reverseDeps_[ref.cellId];
                vec.erase(std::remove(vec.begin(), vec.end(), cellId), vec.end());
            }
        }
    }
    // ... existing removal code ...
}
```

- [x] 1c: Update removeFormula() to clean up reverseDeps_

### 1d: Update getDependents() to use reverse index

```cpp
std::vector<ID> DependencyGraph::getDependents(const ID& cellId) const {
    auto it = reverseDeps_.find(cellId);
    if (it != reverseDeps_.end()) {
        return it->second;  // O(1) lookup!
    }
    return {};
}
```

- [x] 1d: Rewrite getDependents() to use O(1) lookup

### 1e: Update clear() to reset reverse index

- [x] 1e: Add reverseDeps_.clear() to DependencyGraph::clear()

**Test**: All existing dependency_graph_test cases still pass

---

## Phase 2: Wire R-tree for Range Dependencies

The R-tree already exists but isn't populated. Use it for range queries.

### 2a: Populate R-tree in addFormula()

When adding a formula with a range dependency (e.g., `SUM(A1:C3)`), insert the range bounds into the R-tree.

```cpp
void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast) {
    // ... existing code ...

    for (const auto& ref : refs) {
        if (ref.type == DependencyRef::Type::RANGE) {
            // Need column/row positions - requires Sheet access or stored positions
            RTree::Rect rect{minCol, minRow, maxCol, maxRow};
            rtree_.insert(rect, cellId);
            cellRects_[cellId].push_back(rect);
        }
    }
}
```

**Challenge**: DependencyGraph doesn't have Sheet access to resolve cell IDs to positions.

**Note on Cell structure**: Cell has `colId` and `rowId` (IDs), not positions directly.
To get positions: `cell->colId` → `sheet->getColumn(colId)` → `axis->position`.
So Sheet access is still needed for the ID → position lookup.

**IMPORTANT**: Column/row moves change positions without changing formulas (formulas use UUIDs).
Any cached positions become stale on move operations. This rules out static caching.

**Options**:
1. ~~Pass positions to addFormula()~~ - ❌ Stale on column/row move
2. ~~Store positions in DependencyRef~~ - ❌ Same problem
3. **Pass Sheet* to DependencyGraph** - ✅ On-demand lookup, always current
4. **Invalidate R-tree on move** - ✅ Rebuild on move ops (moves are rare vs lookups)

- [ ] 2a: Choose approach and implement R-tree population

### 2b: Update removeFormula() to clean R-tree

Already partially implemented - just ensure cellRects_ tracking is correct.

- [ ] 2b: Verify R-tree cleanup in removeFormula()

### 2c: Add getDependentsInRange() integration

```cpp
std::vector<ID> DependencyGraph::getDependentsForCell(
    const ID& cellId, int32_t col, int32_t row) const {

    // 1. Direct cell deps from reverse index (O(1))
    std::vector<ID> result;
    auto it = reverseDeps_.find(cellId);
    if (it != reverseDeps_.end()) {
        result = it->second;
    }

    // 2. Range deps from R-tree (O(log n))
    auto rangeDeps = rtree_.queryPoint(col, row);
    result.insert(result.end(), rangeDeps.begin(), rangeDeps.end());

    return result;
}
```

- [ ] 2c: Implement combined cell + range dependency lookup

### 2d: Update formula_recalc.cc to use optimized lookup

Replace `getDependentsWithRanges()` with calls to the optimized DependencyGraph method.

- [ ] 2d: Update recalculation engine to use optimized lookups

**Test**: Range dependency tests still pass, performance improved

---

## Phase 3: Add Position Tracking

To make R-tree work, we need positions available in DependencyGraph.

### 3a: Extend addFormula() signature

```cpp
// New signature with position info for range refs
void addFormula(const ID& cellId, const ASTNode* ast,
                const std::vector<RangePosition>& rangePositions);

struct RangePosition {
    size_t refIndex;  // Which ref in the AST
    int32_t minCol, minRow, maxCol, maxRow;
};
```

- [ ] 3a: Add position-aware addFormula() overload

### 3b: Update Sheet::setCellFormula() to pass positions

The Sheet has access to resolve cell IDs to positions, so it can compute range bounds.

- [ ] 3b: Update Sheet to pass range positions to DependencyGraph

**Test**: Integration tests verify positions are tracked correctly

---

## Phase 4: Benchmarks and Verification

### 4a: Add performance benchmark

```cpp
// In dependency_graph_test.cc or separate benchmark file
TEST(DependencyGraphBenchmark, LargeSheetPerformance) {
    // Create 10,000 formulas with various dependencies
    // Measure getDependents() time before/after optimization
    // Target: <1ms for any single lookup
}
```

- [ ] 4a: Add benchmark test

### 4b: Verify memory overhead is acceptable

Reverse index adds memory: ~24 bytes per dependency (ID + vector overhead).
For 100K dependencies: ~2.4MB additional memory.

- [ ] 4b: Document memory trade-off

---

## Summary

| Lookup Type | Before | After |
|-------------|--------|-------|
| Cell → Dependents | O(n*m) | O(1) |
| Range contains cell | O(n*m) | O(log n) |
| Memory overhead | 0 | ~24 bytes/dep |

## Files to Modify

- `core/cells/dependency_graph.h` - Add reverseDeps_, new method signatures
- `core/cells/dependency_graph.cc` - Implement optimized lookups
- `core/cells/model.cc` - Pass positions to addFormula()
- `core/cells/formula_recalc.cc` - Use optimized DependencyGraph methods
- `core/cells/dependency_graph_test.cc` - Add benchmark tests

---

## Known Issues: Volatile Function Handling

### Issue 1: RAND() re-evaluated on every reference

**Symptom**:
- A1 contains `=RAND()`
- B1 contains `=A1`
- A1 and B1 show **different** values

**Expected**: B1 should show the same value as A1 since it references A1's result.

**Root cause**: The calculation engine re-evaluates A1's formula when resolving the reference from B1, instead of using the cached result from A1's cell value.

**Fix approach**: When evaluating a cell reference, use the cell's stored `value` if it's not dirty, rather than re-evaluating the formula. The evaluation should only happen once per recalculation cycle.

### Issue 2: Volatile cells recalculate on any sheet change

**Symptom**: Editing any unrelated cell causes A1 (`=RAND()`) and B1 (`=A1`) to both get new values.

**Expected behavior**: This is partially correct - volatile functions **should** recalculate on sheet changes. However:
1. The recalculation should happen **once** per edit cycle
2. Dependents should use the recalculated value, not trigger another recalculation

**Fix approach**:
1. Mark volatile cells dirty at the start of recalculation
2. Use topological sort to ensure volatile cells evaluate before their dependents
3. Store result in cell value after evaluation
4. Dependents read from stored value, not re-evaluate

### Related code

- `core/cells/formula_eval.cc` - `evaluateCellRef()` may be re-evaluating instead of using cached value
- `core/cells/formula_recalc.cc` - `recalculateVolatile()` handles volatile cell marking
- `core/cells/dependency_graph.cc` - `getVolatileCells()` tracks which cells use volatile functions
