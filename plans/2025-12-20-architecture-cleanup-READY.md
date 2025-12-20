# Architecture Cleanup Plan

This plan addresses three key areas:
1. **Listener Pattern** - Make Workbook the single source of truth with listeners for UI refresh
2. **UI State Machine** - Clean state machine for UI controls
3. **Documentation** - Update docs to reflect current reality

---

## Invariants (bugs to fix)

The following invariants should **always** hold. Currently they can be violated:

1. **A sheet is always loaded.** On startup, load an empty sheet by default. The "New" button creates a fresh empty sheet (not "no sheet displayed" state). This removes the need for "no workbook loaded" UI states.

2. **At least one cell is always selected.** Default to A1 (0,0) on sheet load. There is no "nothing selected" state.

These invariants simplify the UI significantly - no need to handle or render "empty" states.

---

## Phase 0: Enforce Invariants

Fix the current bugs where these invariants can be violated.

- [ ] 0a: Create empty workbook/sheet on startup (before any user action)
- [ ] 0b: Set default selection to A1 (0,0) on startup and after sheet changes
- [ ] 0c: Change "New" button to create fresh empty sheet (not clear to "no sheet" state)
- [ ] 0d: Remove "no workbook loaded" UI code paths and empty state rendering
- [ ] 0e: Ensure selection resets to A1 when switching sheets

---

## Phase 1: Listener Infrastructure in C++ Layer

Add a listener/observer pattern at the C++ bindings layer. The Quadtree already rebuilds after mutations; we add a notification system that the UI can subscribe to.

- [ ] 1a: Define `WorkbookListener` interface in bindings.cc with `onDataChanged()` callback
- [ ] 1b: Add listener registration (`addListener`, `removeListener`) to CellsEngine
- [ ] 1c: Call listeners after all mutation operations (create/update/delete cell, resize, move, sheet changes)
- [ ] 1d: Expose listener system to JavaScript via Embind (callback from WASM to JS)

**Notes:**
- The listener fires *after* `rebuildQuadtree()` completes
- Listener receives change type enum (CELL_CHANGED, STRUCTURE_CHANGED, SHEET_CHANGED, etc.)
- This is the "push" notification; UI still "pulls" viewport data via queryViewport()

---

## Phase 2: Worker & Client Layer Updates

Update the worker and client to support change notifications from WASM.

- [ ] 2a: Add message type for change notifications in worker.js
- [ ] 2b: Update CellsClient to emit events when WASM notifies of changes
- [ ] 2c: Update WasmDataSource to expose `onChange` event subscription
- [ ] 2d: Add change notification callback registration on engine initialization

**Notes:**
- Worker uses `postMessage` to send unsolicited notifications to main thread
- Client maintains optional callback for data changes
- Keeps existing Promise-based API for explicit queries

---

## Phase 3: UI State Machine Design

Create a formal state machine for UI interaction modes. Currently state is scattered across many boolean flags (`isEditing`, `isResizing`, `isDraggingColumn`, `isSelectingRange`, etc.).

- [ ] 3a: Define UIState enum and state machine in new file `apps/wasm/static/shared/ui-state.js`
- [ ] 3b: Implement state transitions with guards and context data
- [ ] 3c: Add modifier key tracking (meta, shift, ctrl, alt) as context
- [ ] 3d: Export state machine API: `getState()`, `transition(event)`, `getContext()`

**State Machine Design:**
```
States:
- IDLE (default - viewing with current selection; at least one cell always selected)
- SELECTING (actively dragging to modify selection range)
- CELL_EDITING (inline cell editor active)
- FORMULA_BAR_EDITING (formula bar focused)
- COLUMN_RESIZING
- ROW_RESIZING
- COLUMN_DRAGGING
- ROW_DRAGGING
- COLUMN_HEADER_EDITING
- SHEET_TAB_EDITING

Context (always available):
- modifiers: { meta, shift, ctrl, alt }
- selectionRange: { start, end } (always valid; single cell = start equals end)
- activeSheet: number
```

**Note:** A single cell is just a range of 1 (start equals end). There's no separate "cell selected" vs "range selected" state - selection always exists.

---

## Phase 4: Integrate State Machine into UI

Replace scattered boolean flags with state machine usage.

- [ ] 4a: Import state machine into index.html, initialize on load
- [ ] 4b: Replace `isEditing`, `isEditingFormulaBar`, `isEditingColumnHeader` with state checks
- [ ] 4c: Replace `isResizing`, `isResizingRow` with state checks
- [ ] 4d: Replace `isDraggingColumn`, `isDraggingRow` with state checks
- [ ] 4e: Replace `isSelectingRange` with SELECTING state check
- [ ] 4f: Update all event handlers to use state transitions instead of direct flag mutations
- [ ] 4g: Add modifier key tracking on keydown/keyup events

**Notes:**
- State machine validates transitions (can't start editing while resizing)
- Event handlers become simpler: check current state, dispatch transition
- Invalid transitions are no-ops (logged in dev mode)

---

## Phase 5: Listener-Driven UI Refresh

Replace explicit `render()` calls scattered throughout the code with listener-driven updates.

- [ ] 5a: Subscribe UI to WasmDataSource `onChange` event on initialization
- [ ] 5b: Create `handleDataChanged(changeType)` handler that fetches viewport and renders
- [ ] 5c: Remove explicit `render()` calls after WASM mutation operations (updateCell, createCell, deleteCell, resize, move)
- [ ] 5d: Keep explicit `render()` only for pure UI changes (scroll, selection visual updates)
- [ ] 5e: Add viewport fetch debouncing to handle rapid successive changes

**Before (scattered throughout):**
```javascript
await dataSource.updateCell(id, value);
await fetchViewportNow();  // explicit refresh
```

**After (centralized):**
```javascript
await dataSource.updateCell(id, value);
// Listener automatically triggers refresh
```

---

## Phase 6: Documentation - README Updates

Update README.md to reflect actual project state.

- [ ] 6a: Update directory structure (lines 95-119) to show only actual directories (apps/cli, apps/wasm, core, docs, plans)
- [ ] 6b: Update "Next Steps" section (lines 209-214) - remove completed items, add current status
- [ ] 6c: Update WebAssembly section (lines 165-194) with current capabilities
- [ ] 6d: Update "Decisions Made" section to reflect WASM-first reality
- [ ] 6e: Add "Current Implementation Status" section showing what's built vs planned

---

## Phase 7: Documentation - GETTING_STARTED Updates

Update GETTING_STARTED.md to reflect current development workflow.

- [ ] 7a: Remove "CLI Tool" section referencing `cells serve` (removed in Phase 2.5)
- [ ] 7b: Update build instructions if any commands changed
- [ ] 7c: Update quick reference section with current commands
- [ ] 7d: Add section on running WASM UI locally

---

## Phase 8: Documentation - Architecture Docs

Add implementation status notes to architecture documents.

- [ ] 8a: Add "Implementation Status" header to `docs/cross-platform.md` noting WASM-only currently
- [ ] 8b: Add "Implementation Status" header to `docs/rendering.md` noting Canvas2D currently
- [ ] 8c: Add "Implementation Status" header to `docs/networking.md` noting not yet implemented
- [ ] 8d: Add "Implementation Status" header to `docs/crdt.md` noting partial/design-only
- [ ] 8e: Update `docs/rendering.md` with actual Canvas-based implementation details

---

## Summary

| Phase | Focus | Files Affected |
|-------|-------|----------------|
| 0 | Enforce Invariants | `apps/wasm/static/index.html` |
| 1 | C++ Listener Infrastructure | `apps/wasm/bindings.cc` |
| 2 | Worker/Client Layer | `apps/wasm/worker.js`, `apps/wasm/client.js` |
| 3 | UI State Machine Design | New: `apps/wasm/static/shared/ui-state.js` |
| 4 | State Machine Integration | `apps/wasm/static/index.html` |
| 5 | Listener-Driven Refresh | `apps/wasm/static/index.html` |
| 6 | README Updates | `README.md` |
| 7 | GETTING_STARTED Updates | `GETTING_STARTED.md` |
| 8 | Architecture Doc Updates | `docs/*.md` |

**Dependencies:**
- Phase 0 is independent (can be done first to simplify later phases)
- Phase 2 depends on Phase 1
- Phase 4 depends on Phase 3
- Phase 5 depends on Phases 2 and 4
- Phases 6-8 are independent and can be done in any order
