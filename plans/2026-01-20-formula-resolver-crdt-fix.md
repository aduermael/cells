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

- [x] 1a: Add `getColumnByPosition(pos)` - returns nullptr if not exists (already exists in sheet.cc:302-308)
- [x] 1b: Add `getRowByPosition(pos)` - returns nullptr if not exists (already exists in sheet.cc:311-317)
- [x] 1c: Add `getCellAtPosition(col_pos, row_pos)` - returns nullptr if not exists (added to model.h:443 and sheet.cc:149-160)
- [x] 1d: Verify these methods exist and work correctly - build passes

## Phase 2: Create FormulaResolver resolution result type

- [x] 2a: Define `RequiredEntities` struct containing:
  - `std::vector<PendingAxis>` columns to create (position, generated ID)
  - `std::vector<PendingAxis>` rows to create (position, generated ID)
  - `std::vector<PendingCell>` cells to create (colId, rowId, generated ID)
- [x] 2b: Add method `FormulaResolver::getRequiredEntities(ast)` that returns `RequiredEntities`
- [x] 2c: Modify `resolve()` to optionally skip entity creation (via existingOnly flag)

## Phase 3: Refactor callers to create entities via CRDT

- [x] 3a: Update `bindings_core.cc` cell editing path:
  - Call resolver to identify needed entities
  - Create column/row/cell ops via `applyOperation`
  - Then resolve AST with existing entities
  - Also added sheetId to PendingCell struct to track target sheet for cell creation
- [x] 3b: Update `bindings_formula.cc` getFormulaReferences (same pattern)
  - Removed snapshot-based workaround, now uses proper two-phase approach
- [x] 3c: Skip `bindings_file.cc` XLSX import - legacy mode appropriate for file loading
  - File import is not a collaborative operation; entities come from the file
  - bootstrapOpLog() handles converting to CRDT ops when collaboration starts
- [x] 3d: Update `fill_range.cc` getFillValueNonNumeric for formula resolution
  - Formula references in auto-fill now use CRDT-compliant two-phase approach
  - Note: axis/cell creation for target cells still uses direct methods (separate issue)
- [x] 3e: Update `luau_api.cc` setCell formula path (same pattern)
  - Formula setting via Luau scripts now uses CRDT-compliant two-phase approach
- [ ] 3f: DEFERRED - `formula_recalc.cc` spill expansion needs deeper refactor
  - Issue: calculateSpillRange() creates axes directly during recalculation
  - Challenge: Array size is dynamic (depends on inputs like =SEQUENCE(A1,B1))
  - Needs: Return required axes info from calculateSpillRange, caller creates via CRDT
  - This is a separate architectural issue from formula resolution input path
  - Tracked as separate work item to avoid blocking Phase 3 completion

## Phase 4: Make CRDT mode the default in FormulaResolver

- [x] 4a: Change resolve() default from existingOnly=false to existingOnly=true
  - CRDT-compliant mode is now the default for all callers
  - Legacy mode (existingOnly=false) only used for XLSX import (bindings_file.cc)
- [x] 4b: Update bindings_file.cc to explicitly use legacy mode (false)
  - Added comment explaining legacy mode is appropriate for file loading
- [x] 4c: Update bindings_core.cc createCell() to use CRDT-compliant two-phase approach
  - Same pattern as setCellValueAt: getRequiredEntities() -> applyOperation() -> resolve(true)
- [x] 4d: Update luau_types.cc cell.value setter to use CRDT-compliant approach
  - Formula setting via Luau scripts now creates entities via CRDT
- [x] 4e: Update all unit tests to explicitly pass false for legacy mode
  - Tests continue to use legacy mode for simplicity
  - Production code uses CRDT mode by default

## Phase 5: Testing

- [x] 5a: Add unit test verifying formula resolution creates CRDT operations
  - Added 10 tests to formula_resolver_test.cc for getRequiredEntities and existingOnly mode
  - Added 4 tests to sync_formula_test.cc for two-phase CRDT resolution workflow
- [x] 5b: Add E2E test verifying formula-created entities sync to peers
  - Added 2 tests to collab.test.mjs:
    - "Formula with new entity reference syncs" - tests value + formula sync
    - "Formula creates distant cell reference via CRDT" - tests entity creation sync
- [x] 5c: Run existing formula tests to verify no regressions
  - All formula tests pass (21/21)
  - All collab tests pass (6/6) including new CRDT entity sync tests
  - All sync_formula_test.cc tests pass

---

## Issue B: Local Peer Doesn't See Own Style After Collaboration

**Symptom:** After collaboration is enabled:
- Peer 1 applies a style (e.g., green background)
- Peer 2 sees the style correctly (sync works!)
- Peer 1 doesn't see their own style locally (BUG)

### Investigation Status

**Tests pass:**
- `collab-style-sync.test.mjs` - all style sync tests pass
- `style-before-collab.test.mjs` - styles before collab sync correctly
- `setCellStyleAt` code path correctly uses `applyOperation`

**Code flow is correct:**
1. C++ `applyOperation` applies style to model
2. C++ `notifyListeners("cell")` sends notification
3. Worker posts "dataChanged" message
4. JS `handleDataChanged` schedules `processDataChanges`
5. JS `fetchViewport()` queries WASM for fresh data
6. JS `render()` should display new style

**Possible causes (need investigation):**
- Race condition between worker messages
- Viewport fetch returning stale data in some cases
- State corruption after collaboration starts
- Missing viewport invalidation in some code path

### Reproduction Steps (from user)
1. Peer 1 sets yellow cell
2. Peer 1 creates collaboration link
3. Peer 2 joins, sees yellow cell
4. Peer 1 sets green cell
5. Peer 2 sees green cell - peer 1 doesn't

### Debug Approach

- [ ] Ba: Add console.log in JS `processDataChanges` to verify it runs
- [ ] Bb: Log viewport data after fetch to verify style is included
- [ ] Bc: Check if `notifyListeners` is being called after local style ops
- [ ] Bd: Test if issue is specific to certain style operations (bg color vs bold)
- [ ] Be: Check for any error suppression that might hide failures

---

## Notes

- The fix follows the established pattern from commit f3f8b82
- Goal: oplog becomes the single source of truth
- All entity creation must go through `applyOperation`
- FormulaResolver becomes a pure query/analysis tool
