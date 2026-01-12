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

The current debounce on `fetchViewportNow()` resets on every scroll event. With trackpad inertia, scroll events continue firing for several seconds after the user lifts their fingers. Each event resets the debounce timer, so the fetch/render may never happen until inertia completely stops - making the grid appear empty or frozen for a long time.

**Solution:** Replace pure debounce with throttle + trailing debounce. Guarantee viewport fetches happen at a minimum frequency (e.g., every 100-150ms) during continuous scrolling, while still debouncing the final fetch after scrolling stops.

- [ ] 3a: Change `fetchViewportNow()` from debounce to throttle-with-trailing pattern: fetch immediately if enough time has passed since last fetch, otherwise schedule trailing fetch
- [ ] 3b: Keep rendering with cached/stale cell data during scroll (render what we have for visible cells, fetch will update them)
- [ ] 3c: Test with long inertial scrolls to verify grid stays populated and responsive
- [ ] 3d: Tune throttle interval (100-150ms) to balance responsiveness vs WASM worker load

## Phase 4: Restore AI Panel Button

The AI panel button (`chat-open-btn`) is hidden by default in HTML and never made visible. The button should be visible when the panel is closed.

- [ ] 4a: Remove `hidden` class from `chat-open-btn` in index.html so button is visible by default
- [ ] 4b: Adjust bottom bar layout so AI button doesn't overlap zoom controls (add proper spacing/margin between zoom-controls and chat-open-btn)
- [ ] 4c: Verify AI panel show/hide toggle works correctly with button visibility
- [ ] 4d: Test that zoom controls remain accessible when AI panel is open
