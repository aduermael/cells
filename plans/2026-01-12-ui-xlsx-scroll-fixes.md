# UI, XLSX Styling, and Scroll Fixes

Fix three issues: XLSX styling differences from Excel, scroll rendering during fast inertial scrolling, and missing AI panel button.

## Phase 1: Fix XLSX Number Format Import

The XLSX reader parses `numFmtId` from cell formats but never applies it to `cell->formatId`. This causes:
- Currency values showing as raw numbers (e.g., "2.29" instead of "$ 2.29")
- Percentages showing as decimals (e.g., "0.3" instead of "30.0%")
- Dates showing as Excel serial numbers (e.g., "42735" instead of "2016-12-31")

- [x] 1a: Parse custom number formats from `<numFmts>` element in styles.xml into a map of numFmtId -> format code. Added `customNumFormats` map to `XLSXStyles` struct and parsing logic in `parseStylesXml()`.
- [x] 1b: Create helper to map XLSX numFmtId to Cells format ID (handle both built-in formats 0-163 and custom formats 164+). Added `mapNumFmtIdToFormatId()` function that handles Excel built-in formats and parses custom format codes.
- [x] 1c: Apply formatId to cells during XLSX import based on their style's numFmtId. Added `getFormatId` lambda with caching and applied formatId to cells during import.
- [x] 1d: Add unit tests for number format import (currency, percentage, date formats). Added 3 tests: `ReadNumberFormatsFromLBOModel`, `ReadNumberFormatsWithStyles`, `NumberFormatsNotImportedWhenStylesDisabled`.

## Phase 2: Implement Merged Cells Support

The `<mergeCells>` element in XLSX is completely ignored, causing:
- Merged text appearing truncated (only in first cell)
- Section headers with backgrounds only styling first cell

- [x] 2a: Add merged cell range storage to Sheet model (vector of merge ranges with top-left anchor). Added `MergeRange` struct and storage to Sheet with index for O(1) lookup by position.
- [x] 2b: Parse `<mergeCells><mergedCell ref="A2:E2"/>` from worksheet XML during import. Added parsing after cell creation in xlsx_reader.cc.
- [x] 2c: Update grid renderer to span text/styling across merged cell ranges. Added merge properties to CellData types, viewport bindings, and grid renderer to draw backgrounds and text across merged regions.
- [x] 2d: Export merged cells back to XLSX during write. Added `<mergeCells>` element generation in xlsx_writer.cc generateWorksheet().
- [x] 2e: Add tests for merged cell import/export round-trip. Added 3 tests: API verification, roundtrip, and styled merges. Fixed xlsx_reader to create missing columns/rows for merge ranges.

## Phase 3: Improve Scroll Rendering with Minimum Frequency

The current debounce on `fetchViewportNow()` resets on every scroll event. With trackpad inertia, scroll events continue firing for several seconds after the user lifts their fingers. Each event resets the debounce timer, so the fetch/render may never happen until inertia completely stops - making the grid appear empty or frozen for a long time.

**Solution:** Replace pure debounce with throttle + trailing debounce. Guarantee viewport fetches happen at a minimum frequency (e.g., every 100-150ms) during continuous scrolling, with a single trailing fetch after scrolling stops.

- [ ] 3a: Change `fetchViewportNow()` to throttle pattern: fetch immediately if enough time has passed since last fetch, track `lastFetchTime` timestamp
- [ ] 3b: Add single trailing fetch: schedule one final fetch after throttle interval, cancel/reschedule if new scroll events arrive, but don't re-trigger once it fires
- [ ] 3c: Add viewport buffer/overscan: fetch extra rows/columns beyond visible area (e.g., 10-20 rows above/below, 5-10 cols left/right) so small scrolls show pre-fetched data
- [ ] 3d: Keep rendering cached cell data during scroll (render what we have, fetch updates will fill in new areas)
- [ ] 3e: Test with long inertial scrolls and small scroll movements to verify behavior

## Phase 4: Restore AI Panel Button

The AI panel button (`chat-open-btn`) is hidden by default in HTML and never made visible. The button should be visible when the panel is closed.

- [ ] 4a: Remove `hidden` class from `chat-open-btn` in index.html so button is visible by default
- [ ] 4b: Adjust bottom bar layout so AI button doesn't overlap zoom controls (add proper spacing/margin between zoom-controls and chat-open-btn)
- [ ] 4c: Verify AI panel show/hide toggle works correctly with button visibility
- [ ] 4d: Test that zoom controls remain accessible when AI panel is open
