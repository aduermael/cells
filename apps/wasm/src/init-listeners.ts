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
      // This is critical for correct scroll height calculation when loading files
      if (app.sheetInfo.rowCount > app.discoveredRows) {
        app.discoveredRows = app.sheetInfo.rowCount;
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

    const startCol = Math.max(0, Math.floor(app.scrollX / DEFAULT_COL_WIDTH) - 2);
    const startRow = Math.max(0, Math.floor(app.scrollY / DEFAULT_ROW_HEIGHT) - 2);
    const endCol = Math.min(
      app.sheetInfo.colCount,
      startCol + Math.ceil(viewWidth / DEFAULT_COL_WIDTH) + 4
    );
    const endRow = Math.min(
      app.sheetInfo.rowCount,
      startRow + Math.ceil(viewHeight / DEFAULT_ROW_HEIGHT) + 4
    );

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
  let fetchDebounceTimer: ReturnType<typeof setTimeout> | null = null;
  const FETCH_DEBOUNCE_MS = 16; // ~1 frame, responsive but prevents flooding

  /**
   * Request a viewport fetch with debouncing.
   * - Debounces rapid scroll events to prevent flooding the WASM worker
   * - If a fetch is in flight, waits for it to complete then fetches again
   * - Uses current scroll position at fetch time (not when queued)
   */
  function fetchViewportNow(): void {
    // Clear any pending debounce timer
    if (fetchDebounceTimer !== null) {
      clearTimeout(fetchDebounceTimer);
    }

    // Debounce: wait a short time for scroll to settle
    fetchDebounceTimer = setTimeout(() => {
      fetchDebounceTimer = null;
      doFetchViewport();
    }, FETCH_DEBOUNCE_MS);
  }

  async function doFetchViewport(): Promise<void> {
    if (fetchInFlight) {
      // A fetch is in progress - schedule another after it completes
      // The next fetch will use the current scroll position
      fetchDebounceTimer = setTimeout(() => {
        fetchDebounceTimer = null;
        doFetchViewport();
      }, FETCH_DEBOUNCE_MS);
      return;
    }

    fetchInFlight = true;
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
