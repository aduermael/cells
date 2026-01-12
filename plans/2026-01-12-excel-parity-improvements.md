# Excel Parity Improvements

Comprehensive fixes to improve Excel compatibility: named range highlighting, column/row sizing on XLSX import, cell text overflow, border support, and UI improvements for zoom controls and AI panel.

## Phase 1: Named Range Highlighting in Formulas

Named ranges should be highlighted when editing a formula that references them, similar to how cell/range references are highlighted. Currently, FormulaHighlight only supports types: "cell", "range", "column", "row".

- [x] 1a: Add "named" type to FormulaHighlight interface in grid-constants.ts with name field and resolved target (cell/range/column/row info). Added namedRangeName and namedTargetType fields.
- [x] 1b: Update formula parser (C++) to return named range references in ReferenceInfo, resolving the named range to its target coordinates. Updated bindings_formula.cc to resolve named ranges and return targetType with col/row coordinates.
- [x] 1c: Update init-rendering.ts referenceToHighlight to handle named range type and create FormulaHighlight with resolved coordinates. Also updated grid-formula-renderer.ts to render named ranges based on their target type.
- [x] 1d: Add visual effect when hovering over a named range in the formula bar. Added dotted underline style and tooltip showing "Named: [name]" on hover via CSS pseudo-element.

## Phase 2: Fix Zoom Reset on Scroll

The zoom level resets to 100% during scrolling because `setStateRefs` is called with `sheetInfo.zoomScale` which comes from the backend (always 100) and overwrites the UI-modified zoom. The zoom should be stored in the renderer and only synced from sheetInfo on initial load.

- [ ] 2a: Modify GridRenderer.setStateRefs to only set zoom from sheetInfo on first call (track with flag), not on subsequent updates
- [ ] 2b: Add setter on ZoomControls to update the renderer's zoom without triggering sheetInfo sync
- [ ] 2c: Test that zoom persists during scrolling, sheet switching, and viewport fetches

## Phase 3: Gradual Zoom Slider

Replace discrete zoom buttons with a slider that allows gradual zoom from 10% to 400% like Excel.

- [ ] 3a: Add HTML slider element to bottom bar between zoom out/in buttons (`<input type="range" min="10" max="400">`)
- [ ] 3b: Update zoom-controls.ts to handle slider input events, syncing slider position with zoom level
- [ ] 3c: Style the slider to match the app theme (compact, fits in bottom bar)
- [ ] 3d: Keep +/- buttons functional for discrete zoom steps, sync slider position when buttons clicked

## Phase 4: AI Panel Above Bottom Bar

Move the AI panel to be positioned above the bottom bar so it doesn't overlap zoom controls. Reposition zoom controls to far right with AI button on its left.

- [ ] 4a: Update chat-panel CSS to position above bottom-bar (bottom: calc(var(--bottom-bar-height) + margin))
- [ ] 4b: Reorder bottom bar elements: sheet tabs | spacer | AI button | zoom controls (zoom controls at far right)
- [ ] 4c: Test that AI panel opens/closes without covering zoom controls or sheet tabs

## Phase 5: XLSX Column Width and Row Height Import

The XLSX reader parses `<cols>` elements but ignores the `width` attribute, defaulting all columns to 100px. Similarly, row heights from `ht` attribute are not applied.

- [ ] 5a: Parse `width` attribute from `<col>` elements in xlsx_reader.cc, store in columnWidths map (convert Excel character-width units to pixels: ~7 pixels per character unit)
- [ ] 5b: Parse `ht` (height) attribute from `<row>` elements, store in rowHeights map (convert from points to pixels: 1pt = 1.33px approximately)
- [ ] 5c: Apply parsed widths to column Axis objects instead of DEFAULT_COLUMN_WIDTH, apply heights to row Axis objects
- [ ] 5d: Add tests for column/row dimension import with the LBO model file

## Phase 6: Cell Text Overflow

Implement Excel-style text overflow: when a cell contains text longer than its width and the adjacent cell(s) to the right are empty, the text should visually extend into those cells.

- [ ] 6a: Add overflow detection in grid-renderer.ts _drawCellContent: measure text width vs cell width
- [ ] 6b: When text overflows right, check if neighbor cells are empty (no value, not part of merge)
- [ ] 6c: If neighbors are empty, extend clip region to include empty neighbors, draw text with full width
- [ ] 6d: Handle left-aligned, center-aligned, and right-aligned text overflow correctly
- [ ] 6e: Ensure overflow text doesn't draw over cells that have content or are selected

## Phase 7: Cell Border Support

Add per-cell border rendering with customizable thickness, color, and style. Currently only grid lines are drawn uniformly.

- [ ] 7a: Add border properties to CellStyle struct in C++ (top/right/bottom/left border color, thickness, style)
- [ ] 7b: Parse `<border>` elements from XLSX styles.xml (left/right/top/bottom with color and style attributes)
- [ ] 7c: Apply parsed borders to CellStyle during XLSX import
- [ ] 7d: Store border data in CellData type for TypeScript rendering
- [ ] 7e: Add border rendering pass in grid-renderer.ts after cell backgrounds, before text (draw borders per-cell based on style)
- [ ] 7f: Export cell borders to XLSX during write

## Phase 8: Integration Testing with LBO Model

Verify all improvements work together with the LBO model file.

- [ ] 8a: Test that column widths match Excel appearance in LBO model
- [ ] 8b: Test that cell text overflow works for labels like "Premium Paid to Target's Share Price:"
- [ ] 8c: Test that borders appear correctly (section dividers, table borders)
- [ ] 8d: Test that zoom slider works and persists during interaction
- [ ] 8e: Test that AI panel positioning doesn't interfere with controls
