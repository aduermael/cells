# Plan: Fix FormulaResolver CRDT Architecture

**Date:** 2026-01-20

## Problem

FormulaResolver can create entities (cells, columns, rows) directly without going through `applyOperation`, which violates the CRDT architecture established in commit f3f8b82.

### Current Issue

When FormulaResolver resolves references (e.g., when user types `=B2` in a formula), it calls Sheet methods that directly create entities:

| FormulaResolver Method | Direct Creation Calls |
|------------------------|----------------------|
| `resolveCellRef()` | `getOrCreateColumnByPosition()`, `getOrCreateRowByPosition()`, `getOrCreateCellAt()` |
| `resolveColumnRef()` | `getOrCreateColumnByPosition()` |
| `resolveRowRef()` | `getOrCreateRowByPosition()` |
| `resolveColumnRangeRef()` | `getOrCreateColumnByPosition()` (×2) |
| `resolveRowRangeRef()` | `getOrCreateRowByPosition()` (×2) |

These Sheet methods (`sheet.cc:286-361`) create entities via `std::make_unique<>` and add them directly, bypassing CRDT operations.

### Consequences
1. **Entities won't sync to collaborators** - remote peers never receive creation ops
2. **Oplog is incomplete** - replaying the oplog won't recreate these entities
3. **Architecture violation** - same pattern that was fixed for styles/formats in commit f3f8b82

### Affected Code Paths
- `bindings_core.cc:522-523` - cell editing with formulas
- `bindings_formula.cc:256-257` - formula operations
- `bindings_file.cc:160` - XLSX import
- `fill_range.cc:244-245` - auto-fill
- `luau_api.cc:315-316` - Luau scripts
- `formula_recalc.cc:503-511` - spill range expansion

---

## Solution Approach

Follow the same pattern established in commit f3f8b82 for styles/formats:

1. **Add lookup-only methods** to Sheet that do NOT create entities
2. **Refactor FormulaResolver** to use a two-phase approach:
   - Phase 1: Resolve AST and collect what needs to be created
   - Phase 2: Return creation info to caller
3. **Refactor callers** to create entities via CRDT operations before/after resolution

### Key Design Decision

FormulaResolver itself should NOT create entities. Instead:
- FormulaResolver should **collect** which entities need to be created
- The **caller** (bindings, fill_range, etc.) should create entities via `applyOperation`
- This maintains the architecture: all mutations go through CRDT

---

## Phase 1: Add lookup-only methods to Sheet

- [ ] 1a: Add `getColumnByPosition(pos)` - returns nullptr if not exists (already exists)
- [ ] 1b: Add `getRowByPosition(pos)` - returns nullptr if not exists (already exists)
- [ ] 1c: Add `getCellAtPosition(col_pos, row_pos)` - returns nullptr if not exists
- [ ] 1d: Verify these methods exist and work correctly

## Phase 2: Create FormulaResolver resolution result type

- [ ] 2a: Define `ResolvedEntities` struct containing:
  - `std::vector<PendingAxis>` columns to create (position, generated ID)
  - `std::vector<PendingAxis>` rows to create (position, generated ID)
  - `std::vector<PendingCell>` cells to create (colId, rowId, generated ID)
- [ ] 2b: Add method `FormulaResolver::getRequiredEntities(ast)` that returns `ResolvedEntities`
- [ ] 2c: Modify `resolve()` to optionally skip entity creation (via flag or separate method)

## Phase 3: Refactor callers to create entities via CRDT

- [ ] 3a: Update `bindings_core.cc` cell editing path:
  - Call resolver to identify needed entities
  - Create column/row/cell ops via `applyOperation`
  - Then resolve AST with existing entities
- [ ] 3b: Update `bindings_formula.cc` formula editing path (same pattern)
- [ ] 3c: Update `bindings_file.cc` XLSX import path (same pattern)
- [ ] 3d: Update `fill_range.cc` auto-fill path (same pattern)
- [ ] 3e: Update `luau_api.cc` Luau script path (same pattern)
- [ ] 3f: Update `formula_recalc.cc` spill expansion path (same pattern)

## Phase 4: Remove direct entity creation from FormulaResolver

- [ ] 4a: Remove calls to `getOrCreateColumnByPosition` from FormulaResolver
- [ ] 4b: Remove calls to `getOrCreateRowByPosition` from FormulaResolver
- [ ] 4c: Remove calls to `getOrCreateCellAt` from FormulaResolver
- [ ] 4d: Update tests to use new pattern

## Phase 5: Testing

- [ ] 5a: Add unit test verifying formula resolution creates CRDT operations
- [ ] 5b: Add E2E test verifying formula-created entities sync to peers
- [ ] 5c: Run existing formula tests to verify no regressions

---

## Secondary Issue: Background Color After Collaboration

User reports: "after collaboration started, I can't even locally keep setting background colors"

### Investigation Status
- Style sync tests (`collab-style-sync.test.mjs`) pass
- Style-before-collab tests pass
- `setCellStyleAt` code path looks correct (uses `applyOperation`)

### Next Steps
- [ ] Get more specific reproduction steps from user
- [ ] Check browser console for errors during style application
- [ ] Verify the specific scenario (single peer? multi-peer? which cells?)

This may be a UI/event handling issue rather than a CRDT issue.

---

## Notes

- The fix follows the established pattern from commit f3f8b82
- Goal: oplog becomes the single source of truth
- All entity creation must go through `applyOperation`
- FormulaResolver becomes a pure query/analysis tool
