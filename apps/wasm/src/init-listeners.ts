// =============================================================================
// Data Change Listeners and Viewport Fetching
// =============================================================================
//
// Handles data synchronization between the WASM data source and UI components.
// Manages viewport fetching, sheet info updates, and change propagation.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Fetch viewport data from WASM (cells, columns, rows)
// - Fetch sheet info and sheet list
// - Handle data change events with batching
// - Update UI caches (colWidths, rowHeights, colNames)
// - Coordinate viewport refresh with debouncing
//
// =============================================================================

import type { App } from "./app";
import type { SheetTabsManager } from "./sheet-tabs";
import type { DataChangeType } from "./wasm-data-source";
import { DEFAULT_COL_WIDTH, DEFAULT_ROW_HEIGHT } from "./grid-renderer";

// =============================================================================
// Types
// =============================================================================

/** Configuration for data listeners */
export interface DataListenersConfig {
  app: App;
  sheetTabsManager: SheetTabsManager;
  render: () => void;
  updateFormulaBar: () => void;
  updateScrollbars: () => void;
}

// =============================================================================
// Data Fetching Setup
// =============================================================================

/**
 * Set up data fetching and change listeners.
 * Returns functions for fetching and handling data changes.
 */
export function setupDataListeners(config: DataListenersConfig): {
  fetchSheetInfo: () => Promise<void>;
  fetchViewport: () => Promise<void>;
  fetchViewportNow: () => void;
  fetchSheets: () => Promise<void>;
  handleDataChanged: (changeType: DataChangeType) => void;
} {
  const {
    app,
    sheetTabsManager,
    render,
    updateFormulaBar,
    updateScrollbars,
  } = config;

  // =========================================================================
  // Data Fetching
  // =========================================================================

  async function fetchSheetInfo(): Promise<void> {
    if (!app.dataSource) return;
    try {
      app.sheetInfo = await app.dataSource.getSheetInfo();
      app.elements.sheetName.textContent = app.sheetInfo.name;
      // Ensure discoveredRows is at least as large as the actual row count
      // plus a buffer to fill the viewport with empty rows below the data.
      // This is critical for correct scroll height calculation when loading files.
      const viewportBuffer = 100; // Extra rows beyond data to enable scrolling/editing
      const minDiscoveredRows = app.sheetInfo.rowCount + viewportBuffer;
      if (minDiscoveredRows > app.discoveredRows) {
        app.discoveredRows = minDiscoveredRows;
      }
    } catch (e) {
      console.error("Error fetching sheet info:", e);
      throw e;
    }
  }

  async function fetchViewport(): Promise<void> {
    if (!app.sheetInfo || !app.dataSource) return;

    const container = document.getElementById("canvas-container");
    if (!container) return;
    const viewWidth = container.clientWidth;
    const viewHeight = container.clientHeight;

    // Overscan: fetch extra rows/columns beyond visible area so small scrolls
    // show pre-fetched data without waiting for a new fetch
    const OVERSCAN_COLS = 8;  // Columns to fetch beyond visible area on each side
    const OVERSCAN_ROWS = 15; // Rows to fetch beyond visible area on each side

    // Calculate scrollable viewport range
    let startCol = Math.max(0, Math.floor(app.scrollX / DEFAULT_COL_WIDTH) - OVERSCAN_COLS);
    let startRow = Math.max(0, Math.floor(app.scrollY / DEFAULT_ROW_HEIGHT) - OVERSCAN_ROWS);
    const endCol = Math.min(
      app.sheetInfo.colCount,
      startCol + Math.ceil(viewWidth / DEFAULT_COL_WIDTH) + OVERSCAN_COLS * 2
    );
    const endRow = Math.min(
      app.sheetInfo.rowCount,
      startRow + Math.ceil(viewHeight / DEFAULT_ROW_HEIGHT) + OVERSCAN_ROWS * 2
    );

    // Always include frozen rows and columns in the viewport
    // This ensures frozen cells are available even when scrolled far away
    const freezeCol = app.sheetInfo.freezeCol || 0;
    const freezeRow = app.sheetInfo.freezeRow || 0;
    if (freezeCol > 0) startCol = 0;
    if (freezeRow > 0) startRow = 0;

    try {
      const data = await app.dataSource.getViewport(
        startCol,
        startRow,
        endCol,
        endRow
      );
      app.cells = data.cells || [];
      app.columns = data.columns || [];
      app.rows = data.rows || [];
      app.styleRanges = data.styleRanges || [];

      // DEBUG: Log style ranges from viewport
      if (app.styleRanges.length > 0) {
        console.log("[DEBUG] Viewport returned styleRanges:", JSON.stringify(app.styleRanges, null, 2));
      }

      // Clear caches for viewport range before repopulating
      for (let col = startCol; col < endCol; col++) {
        app.colWidths.delete(col);
        app.colPixelOffsets.delete(col);
        app.colNames.delete(col);
      }
      for (let row = startRow; row < endRow; row++) {
        app.rowHeights.delete(row);
        app.rowPixelOffsets.delete(row);
      }

      // Repopulate from Workbook
      // NOTE: We only cache widths/heights, NOT pixel offsets. The pixel offsets
      // from the viewport query are tree-based (dense) which is wrong for sparse
      // columns/rows. The rendering code has fallback logic to compute sparse
      // offsets correctly based on position and default widths.
      // Hidden columns/rows get 0 width/height to effectively hide them.
      for (const col of app.columns) {
        app.colWidths.set(col.pos, col.hidden ? 0 : (col.width || DEFAULT_COL_WIDTH));
        if (col.name) {
          app.colNames.set(col.pos, col.name);
        }
      }
      for (const row of app.rows) {
        app.rowHeights.set(row.pos, row.hidden ? 0 : (row.height || DEFAULT_ROW_HEIGHT));
      }
    } catch (e) {
      console.error("Error fetching viewport:", e);
    }
  }

  let fetchInFlight = false;
  let lastFetchTime = 0;
  let trailingFetchTimer: ReturnType<typeof setTimeout> | null = null;
  const THROTTLE_INTERVAL_MS = 100; // Minimum time between fetches during continuous scroll

  /**
   * Request a viewport fetch with throttle + trailing pattern.
   *
   * During continuous scrolling (like trackpad inertia), this ensures:
   * 1. Fetches happen at minimum THROTTLE_INTERVAL_MS frequency (not blocked forever)
   * 2. A single trailing fetch fires after scrolling stops
   *
   * This replaces pure debounce which would reset on every scroll event,
   * causing the grid to appear frozen during long inertial scrolls.
   */
  function fetchViewportNow(): void {
    const now = Date.now();
    const timeSinceLastFetch = now - lastFetchTime;

    // Clear any pending trailing fetch timer
    if (trailingFetchTimer !== null) {
      clearTimeout(trailingFetchTimer);
      trailingFetchTimer = null;
    }

    if (timeSinceLastFetch >= THROTTLE_INTERVAL_MS) {
      // Enough time has passed - fetch immediately
      doFetchViewport();
    }

    // Always schedule a trailing fetch for when scrolling stops
    // This ensures we get a final accurate render after inertia ends
    trailingFetchTimer = setTimeout(() => {
      trailingFetchTimer = null;
      doFetchViewport();
    }, THROTTLE_INTERVAL_MS);
  }

  async function doFetchViewport(): Promise<void> {
    if (fetchInFlight) {
      // A fetch is already in progress - the trailing timer will catch up
      return;
    }

    fetchInFlight = true;
    lastFetchTime = Date.now();
    try {
      await fetchViewport();
      render();
    } finally {
      fetchInFlight = false;
    }
  }

  async function fetchSheets(): Promise<void> {
    if (!app.dataSource) return;
    try {
      const result = await app.dataSource.getSheets();
      sheetTabsManager.setSheets(result.sheets);
      sheetTabsManager.setActiveSheetIndex(result.activeIndex);
      app.activeSheetIndex = result.activeIndex;
      sheetTabsManager.renderSheetTabs();
    } catch (e) {
      console.error("Error fetching sheets:", e);
    }
  }

  // =========================================================================
  // Data Change Handler
  // =========================================================================

  let pendingChangeTypes = new Set<DataChangeType>();
  let changeHandlerScheduled = false;

  function handleDataChanged(changeType: DataChangeType): void {
    console.debug("[DataChanged]", changeType);
    pendingChangeTypes.add(changeType);

    if (!changeHandlerScheduled) {
      changeHandlerScheduled = true;
      queueMicrotask(processDataChanges);
    }
  }

  async function processDataChanges(): Promise<void> {
    changeHandlerScheduled = false;
    const changeTypes = pendingChangeTypes;
    pendingChangeTypes = new Set();

    if (
      changeTypes.has("structure") ||
      changeTypes.has("sheet") ||
      changeTypes.has("loaded")
    ) {
      await fetchSheetInfo();
    }

    if (changeTypes.has("sheet") || changeTypes.has("loaded")) {
      await fetchSheets();
    }

    await fetchViewport();
    render();
    updateFormulaBar();
    updateScrollbars();

    // Queue operations broadcast to peers
    if (app.collaborationInitialized && app.syncAdapter) {
      await app.syncAdapter.queueLocalOperationsBroadcast();
    }
  }

  return {
    fetchSheetInfo,
    fetchViewport,
    fetchViewportNow,
    fetchSheets,
    handleDataChanged,
  };
}
