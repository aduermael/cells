Status: READY
Created At: 2026-01-03 23:17 UTC
Updated At: 2026-01-03 23:59 UTC
Following plan management guidelines defined in AGENTS.md

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

## Phase 4: Update Formula Colorizer Integration

- [ ] 4a: Simplify colorizer cursor handling
  - Update `applyColorizedFormula()` to read cursor from session
  - Remove cursor position parameters from colorizer callbacks
  - Colorizer restores cursor from session after updating innerHTML

- [ ] 4b: Update formula highlight updates
  - `onUpdateFormulaHighlights()` reads cursor from session
  - Remove cursor position parameters from async highlight functions
  - Ensure highlights don't cause cursor jumps

---

## Phase 5: E2E Tests for Cursor Behavior

- [ ] 5a: Add E2E tests for cursor persistence
  - Test: Start editing cell, click formula bar, cursor position preserved
  - Test: Type in formula bar, click back to cell, cursor preserved
  - Test: Insert cell reference, cursor positioned after reference
  - Test: Arrow keys move cursor within cell (don't navigate to adjacent cells until at boundary)

- [ ] 5b: Add E2E tests for formula reference insertion
  - Test: Click cell during formula editing, reference inserted at cursor
  - Test: Select range during formula editing, range reference inserted
  - Test: Multiple reference insertions maintain correct cursor positions

- [ ] 5c: Add E2E tests for focus transitions
  - Test: Blur cell editor by clicking outside, then refocus, cursor correct
  - Test: Edit in cell, switch to formula bar, switch back, cursor preserved
  - Test: Autocomplete selection preserves cursor after function name

---

## Phase 6: Cleanup and Final Verification

- [ ] 6a: Remove deprecated cursor tracking code
  - Remove any remaining `lastKnownCursorPos` / `lastKnownValue` variables
  - Remove cursor position parameters from internal methods
  - Update any remaining direct DOM cursor queries to use session

- [ ] 6b: Final testing and documentation
  - Run full E2E test suite
  - Verify all cursor edge cases work correctly
  - Add inline documentation for EditingSession API

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
