# Toolbar Layout Reorganization

Reorganize the UI layout to be more familiar to Excel users by separating the formatting toolbar from the formula bar.

**Current Layout:**
- Single row containing: cell reference, formula input, style controls, format controls, settings button, code button

**Target Layout:**
- Row 1 (Formatting Toolbar): style controls (B/I/U, font, size, alignment, colors, borders, merge) + format controls (General, $, .0-, .00, %)
- Row 2 (Formula Bar): cell reference dropdown | formula input (full width) | code button

## Phase 1: HTML Structure Reorganization
- [x] 1a: Create new `#formatting-toolbar` div between `#header` and `#formula-bar` in index.html
- [x] 1b: Move `#style-controls` into the new `#formatting-toolbar`
- [x] 1c: Move `#format-controls` into the new `#formatting-toolbar`
- [x] 1d: Remove the settings button (`#settings-btn`) and its container
- [x] 1e: Move `#script-panel-btn` directly into `#formula-bar` (after formula input container)
- [x] 1f: Remove the separator divs (`.style-separator`, `.format-separator`) and `#formula-bar-actions` wrapper

## Phase 2: CSS Layout Updates
- [x] 2a: Add CSS for `#formatting-toolbar` (flex row, same background/border styling as formula-bar, full width)
- [x] 2b: Update `#style-controls` CSS to be horizontal (single row, not two rows stacked)
- [x] 2c: Update `#format-controls` CSS to be horizontal (single row, not two rows stacked)
- [x] 2d: Update `#formula-bar` CSS - formula input container should flex-grow to fill available width
- [x] 2e: Style the code button to be positioned at the right edge of the formula bar (it naturally flows to the right due to flex layout)

## Phase 3: JavaScript/TypeScript Updates
- [x] 3a: Remove settings button element query and event handler from init code (app.ts, init-components.ts)
- [x] 3b: Add formattingToolbar element to app.ts and file-loader.ts, show it when loading files
