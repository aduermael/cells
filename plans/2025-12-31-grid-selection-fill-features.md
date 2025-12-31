Status: READY
Created At: 2025-12-31 06:19 UTC
Updated At: 2025-12-31 08:50 UTC
Following plan management guidelines defined in AGENTS.md

# Grid Selection & Fill Features

## Overview

This plan addresses several grid manipulation bugs and adds essential Excel-like features:

1. **Bug Fix**: First cell value hidden in range selection (anchor cell background covers text)
2. **Bug Fix**: Anchor cell border too thick (should match range border)
3. **Feature**: Add fill handle (bottom-right corner dot for drag-fill)
4. **Feature**: Auto-fill value sequences (1, 2 → 3, 4, 5...)
5. **Feature**: Auto-fill formulas with relative reference adjustment
6. **Feature**: Copy/paste formulas with relative reference adjustment

## Root Cause Analysis

### Issue 1: First Cell Value Hidden
**Location**: `apps/wasm/src/grid-selection-renderer.ts:128-130`

The render order is:
1. Cell values drawn (`_drawCellValues`)
2. Selection drawn (`drawRangeSelection`)

In `drawRangeSelection`, the anchor cell draws a white background:
```typescript
ctx.fillStyle = colors.cellBg;
ctx.fillRect(clipX + 1, clipY + 1, clipW - 2, clipH - 2);
```

This covers the cell text that was already rendered.

**Fix**: Remove the cellBg fill for anchor cell. The cell background is already drawn in the grid renderer, so the anchor cell only needs the border highlight.

### Issue 2: Anchor Cell Border Too Thick
**Location**: `apps/wasm/src/grid-selection-renderer.ts:133-135`

Anchor cell uses 2px border while range uses 1px border.

**Fix**: Change anchor cell border to 1px to match range border.

### Issue 3-6: Fill Handle & Auto-Fill
The fill handle and auto-fill functionality are not implemented. Need to add:
- Fill handle rendering (small square at selection bottom-right)
- Fill handle mouse interaction (detect hover, start drag)
- Pattern detection for numeric sequences
- Formula reference adjustment for fill and copy/paste

---

## Phase 1: Fix Selection Rendering Bugs

- [x] 1a: Remove anchor cell background fill (fix hidden value)
- [x] 1b: Change anchor cell border to 1px (match range border)
- [x] 1c: Add E2E test for range selection visibility

## Phase 2: Add Fill Handle Rendering

- [x] 2a: Add fill handle state to SelectionRendererState
- [x] 2b: Render fill handle square at selection bottom-right corner
- [x] 2c: Add fill handle hover cursor detection in app-events.ts
- [x] 2d: Add E2E test for fill handle visibility

## Phase 3: Implement Fill Handle Drag Interaction

- [x] 3a: Add fill drag state (isFillDragging, fillDragStart, fillDragEnd)
- [x] 3b: Handle mousedown on fill handle to start drag
- [x] 3c: Handle mousemove to update fill preview range
- [x] 3d: Render fill preview (dashed border showing target range)
- [x] 3e: Add E2E test for fill handle drag preview

## Phase 4: Implement Fill Operation in C++

Core fill logic in C++ for Luau scripting access:

- [ ] 4a: Add `fillRange()` function in core/cells (detect patterns, extrapolate sequences)
- [ ] 4b: Expose fill to Luau API via `rangeFill({from, to, target})` in luau_sandbox.cc
- [ ] 4c: Expose fill to WASM via `CellsEngine::fillRange()` in bindings.cc
- [ ] 4d: Connect TypeScript mouseup handler to call fillRange via WASM
- [ ] 4e: Add C++ unit tests for fill pattern detection
- [ ] 4f: Add E2E test for numeric sequence fill (1, 2 → 3, 4, 5)

## Phase 5: Implement Formula Reference Adjustment

- [ ] 5a: Add formula reference parser to extract cell references (A1, $A1, A$1, $A$1)
- [ ] 5b: Add formula reference adjuster to offset relative references
- [ ] 5c: Apply reference adjustment in fill operations
- [ ] 5d: Apply reference adjustment in copy/paste operations
- [ ] 5e: Add E2E tests for formula fill (=A1 filled down becomes =A2, =A3)
- [ ] 5f: Add E2E tests for absolute reference preservation (=$A$1 stays =$A$1)

---

## Technical Details

### Fill Handle Rendering
The fill handle is a small 6x6px square at the bottom-right corner of the selection:
- Position: `(selectionRight - 3, selectionBottom - 3)`
- Color: Same as selection border (green)
- Filled solid, no border

### Pattern Detection Algorithm
For numeric sequences:
1. If single cell selected: repeat value (no increment)
2. If 2+ cells selected: calculate step = (last - first) / (count - 1)
3. For each fill position: value = lastValue + step * (position - lastPosition)

### Formula Reference Adjustment
Parse formula to find cell references:
- Regex: `/(\$?)([A-Z]+)(\$?)(\d+)/g`
- If `$` present before column: column is absolute (don't adjust)
- If `$` present before row: row is absolute (don't adjust)
- Adjust relative parts by the paste/fill offset

Example:
- Original: `=A1+$B$2` at C3
- Copy to D4 (offset: +1 col, +1 row)
- Result: `=B2+$B$2` (A1 becomes B2, $B$2 unchanged)

### State Additions to grid-events.ts
```typescript
// Fill handle drag state
isFillDragging: boolean = false;
fillDragStartPos: Position | null = null;
fillDragCurrentPos: Position | null = null;
```

### New Files
- None required - all changes in existing files

### Files Modified
- `apps/wasm/src/grid-selection-renderer.ts` - Selection rendering fixes + fill handle
- `apps/wasm/src/grid-events.ts` - Fill handle interaction
- `apps/wasm/src/clipboard.ts` - Pattern detection + reference adjustment
- `apps/wasm/tests/selection.test.mjs` - New E2E test file

---

## Testing Strategy

Each phase includes E2E tests to verify the feature:

1. **Selection visibility test**: Select range, verify all cell values visible
2. **Fill handle test**: Select cell, verify fill handle visible at corner
3. **Fill drag test**: Drag fill handle, verify preview appears
4. **Sequence fill test**: Enter 1, 2, drag fill handle down, verify 3, 4, 5
5. **Formula fill test**: Enter =A1, drag down, verify =A2, =A3
6. **Absolute ref test**: Enter =$A$1, drag down, verify =$A$1 preserved

---

## Dependencies

- Phase 2 depends on Phase 1 (selection rendering must work first)
- Phase 3 depends on Phase 2 (need fill handle to drag it)
- Phase 4 depends on Phase 3 (need drag interaction)
- Phase 5 depends on Phase 4 (formula fill uses same mechanism)
