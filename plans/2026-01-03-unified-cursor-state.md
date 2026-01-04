Status: COMPLETED
Created At: 2026-01-03 23:17 UTC
Updated At: 2026-01-04 04:17 UTC
Following plan management guidelines defined in AGENTS.md

## Bug Fixes (during Phase 3)

Two bugs were discovered and fixed during Phase 3 testing:

### Bug 1: Edition continues in formula bar instead of cell after reference insertion
- **Cause**: When clicking canvas to insert reference, browser focus change could trigger formula bar's focus handler, changing state from CELL_EDITING to FORMULA_BAR_EDITING
- **Fix**: Added `activeEditor` field to EditingSession to track which editor initiated the session. Reference insertion now uses this to determine where to insert.

### Bug 2: Cursor resets to 0 when editing in formula bar and clicking to insert reference
- **Cause**: Same root cause - state corruption from focus changes
- **Fix**: Same fix - using EditingSession.activeEditor ensures cursor position from the correct editor is used

## Commands

| Task | Command |
|------|---------|
| Build WASM | `make wasm-dist` |
| Unit tests (C++) | `make test` |
| TypeScript unit tests | `cd apps/wasm && npm test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| TypeScript check | `make check-types` |

---

# Unified Cursor State for Cell Editing

## Problem Statement

The current cursor positioning implementation is fragmented across multiple components:

1. **Duplicated cursor tracking** - Both `CellEditor` and `FormulaBarEditor` maintain identical `lastKnownCursorPos` / `lastKnownValue` state
2. **No single source of truth** - Cursor position updated in 5+ different places (input events, selectionchange, reference insertion, autocomplete, colorization)
3. **Focus/blur side effects** - Losing focus can reset cursor position unexpectedly
4. **Sync failures** - Four elements must stay synchronized (cellEditorInput, cellDisplay, formulaInput, formulaDisplay)

## Solution

Create a centralized `EditingSession` state object that:
- Maintains the single source of truth for what cell is being edited and cursor position
- Is shared between CellEditor and FormulaBarEditor
- Persists cursor position across focus changes
- Provides clean APIs for updating cursor and inserting content

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     EditingSession                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  sheetId: string                                      │  │
│  │  col: number                                          │  │
│  │  row: number                                          │  │
│  │  value: string                                        │  │
│  │  cursorStart: number                                  │  │
│  │  cursorEnd: number  (same as start when no selection) │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  Methods:                                                   │
│  - setCursor(start, end?)                                   │
│  - insertAt(position, text) -> newCursorPosition            │
│  - replaceRange(start, end, text) -> newCursorPosition      │
│  - getValue() / setValue(text)                              │
│  - getSelection() -> { start, end }                         │
└─────────────────────────────────────────────────────────────┘
           │                              │
           ▼                              ▼
    ┌─────────────┐              ┌─────────────────┐
    │ CellEditor  │              │ FormulaBarEditor│
    │  (renders)  │◄────sync────►│    (renders)    │
    └─────────────┘              └─────────────────┘
```

### Key Behaviors

1. **State changes when:**
   - Starting to edit a new cell → new session created
   - Inserting content (e.g., clicking cell reference) → `insertAt()` updates value and cursor
   - User types → `setValue()` + `setCursor()` called from input handler
   - Edition is committed or cancelled → session cleared

2. **Focus/blur does NOT change state:**
   - Losing focus on cellDisplay or formulaDisplay does NOT reset cursor
   - When regaining focus, cursor is restored from session state
   - Focus changes between cell and formula bar share the same session

3. **Rendering:**
   - CellEditor and FormulaBarEditor subscribe to session changes
   - When session updates, both renderers sync their displays
   - Cursor position is applied from session state after rendering

---

## Phase 1: Create EditingSession Module

- [x] 1a: Create `editing-session.ts` with core state interface and class
  - Define `EditingSessionState` interface
  - Create `EditingSession` class with state management
  - Implement `setCursor()`, `insertAt()`, `replaceRange()`, `getValue()`, `setValue()`, `getSelection()`
  - Add event emitter for state change notifications
  - Export singleton or factory function

- [x] 1b: Add TypeScript unit tests for EditingSession
  - Test session creation with initial state
  - Test `setCursor()` updates both start and end
  - Test `insertAt()` inserts text and updates cursor correctly
  - Test `replaceRange()` replaces selection and updates cursor
  - Test `setValue()` preserves cursor when possible
  - Test event notifications fire on state changes
  - Test session clear resets all state

---

## Phase 2: Integrate EditingSession into CellEditor

- [x] 2a: Refactor CellEditor to use EditingSession
  - Remove `lastKnownCursorPos` and `lastKnownValue` from CellEditor
  - Import and use EditingSession
  - Update `startEditing()` to create/initialize session
  - Update `confirmEditing()` and `cancelEditing()` to clear session
  - Update `getValue()` and `setValue()` to delegate to session

- [x] 2b: Refactor cursor tracking in CellEditor
  - Update `input` event handler to sync cursor to session
  - Update `selectionchange` handler to sync cursor to session
  - Update `insertReferenceAtCursor()` to use session's `insertAt()`
  - Remove direct cursor position passing to colorizer callbacks

- [x] 2c: Refactor focus/blur handling in CellEditor
  - On blur: do NOT reset cursor, keep session state
  - On focus: restore cursor position from session
  - Update `focusEditor()` to apply cursor from session after focusing

---

## Phase 3: Integrate EditingSession into FormulaBarEditor

- [x] 3a: Refactor FormulaBarEditor to use shared EditingSession
  - Remove `lastKnownCursorPos` and `lastKnownValue` from FormulaBarEditor
  - Use same EditingSession instance as CellEditor
  - Update focus handler to sync display from session
  - Update input handler to sync changes to session

- [x] 3b: Implement two-way sync between editors via session
  - Two-way sync already implemented via imperative approach:
    - CellEditor input handler syncs to EditingSession AND updates formulaInput/formulaDisplay
    - FormulaBarEditor input handler syncs to EditingSession AND updates cellEditorInput/cellDisplay
  - Event subscription available in EditingSession for future use if needed
  - Cursor position preserved through EditingSession as single source of truth

- [x] 3c: Refactor focus/blur handling in FormulaBarEditor
  - On blur with suppression: cursor stays in EditingSession (not reset)
  - On focus: cursor position restored from session (setCursorPosition in focus handler)
  - Removed redundant lastKnownCursorPos and lastKnownValue fields (done in 3a)

---

## Phase 4: Fix Reference Insertion Flicker and Cursor

Root cause: `insertReferenceAtCursor` sets `textContent` (plain text) immediately,
then async colorization replaces it with `innerHTML` (colored). This causes:
1. Visible flicker (plain → colored)
2. Cursor position issues when element doesn't have focus

Solution: Don't update display elements until colorized HTML is ready.
Only update EditingSession and hidden inputs synchronously.

- [x] 4a: Refactor insertReferenceAtCursor to not update display elements
  - Removed `cellDisplay.textContent` and `formulaDisplay.textContent` updates
  - Only update `cellEditorInput.value` and `formulaInput.value` (hidden)
  - Async colorization handles all visible DOM updates
  - Cursor position passed to colorization for restoration
  - Applied to both CellEditor (cell-editor.ts) and FormulaBarEditor (header-editor.ts)

- [x] 4b: Ensure updateColoredDisplays handles unfocused elements correctly
  - Changed updateColoredDisplays in init.ts to:
    1. Update both innerHTML elements
    2. Focus the appropriate editor (based on EditingSession.activeEditor)
    3. Then restore cursor position
  - This ensures cursor works even when element lost focus during click

- [x] 4c: Apply same pattern to replaceReferenceAtPosition
  - Removed display element updates from both CellEditor and FormulaBarEditor
  - Async colorization handles visual updates

- [x] 4d: Handle input event colorization (typing)
  - Already handled: colorization is debounced and flicker is not noticeable during typing

---

## Phase 5: E2E Tests for Cursor Behavior

- [x] 5a: Add E2E tests for cursor persistence
  - Test: Arrow keys move cursor within cell (not navigation) when not at boundary
  - Test: Arrow keys navigate cells when cursor at boundary
  - Test: Up/Down arrow keys navigate cells during editing

- [x] 5b: Add E2E tests for formula reference insertion
  - Test: Click cell during formula editing inserts reference at cursor
  - Test: Multiple reference insertions maintain correct cursor positions
  - Test: Reference insertion then typing places cursor correctly
  - Test: Formula with SUM function and multiple clicks

- [x] 5c: Add E2E tests for focus transitions
  - Test: Typing in formula bar updates value
  - Test: Formula bar shows formula when cell selected
  - Test: Escape cancels edit and returns to canvas
  - Test: Enter commits edit and moves down
  - Test: Tab commits edit and moves right

---

## Phase 6: Cleanup and Final Verification

- [x] 6a: Remove deprecated cursor tracking code
  - Verified no remaining `lastKnownCursorPos` / `lastKnownValue` variables (already removed in earlier phases)
  - Verified cursor position parameters use EditingSession correctly
  - Updated updateColoredDisplays to skip innerHTML replacement during non-formula typing
    - Fixes race condition where fast typing caused cursor position corruption
    - Only updates innerHTML when colorization is needed (formulas) or cursor must be restored (reference insertion)

- [x] 6b: Final testing and documentation
  - All 53 E2E tests pass consistently
  - All 47 C++ unit tests pass
  - EditingSession API already has comprehensive JSDoc documentation
  - Updated test delays (50ms between keystrokes) to prevent timing issues during parallel test execution

---

## Test Strategy

### Unit Tests (TypeScript - new)

Location: `apps/wasm/tests/unit/editing-session.test.ts`

Tests:
1. Session initialization with cell coordinates
2. Cursor position get/set
3. `insertAt()` text insertion at various positions
4. `replaceRange()` selection replacement
5. `setValue()` with cursor preservation
6. Event emission on state changes
7. Session clear/reset

### E2E Tests (Playwright)

Location: `apps/wasm/tests/cursor.test.mjs`

Tests:
1. Cursor persistence across focus changes
2. Cell reference insertion cursor behavior
3. Formula bar <-> cell editor cursor sync
4. Autocomplete cursor positioning
5. Arrow key navigation within/between cells

---

## Files to Modify

### New Files
- `apps/wasm/src/editing-session.ts` - Core session state management
- `apps/wasm/tests/unit/editing-session.test.ts` - Unit tests
- `apps/wasm/tests/cursor.test.mjs` - E2E cursor tests

### Modified Files
- `apps/wasm/src/cell-editor.ts` - Use EditingSession, remove local cursor tracking
- `apps/wasm/src/header-editor.ts` - Use shared EditingSession
- `apps/wasm/src/formula-colorizer.ts` - Read cursor from session
- `apps/wasm/src/init.ts` - Wire up session between editors
- `apps/wasm/package.json` - Add unit test script if needed

---

## Success Criteria

1. Single source of truth for cursor position (EditingSession)
2. Focus/blur events do NOT reset cursor position
3. Cursor syncs correctly between cell editor and formula bar
4. Reference insertion places cursor correctly after inserted text
5. All existing E2E tests pass
6. New cursor-specific E2E tests pass
7. TypeScript unit tests provide coverage for EditingSession logic
