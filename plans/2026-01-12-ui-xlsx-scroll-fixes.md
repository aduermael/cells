# UI, XLSX Styling, and Scroll Fixes

Fix three issues: XLSX styling differences from Excel, scroll rendering during fast inertial scrolling, and missing AI panel button.

## Phase 1: Fix XLSX Number Format Import

The XLSX reader parses `numFmtId` from cell formats but never applies it to `cell->formatId`. This causes:
- Currency values showing as raw numbers (e.g., "2.29" instead of "$ 2.29")
- Percentages showing as decimals (e.g., "0.3" instead of "30.0%")
- Dates showing as Excel serial numbers (e.g., "42735" instead of "2016-12-31")

- [ ] 1a: Parse custom number formats from `<numFmts>` element in styles.xml into a map of numFmtId -> format code
- [ ] 1b: Create helper to map XLSX numFmtId to Cells format ID (handle both built-in formats 0-163 and custom formats 164+)
- [ ] 1c: Apply formatId to cells during XLSX import based on their style's numFmtId
- [ ] 1d: Add unit tests for number format import (currency, percentage, date formats)

## Phase 2: Implement Merged Cells Support

The `<mergeCells>` element in XLSX is completely ignored, causing:
- Merged text appearing truncated (only in first cell)
- Section headers with backgrounds only styling first cell

- [ ] 2a: Add merged cell range storage to Sheet model (vector of merge ranges with top-left anchor)
- [ ] 2b: Parse `<mergeCells><mergedCell ref="A2:E2"/>` from worksheet XML during import
- [ ] 2c: Update grid renderer to span text/styling across merged cell ranges
- [ ] 2d: Export merged cells back to XLSX during write
- [ ] 2e: Add tests for merged cell import/export round-trip

## Phase 3: Improve Scroll Rendering with Minimum Frequency

During fast inertial scrolling, the 16ms debounce on `fetchViewportNow()` can cause the grid to appear empty because data fetching lags behind rendering.

- [ ] 3a: Add throttled rendering with minimum frequency guarantee (render at least every 100ms during scroll, even if debounced fetch is pending)
- [ ] 3b: Show stale cached cell data during scroll instead of empty cells (render what we have, even if viewport fetch is pending)
- [ ] 3c: Add visual indicator for cells being loaded (subtle loading state) to reduce perceived emptiness
- [ ] 3d: Test scroll performance with large spreadsheets to ensure no lag is introduced

## Phase 4: Restore AI Panel Button

The AI panel button (`chat-open-btn`) is hidden by default in HTML and never made visible. The button should be visible when the panel is closed.

- [ ] 4a: Remove `hidden` class from `chat-open-btn` in index.html so button is visible by default
- [ ] 4b: Adjust bottom bar layout so AI button doesn't overlap zoom controls (add proper spacing/margin between zoom-controls and chat-open-btn)
- [ ] 4c: Verify AI panel show/hide toggle works correctly with button visibility
- [ ] 4d: Test that zoom controls remain accessible when AI panel is open
