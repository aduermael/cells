# Plan: Axis Style Rendering & Dropdown Boundary Fixes

This plan addresses two issues:

1. **Column/row styles only apply to non-empty cells**: When setting a background color (or borders) on an entire column/row, it only renders on cells with content. It should render across the full axis, just like Excel does.

2. **Dropdown boundaries not enforced**: Most dropdowns (except context menu) can overflow the screen edges without adjustment.

## Issue Analysis

### Issue 1: Axis Styles Not Rendering on Empty Cells

**Current behavior:**
- When applying a background color to column B, only B2, B3, B5 (cells with data) show the color
- Empty cells (B1, B4, B6, etc.) remain white

**Expected behavior (Excel parity):**
- Background color should display on ALL cells in column B within the visible viewport
- Cell-level and range-level styles should override axis styles where applicable

**Root cause:**
The viewport rendering pipeline has two paths:
1. `_drawCellBackgrounds()` - iterates over `this.cells` array (only non-empty cells)
2. `_drawStyleRangeBackgrounds()` - iterates over `this.styleRanges` (RANGE_STYLE ranges)

Column/row axis styles are:
- Stored correctly in the C++ engine (via `setColumnStyle()`, `setRowStyle()`)
- Resolved correctly for individual cells in `getEffectiveStyle()`
- **NOT exposed as separate data** in the viewport response for rendering

The `styleRanges` array in the viewport response only includes explicit `RANGE_STYLE` ranges, not synthesized ranges from axis styles.

**Solution:**
Include axis styles in the viewport response as synthetic `axisStyles` (separate from `styleRanges`) with type indicator. The renderer will draw these as full-viewport-spanning fills before other layers.

### Issue 2: Dropdown Boundary Overflow

**Current behavior:**
- Context menu has proper boundary checking with 8px padding
- Other dropdowns (border-controls, style-controls, format-controls, named-ranges) position via CSS only and can overflow screen edges

**Expected behavior:**
- All dropdowns should stay within screen bounds with padding

**Solution:**
Create a shared utility function based on context-menu's `positionMenu()` logic and apply it to all dropdown components.

---

## Phase 1: Include Axis Styles in Viewport Response

Add `axisStyles` array to viewport response containing styled columns/rows.

- [x] 1a: Define `AxisStyleInfo` type in `types.ts`:
  ```typescript
  interface AxisStyleInfo {
    type: 'column' | 'row';
    position: number;  // column or row index
    style: { bgColor?: string; textColor?: string; /* etc */ };
  }
  ```

- [x] 1b: Update `bindings_viewport.cc` to include `axisStyles` array in JSON response
  - Iterate through columns with `hasStyle()` flag
  - Iterate through rows with `hasStyle()` flag
  - For each styled axis, include position and style properties

- [x] 1c: Update TypeScript types (`ViewportResult`, `wasm-data-source.ts`, `client-types.ts`) to include `axisStyles`

- [x] 1d: Store `axisStyles` in `app.ts` state alongside `styleRanges`

## Phase 2: Render Axis Styles as Full-Viewport Backgrounds

Draw axis styles as full column/row fills before other backgrounds.

- [x] 2a: Add `_drawAxisStyleBackgrounds()` method to `grid-renderer.ts`
  - For column styles: draw a vertical strip spanning the full visible height
  - For row styles: draw a horizontal strip spanning the full visible width
  - Draw column styles after row styles (column takes priority at intersection)

- [x] 2b: Update `GridRenderer.render()` to call `_drawAxisStyleBackgrounds()` before `_drawStyleRangeBackgrounds()` and `_drawCellBackgrounds()`
  - Render order should be: axis styles → range styles → cell styles (layered properly so higher priority draws last and covers lower)

- [x] 2c: Pass `axisStyles` to `GridRenderer` in `init-components.ts`

- [x] 2d: Add E2E tests for axis style rendering
  - Added visual rendering tests that verify canvas pixel colors
  - Tests column style renders on empty cells
  - Tests row style renders on empty cells
  - Tests cell style overrides column style
  - Tests range style overrides row style
  - Fixed client.ts to include axisStyles in queryViewport response
  - Fixed viewport position output to use Axis.position instead of loop index

## Phase 3: Apply Axis Styles to Borders

Borders applied at the axis level should also render on empty cells.

- [x] 3a: Include border properties in axis style output from `bindings_viewport.cc`
  - Added border.hasValue() check to axis style visibility condition
  - Added border serialization (top, right, bottom, left) to column and row axis style output

- [x] 3b: Update `_drawAxisStyleBackgrounds()` to also draw borders
  - Added new `_drawAxisStyleBorders()` method to grid-renderer.ts
  - For column borders: draws left/right borders spanning full visible height
  - For row borders: draws top/bottom borders spanning full visible width
  - Supports all border styles including double borders and dashed patterns
  - Added CellBorder type to AxisStyleInfo interface
  - Updated render() to call axis borders before cell borders

- [ ] 3c: Add tests for axis border rendering

## Phase 4: Create Shared Dropdown Positioning Utility

- [ ] 4a: Create `dropdown-utils.ts` with `positionDropdown()` function
  ```typescript
  export function positionDropdown(
    dropdown: HTMLElement,
    anchorRect: DOMRect,
    options?: { padding?: number; preferBelow?: boolean }
  ): void
  ```
  - Measure dropdown dimensions
  - Calculate position relative to anchor
  - Adjust if overflow right/bottom/left/top
  - Apply final position

- [ ] 4b: Apply `positionDropdown()` to `border-controls.ts`

- [ ] 4c: Apply `positionDropdown()` to `style-controls.ts` (color picker, font dropdown)

- [ ] 4d: Apply `positionDropdown()` to `format-controls.ts`

- [ ] 4e: Apply `positionDropdown()` to `named-ranges-dropdown.ts`

- [ ] 4f: Add E2E test for dropdown boundary enforcement
  - Test all dropdown menus (border, color picker, font, format, named ranges)
  - Position window/viewport so dropdowns would overflow edges
  - Verify each dropdown stays within screen bounds with minimum padding from edges

---

## Architecture Notes

### Rendering Order (back to front)

1. **Row axis styles** - Full-width horizontal strips for styled rows
2. **Column axis styles** - Full-height vertical strips for styled columns (overrides row at intersection)
3. **Range styles** - Rectangular regions for RANGE_STYLE ranges
4. **Cell backgrounds** - Individual cell styles
5. **Grid lines**
6. **Cell content**

This order ensures that higher-priority styles visually override lower-priority ones.

### Viewport Response Structure

```json
{
  "cells": [...],
  "columns": [...],
  "rows": [...],
  "styleRanges": [...],
  "axisStyles": [
    { "type": "column", "position": 1, "style": { "bgColor": "#e8f5e9" } },
    { "type": "row", "position": 3, "style": { "bgColor": "#fff3e0" } }
  ]
}
```
