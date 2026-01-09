# Cell Styles and Enhanced Formatting

Status: READY
Created At: 2026-01-09 02:13 UTC
Updated At: 2026-01-09 19:45 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |

---

## Overview

Add comprehensive cell styling support including text formatting (bold, italic, underline), colors (background, text), fonts, alignment, and expose these through the Luau scripting API. Also fix multi-cell selection to apply format/style changes to all selected cells.

### Current State

- **Number formats**: Working via `formatId` on Cell struct, applied via `CELL_SET_FORMAT` CRDT op
- **Format controls**: Located on right side of formula bar (General dropdown, currency, decimals, percent)
- **Selection**: Tracked via `UIStateMachine.getSelectionRange()` with start/end positions
- **Bug**: Format changes only apply to single cell (`getSelectedCell()`), ignoring range selection
- **Luau API**: `getCell()` exposes `.value`, `.formula`, `.ref` but not `.format` or `.style`
- **AI Agent**: Tool definition includes basic API but no formatting functions

### Design Decisions

1. **Style as separate CRDT field**: Add `styleId` to Cell (like `formatId`) rather than embedding inline
2. **Style registry**: Define styles in Workbook similar to custom formats (allows CRDT sync)
3. **Style struct**: Contains bold, italic, underline, bgColor, textColor, fontFamily, fontSize, hAlign, vAlign
4. **Toolbar layout**: Style buttons on LEFT of format controls in formula bar
5. **Multi-cell operations**: Loop through selection range, apply same op to each cell
6. **Luau API**: `cell.style` returns style object, `setStyle(range, style)` for bulk operations

---

## Phase 1: Cell Style Data Model (C++)

Add style support to the core data model with CRDT synchronization.

- [x] 1a: Define CellStyle struct in model.h
  - Add `enum class TextAlign { LEFT, CENTER, RIGHT, JUSTIFY }`
  - Add `enum class VerticalAlign { TOP, MIDDLE, BOTTOM }`
  - Add `struct CellStyle { bool bold, italic, underline; std::string bgColor, textColor, fontFamily; uint8_t fontSize; TextAlign hAlign; VerticalAlign vAlign; }`
  - Add `ID styleId` field to Cell struct
  - Add style registry to Workbook (like custom formats)

- [x] 1b: Add CRDT operations for styles
  - Add `CELL_SET_STYLE` operation type in operation.h
  - Add `STYLE_DEFINE` operation type for style definitions
  - Implement `makeCellSetStyleOp()` in crdt.cc
  - Implement `makeStyleDefineOp()` in crdt.cc
  - Handle in `applyOperation()` switch

- [x] 1c: Add style serialization for ZCD format
  - JSON payload format: `{"bold":true,"italic":false,"bgColor":"#FF0000",...}`
  - Add style serialization in serializer.cc for .cells/.zcd format
  - Add style deserialization in deserializer.cc
  - Add `bootstrapOpLog` support for styles
  - Ensure styles are included in ZCD file output

- [x] 1d: Add C++ unit tests
  - Test CellStyle struct defaults
  - Test CELL_SET_STYLE operation application
  - Test STYLE_DEFINE operation
  - Test style serialization round-trip
  - Test LWW conflict resolution for styles

---

## Phase 2: File Format Import/Export

Ensure styles are properly preserved when importing/exporting XLSX and ZCD files.

- [x] 2a: XLSX style import
  - Parse cell styles from XLSX `styles.xml` (fonts, fills, borders, alignment)
  - Map XLSX font properties to CellStyle (bold, italic, underline, fontFamily, fontSize)
  - Map XLSX fill patterns to bgColor (solid fills, pattern fills)
  - Map XLSX font color to textColor
  - Map XLSX alignment to hAlign/vAlign
  - Update xlsx_reader.cc to populate Cell.styleId

- [x] 2b: XLSX style export
  - Generate `styles.xml` with cell styles on XLSX export
  - Create font entries for unique font combinations
  - Create fill entries for unique background colors
  - Create cellXfs entries mapping fonts + fills + alignment
  - Update xlsx_writer.cc to reference style indices
  - Handle style deduplication (same style = same index)

- [x] 2c: ZCD round-trip tests
  - Create test file with various styles (bold, colors, fonts, alignment)
  - Save to ZCD, reload, verify all styles preserved
  - Test edge cases: empty styles, partial styles, all properties set
  - Add to C++ unit test suite

- [x] 2d: XLSX round-trip tests
  - Import styled XLSX file
  - Export back to XLSX
  - Re-import and verify styles match original
  - Test with real-world XLSX files (Excel, Google Sheets exports)
  - Add to C++ unit test suite

- [x] 2e: CSV export consideration
  - CSV has no style support (document this limitation)
  - Optionally: warn user when exporting styled sheet to CSV

---

## Phase 3: WASM Bindings and TypeScript Types

Expose style operations through the WASM bridge.

- [x] 3a: Add bindings for style operations
  - `setCellStyle(cellId, styleJson)` - set style on single cell
  - `setCellStyleAt(col, row, styleJson)` - set style by position
  - `getCellStyle(cellId)` - get style as JSON
  - `createStyle(styleJson)` - register style, return styleId
  - `getAvailableStyles()` - list registered styles

- [x] 3b: Add TypeScript types
  - Add `CellStyle` interface in types.ts
  - Add `TextAlign` and `VerticalAlign` enums
  - Update `CellData` to include `styleId` and `style`
  - Add style-related methods to `CellsClient` interface

- [x] 3c: Update WasmDataSource
  - Add `setCellStyleAt(col, row, style)` method
  - Add `getCellStyleAt(col, row)` method
  - Add range methods: `setStyleForRange(startCol, startRow, endCol, endRow, style)`

---

## Phase 4: Style Toolbar UI

Add style buttons to the left side of the format controls in the formula bar.

- [x] 4a: Update HTML structure
  - Add `#style-controls` div before `#format-controls` in formula-bar
  - Add row 1: Bold (B), Italic (I), Underline (U) buttons
  - Add row 2: Background color picker, Text color picker
  - Add row 3: Font dropdown, Font size dropdown
  - Add separator between style and format controls

- [x] 4b: Create StyleControls class
  - New file: `apps/wasm/src/style-controls.ts`
  - Mirror structure of FormatControls
  - Handle button clicks, apply styles via WasmDataSource
  - Update active state based on current cell's style

- [x] 4c: Add color picker component
  - Palette with common colors (8-16 colors)
  - Custom color input (hex)
  - "No color" / transparent option for background
  - Recent colors row (track last 5 used) - deferred

- [x] 4d: Add font controls
  - Font family dropdown: System fonts + common web-safe fonts
  - Font size dropdown: 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72
  - Show current cell's font in dropdowns

- [x] 4e: Add CSS styles
  - Style button active states
  - Color picker styling (light/dark theme)
  - Font dropdown styling
  - Keyboard shortcuts display

---

## Phase 5: Alignment Controls

Add horizontal and vertical alignment buttons.

- [x] 5a: Update HTML with alignment buttons
  - Add alignment button group after font controls
  - Horizontal: Left, Center, Right (mutually exclusive)
  - Vertical: Top, Middle, Bottom (mutually exclusive)
  - Use toggle button group pattern

- [ ] 5b: Implement alignment in StyleControls
  - Handle alignment button clicks
  - Update button active states
  - Apply alignment via WasmDataSource

- [ ] 5c: Update grid renderer for alignment
  - Read cell style for alignment values
  - Adjust text drawing position based on alignment
  - Handle text overflow with alignment

---

## Phase 6: Multi-Cell Selection Support

Fix format and style operations to apply to all selected cells.

- [ ] 6a: Update FormatControls for multi-cell
  - Change `getSelectedCell()` to use `getSelectionRange()`
  - Loop through range: `for col in [startCol, endCol], for row in [startRow, endRow]`
  - Apply format to each cell in range
  - Single CRDT transaction for atomicity (if supported) or sequential ops

- [ ] 6b: Update StyleControls for multi-cell
  - Same pattern as FormatControls
  - Apply style to each cell in selection range
  - Update UI to show "mixed" state when selection has different styles

- [ ] 6c: Add "mixed" state UI indicators
  - Format dropdown: Show "Multiple" when cells have different formats
  - Style buttons: Show indeterminate state for mixed bold/italic/underline
  - Color pickers: Show "mixed" indicator for different colors

- [ ] 6d: Add E2E tests for multi-cell formatting
  - Select range, apply format, verify all cells updated
  - Select range, apply style, verify all cells updated
  - Verify mixed state display

---

## Phase 7: Grid Renderer Style Support

Update the canvas renderer to display styled text.

- [ ] 7a: Add style-aware text rendering
  - Fetch cell style from data source
  - Set canvas font with bold/italic
  - Draw underline as separate line
  - Apply text alignment calculations

- [ ] 7b: Add background color rendering
  - Draw cell background before grid lines (or after, with proper layering)
  - Handle transparent/no background
  - Consider performance (batch by color)

- [ ] 7c: Add text color rendering
  - Set fillStyle for text based on cell style
  - Default to theme text color
  - Ensure contrast with background

- [ ] 7d: Performance optimization
  - Cache computed styles per cell
  - Batch draw operations by style (all bold text, all red backgrounds, etc.)
  - Only recalculate visible cells

---

## Phase 8: Luau Scripting API

Expose formatting and styling through the Luau API.

- [ ] 8a: Add cell.format property
  - Read: returns format ID string or nil
  - Write: `cell.format = "FMT_C002"` sets format via CRDT
  - Add to luaCellIndex metamethod

- [ ] 8b: Add cell.style property
  - Read: returns style table `{bold=true, bgColor="#FF0000", ...}`
  - Write: `cell.style = {bold=true}` (merges with existing)
  - Write: `cell.style = nil` clears style
  - Add to luaCellIndex and luaCellNewindex metamethods

- [ ] 8c: Add setFormat() function
  - `setFormat(range, formatId)` - apply format to range
  - `setFormat("A1:B10", "FMT_P002")` - percentage with 2 decimals
  - Range parsing reuses existing parseA1Range helper

- [ ] 8d: Add setStyle() function
  - `setStyle(range, styleTable)` - apply style to range
  - `setStyle("A1:B10", {bold=true, bgColor="#FFFF00"})`
  - Merges provided properties with defaults

- [ ] 8e: Add getFormats() function
  - `getFormats()` - returns array of available format IDs with descriptions
  - Useful for scripts to discover valid format options

- [ ] 8f: Add helper style constants
  - `ALIGN_LEFT`, `ALIGN_CENTER`, `ALIGN_RIGHT`
  - `VALIGN_TOP`, `VALIGN_MIDDLE`, `VALIGN_BOTTOM`
  - Common colors: `COLOR_RED`, `COLOR_GREEN`, `COLOR_BLUE`, etc.

- [ ] 8g: Add Luau unit tests
  - Test cell.format read/write
  - Test cell.style read/write
  - Test setFormat() on ranges
  - Test setStyle() on ranges
  - Test style merging behavior

---

## Phase 9: AI Agent Integration

Update the AI agent's tool definition to include formatting APIs.

- [ ] 9a: Update execute_code tool description
  - Add format functions: `setFormat(range, formatId)`, `getFormats()`
  - Add style functions: `setStyle(range, style)`, cell.style
  - Document style table properties
  - Add alignment constants

- [ ] 9b: Add example prompts for formatting
  - "Make the header row bold with a blue background"
  - "Format column B as currency"
  - "Center align all cells in the table"
  - Test these prompts work correctly

- [ ] 9c: Update system prompt with style capabilities
  - Describe available style properties
  - Provide common styling patterns
  - Include color naming conventions

---

## Phase 10: Keyboard Shortcuts

Add keyboard shortcuts for common formatting operations.

- [ ] 10a: Implement text formatting shortcuts
  - Cmd/Ctrl+B: Toggle bold
  - Cmd/Ctrl+I: Toggle italic
  - Cmd/Ctrl+U: Toggle underline
  - Only active when not editing cell content

- [ ] 10b: Document shortcuts
  - Add tooltips to buttons showing shortcuts
  - Consider adding keyboard shortcut help modal

---

## Phase 11: Final Polish and Testing

- [ ] 11a: Run full test suite
  - Unit tests: `make test`
  - E2E tests: `cd apps/wasm && npm run test:parallel -- stable`
  - Lint: `make lint`
  - Format check: `make format-check`

- [ ] 11b: Manual testing
  - Test all style buttons
  - Test multi-cell selection with styles
  - Test Luau API
  - Test AI agent formatting commands
  - Test CRDT sync between peers
  - Test XLSX import/export with styled files
  - Test ZCD round-trip preserves all styles

- [ ] 11c: Update plan status to DONE
  - Rename file with -DONE suffix
  - Set status to DONE

---

## Technical Notes

### Style ID Generation

Style IDs follow the same pattern as format IDs:
- Built-in styles: `STY_BOLD`, `STY_ITAL`, etc.
- Custom styles: 8-char base62 UUID

### Color Format

Colors use CSS hex format:
- `#RRGGBB` - 6-char hex
- `#RGB` - 3-char shorthand (expanded internally)
- `transparent` or empty string for no color

### Font Stack

Default font: `system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif`

Available fonts (common web-safe):
- Arial
- Helvetica
- Times New Roman
- Georgia
- Courier New
- Verdana
- Trebuchet MS

### CRDT Considerations

- Style changes follow same LWW (Last-Writer-Wins) as cell values
- Each cell tracks its own style independently
- Style definitions are workbook-level (shared across sheets)
- Bootstrap generates STYLE_DEFINE ops before CELL_SET_STYLE ops

### Performance Budget

- Style lookup: < 1ms per cell
- Batch style operations: < 100ms for 1000 cells
- Render with styles: < 16ms for visible viewport (60fps)

### XLSX Format Reference (ECMA-376 / ISO/IEC 29500)

XLSX files follow the **Office Open XML (OOXML)** standard:
- **ECMA-376**: https://www.ecma-international.org/publications-and-standards/standards/ecma-376/
- **ISO/IEC 29500**: https://www.iso.org/standard/71691.html

File structure (ZIP archive):
```
xl/
├── workbook.xml         # Workbook structure, sheet references
├── styles.xml           # Styles: fonts, fills, borders, numFmts, cellXfs
├── sharedStrings.xml    # Shared string table (optional)
└── worksheets/
    └── sheetN.xml       # Cell data with style index references (s="N")
```

Key elements in `xl/styles.xml`:
- `<numFmts>` - Custom number format codes
- `<fonts>` - Font definitions (name, size, bold, italic, color)
- `<fills>` - Fill patterns and colors (patternFill, fgColor, bgColor)
- `<borders>` - Border styles (left, right, top, bottom, diagonal)
- `<cellXfs>` - Cell format records combining font + fill + border + alignment + numFmt

Cell style reference in `xl/worksheets/sheetN.xml`:
```xml
<c r="A1" s="3" t="s">  <!-- s="3" references cellXfs index 3 -->
  <v>0</v>
</c>
```

Color formats in XLSX:
- `<color rgb="FFRRGGBB"/>` - ARGB hex (alpha + RGB)
- `<color theme="N"/>` - Theme color index
- `<color indexed="N"/>` - Legacy indexed color
