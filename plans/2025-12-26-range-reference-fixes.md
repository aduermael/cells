# Range Reference and Dependency Graph Fixes

```
Status: READY
Created At: 2025-12-26 22:24 UTC
Updated At: 2025-12-26 22:24 UTC
Following plan management guidelines defined in AGENTS.md
```

## Overview

This plan addresses two related issues with range references in the formula engine:

1. **#REF! error for cell ranges** - `=SUM(B1:B4)` returns #REF! even when cells have data
2. **Dependency graph for column/row refs** - `=SUM(B:B)` doesn't recalculate when new cells are added to column B

## Root Cause Analysis

### Issue 1: #REF! for Cell Ranges

The current implementation was partially fixed to use position bounds for rows, but testing shows it still fails. Need to investigate:
- Whether column lookup by name is working correctly
- Whether the position bounds are being set and read correctly
- Debug the actual evaluation path

### Issue 2: Dependency Graph for Column/Row References

When a formula like `=SUM(B:B)` is evaluated:
- It registers dependencies only on cells that currently exist in column B
- When a NEW cell is added to column B, there's no dependency link to trigger recalculation
- The formula should depend on the "column" itself, not just existing cells

---

## Phase 1: Debug and Fix #REF! Error

**Goal:** Understand why `=SUM(B1:B4)` still fails and fix it.

### Tasks

- [ ] 1a: Add debug logging to `evaluateRangeRef()` to trace column/row lookup
- [ ] 1b: Write a minimal test case that reproduces the #REF! error
- [ ] 1c: Identify the exact failure point (column lookup? row iteration? cell access?)
- [ ] 1d: Implement the fix based on findings
- [ ] 1e: Verify all formula_eval_test and fn_lookup_test pass
- [ ] 1f: Remove debug logging and clean up

---

## Phase 2: Dependency Graph for Range References

**Goal:** Make formulas with column/row references recalculate when new cells are added.

### Architecture

Current dependency tracking:
- `DependencyGraph` stores cell-to-cell dependencies
- When a cell changes, it looks up dependents and marks them dirty
- Column refs like `B:B` only register deps on existing cells

New approach:
- Add "column dependency" and "row dependency" tracking
- When iterating a column/row ref, register dependency on the column/row ID (not cells)
- When ANY cell in that column/row changes (including new cells), trigger recalc

### Tasks

- [ ] 2a: Add `columnDependents` and `rowDependents` maps to `DependencyGraph`
- [ ] 2b: Add `registerColumnDependency(cellId, columnId)` and `registerRowDependency(cellId, rowId)` methods
- [ ] 2c: Modify formula evaluation to call these when evaluating column/row refs
- [ ] 2d: Update `markDirty()` to also check column/row dependents when a cell changes
- [ ] 2e: Add tests for column dependency (new cell in column triggers recalc)
- [ ] 2f: Add tests for row dependency (new cell in row triggers recalc)
- [ ] 2g: Add tests for range dependency (B1:B10 should also register column B dependency)

---

## Phase 3: Range Reference Dependency Tracking

**Goal:** Cell ranges like `B1:B4` should also trigger recalc when cells within the range change.

### Tasks

- [ ] 3a: Add `rangeDependents` map to track (col, startRow, endRow) -> dependent cells
- [ ] 3b: Register range dependencies when evaluating `CELL_RANGE` references
- [ ] 3c: When a cell changes, check if it falls within any tracked range and mark those dirty
- [ ] 3d: Add tests for range-based dependency tracking

---

## Summary

| Phase | Focus | Key Files |
|-------|-------|-----------|
| 1 | Fix #REF! error | `formula_eval.cc` |
| 2 | Column/row dependencies | `dependency_graph.cc`, `formula_eval.cc` |
| 3 | Range dependencies | `dependency_graph.cc` |
