# Plan: Formula Range Resize and Move via Drag (Excel-like)

Enable users to resize and move formula reference ranges by dragging, similar to Excel. Modifying the range should update the formula text in real-time.

## Feature Overview

When editing a formula (e.g., `=SUM(A1:A6)`), the referenced ranges are highlighted with colored borders. This feature adds:
1. **Move** - Drag the border (not corner) to relocate the entire reference
2. **Resize** - Drag corners to expand/shrink the range
3. Live formula text updates as the range is modified
4. Support for resizing ranges down to a single cell or expanding single cells to ranges
5. Consistent cursor icons to indicate available actions

## Interaction Model

Three distinct actions based on cursor position over formula highlights:

| Hover Position | Cursor | Action |
|----------------|--------|--------|
| Border (not corner) | `grab` / `grabbing` | Move entire reference |
| Corner | `nwse-resize` etc. | Resize range |
| Inside (main selection only) | `crosshair` | Fill (existing behavior) |

This interaction model should be consistent between formula reference highlights AND the main cell selection for a cohesive UX.

## Current Architecture

The formula range highlighting system already provides the foundation:
- `FormulaHighlight` type stores range coordinates and source position in formula text
- `drawFormulaHighlights()` renders colored borders on referenced ranges
- `replaceReferenceAtPosition()` in CellEditor updates formula text at specific positions
- `updateFormulaHighlights()` re-parses formula and updates highlights after text changes
- Hover detection exists via `checkFormulaHighlightHover()` in mouse-events.ts

Key files:
- `grid-formula-renderer.ts` - Renders formula highlights
- `mouse-events.ts` - Mouse event handling, already has formula drag for insertion
- `cell-editor.ts` - Has `replaceReferenceAtPosition()` for updating references
- `grid-constants.ts` - `FormulaHighlight` interface with `sourceStart`/`sourceEnd`
- `init-components.ts` - `updateFormulaHighlights()` for live refresh

---

## Phase 1: Add Visual Affordances (Handles) to Formula Highlights

Draw resize handles at corners and make borders interactive for moving.

- [x] 1a: Extend `FormulaHighlight` interface to track interaction zones
  - Cell references: 4 corners (resize), 4 borders (move)
  - Range references: 4 corners (resize), 4 borders (move)
  - Column/row references: relevant edges only
  - Added interaction types to grid-constants.ts: `CornerPosition`, `BorderPosition`, `InteractionZoneType`, `FormulaHighlightInteraction`
  - Added constants: `FORMULA_HANDLE_SIZE`, `FORMULA_HANDLE_PADDING`, `FORMULA_BORDER_HIT_WIDTH`

- [x] 1b: Create `drawFormulaHighlightHandles()` function in grid-formula-renderer.ts
  - Draw small squares (6x6px) at corners of range highlights
  - Only draw handles when editing a formula (not just viewing highlights)
  - Use the same color as the highlight border
  - Borders themselves become the "move" interaction zone (no extra visual needed)
  - Added `isFormulaEditing` state to control handle visibility
  - Integrated handle drawing into grid-renderer.ts

- [x] 1c: Add interaction zone bounds tracking to support hit testing
  - Export `FormulaHighlightInteraction` interface:
    ```typescript
    interface FormulaHighlightInteraction {
      highlightIndex: number;
      zone: "corner" | "border" | "inside";
      corner?: "nw" | "ne" | "sw" | "se";
      border?: "n" | "s" | "e" | "w";
      bounds: { x, y, width, height };
    }
    ```
  - Return all interaction zones from drawing function for mouse event handling
  - Added `computeFormulaInteractionZones()` for hit testing without drawing

---

## Phase 2: Hit Testing and Cursor Feedback

Detect when the mouse is over formula highlight interaction zones and show appropriate cursors.

- [x] 2a: Add `hitTestFormulaHighlight()` function in new `formula-interaction.ts` module
  - Takes mouse position, formula highlights, scroll, and dimension info
  - Priority order: corners (resize) > borders (move) > inside (no action for formula refs)
  - Returns: `{ highlightIndex, action: "resize" | "move", corner?, border? } | null`
  - Created `formula-interaction.ts` with `hitTestFormulaHighlight()` and cursor helpers

- [x] 2b: Define cursor mapping for all interaction zones
  - **Corners (resize)**:
    - `nw` / `se`: `nwse-resize`
    - `ne` / `sw`: `nesw-resize`
  - **Borders (move)**:
    - All borders: `grab` (or `grabbing` when dragging)
  - **Inside formula ref**: default cursor (no action)
  - **Main selection fill handle**: `crosshair` (existing)
  - Implemented via `getCursorForCorner()` and `getCursorForHitResult()` in formula-interaction.ts

- [ ] 2c: Update `handleMouseMove()` in mouse-events.ts for cursor changes
  - Call `hitTestFormulaHighlight()` when in formula editing mode
  - Set cursor based on returned action type
  - Only check when formula highlights exist

- [ ] 2d: Track `hoveredFormulaInteraction` in app state
  - Stores current interaction zone info for visual feedback
  - Used to highlight the corner handle being hovered

---

## Phase 3: Implement Drag Behaviors (Move and Resize)

Handle mouse drag to move or resize formula ranges and update the formula text.

- [ ] 3a: Add formula range manipulation state tracking
  ```typescript
  interface FormulaRangeDragState {
    action: "move" | "resize";
    highlightIndex: number;
    corner?: "nw" | "ne" | "sw" | "se";  // For resize
    originalRange: { startCol, startRow, endCol, endRow };
    sourcePosition: { start, end };  // Position in formula text
    dragStartCell: { col, row };  // Grid cell where drag started
  }
  ```

- [ ] 3b: Handle mousedown on formula highlight interaction zone
  - When in formula editing mode and clicking a border or corner:
  - Determine action (resize for corners, move for borders)
  - Set drag state, capture pointer
  - Store original range bounds and source position
  - Change cursor to `grabbing` for move actions

- [ ] 3c: Handle mousemove during formula range manipulation
  - **For resize**: Calculate new range bounds based on which corner is dragged
    - Only the dragged corner moves, opposite corner stays fixed
    - Minimum size is 1 cell
  - **For move**: Offset entire range by drag delta
    - All four corners move together
    - Clamp to valid grid coordinates (>= 0)
  - Generate new reference text
  - Call `replaceReferenceAtPosition()` to update formula
  - Formula highlights update automatically

- [ ] 3d: Handle mouseup to complete manipulation
  - Clear drag state
  - Release pointer capture
  - Restore cursor to `grab` or default
  - Focus back on formula editor

- [ ] 3e: Handle Escape to cancel manipulation
  - Restore original reference text
  - Clear drag state
  - Focus back on formula editor

---

## Phase 4: Reference Text Generation

Convert grid coordinates to A1 notation for formula updates.

- [ ] 4a: Create utility function `rangeToA1Notation()`
  - Input: startCol, startRow, endCol, endRow
  - Output: "A1:B5" for ranges, "A1" for single cells
  - Handle column references (e.g., "A:B")
  - Handle row references (e.g., "1:5")
  - Preserve absolute reference markers ($) if present in original

- [ ] 4b: Preserve original reference properties
  - Track if original reference had $ markers (absolute references)
  - When generating new reference text, preserve these markers on unchanged parts
  - E.g., resizing "$A$1:B2" horizontally should keep "$A" absolute

---

## Phase 5: Visual Feedback During Resize

Provide clear visual feedback while resizing.

- [ ] 5a: Draw resize preview overlay
  - Show dashed border for the new range bounds during drag
  - Use same color as the highlight being resized
  - Similar to existing fill preview but for formula ranges

- [ ] 5b: Update formula bar display during resize
  - Formula text updates live as range is resized
  - Colored reference spans update to reflect new range

- [ ] 5c: Handle edge cases gracefully
  - Minimum range size is 1 cell (can't resize to 0)
  - Maximum range determined by sheet bounds
  - Visual indication when at boundary

---

## Phase 6: Main Selection Cursor Consistency (Optional Enhancement)

Apply consistent cursor feedback to the main cell selection for a cohesive UX.

- [ ] 6a: Review current main selection interaction zones
  - Fill handle at bottom-right corner → `crosshair`
  - Currently no move/resize affordances on main selection

- [ ] 6b: Consider adding consistent cursors to main selection (optional)
  - Border hover → `grab` cursor (potential future: drag to move cells)
  - Corner hover → resize cursor (potential future: extend selection)
  - This phase is optional/future work - focus on formula refs first

---

## Phase 7: Integration and Polish

Connect all pieces and handle edge cases.

- [ ] 7a: Integrate with existing formula editing flow
  - Works with both cell editor and formula bar
  - Respects cross-sheet references (preserve sheet prefix)
  - Handles named ranges appropriately (move/resize not applicable)

- [ ] 7b: Handle keyboard modifiers during manipulation
  - Shift during resize: maintain aspect ratio (optional)
  - Shift during move: constrain to axis (optional)

- [ ] 7c: Add comprehensive E2E tests
  - **Resize tests:**
    - Resize cell reference to range (drag corner)
    - Resize range reference smaller/larger
    - Resize to single cell
    - With absolute references ($)
    - With cross-sheet references
  - **Move tests:**
    - Move cell reference to new location (drag border)
    - Move range reference to new location
    - With absolute references ($) - should update
    - With cross-sheet references - keep sheet prefix
  - **General:**
    - Cancel with Escape restores original
    - Formula text updates correctly
    - Multiple references in same formula work independently

---

## Architecture Notes

### State Flow During Manipulation

```
User hovers formula highlight
    ↓
hitTestFormulaHighlight() determines zone (corner/border)
    ↓
Cursor changes to resize/grab cursor
    ↓
User clicks (mousedown)
    ↓
Set drag state, capture pointer, store original range
    ↓
Mouse move calculates new range (resize) or offset (move)
    ↓
Generate new A1 reference text
    ↓
cellEditor.replaceReferenceAtPosition(start, end, newRef)
    ↓
editingSession.replaceRange() updates formula text
    ↓
onUpdateFormulaHighlights() re-parses formula
    ↓
Highlights update with new range bounds
    ↓
Grid re-renders showing updated highlight
    ↓
User releases (mouseup) → complete, or Escape → restore original
```

### Interaction Zones by Reference Type

| Reference Type | Corners (Resize) | Borders (Move) |
|---------------|------------------|----------------|
| Cell (A1) | All 4 corners | All 4 borders |
| Range (A1:B5) | All 4 corners | All 4 borders |
| Column (A:A) | - | Left/Right borders |
| Row (1:1) | - | Top/Bottom borders |
| Column Range (A:C) | Left/Right edges | Left/Right borders |
| Row Range (1:5) | Top/Bottom edges | Top/Bottom borders |

### Cursor Mapping

| Zone | Cursor | Action |
|------|--------|--------|
| Corner NW | `nwse-resize` | Resize from top-left |
| Corner NE | `nesw-resize` | Resize from top-right |
| Corner SW | `nesw-resize` | Resize from bottom-left |
| Corner SE | `nwse-resize` | Resize from bottom-right |
| Border (any) | `grab` | Move (hover) |
| Border (any) | `grabbing` | Move (dragging) |
| Inside | `default` | No action |
| Fill handle | `crosshair` | Fill (main selection) |

### Constants

```typescript
const FORMULA_HANDLE_SIZE = 6;      // 6x6 pixel corner handles
const FORMULA_HANDLE_PADDING = 3;   // Extra hit area padding
const FORMULA_BORDER_WIDTH = 4;     // Border hit area width for move
```

### Move vs Resize Logic

**Resize (corner drag):**
- Dragged corner follows mouse
- Opposite corner stays fixed
- Range can shrink to 1 cell minimum
- If start > end, swap them for valid range

**Move (border drag):**
- Calculate delta from drag start position
- Offset all four corners by delta
- Clamp to grid bounds (col >= 0, row >= 0)
- Range size stays constant
