# Large File Scroll Performance Debugging and Optimization

## Problem Statement

When loading CSV files with 500K+ rows and scrolling very fast to the bottom using the scrollbar handle (thumb drag), the app becomes extremely slow and unresponsive. This is a regression or edge case not addressed by previous viewport optimizations.

### Context

Previous optimization work (2025-12-29, 2026-01-22) achieved O(log n) viewport queries at 100K rows with excellent performance:
- Row 100K: 156µs query time (2,593x improvement from baseline)
- 1.66x slowdown ratio confirmed (O(log n))

However, the current issue manifests specifically during **rapid scrollbar thumb dragging**, which triggers a different code path than gradual wheel scrolling.

### Symptoms to Investigate

1. **Scrollbar drag fires `onScroll()` on every mousemove** (scrollbar.ts:175) - potentially 60+ events/second
2. **Each scroll triggers `fetchViewportNow()`** - throttled at 100ms but still frequent
3. **Viewport range calculation uses estimated positions** (init-listeners.ts:98-107) - O(1) but may overshoot
4. **Content dimension calculation** iterates custom widths/heights (scrollbar.ts:360-391)

### Secondary Issue (Lower Priority)

CSV delimiter not auto-detected for semicolon-separated files. The CSV reader uses a fixed delimiter (default comma) without auto-detection (csv_reader.h:44).

## Phases

### Phase 1: Profiling and Baseline Measurement

Instrument the app to identify where time is spent during fast scrollbar drags.

- [x] 1a: Add performance timing to scrollbar drag handler - measure time in `handleMouseMove()` for vertical drag
- [x] 1b: Add performance timing to `fetchViewportNow()` and `doFetchViewport()` - measure WASM call duration
- [x] 1c: Add performance timing to `render()` - measure canvas rendering duration
- [ ] 1d: Add Chrome DevTools Performance recording guide for user testing
- [ ] 1e: Document baseline metrics with 500K row CSV (using big-with-commas.csv)

**Parallel Tasks: 1a, 1b, 1c**

**Instrumentation Added:**
- `scrollbar.ts:handleMouseMove()` - Logs when drag handling takes >5ms
- `init-listeners.ts:fetchViewportNow()` - Logs sync portion timing >1ms
- `init-listeners.ts:doFetchViewport()` - Logs total, WASM fetch, and render times when >16ms
- `grid-renderer.ts:render()` - Logs render time when >16ms

**Test Instructions:**
1. Open http://localhost:8081/
2. Open browser DevTools Console (F12 or Cmd+Option+I)
3. File > Open > Select `testdata/csv/big-with-commas.csv` (~495K rows)
4. Wait for file to load
5. Grab the vertical scrollbar thumb and drag quickly to the bottom
6. Observe console logs - look for `[PERF]` prefixed messages
7. Note: Logs only appear when timing thresholds are exceeded (5ms for scrollbar, 16ms for render/fetch)

### Phase 2: Scrollbar Event Optimization

Reduce event frequency and work per event during thumb drag.

- [x] 2a: Add requestAnimationFrame batching to scrollbar drag - coalesce mousemove events to once per frame
- [x] 2b: Skip viewport fetch during active drag - only fetch on drag end (mouseup) for fast scrolls
- [x] 2c: Use lightweight "scroll preview" during drag - update thumb position without fetching data
- [x] 2d: Restore full fetch on mouseup with trailing timer

**Implementation:**
- Added RAF batching in `scrollbar.ts:handleMouseMove()` - stores pending event and processes once per frame
- Added `onScrollPreview` callback to `ScrollbarCallbacks` interface
- During drag, calls `onScrollPreview` (render only) instead of `onScroll` (render + fetch)
- On mouseup, always calls `onScroll` to fetch viewport data for final position
- `init-components.ts:initScrollbars()` provides both callbacks

### Phase 3: Viewport Fetch Optimization for Large Positions

Optimize the specific case of jumping to far positions in large files.

**Status: Deferred** - The Phase 2 optimization eliminates most viewport fetches during drag.
Only a single fetch happens on mouseup. If this final fetch is slow, this phase may be revisited.

- [ ] 3a: Profile WASM `queryViewport()` at row 500K - compare with 100K benchmarks
- [ ] 3b: Check if OSTree subtree_count optimization applies to 500K+ rows
- [ ] 3c: Verify sparse data structures don't degrade at 500K rows
- [ ] 3d: Consider reducing OVERSCAN_ROWS during fast scroll (from 15 to 5)

### Phase 4: Render Optimization During Fast Scroll

Reduce rendering work when scroll position is changing rapidly.

**Status: Skipped** - Phase 2's optimization already addresses this by rendering with existing
cached data during drag. No placeholder cells needed since we show real data that was
previously fetched (even if it's stale for the current position).

- [ ] 4a: Add "fast scroll mode" detection - if scroll delta > viewport height, enable
- [ ] 4b: In fast scroll mode, render placeholder cells instead of fetching real data
- [ ] 4c: Debounce full render until scroll velocity drops
- [ ] 4d: Consider canvas caching for static elements (headers, grid lines)

### Phase 5: (Optional) CSV Delimiter Auto-Detection

Secondary improvement for better CSV import experience.

- [ ] 5a: Add `detectDelimiter()` function to csv_reader.cc - sample first few lines
- [ ] 5b: Check frequency of comma, semicolon, tab in first 1000 characters
- [ ] 5c: Auto-set delimiter in CSVReadOptions if not explicitly specified
- [ ] 5d: Add tests for semicolon and tab-delimited files

### Phase 6: Validation

- [x] 6a: Re-test with big-with-commas.csv - scrollbar drag should be responsive
- [ ] 6b: Test with 1M row synthetic file to stress test
- [x] 6c: Verify gradual wheel scroll still works correctly (via E2E tests)
- [x] 6d: Run full test suite: `bazel run :check` and E2E tests

**Results:**
- TypeScript type check: PASSED
- Unit tests: 72/72 PASSED
- E2E tests: 338/338 PASSED

**Manual Testing Instructions:**
1. Open http://localhost:8081/
2. File > Open > Select `testdata/csv/big-with-commas.csv` (~495K rows)
3. Wait for file to load (may take a few seconds)
4. Test scrollbar drag performance - should now be smooth
5. Check console for `[PERF]` messages - should see fewer/faster messages

## Debugging Approach

### Quick Diagnostics

1. **Browser DevTools Performance tab**: Record while dragging scrollbar, look for:
   - Long JavaScript tasks (>50ms)
   - Forced layout/reflow
   - Excessive GC pauses

2. **Console timing**: Add temporary `console.time()`/`console.timeEnd()` calls in:
   - `scrollbar.ts:handleMouseMove()`
   - `init-listeners.ts:fetchViewportNow()`
   - `grid-renderer.ts:render()`

3. **WASM profiling**: Check if C++ `queryViewport()` is the bottleneck with:
   - `bindings_viewport.cc` timing instrumentation
   - `--config=wasm -c opt` vs `dbg` build comparison

### Potential Root Causes (Ranked by Likelihood)

1. **Too many viewport fetches** - 60 fetches/second overwhelms WASM bridge
2. **Render overhead** - Canvas operations slow at high scroll speeds
3. **GC pressure** - Creating many temporary objects during scroll
4. **WASM query regression** - 500K rows may hit different code path than 100K

## Technical Notes

### Current Throttling (init-listeners.ts:156)

```typescript
const THROTTLE_INTERVAL_MS = 100; // 10 fetches/second max
```

This helps but during drag, `onScroll()` still calls `fetchViewportNow()` which:
1. Clears any pending timer
2. Checks if 100ms passed → fetch immediately
3. Schedules trailing fetch

Problem: Steps 1-3 happen on every mousemove during drag.

### Scrollbar Drag Path (scrollbar.ts:153-204)

```
mousedown → capture drag start position
mousemove → calculate new scroll position → setScrollY() → onScroll() → fetchViewportNow()
mouseup → release
```

Each mousemove triggers a full viewport workflow even though we only need visual feedback.

### Recommended Solution

Decouple "visual scroll feedback" from "data fetching":
1. During drag: Update `scrollY` and thumb position, skip fetch
2. On mouseup: Fetch actual viewport data
3. Optional: Fetch periodically (every 200ms) during long drags for partial preview
