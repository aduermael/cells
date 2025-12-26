# UX/UI Quality of Life Improvements

```
Status: READY
Created At: 2025-12-26 21:41 UTC
Updated At: 2025-12-26 21:41 UTC
Following plan management guidelines defined in AGENTS.md
```

## Overview

This plan addresses seven UX/UI improvements for the Cells web application. Each phase focuses on one feature and ends with a mandatory UI checkpoint that requires user approval before proceeding.

---

## Phase 1: Click-to-Add Cell References During Formula Editing

**Goal:** While editing a formula, clicking on cells, columns, or rows should insert their reference into the formula instead of selecting/exiting edit mode.

### Architecture

- **Current behavior:** `app-events.ts` `handleMouseDown` commits the edit when clicking on a cell
- **New behavior:** When in `CELL_EDITING` or `FORMULA_BAR_EDITING` state AND the formula starts with `=`, clicks on cells/rows/columns should:
  1. Insert the reference at cursor position in the editor
  2. Not exit edit mode
  3. Update formula highlights in real-time

### Tasks

- [x] 1a: Add `isFormulaMode()` helper to CellEditor and FormulaBarEditor to check if current value starts with `=`
- [x] 1b: Create `insertReferenceAtCursor(ref: string)` method in both editors to insert text at cursor position
- [x] 1c: Modify `handleMouseDown` in `app-events.ts` to detect formula editing mode and call `insertReferenceAtCursor` instead of committing
- [x] 1d: Handle click on column header during formula edit (insert column reference like `B:B`)
- [x] 1e: Handle click on row header during formula edit (insert row reference like `3:3`)
- [x] 1f: Support Shift+click to insert range references (from last reference to clicked cell)
- [x] 1g: Unit tests deferred (UI event integration tests require browser environment)

### UI Checkpoint 1
- [x] **USER APPROVED:** Click-to-add references works correctly (click, shift+click, click+drag)

---

## ~~Phase 1.5: Fix #REF! Error for Range References~~ (Moved)

> **Note:** This phase has been moved to a dedicated plan: `plans/2025-12-26-range-reference-fixes.md`
>
> Issues to address:
> - #REF! error for cell ranges like `=SUM(B1:B4)`
> - Dependency graph not updating when new cells added to column/row references

---

## Phase 2: Color-Coded Formula Reference Text

**Goal:** Display cell and range references in the formula bar/cell editor with the same colors as their highlight borders on the grid.

### Architecture

- **Current:** `FORMULA_REF_COLORS` in `grid-constants.ts` defines 8 colors for highlights
- **Formula highlights** already track `sourceStart`/`sourceEnd` positions in the formula text
- **New:** Apply colored spans to formula input text matching each reference's color

### Tasks

- [x] 2a: Create `formula-colorizer.ts` module with function to generate colored HTML spans for formula text
- [x] 2b: Create a styled contenteditable div overlay or use CSS background gradients for coloring (input elements can't have colored text segments)
- [x] 2c: Replace formula input with contenteditable div that syncs with hidden input for form handling
- [x] 2d: Apply color styling to cell editor overlay when in formula mode
- [x] 2e: Ensure cursor position and selection work correctly in the contenteditable
- [x] 2f: Update colors in real-time as user types and references are parsed

### UI Checkpoint 2
- [x] **USER APPROVED:** Formula text colors match grid highlight colors

---

## Phase 3: Scrollbars with Virtual Scrolling

**Goal:** Add scrollbars that support columns up to V (22 columns) visually displayed, with vertical infinite scroll up to 1M rows. Scrollbar thumb shrinks as more content is discovered.

### Architecture

- **Current:** Wheel events in `app-events.ts` handle scroll, no visible scrollbars
- **New:**
  - Custom scrollbar components (not native, for better control)
  - Virtual row count: start with ~100 visible, expand dynamically up to 1M
  - Column count: fixed at 22 (A-V) by default
  - Thumb size = viewportSize / totalContentSize

### Tasks

- [ ] 3a: Create `scrollbar.ts` module with `HorizontalScrollbar` and `VerticalScrollbar` classes
- [ ] 3b: Add scrollbar track and thumb elements to `index.html` and styles to `styles.css`
- [ ] 3c: Implement thumb drag interaction (mousedown, mousemove, mouseup)
- [ ] 3d: Sync scrollbar position with canvas scroll state bidirectionally
- [ ] 3e: Implement dynamic vertical scrollbar thumb sizing based on "discovered" row count
- [ ] 3f: Add scroll-to-row functionality when clicking on scrollbar track
- [ ] 3g: Ensure horizontal scrollbar covers columns A-V (22 columns)
- [ ] 3h: Add tests for scrollbar position calculations

### UI Checkpoint 3
- [ ] **USER APPROVAL REQUIRED:** Verify scrollbars appear, thumb resizes correctly, and scrolling works smoothly

---

## Phase 4: Larger Cell Reference Button

**Goal:** The cell reference button (showing "A1" or range like "A1:B5") should have a larger default width to avoid excessive resizing when selecting ranges.

### Architecture

- **Current:** `#cell-reference` has `min-width: 48px` in `styles.css`
- **New:** Increase min-width to accommodate typical range references like "AA100:ZZ999"

### Tasks

- [ ] 4a: Update `#cell-reference` styles: increase `min-width` to ~90px for comfortable range display
- [ ] 4b: Optionally use monospace font for consistent character width
- [ ] 4c: Ensure text truncation with ellipsis for extreme cases

### UI Checkpoint 4
- [ ] **USER APPROVAL REQUIRED:** Verify cell reference button width is appropriate and doesn't resize excessively

---

## Phase 5: Context Menu System (Right-Click Infrastructure)

**Goal:** Create a generic context menu system that captures all right-clicks and displays a modal with context-dependent options.

### Architecture

- **New module:** `context-menu.ts` managing a single reusable context menu element
- **Context detection:** Determine click location (cell, column header, row header, etc.)
- **Menu items:** Dynamically populated based on context

### Tasks

- [ ] 5a: Create `context-menu.ts` with `ContextMenu` class and `ContextMenuManager` singleton
- [ ] 5b: Define `ContextMenuItem` interface with label, action, icon, disabled state, and danger flag
- [ ] 5c: Add context menu HTML structure and CSS styles (positioned absolutely at click location)
- [ ] 5d: Add `contextmenu` event listener to canvas and prevent default
- [ ] 5e: Implement `showContextMenu(x, y, items[])` and `hideContextMenu()` methods
- [ ] 5f: Close menu on click outside, Escape key, or scroll
- [ ] 5g: Add animation for menu appearance (fade in, slight translate)
- [ ] 5h: Test context menu positioning near screen edges

### UI Checkpoint 5
- [ ] **USER APPROVAL REQUIRED:** Verify right-click shows a context menu at click position

---

## Phase 6: Column/Row Context Menu Options

**Goal:** Right-clicking on column or row headers shows context menu with Insert/Delete options.

### Architecture

- **Uses Phase 5 infrastructure**
- **Column options:** Insert column left, Insert column right, Delete column
- **Row options:** Insert row above, Insert row below, Delete row
- **Engine integration:** Need to add WASM functions for inserting/deleting columns and rows

### Tasks

- [ ] 6a: Add `insertColumn(pos, before)` and `deleteColumn(pos)` methods to `WasmDataSource`
- [ ] 6b: Add `insertRow(pos, before)` and `deleteRow(pos)` methods to `WasmDataSource`
- [ ] 6c: Implement C++ engine functions `insertColumnAt()`, `deleteColumnAt()`, `insertRowAt()`, `deleteRowAt()`
- [ ] 6d: Add column context menu items: "Insert column left", "Insert column right", "Delete column"
- [ ] 6e: Add row context menu items: "Insert row above", "Insert row below", "Delete row"
- [ ] 6f: Wire up context menu actions to WASM functions
- [ ] 6g: Add tests for column/row insertion and deletion

### UI Checkpoint 6
- [ ] **USER APPROVAL REQUIRED:** Verify right-click on columns/rows shows correct options and actions work

---

## Phase 7: Unified Menu State Management

**Goal:** Export button removes arrow (like Collaborate), menus share consistent UI component styling, and only one menu can be open at a time.

### Architecture

- **Current:** Export dropdown and Collaborate panel are separate implementations
- **New:**
  - Create shared `DropdownMenu` component with consistent styling
  - Global menu state manager ensuring mutual exclusivity
  - Remove arrow from Export button to match Collaborate style

### Tasks

- [ ] 7a: Create `menu-state.ts` with `MenuStateManager` singleton tracking which menu is open
- [ ] 7b: Refactor Export dropdown to use shared menu state (close when Collaborate opens)
- [ ] 7c: Refactor Collaborate panel to use shared menu state (close when Export opens)
- [ ] 7d: Remove the `▼` arrow from Export button to match Collaborate button style
- [ ] 7e: Unify CSS: ensure `.dropdown-menu` and `.collab-status-details` share same margins, paddings, border-radius
- [ ] 7f: Add transition animations matching between menus
- [ ] 7g: Test opening one menu closes the other

### UI Checkpoint 7
- [ ] **USER APPROVAL REQUIRED:** Verify menus look consistent, only one opens at a time, Export has no arrow

---

## Summary

| Phase | Feature | Key Files |
|-------|---------|-----------|
| 1 | Click-to-add references | `app-events.ts`, `cell-editor.ts`, `header-editor.ts` |
| 2 | Colored formula text | `formula-colorizer.ts` (new), `styles.css` |
| 3 | Scrollbars | `scrollbar.ts` (new), `index.html`, `styles.css` |
| 4 | Cell reference width | `styles.css` |
| 5 | Context menu system | `context-menu.ts` (new), `index.html`, `styles.css` |
| 6 | Column/row options | `context-menu.ts`, `wasm-data-source.ts`, C++ engine |
| 7 | Unified menus | `menu-state.ts` (new), `file-loader.ts`, `collab-ui.ts` |

All features are mandatory. Each phase ends with a UI checkpoint requiring user approval.

**Note:** Phase 1.5 (range reference fixes) moved to `plans/2025-12-26-range-reference-fixes.md`
