# Excel Rendering Fixes

Fix remaining visual differences between our app and Excel when rendering XLSX files, focusing on the LBO model test file.

## Phase 1: Theme and Indexed Color Support

Excel XLSX files use theme colors (e.g., `theme="4" tint="-0.5"`) and indexed colors instead of direct RGB values. Currently only RGB colors are parsed, causing missing blue/grey backgrounds for section titles.

- [ ] 1a: Parse theme.xml from XLSX to extract the 12 theme colors (dk1, lt1, dk2, lt2, accent1-6, hlink, folHlink)
- [ ] 1b: Implement tint application algorithm (tint < 0 darkens toward black, tint > 0 lightens toward white)
- [ ] 1c: Update fill color parsing in xlsx_reader.cc to resolve theme colors with tint
- [ ] 1d: Update font color parsing to also support theme colors
- [ ] 1e: Add indexed color lookup table (Excel's 64 standard indexed colors) for `indexed="N"` attributes
- [ ] 1f: Add tests for theme color parsing with the LBO model file

## Phase 2: Border Deduplication

When adjacent cells both have borders (e.g., cell A has bottom border, cell B below has top border), both borders render creating a visually thicker line. Excel only renders one border.

- [ ] 2a: In grid-renderer.ts, before drawing borders, build a map of all border edges by position
- [ ] 2b: When two cells share an edge, only draw the border once (prefer the cell that appears first, or use priority rules)
- [ ] 2c: Add test case verifying border thickness is correct for adjacent bordered cells

## Phase 3: Text Alignment Fixes

Some cells show different alignment in our app vs Excel. Need to verify alignment parsing handles all cases.

- [ ] 3a: Debug alignment parsing for specific misaligned cells in the LBO model (identify which cells are wrong)
- [ ] 3b: Check if default alignment differs (Excel defaults vary by cell content type - numbers right-align, text left-aligns)
- [ ] 3c: Implement content-type-based default alignment if not already done
- [ ] 3d: Add tests for alignment edge cases

## Phase 4: Freeze Pane Scrolling

Freeze pane separator lines are drawn but the actual freezing behavior during scroll is not implemented.

- [ ] 4a: Modify scroll handling to keep frozen rows at fixed Y position (offset content area only)
- [ ] 4b: Modify scroll handling to keep frozen columns at fixed X position
- [ ] 4c: Adjust cell rendering to draw frozen cells at their fixed positions, not scrolled positions
- [ ] 4d: Ensure selection and cell editor work correctly with frozen panes
- [ ] 4e: Add E2E test that scrolls and verifies frozen cells remain visible

## Phase 5: Proper Zoom Implementation

Current zoom uses CSS transform which scales pixels but doesn't change the viewport. Zoom should display more/fewer cells while maintaining crisp rendering.

- [ ] 5a: Remove CSS transform-based zoom approach
- [ ] 5b: Implement zoom by adjusting effective column widths and row heights (multiply by zoom factor)
- [ ] 5c: Update cell rendering to use zoomed dimensions for positioning
- [ ] 5d: Update text rendering to scale font size by zoom factor
- [ ] 5e: Ensure hit-testing and selection use zoomed coordinates correctly
- [ ] 5f: Verify scroll behavior works correctly with zoomed viewport
- [ ] 5g: Add test for zoom in/out displaying correct number of cells

## Phase 6: Document Title Selection Cleanup

When editing the document title and clicking on a cell, the title selection sometimes remains visible.

- [ ] 6a: Investigate the blur/focus flow between workbook-title-editor and canvas
- [ ] 6b: Clear window selection when canvas receives focus
- [ ] 6c: Add test for title edit followed by cell selection
