# UI Improvements Plan

This plan consolidates and improves the web UI for both WASM and CLI versions.

## Phase 1: Consolidate Shared UI Code

Extract common CSS, rendering logic, and utilities into shared modules to avoid duplication between `apps/wasm/static/index.html` (~1477 lines) and `apps/cli/web/index.html` (~1920 lines).

- [x] 1a: Create `apps/shared/` directory with shared CSS (`styles.css`)
- [x] 1b: Create shared JavaScript module (`grid-renderer.js`) with canvas rendering logic
- [x] 1c: Create shared JavaScript module (`grid-events.js`) with event handling
- [x] 1d: Create shared JavaScript module (`utils.js`) with utility functions
- [x] 1e: Refactor `apps/wasm/static/index.html` to use shared modules
- [x] 1f: Refactor `apps/cli/web/index.html` to use shared modules

## Phase 2: Formula Bar

Add a formula/cell bar between the header toolbar and the canvas area. Shows:
- Selected cell reference (e.g., "A1", "B5")
- Full cell value or formula in an editable input field

- [x] 2a: Add formula bar HTML structure and CSS styles
- [x] 2b: Implement formula bar state updates (selected cell display)
- [x] 2c: Enable editing cell value directly from formula bar

## Phase 2.5: Remove CLI Server Mode (Cleanup)

Now that WASM is stable, remove the `cli server` command and the web UI served via HTTP server. This simplifies the codebase by removing the need for shared files between two different serving modes.

- [x] 2.5a: Remove `server` subcommand from CLI
- [x] 2.5b: Remove `apps/cli/web/` directory and related server code
- [x] 2.5c: Update `apps/shared/` to only serve WASM needs (remove server-specific code if any)
- [x] 2.5d: Clean up any build rules related to CLI server mode

## Phase 2.6: Core Cell Editing Behavior

Fix fundamental cell editing behavior to match spreadsheet conventions.

**Single-click vs Double-click:**
- Single click + start typing → erases content, starts inserting new content (replace mode)
- Double click → puts cursor at end of existing content (append/edit mode)

**Auto-commit on navigation:**
- Content is committed immediately when navigating away (click another cell, arrow keys, Tab, Enter)
- ENTER moves to cell below (commits content as side effect)
- No explicit "confirm" action required

**Empty cell handling:**
- Clicking an empty cell should allow editing (create cell on demand with new UUID)
- Deleting all content from a cell should remove the cell UUID entirely

**Formula bar sync:**
- Editing via formula bar and clicking another cell should cleanly commit and move selection
- Fix split/duplicate selector visual bug when clicking away from formula bar

- [x] 2.6a: Implement single-click selection without entering edit mode
- [x] 2.6b: Implement typing-to-replace behavior (single click + type = replace content)
- [x] 2.6c: Implement double-click to enter edit mode with cursor at end
- [x] 2.6d: Auto-commit cell content on any navigation (click, arrows, Tab, Enter)
- [x] 2.6e: ENTER commits and moves selection down, Shift+ENTER moves up
- [x] 2.6f: Create cells on demand when editing empty positions (assign UUID)
- [x] 2.6g: Delete cell UUID when content is completely cleared
- [x] 2.6h: Fix formula bar commit when clicking another cell (single clean selection)

## Phase 3: Visual Polish

Minor visual improvements for a cleaner look.

- [x] 3a: Add separator lines between header cells (A, B, C... and 1, 2, 3...)
- [x] 3b: Remove "Local (WASM)" badge (no longer needed after CLI server removal) - N/A, badge never existed
- [x] 3c: Remove sheet dimensions display ("X rows x Y columns")

## Phase 4: Export Dropdown

Replace multiple export buttons with a single dropdown.

- [ ] 4a: Create dropdown button component with CSS
- [ ] 4b: Replace export buttons with single "Export" dropdown menu
- [ ] 4c: Populate dropdown with format options (CSV, XLSX, CELLS)

## Phase 5: Row Height Resizing

Allow users to resize row heights by dragging the bottom edge of row headers.

- [ ] 5a: Add row resize handle detection (similar to column resize)
- [ ] 5b: Implement row resize preview during drag
- [ ] 5c: Add row resize completion and persistence

## Phase 6: Column Renaming

Allow users to rename columns by double-clicking the column header.

- [ ] 6a: Add column name support in data model (if not already present)
- [ ] 6b: Add double-click handler on column headers to show inline editor
- [ ] 6c: Implement column rename save via API/WASM
- [ ] 6d: Display custom column names instead of letters when set

## Phase 7: Improved Drag Behavior

Fix drag-and-drop placeholder size to match the dragged entity.

- [ ] 7a: Fix column drag placeholder to maintain dragged column width
- [ ] 7b: Fix row drag placeholder to maintain dragged row height
- [ ] 7c: Ensure moved column/row keeps original size after drop

## Phase 8: Persist File on Refresh

Remember the loaded file across page refreshes using localStorage or IndexedDB.

- [ ] 8a: Save file data to IndexedDB when loaded
- [ ] 8b: Save file metadata (name, format) to localStorage
- [ ] 8c: Auto-load persisted file on page load
- [ ] 8d: Add "Clear" or "New" option to reset persisted state

## Phase 9: Range Selection

Allow click-and-drag to select a rectangular range of cells.

- [ ] 9a: Add range selection state (`selectionStart`, `selectionEnd`)
- [ ] 9b: Implement mousedown to set selection start
- [ ] 9c: Implement mousemove during drag to update selection end
- [ ] 9d: Render range selection highlight
- [ ] 9e: Support Shift+click to extend selection
- [ ] 9f: Display range in formula bar (e.g., "A1:B5")

## Phase 10: Sheet Tabs

Display sheet tabs at the bottom of the grid for multi-sheet navigation and management.

- [ ] 10a: Add sheet tabs container HTML/CSS at bottom of canvas area
- [ ] 10b: Fetch and display list of sheets from workbook
- [ ] 10c: Implement sheet switching (click tab to change active sheet)
- [ ] 10d: Add "+" button to create new sheet
- [ ] 10e: Add context menu or "x" button to delete sheet
- [ ] 10f: Auto-create new sheet when deleting the last remaining sheet
- [ ] 10g: Implement drag-and-drop to reorder sheet tabs
- [ ] 10h: Add double-click to rename sheet

---

## Technical Notes

### File Structure After Consolidation (Post Phase 2.5)

```
apps/
├── shared/
│   ├── styles.css           # Common CSS
│   ├── grid-renderer.js     # Canvas rendering
│   ├── grid-events.js       # Mouse/keyboard handlers
│   └── utils.js             # Helper functions
└── wasm/
    ├── static/
    │   └── index.html       # WASM web UI
    ├── client.js
    └── worker.js

# REMOVED (Phase 2.5):
# └── cli/web/               # CLI server mode removed - WASM only
```

### Header Separator Colors
Header separators: `rgba(0, 0, 0, 0.06)` (very subtle lines between A, B, C... and 1, 2, 3...)

### Formula Bar Layout
```
┌─────────────────────────────────────────────────────────────┐
│ [Open File]  Sheet Name                    [Export ▼]       │ ← Header
├─────────────────────────────────────────────────────────────┤
│ A1  │  =SUM(B1:B10)                                        │ ← Formula Bar
├─────────────────────────────────────────────────────────────┤
│     │  A  │  B  │  C  │  D  │                              │
│  1  │     │     │     │     │                              │
│  2  │     │     │     │     │                              │ ← Grid
└─────────────────────────────────────────────────────────────┘
```

### IndexedDB Schema for File Persistence
```javascript
{
  store: 'files',
  key: 'current',
  value: {
    name: 'filename.xlsx',
    format: 'xlsx',
    data: ArrayBuffer,
    timestamp: Date.now()
  }
}
```

### Sheet Tabs Layout
```
┌─────────────────────────────────────────────────────────────┐
│ [Open File]  Sheet Name                    [Export ▼]       │ ← Header
├─────────────────────────────────────────────────────────────┤
│ A1  │  =SUM(B1:B10)                                        │ ← Formula Bar
├─────────────────────────────────────────────────────────────┤
│     │  A  │  B  │  C  │  D  │                              │
│  1  │     │     │     │     │                              │
│  2  │     │     │     │     │                              │ ← Grid
│  3  │     │     │     │     │                              │
├─────────────────────────────────────────────────────────────┤
│ [Sheet1] [Sheet2] [Sheet3]  [+]                            │ ← Sheet Tabs
└─────────────────────────────────────────────────────────────┘
```

Sheet tab features:
- Active tab visually distinct (white bg, no bottom border)
- Inactive tabs slightly dimmed
- Hover state for tabs
- "+" button on the right to add new sheet
- Right-click context menu: Rename, Delete
- Drag tabs to reorder
