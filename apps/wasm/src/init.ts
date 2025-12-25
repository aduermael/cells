// Init - Application initialization and module wiring
// This module creates and connects all application components, providing
// a unified interface for the main entry point (index.html).

import type { CellsClient } from "./client";
import type { Position } from "./types";
import type { DataChangeType } from "./wasm-data-source";
import { App, createApp } from "./app";
import { CellEditor } from "./cell-editor";
import { ColumnHeaderEditor, FormulaBarEditor } from "./header-editor";
import { SheetTabsManager } from "./sheet-tabs";
import { PresenceBroadcaster } from "./presence-broadcast";
import { AppEventManager } from "./app-events";
import { FileLoader } from "./file-loader";
import { persistence } from "./persistence";
import { AstDebugPanel } from "./ast-debug";
import { CppSyncAdapter } from "./cpp-sync-adapter";
import { RoomManager, getRoomIdFromUrl, clearRoomIdFromUrl } from "./room-url";
import {
  getCellAt,
  colToLetter,
  getNormalizedRange,
  hasRangeSelection,
} from "./grid-utils";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  type FormulaHighlight,
} from "./grid-renderer";
import type { ReferenceInfo } from "./client-types";

// =============================================================================
// Types
// =============================================================================

/** Application context returned by initApp */
export interface AppContext {
  app: App;
  fileLoader: FileLoader;
  cellEditor: CellEditor;
  columnHeaderEditor: ColumnHeaderEditor;
  formulaBarEditor: FormulaBarEditor;
  sheetTabsManager: SheetTabsManager;
  presenceBroadcaster: PresenceBroadcaster;
  eventManager: AppEventManager;
  astDebugPanel: AstDebugPanel;

  // Methods exposed for index.html onclick handlers
  openFile: () => void;
  newFile: () => Promise<void>;
  exportAs: (format: "csv" | "xlsx" | "zcd") => Promise<void>;

  // Start the application
  init: () => Promise<void>;
}

// =============================================================================
// Initialization
// =============================================================================

/**
 * Initialize the application and wire all modules together
 * @returns AppContext with all modules and exposed methods
 */
export function initApp(): AppContext {
  // =========================================================================
  // Create App and core state
  // =========================================================================

  const app = createApp();
  const elements = app.elements;

  // =========================================================================
  // Create PresenceBroadcaster
  // =========================================================================

  const presenceBroadcaster = new PresenceBroadcaster(app.renderer);

  // =========================================================================
  // Create AST Debug Panel
  // =========================================================================

  const astDebugPanel = new AstDebugPanel({
    panel: elements.astDebugPanel,
    errorsEl: elements.astErrors,
    treeEl: elements.astTree,
    ensureWasmClient: async () => fileLoader.ensureWasmClient(),
  });

  // =========================================================================
  // Rendering helpers
  // =========================================================================

  function updateRendererState(): void {
    app.renderer.setStateRefs({
      sheetInfo: app.sheetInfo,
      cells: app.cells,
      columns: app.columns,
      rows: app.rows,
      colWidths: app.colWidths,
      rowHeights: app.rowHeights,
      colNames: app.colNames,
      scrollX: app.scrollX,
      scrollY: app.scrollY,
      selectedCell: app.selectedCell,
      selectedColumn: app.selectedColumn >= 0 ? app.selectedColumn : null,
      selectedRow: app.selectedRow >= 0 ? app.selectedRow : null,
      selectionStart: app.selectionStart,
      selectionEnd: app.selectionEnd,
      isDraggingColumn: app.isDraggingColumn(),
      isDraggingRow: app.isDraggingRow(),
      dragSourceIndex: app.dragSourceIndex,
      dragTargetIndex: app.dragTargetIndex,
      dragMouseX: app.dragMouseX,
      dragMouseY: app.dragMouseY,
      isResizing: app.isResizing(),
      resizePreviewX: app.resizePreviewX,
      isResizingRow: app.isResizingRow(),
      resizePreviewY: app.resizePreviewY,
      editingColumnIndex: app.isEditingColumnHeader()
        ? app.editingColumnIndex
        : -1,
      formulaHighlights: app.formulaHighlights,
    });
  }

  function render(): void {
    updateRendererState();
    app.renderer.render();
    app.renderer.drawRemotePresence();
    if (app.isResizing() || app.isResizingRow()) {
      app.renderer.drawResizePreview();
    }
    if (app.isDraggingColumn() || app.isDraggingRow()) {
      app.renderer.drawDragGhost();
    }
    // Broadcast local presence
    presenceBroadcaster.broadcastLocalPresence(
      app.selectedCell,
      app.selectionStart,
      app.selectionEnd
    );
  }

  function renderPresenceOnly(): void {
    updateRendererState();
    app.renderer.render();
    app.renderer.drawRemotePresence();
    if (app.isResizing() || app.isResizingRow()) {
      app.renderer.drawResizePreview();
    }
    if (app.isDraggingColumn() || app.isDraggingRow()) {
      app.renderer.drawDragGhost();
    }
  }

  function resizeCanvas(): void {
    const container = document.getElementById("canvas-container");
    if (!container) return;
    const dpr = window.devicePixelRatio || 1;
    elements.canvas.width = container.clientWidth * dpr;
    elements.canvas.height = container.clientHeight * dpr;
    elements.canvas.style.width = container.clientWidth + "px";
    elements.canvas.style.height = container.clientHeight + "px";
    app.renderer.ctx.scale(dpr, dpr);
    render();
  }

  // =========================================================================
  // Formula bar update
  // =========================================================================

  function updateFormulaBar(): void {
    console.log("[FORMULA_DEBUG] updateFormulaBar called, selectedCell=", app.selectedCell);
    // Don't overwrite formula bar while user is actively editing
    if (cellEditor.isEditing() || formulaBarEditor.isEditingFormulaBar()) {
      if (app.selectedCell) {
        if (hasRangeSelection(app.selectionStart, app.selectionEnd)) {
          const range = getNormalizedRange(
            app.selectionStart,
            app.selectionEnd
          );
          if (range) {
            const startRef = colToLetter(range.minCol) + (range.minRow + 1);
            const endRef = colToLetter(range.maxCol) + (range.maxRow + 1);
            elements.cellReference.textContent = startRef + ":" + endRef;
          }
        } else {
          const ref =
            colToLetter(app.selectedCell.col) + (app.selectedCell.row + 1);
          elements.cellReference.textContent = ref;
        }
      }
      return;
    }

    if (!app.selectedCell) {
      elements.cellReference.textContent = "";
      elements.formulaInput.value = "";
      elements.formulaInput.placeholder =
        "Select a cell to view or edit its value";
      return;
    }

    if (hasRangeSelection(app.selectionStart, app.selectionEnd)) {
      const range = getNormalizedRange(app.selectionStart, app.selectionEnd);
      if (range && app.selectionStart) {
        const startRef = colToLetter(range.minCol) + (range.minRow + 1);
        const endRef = colToLetter(range.maxCol) + (range.maxRow + 1);
        elements.cellReference.textContent = startRef + ":" + endRef;
        const anchorCell = getCellAt(
          app.selectionStart.col,
          app.selectionStart.row,
          app.cells
        );
        if (anchorCell) {
          elements.formulaInput.value =
            anchorCell.formula || anchorCell.value || "";
        } else {
          elements.formulaInput.value = "";
        }
      }
    } else {
      const ref =
        colToLetter(app.selectedCell.col) + (app.selectedCell.row + 1);
      elements.cellReference.textContent = ref;
      const cell = getCellAt(
        app.selectedCell.col,
        app.selectedCell.row,
        app.cells
      );
      if (cell) {
        const formulaValue = cell.formula || cell.value || "";
        console.log("[FORMULA_DEBUG] updateFormulaBar: cell=", cell, "formula=", formulaValue);
        elements.formulaInput.value = formulaValue;
      } else {
        elements.formulaInput.value = "";
      }
    }
    elements.formulaInput.placeholder = "";
  }

  // =========================================================================
  // Formula highlighting
  // =========================================================================

  /**
   * Convert a reference position (cellId) to col/row coordinates
   * by looking up the cell in the current viewport data.
   */
  function getCellPosition(cellId: string): { col: number; row: number } | null {
    // Look up cell in current cells array
    const cell = app.cells.find((c) => c.id === cellId);
    if (cell) {
      return { col: cell.col, row: cell.row };
    }
    // If not in viewport, we can't highlight it (for now)
    return null;
  }

  /**
   * Convert ReferenceInfo from WASM to FormulaHighlight for rendering.
   * This resolves cell UUIDs to col/row positions.
   */
  function referenceToHighlight(
    ref: ReferenceInfo,
    colorIndex: number
  ): FormulaHighlight | null {
    switch (ref.type) {
      case "cell":
        if (ref.cellId) {
          const pos = getCellPosition(ref.cellId);
          if (pos) {
            return {
              type: "cell",
              colorIndex,
              col: pos.col,
              row: pos.row,
              sourceStart: ref.sourceStart,
              sourceEnd: ref.sourceEnd,
            };
          }
        }
        break;

      case "range":
        if (ref.topLeftCellId && ref.bottomRightCellId) {
          const topLeft = getCellPosition(ref.topLeftCellId);
          const bottomRight = getCellPosition(ref.bottomRightCellId);
          if (topLeft && bottomRight) {
            return {
              type: "range",
              colorIndex,
              startCol: topLeft.col,
              startRow: topLeft.row,
              endCol: bottomRight.col,
              endRow: bottomRight.row,
              sourceStart: ref.sourceStart,
              sourceEnd: ref.sourceEnd,
            };
          }
        }
        break;

      case "column":
        // Column refs highlight the entire column - need axis lookup
        // For now, we'll skip these since they require axis position lookup
        break;

      case "row":
        // Row refs highlight the entire row - need axis lookup
        // For now, we'll skip these since they require axis position lookup
        break;
    }
    return null;
  }

  /**
   * Update formula highlights based on formula text.
   * Called live as user types in formula bar.
   */
  async function updateFormulaHighlights(value: string): Promise<void> {
    // Clear highlights if not editing or empty value
    if (!value || !value.startsWith("=")) {
      app.formulaHighlights = [];
      render();
      return;
    }

    // Need a data source to call WASM
    if (!app.dataSource) {
      app.formulaHighlights = [];
      return;
    }

    try {
      // Get references from partial formula (handles incomplete formulas)
      const result = await app.dataSource.client.getReferencesFromPartial(value);

      if (result.error) {
        console.warn("Formula parse error:", result.error);
        app.formulaHighlights = [];
        render();
        return;
      }

      // Convert references to highlights
      const highlights: FormulaHighlight[] = [];
      for (let i = 0; i < result.references.length; i++) {
        const ref = result.references[i];
        if (!ref) continue;
        const highlight = referenceToHighlight(ref, i);
        if (highlight) {
          highlights.push(highlight);
        }
      }

      app.formulaHighlights = highlights;
      render();
    } catch (e) {
      console.warn("Error updating formula highlights:", e);
      app.formulaHighlights = [];
      render();
    }
  }

  // =========================================================================
  // Data fetching
  // =========================================================================

  async function fetchSheetInfo(): Promise<void> {
    if (!app.dataSource) return;
    try {
      app.sheetInfo = await app.dataSource.getSheetInfo();
      elements.sheetName.textContent = app.sheetInfo.name;
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
        app.colNames.delete(col);
      }
      for (let row = startRow; row < endRow; row++) {
        app.rowHeights.delete(row);
      }

      // Repopulate from Workbook
      for (const col of app.columns) {
        app.colWidths.set(col.pos, col.width || DEFAULT_COL_WIDTH);
        if (col.name) {
          app.colNames.set(col.pos, col.name);
        }
      }
      for (const row of app.rows) {
        app.rowHeights.set(row.pos, row.height || DEFAULT_ROW_HEIGHT);
      }
    } catch (e) {
      console.error("Error fetching viewport:", e);
    }
  }

  let fetchInFlight = false;
  let fetchPending = false;

  async function fetchViewportNow(): Promise<void> {
    if (fetchInFlight) {
      fetchPending = true;
      return;
    }
    fetchInFlight = true;
    try {
      await fetchViewport();
      render();
    } finally {
      fetchInFlight = false;
      if (fetchPending) {
        fetchPending = false;
        fetchViewportNow();
      }
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
  // Data change handler
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
    console.log("[FORMULA_DEBUG] processDataChanges: changeTypes=", [...changeTypes]);

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

    console.log("[FORMULA_DEBUG] processDataChanges: calling fetchViewport");
    await fetchViewport();
    console.log("[FORMULA_DEBUG] processDataChanges: cells after fetchViewport=", app.cells.filter(c => c.formula));
    render();
    updateFormulaBar();

    // Queue operations broadcast to peers
    if (app.collaborationInitialized && app.syncAdapter) {
      await app.syncAdapter.queueLocalOperationsBroadcast();
    }
  }

  // =========================================================================
  // Selection helper
  // =========================================================================

  function setSelection(
    cell: Position,
    start: Position,
    end: Position
  ): void {
    app.selectedCell = cell;
    app.selectionStart = start;
    app.selectionEnd = end;
  }

  // =========================================================================
  // Create CellEditor
  // =========================================================================

  const cellEditor = new CellEditor({
    uiStateMachine: app.uiStateMachine,
    cellEditorInput: elements.cellEditor,
    formulaInput: elements.formulaInput,
    canvas: elements.canvas,
    getSelectedCell: () => app.selectedCell,
    getSelectionStart: () => app.selectionStart,
    getSelectionEnd: () => app.selectionEnd,
    getSheetInfo: () => app.sheetInfo,
    getColWidths: () => app.colWidths,
    getRowHeights: () => app.rowHeights,
    getScrollX: () => app.scrollX,
    getScrollY: () => app.scrollY,
    onFetchViewport: fetchViewport,
    onRender: render,
    onUpdateFormulaBar: updateFormulaBar,
    onSetSelection: setSelection,
  });

  // =========================================================================
  // Create ColumnHeaderEditor
  // =========================================================================

  const columnHeaderEditor = new ColumnHeaderEditor({
    uiStateMachine: app.uiStateMachine,
    columnHeaderEditorInput: elements.columnHeaderEditor,
    canvas: elements.canvas,
    getColWidths: () => app.colWidths,
    getColNames: () => app.colNames,
    getScrollX: () => app.scrollX,
    onRender: render,
    onSetColName: (colIndex, name) => {
      if (name) {
        app.colNames.set(colIndex, name);
      } else {
        app.colNames.delete(colIndex);
      }
    },
    onSetEditingColumnIndex: (index) => {
      app.editingColumnIndex = index;
    },
  });

  // =========================================================================
  // Create FormulaBarEditor
  // =========================================================================

  const formulaBarEditor = new FormulaBarEditor({
    uiStateMachine: app.uiStateMachine,
    formulaInput: elements.formulaInput,
    cellEditorInput: elements.cellEditor,
    canvas: elements.canvas,
    getSelectedCell: () => app.selectedCell,
    getSelectionStart: () => app.selectionStart,
    getSheetInfo: () => app.sheetInfo,
    getCells: () => app.cells,
    setCells: (cells) => {
      app.cells = cells;
    },
    onFetchViewport: fetchViewport,
    onRender: render,
    onUpdateFormulaBar: updateFormulaBar,
    onSetSelection: setSelection,
    onUpdateAstDebugPanel: (value) => astDebugPanel.update(value),
    onUpdateFormulaHighlights: updateFormulaHighlights,
    isEditing: () => cellEditor.isEditing(),
  });

  // =========================================================================
  // Create SheetTabsManager
  // =========================================================================

  const sheetTabsManager = new SheetTabsManager({
    uiStateMachine: app.uiStateMachine,
    sheetTabsContainer: elements.sheetTabsContainer,
    addSheetBtn: elements.addSheetBtn,
    onSetActiveSheetIndex: (index) => {
      app.activeSheetIndex = index;
      presenceBroadcaster.setActiveSheetIndex(index);
    },
    onSetEditingSheetIndex: (index) => {
      app.editingSheetIndex = index;
    },
    onResetViewState: () => {
      app.resetViewportData();
      app.uiStateMachine.reset();
    },
  });

  // =========================================================================
  // Create FileLoader
  // =========================================================================

  const fileLoader = new FileLoader({
    loading: elements.loading,
    error: elements.error,
    canvas: elements.canvas,
    formulaBar: elements.formulaBar,
    sheetTabs: elements.sheetTabs,
    emptyState: elements.emptyState,
    fileInput: elements.fileInput,
    dropZone: elements.dropZone,

    saveFileToIndexedDB: (data) => persistence.saveFileToIndexedDB(data),
    saveFileMeta: (name, format) => persistence.saveFileMeta(name, format),
    loadFileFromIndexedDB: () => persistence.loadFileFromIndexedDB(),
    loadFileMeta: () => persistence.loadFileMeta(),
    clearPersistedFile: () => persistence.clearPersistedFile(),

    onDataSourceCreated: (dataSource) => {
      app.dataSource = dataSource;
      cellEditor.setDataSource(dataSource);
      columnHeaderEditor.setDataSource(dataSource);
      formulaBarEditor.setDataSource(dataSource);
      sheetTabsManager.setDataSource(dataSource);
    },
    onDataChanged: handleDataChanged,
    getDataSource: () => app.dataSource,

    resetViewState: () => {
      app.resetViewportData();
      setDefaultSelection();
      app.uiStateMachine.reset();
    },
    resizeCanvas,
    fetchSheetInfo,
    fetchViewport,
    fetchSheets,
    render,
    updateFormulaBar,
    checkAutoJoinRoom,
    leaveCollaborationRoom: () => {
      if (app.roomManager) {
        app.roomManager.leaveRoom();
      }
    },
    clearRoomIdFromUrl,
    resetSheetState: () => {
      sheetTabsManager.setSheets([]);
      sheetTabsManager.setActiveSheetIndex(0);
    },
  });

  // =========================================================================
  // Create AppEventManager
  // =========================================================================

  const eventManager = new AppEventManager({
    canvas: elements.canvas,
    uiStateMachine: app.uiStateMachine,
    cellEditor,
    columnHeaderEditor,
    presenceBroadcaster,
    formulaInput: elements.formulaInput,

    getSheetInfo: () => app.sheetInfo,
    getSelectedCell: () => app.selectedCell,
    getSelectionStart: () => app.selectionStart,
    getSelectionEnd: () => app.selectionEnd,
    getScrollX: () => app.scrollX,
    getScrollY: () => app.scrollY,
    getColWidths: () => app.colWidths,
    getRowHeights: () => app.rowHeights,
    getColumns: () => app.columns,
    getRows: () => app.rows,
    getDataSource: () => app.dataSource,
    getSyncAdapter: () => app.syncAdapter,

    getResizeColIndex: () => app.resizeColIndex,
    setResizeColIndex: (v) => { app.resizeColIndex = v; },
    getResizeStartX: () => app.resizeStartX,
    setResizeStartX: (v) => { app.resizeStartX = v; },
    getResizeStartWidth: () => app.resizeStartWidth,
    setResizeStartWidth: (v) => { app.resizeStartWidth = v; },
    getResizePreviewX: () => app.resizePreviewX,
    setResizePreviewX: (v) => { app.resizePreviewX = v; },
    getResizeRowIndex: () => app.resizeRowIndex,
    setResizeRowIndex: (v) => { app.resizeRowIndex = v; },
    getResizeStartY: () => app.resizeStartY,
    setResizeStartY: (v) => { app.resizeStartY = v; },
    getResizeStartHeight: () => app.resizeStartHeight,
    setResizeStartHeight: (v) => { app.resizeStartHeight = v; },
    getResizePreviewY: () => app.resizePreviewY,
    setResizePreviewY: (v) => { app.resizePreviewY = v; },

    getDragSourceIndex: () => app.dragSourceIndex,
    setDragSourceIndex: (v) => { app.dragSourceIndex = v; },
    getDragTargetIndex: () => app.dragTargetIndex,
    setDragTargetIndex: (v) => { app.dragTargetIndex = v; },
    getDragMouseX: () => app.dragMouseX,
    setDragMouseX: (v) => { app.dragMouseX = v; },
    getDragMouseY: () => app.dragMouseY,
    setDragMouseY: (v) => { app.dragMouseY = v; },
    getPendingDragColumn: () => app.pendingDragColumn,
    setPendingDragColumn: (v) => { app.pendingDragColumn = v; },
    getPendingDragRow: () => app.pendingDragRow,
    setPendingDragRow: (v) => { app.pendingDragRow = v; },
    getPendingDragStartX: () => app.pendingDragStartX,
    setPendingDragStartX: (v) => { app.pendingDragStartX = v; },
    getPendingDragStartY: () => app.pendingDragStartY,
    setPendingDragStartY: (v) => { app.pendingDragStartY = v; },

    setSelectedCell: (cell) => { app.selectedCell = cell; },
    setSelectedColumn: (col) => { app.selectedColumn = col ?? -1; },
    setSelectedRow: (row) => { app.selectedRow = row ?? -1; },
    setSelectionStart: (pos) => { app.selectionStart = pos; },
    setSelectionEnd: (pos) => { app.selectionEnd = pos; },
    setSelection,

    setScrollX: (v) => { app.scrollX = v; },
    setScrollY: (v) => { app.scrollY = v; },

    render,
    updateFormulaBar,
    resizeCanvas,
    fetchViewportNow,
    toggleAstDebugPanel: () => astDebugPanel.toggle(elements.formulaInput.value),
    commitFormulaBarEdit: () => formulaBarEditor.commitFormulaBarEdit(),
  });

  // =========================================================================
  // Collaboration
  // =========================================================================

  async function initializeCollaboration(
    client: CellsClient
  ): Promise<void> {
    if (app.collaborationInitialized) {
      console.log("Collaboration already initialized");
      return;
    }
    if (app.collaborationInitializing) {
      console.log("Collaboration initialization in progress, waiting...");
      while (app.collaborationInitializing && !app.collaborationInitialized) {
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
      return;
    }
    app.collaborationInitializing = true;
    console.log("Starting collaboration initialization...");

    try {
      // Create C++ sync adapter
      app.syncAdapter = new CppSyncAdapter({ client });

      // Create room manager
      app.roomManager = new RoomManager({ collabManager: app.syncAdapter });

      // Initialize the adapter
      await app.syncAdapter.initialize();

      // Connect UI
      app.collabUI.setCollabManager(app.syncAdapter);
      app.collabUI.setPresenceManager(app.syncAdapter);
      app.collabUI.setRoomManager(app.roomManager);

      // Set up for modules
      cellEditor.setSyncAdapter(app.syncAdapter);
      formulaBarEditor.setSyncAdapter(app.syncAdapter);
      presenceBroadcaster.setSyncAdapter(app.syncAdapter);
      presenceBroadcaster.setCollaborationInitialized(true);

      // Listen for presence updates
      app.syncAdapter.on("presenceupdated", () => {
        presenceBroadcaster.updateRemotePresenceDisplay();
        renderPresenceOnly();
      });
      app.syncAdapter.on("peerleft", () => {
        presenceBroadcaster.updateRemotePresenceDisplay();
        renderPresenceOnly();
      });

      // Start presence render loop
      setInterval(() => {
        if (app.collaborationInitialized && app.syncAdapter) {
          const hadPresence = app.renderer.remotePresence.length > 0;
          presenceBroadcaster.updateRemotePresenceDisplay();
          const hasPresence = app.renderer.remotePresence.length > 0;
          if (hadPresence || hasPresence) {
            renderPresenceOnly();
          }
        }
      }, 50);

      app.collaborationInitialized = true;
      app.collaborationInitializing = false;
      console.log(
        "Collaboration initialized successfully, peer ID:",
        app.syncAdapter.peerId
      );
    } catch (err) {
      console.error("Failed to initialize collaboration:", err);
      app.collaborationInitializing = false;
      throw err;
    }
  }

  async function checkAutoJoinRoom(): Promise<void> {
    const roomIdFromUrl = getRoomIdFromUrl();
    if (roomIdFromUrl && fileLoader.getHasFileLoaded()) {
      console.log("Auto-joining room from URL:", roomIdFromUrl);
      try {
        const client = await fileLoader.ensureWasmClient();
        await initializeCollaboration(client);
        await app.roomManager!.joinRoom(roomIdFromUrl);
      } catch (err) {
        console.error("Failed to auto-join room:", err);
      }
    }
  }

  // Set up collab UI callback
  app.collabUI.setOnInitializeRequest(async () => {
    const client = await fileLoader.ensureWasmClient();
    if (!fileLoader.getHasFileLoaded()) {
      await fileLoader.createEmptyWorkbook();
    }
    await initializeCollaboration(client);
  });

  // Handle browser back/forward
  window.addEventListener("popstate", async () => {
    const roomIdFromUrl = getRoomIdFromUrl();
    const currentRoomId = app.roomManager?.currentRoomId;

    if (
      roomIdFromUrl &&
      roomIdFromUrl !== currentRoomId &&
      fileLoader.getHasFileLoaded()
    ) {
      console.log("Popstate: rejoining room from URL:", roomIdFromUrl);
      try {
        const client = await fileLoader.ensureWasmClient();
        await initializeCollaboration(client);
        await app.roomManager!.joinRoom(roomIdFromUrl);
      } catch (err) {
        console.error("Failed to rejoin room from popstate:", err);
      }
    } else if (!roomIdFromUrl && currentRoomId) {
      console.log("Popstate: leaving room (no room in URL)");
      app.roomManager!.leaveRoom();
    }
  });

  // =========================================================================
  // Default selection
  // =========================================================================

  function setDefaultSelection(): void {
    app.selectedCell = { col: 0, row: 0 };
    app.selectionStart = { col: 0, row: 0 };
    app.selectionEnd = { col: 0, row: 0 };
    app.selectedColumn = -1;
    app.selectedRow = -1;
  }

  // =========================================================================
  // Main init
  // =========================================================================

  async function init(): Promise<void> {
    // Set up event listeners
    eventManager.setupEventListeners();
    fileLoader.setupFileInput();
    fileLoader.setupDragAndDrop();
    fileLoader.setupExportDropdown();

    resizeCanvas();

    // Try to auto-load persisted file
    const loaded = await fileLoader.tryAutoLoadPersistedFile();

    if (!loaded) {
      // Create empty workbook on startup
      await fileLoader.createEmptyWorkbook();
    } else {
      // If file loaded, ensure we have a default selection
      if (!app.selectedCell) {
        setDefaultSelection();
        render();
        updateFormulaBar();
      }
    }
  }

  // =========================================================================
  // Return AppContext
  // =========================================================================

  return {
    app,
    fileLoader,
    cellEditor,
    columnHeaderEditor,
    formulaBarEditor,
    sheetTabsManager,
    presenceBroadcaster,
    eventManager,
    astDebugPanel,

    openFile: () => fileLoader.openFile(),
    newFile: () => fileLoader.newFile(),
    exportAs: (format) => fileLoader.exportAs(format),

    init,
  };
}
