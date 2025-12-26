# Collaboration Fixes

Status: IN_PROGRESS
Created At: 2025-12-25 23:41 UTC
Updated At: 2025-12-26 04:30 UTC
Following plan management guidelines defined in AGENTS.md

## Overview

Fix collaboration issues discovered after implementing the Formula AST Engine. The main symptom is that formulas entered on one client display incorrectly on other clients - showing UUID format (`=gDI8Hr7A`) instead of A1 notation (`=B1`).

### Root Cause Analysis

From investigating the sync architecture:

1. **Formula storage**: Formulas are stored in UUID format internally (e.g., `=~~gDI8Hr7A`)
2. **Sync flow**: Operations are synced with UUID-format formulas
3. **Display conversion**: `RefConverter.formulaToA1()` should convert UUID→A1 for display
4. **The bug**: When remote operations arrive, the RefConverter context may not be properly set, causing UUID→A1 conversion to fail

**Data flow**:
```
LOCAL: User types "=B1" → converts to "=~~cellUUID" → syncs to peers
REMOTE: Receives "=~~cellUUID" → should convert to "=B1" → FAILS, shows UUID
```

### Known Issues to Fix

1. **Formula display on remote clients** - Shows UUID instead of A1 (critical)
2. **RefConverter rebuild timing** - May need to rebuild after operations create new cells
3. **Potential race conditions** - UI may query before RefConverter is ready
4. **Missing tests** - Collaboration code has no automated tests

---

## Phase 1: Reproduce and Diagnose

First, add logging and tests to understand exactly where the conversion fails.

- [x] 1a: Add diagnostic logging to formula conversion path
  - Add log in `RefConverter::formulaToA1()` when conversion fails
  - Add log in `queryViewport()` showing formula text before/after conversion
  - Add log in `applyRemoteOperation()` showing operation details
  - **Test**: Run two clients, enter formula, observe logs on receiving client
  - **DONE**: Added LOG_INFO statements to ref_converter.cc and bindings.cc

- [x] 1b: Create minimal reproduction test case
  - Add integration test in `core/cells/sync_formula_test.cc`
  - Test: Apply SetCellValue operation with formula, verify A1 display
  - Test: Simulate two-client scenario with operation exchange
  - **Test**: Test case reliably reproduces the bug
  - **DONE**: Created sync_formula_test.cc with 7 tests covering formula sync

- [x] 1c: Identify exact failure point
  - Is RefConverter context empty/stale?
  - Is the cell UUID not found in the quadtree?
  - Is the formula text malformed?
  - Document findings in this plan
  - **DONE**: Root cause identified - cells created by formula resolution weren't synced

### Phase 1 Findings

**C++ tests pass**: The `sync_formula_test.cc` tests demonstrate that:
1. Formula operations sync correctly between two workbooks
2. UUID→A1 conversion works when RefConverter context is set
3. Conversion fails gracefully (returns UUID) when context is missing

**The bug is likely in the WASM/JS layer**, not C++. Possible causes:
1. RefConverter context not set before `queryViewport()` is called
2. Race condition: UI queries before `rebuildQuadtree()` completes
3. The referenced cell doesn't exist in the receiving client's sheet

**Next step**: Run manual test with two browsers to observe the debug logs.

### Root Cause Identified!

After code analysis, the root cause is:
1. When user types `=B1` in cell A2, `FormulaResolver::resolve()` is called
2. If cell B1 doesn't exist, `getOrCreateCellAt()` creates it with a new ID
3. This cell is created **directly in the model, NOT via operations**
4. The formula stores B1's ID in UUID format, and the formula operation syncs
5. **But cell B1 itself doesn't sync - no operation was generated for it!**
6. On the receiving client, the formula UUID can't be resolved because B1 doesn't exist

**The fix**: Generate operations for cells/axes created during formula resolution.

---

## Phase 2: Fix RefConverter Context

Based on diagnosis, fix the RefConverter context management.

- [x] 2a: Generate operations for cells created during formula resolution
  - When formula references empty cells, they get created by FormulaResolver
  - These cells were created directly in model, not via operations
  - **FIX**: In `getFormulaReferences()`, snapshot existing entities before resolution,
    then generate DIM_INSERT_AXIS and CELL_SET_VALUE operations for new entities
  - Operations are added to OpLog and broadcast to peers
  - **Test**: ✅ Manual testing confirmed - formulas display correctly on remote clients!

- [x] 2b: Fix formula resolution for remote operations
  - When receiving a formula operation, the AST needs to be re-resolved
  - The `formula.text` contains UUID format - need to parse and resolve
  - Ensure dependency graph is updated for remote formulas
  - **Test**: Formula references highlight correctly on remote client
  - **DONE**: Added `formula->parse()` call in `applyCellSetValue()` for remote operations
  - Also updates dependency graph with `addFormula()` and tracks volatile functions
  - Added 3 new tests: `DependencyGraphUpdatedOnRemoteFormulaSync`,
    `DependencyGraphClearedWhenFormulaReplacedWithValue`, `VolatileFunctionTrackedOnRemoteSync`

- [x] 2c: Handle cells created by formula references
  - Formula `=ZZ999` creates cell ZZ999 if it doesn't exist
  - This cell needs to be in RefConverter for display conversion
  - May need to rebuild RefConverter after formula resolution
  - **Test**: Remote formula referencing non-existent cell displays correctly
  - **DONE**: Already handled by Phase 2a (cells get operations) + existing `rebuildQuadtree()`
  - Operations are sorted by HLC, so axis/cell ops apply before formula ops
  - `rebuildQuadtree()` updates RefConverter after each operation
  - Existing test `FormulaReferencingNewCellDisplaysCorrectly` verifies this works

---

## Phase 3: Fix Operation Handling

Ensure operations are properly structured and handled.

- [x] 3a: Audit SetCellValue operation for formulas
  - Verify formula text is in correct UUID format in operation payload
  - Verify operation deserialization preserves formula correctly
  - Check if AST is serialized/deserialized or just the text
  - **Test**: Operation round-trip preserves formula exactly
  - **DONE**: Added `FormulaOperationRoundTrip` test verifying JSON and string round-trips

- [x] 3b: Fix any operation serialization issues
  - Formula text may need escaping in JSON
  - Special characters in formulas (`=`, `+`, etc.) may cause issues
  - **Test**: Complex formulas sync correctly (e.g., `=SUM(A1:B2)+C3*D4`)
  - **DONE**: Fixed bug in `extractJSONString()` - wasn't unescaping JSON strings
  - Added `jsonUnescape()` function to `crdt.cc` to properly handle escaped quotes/chars
  - Added tests: `ComplexFormulaWithSpecialCharsRoundTrip`, `FormulaWithMathOperatorsRoundTrip`,
    `NestedFunctionFormulaRoundTrip`

- [x] 3c: Ensure dependency graph updates on remote operations
  - When remote formula arrives, add to dependency graph
  - When remote formula changes, update dependencies
  - **Test**: Dependency highlights work on remote client
  - **DONE**: Already handled in Phase 2b - tests confirm it works

---

## Phase 4: Add Collaboration Tests

Add comprehensive tests to prevent regressions.

- [ ] 4a: Add sync round-trip tests for formulas
  - Test simple cell reference: `=A1`
  - Test range reference: `=SUM(A1:B2)`
  - Test absolute references: `=$A$1`
  - Test complex formula: `=IF(A1>0,B1,C1)`
  - **Test**: All formula types sync correctly

- [ ] 4b: Add multi-client simulation tests
  - Simulate two clients modifying same sheet
  - Test formula on client A, value on client B (that formula references)
  - Test concurrent edits to same cell
  - **Test**: All simulation tests pass

- [ ] 4c: Add RefConverter robustness tests
  - Test conversion with stale context
  - Test conversion with missing cells
  - Test conversion after column/row moves
  - **Test**: RefConverter handles edge cases gracefully

---

## Phase 5: UI Integration Fixes

Fix any UI-side issues discovered.

- [ ] 5a: Fix TypeScript formula display handling
  - Ensure `queryViewport()` returns A1 notation, not UUID
  - Handle fallback gracefully if conversion fails (show error, not UUID)
  - **Test**: UI never shows raw UUID to user

- [ ] 5b: Fix formula bar display for remote cells
  - When selecting cell modified by remote client, formula bar shows A1
  - Editing remote formula should work correctly
  - **Test**: Formula bar shows correct A1 notation for synced formulas

- [ ] 5c: Fix formula highlighting for remote cells
  - Selecting synced formula cell should highlight references
  - Reference colors should work correctly
  - **Test**: Highlights work for synced formulas

---

## Phase 6: Polish and Edge Cases

Handle remaining edge cases.

- [ ] 6a: Handle deleted cell references gracefully
  - If remote client deletes a cell that a formula references
  - Should show #REF! or similar error, not crash
  - **Test**: Deleting referenced cell shows error

- [ ] 6b: Handle sheet/workbook structure changes
  - Remote client adds/removes sheet
  - Cross-sheet references after sync
  - **Test**: Structure changes sync correctly

- [ ] 6c: Final integration testing
  - Manual testing with multiple browsers
  - Test all formula editing scenarios from Phase 8h checklist
  - Document any remaining issues
  - **Test**: All collaboration scenarios work correctly

---

## Testing Strategy

Each phase requires tests before proceeding:

1. **C++ unit tests**: `core/cells/sync_integration_test.cc`
2. **Manual testing**: Two browser windows in same room
3. **Regression**: Ensure existing formula tests still pass

**Run tests**: `bazel test //core/cells:all`

---

## Files Likely to Modify

| File | Purpose |
|------|---------|
| `apps/wasm/bindings.cc` | WASM bindings - `applyRemoteOperation()`, `queryViewport()` |
| `core/cells/ref_converter.cc` | UUID↔A1 conversion |
| `core/cells/operation.cc` | Operation serialization |
| `core/cells/sync_manager.cc` | Sync state management |
| `apps/wasm/src/client.ts` | TypeScript client API |
| `apps/wasm/src/grid-renderer.ts` | Cell rendering |
