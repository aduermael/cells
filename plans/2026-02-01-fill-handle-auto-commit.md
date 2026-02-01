# Fill Handle Auto-Commit During Cell Editing

## Problem
When a user is typing a value in a cell and then tries to use the fill handle to fill cells, the fill operation doesn't work correctly because the value hasn't been committed yet (user hasn't pressed Enter or selected another cell). The fill handle should automatically commit the editing value before starting the fill drag operation.

## Solution
Modify the fill handle click handler in `mouse-events.ts` to check if the cell editor is active and commit the edit before initiating the fill drag. This matches the pattern already used for other operations like column resize.

## Phase 1: Fix Fill Handle to Auto-Commit Edit
- [x] 1a: Modify fill handle click handler to check for active editing and commit before fill drag. Added `startFillDrag` callback pattern matching the cell selection approach - checks for cell editing or formula bar editing and commits asynchronously before starting the fill drag operation.

The fix location is in `apps/wasm/src/mouse-events.ts` around line 800-819 in the fill handle click handler. The handler currently starts fill drag without checking if editing is in progress:

```typescript
// Fill handle click
if (this.isPointInFillHandle(x, y)) {
    const selStart = getSelectionStart();
    const { getSelectionEnd, setIsFillDragging } = this.config;
    const selEnd = getSelectionEnd();
    if (selStart && selEnd) {
        setIsFillDragging(true);
        // ...
    }
    // ...
}
```

Needs to be updated to match the pattern used elsewhere (e.g., column resize at lines 601-605):

```typescript
if (cellEditor.isEditing()) {
    cellEditor.confirmEditing();
} else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
    commitFormulaBarEdit();
}
```

Since `confirmEditing()` is async, the fill drag setup should happen in a `.then()` callback or after awaiting.

## Phase 2: Add E2E Test
- [ ] 2a: Create E2E test for fill handle with uncommitted cell value

Create `apps/wasm/tests/fill-auto-commit.test.mjs` that tests:
1. Select cell A1
2. Type a value (without pressing Enter)
3. Drag fill handle to A3
4. Verify A1, A2, A3 all have the typed value

Test should use the existing `dragFillHandle` helper from `helpers.mjs` and verify the behavior works correctly.
