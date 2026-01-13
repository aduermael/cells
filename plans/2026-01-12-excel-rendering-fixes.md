# Excel Rendering Fixes

Fix remaining visual differences between our app and Excel when rendering XLSX files, focusing on the LBO model test file.

## Phase 1: Theme and Indexed Color Support

Excel XLSX files use theme colors (e.g., `theme="4" tint="-0.5"`) and indexed colors instead of direct RGB values. Currently only RGB colors are parsed, causing missing blue/grey backgrounds for section titles.

- [x] 1a: Parse theme.xml from XLSX to extract the 12 theme colors (dk1, lt1, dk2, lt2, accent1-6, hlink, folHlink). Added `parseThemeXml()` function and `XLSXThemeColors` struct to extract colors from `xl/theme/theme1.xml`.
- [x] 1b: Implement tint application algorithm (tint < 0 darkens toward black, tint > 0 lightens toward white). Added `applyTint()` function using HSL color space conversion per ECMA-376 spec.
- [x] 1c: Update fill color parsing in xlsx_reader.cc to resolve theme colors with tint. Updated `parseStylesXml()` to use `resolveColor()` for fill colors.
- [x] 1d: Update font color parsing to also support theme colors. Updated font and border color parsing to use the unified `resolveColor()` function.
- [x] 1e: Add indexed color lookup table (Excel's 64 standard indexed colors) for `indexed="N"` attributes. Added `kIndexedColors` array and `getIndexedColor()` function.
- [x] 1f: Add tests for theme color parsing with the LBO model file. Added `ReadThemeColorsFromLBOModel` and `ReadLightGrayBackgroundFromTheme` tests verifying theme colors are correctly resolved.

## Phase 2: Border Deduplication

When adjacent cells both have borders (e.g., cell A has bottom border, cell B below has top border), both borders render creating a visually thicker line. Excel only renders one border.

**Design considerations:** Ideally borders should be shared between cells (each border has 2 owners), but with UUID-based sparse cells this is complex, especially when moving cells. Alternative approach: keep all 4 borders per cell but render centered on the edge, drawing the most visible line (thicker, darker) in priority over neighbor's line.

- [x] 2a: In grid-renderer.ts, before drawing borders, build a map of all border edges by position. Added `_edgeKey()` and `_buildBorderEdgeMap()` methods.
- [x] 2b: When two cells share an edge, only draw the border once (prefer thicker/darker line, or cell that appears first as tiebreaker). Added `_getBorderPriority()` that considers line width first, then color darkness.
- [x] 2c: Ensure borders render centered on cell edges rather than inside/outside. Rewrote `_drawCellBorders()` to iterate unique edges and draw centered with 0.5px offset for crisp lines.
- [x] 2d: Add test case verifying border thickness is correct for adjacent bordered cells. Added test to lbo-integration.test.mjs that identifies shared edges (found 29 in LBO model).

## Phase 3: Text Alignment Fixes

Some cells show different alignment in our app vs Excel. Need to verify alignment parsing handles all cases.

- [x] 3a: Debug alignment parsing for specific misaligned cells in the LBO model (identify which cells are wrong). Found that XLSX files use "general" alignment (content-type-aware) when no explicit alignment is set, but we were defaulting to LEFT.
- [x] 3b: Check if default alignment differs (Excel defaults vary by cell content type - numbers right-align, text left-aligns). Confirmed: Excel uses "general" alignment which means numbers/dates right-align, text left-aligns.
- [x] 3c: Implement content-type-based default alignment if not already done. Added GENERAL enum value to TextAlign, updated XLSX parser to return GENERAL for missing/empty alignment, updated renderer to resolve alignment based on cell type (n/d/t → right, s/b/e → left, f → depends on result).
- [x] 3d: Add tests for alignment edge cases. Added C++ unit test `ReadGeneralAlignmentFromLBOModel` and E2E test `Numbers use right alignment by default (general alignment)`.

## Phase 4: Freeze Pane Scrolling

Freeze pane separator lines are drawn but the actual freezing behavior during scroll is not implemented.

- [x] 4a: Modify scroll handling to keep frozen rows at fixed Y position (offset content area only). Updated `getDragAdjustedRowY()` in grid-header-renderer.ts to not apply scrollY for frozen rows (row < freezeRow).
- [x] 4b: Modify scroll handling to keep frozen columns at fixed X position. Updated `getDragAdjustedColX()` to not apply scrollX for frozen columns (col < freezeCol).
- [x] 4c: Adjust cell rendering to draw frozen cells at their fixed positions, not scrolled positions. Modified render() in grid-renderer.ts to draw in 4 quadrants: Q1 (scrollable), Q2 (frozen rows), Q3 (frozen columns), Q4 (frozen corner), with proper clipping regions.
- [x] 4d: Ensure selection and cell editor work correctly with frozen panes. Position functions handle frozen/non-frozen correctly; selection rendering uses same position calculations.
- [x] 4e: Add E2E test that scrolls and verifies frozen cells remain visible. Added `'Frozen cells remain visible when scrolling'` test to lbo-integration.test.mjs that sets freeze panes, scrolls, and verifies frozen cell positions don't change.

## Phase 5: Proper Zoom Implementation

Current zoom uses CSS transform which scales pixels but doesn't change the viewport. Zoom should display more/fewer cells while maintaining crisp rendering.

- [x] 5a: Remove CSS transform-based zoom approach. Changed `setZoomScale()` to clear CSS transform instead of applying it.
- [x] 5b: Implement zoom by adjusting effective column widths and row heights. Added zoom helper functions (`getZoomedHeaderHeight/Width`, `getZoomedColWidth`, `getZoomedRowHeight`, `getZoomedFontSize`, `getZoomedCellPadding`) to grid-constants.ts.
- [x] 5c: Update cell rendering to use zoomed dimensions for positioning. Updated `_drawCellBackgrounds()`, `_drawGridLines()`, `_drawCellBorders()`, and render() quadrant clipping to use zoomed dimensions.
- [x] 5d: Update text rendering to scale font size by zoom factor. Updated `_drawCellValues()` to use `getZoomedFontSize()` and zoomed padding.
- [x] 5e: Ensure hit-testing and selection use zoomed coordinates correctly. Updated grid-utils.ts (`getColAtX`, `getRowAtY`, resize/drop target functions) and grid-events.ts to apply zoom factor. Mouse coordinates are now 1:1 with canvas since no CSS transform.
- [x] 5f: Verify scroll behavior works correctly with zoomed viewport. Scroll values remain in logical units; zoomed scroll offset applied when rendering positions.
- [x] 5g: Updated zoom E2E test to verify zoom scale is stored in renderer instead of checking for CSS transform.

## Phase 6: Document Title Selection Cleanup

When editing the document title and clicking on a cell, the title selection sometimes remains visible.

- [ ] 6a: Investigate the blur/focus flow between workbook-title-editor and canvas
- [ ] 6b: Clear window selection when canvas receives focus
- [ ] 6c: Add test for title edit followed by cell selection
