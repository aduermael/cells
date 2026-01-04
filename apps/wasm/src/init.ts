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
import { ScriptPanel } from "./script-panel";
import { AgentPanel } from "./agent-panel";
import { CppSyncAdapter } from "./cpp-sync-adapter";
import { RoomManager, getRoomIdFromUrl, clearRoomIdFromUrl } from "./room-url";
import {
  getCellAt,
  colToLetter,
  hasRangeSelection,
} from "./grid-utils";
import { ClipboardManager } from "./clipboard";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  HEADER_WIDTH,
  HEADER_HEIGHT,
  type FormulaHighlight,
} from "./grid-renderer";
import type { ReferenceInfo } from "./client-types";
import { colorizeFormula } from "./formula-colorizer.js";
import { ScrollbarManager, calculateContentDimensions, calculateDiscoveredRows } from "./scrollbar.js";
import { FocusManager } from "./focus-manager";
import { WorkbookTitleEditor } from "./workbook-title-editor";
import { editingSession } from "./editing-session";
import { FormatControls } from "./format-controls";
import { initTheme } from "./theme";

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
  scriptPanel: ScriptPanel;
  agentPanel: AgentPanel;
  workbookTitleEditor: WorkbookTitleEditor;
  clipboardManager: ClipboardManager;
  formatControls: FormatControls;

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
  // Initialize theme (must be early to avoid flash of wrong theme)
  // =========================================================================

  initTheme();

  // =========================================================================
  // Create App and core state
  // =========================================================================

  const app = createApp();
  const elements = app.elements;

  // Forward reference for formatControls (initialized later)
  let formatControlsRef: FormatControls | null = null;

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
  // Create Script Panel
  // =========================================================================

  const scriptPanel = new ScriptPanel({
    panel: elements.scriptPanel,
    toggleBtn: elements.scriptPanelBtn,
    editor: elements.scriptEditor,
    backdrop: elements.scriptEditorBackdrop,
    highlight: elements.scriptEditorHighlight,
    lineNumbers: elements.scriptLineNumbers,
    runBtn: elements.scriptRunBtn,
    statusEl: elements.scriptStatus,
    resizeHandle: elements.scriptPanelResizeHandle,
    consoleEl: elements.scriptConsole,
    consoleContentEl: elements.scriptConsoleContent,
    consoleCloseBtn: elements.scriptConsoleCloseBtn,
    consoleClearBtn: elements.scriptConsoleClearBtn,
    consoleResizeHandle: elements.scriptConsoleResizeHandle,
    executeScript: async (script: string) => {
      if (!app.dataSource) {
        return {
          success: false,
          error: "No data source available",
          instructions: 0,
        };
      }
      return app.dataSource.executeScript(script);
    },
    onScriptExecuted: async () => {
      // Refresh viewport to show any changes made by the script
      fetchViewportNow();

      // Refresh workbook name in case setDocumentTitle was called
      if (app.dataSource) {
        const name = await app.dataSource.client.getWorkbookName();
        if (name) {
          app.dataSource.setWorkbookName(name);
          workbookTitleEditor.setTitle(name);
        }
      }
    },
    tokenize: async (source: string) => {
      if (!app.dataSource) {
        return [];
      }
      return app.dataSource.tokenizeLuau(source);
    },
    getAutocomplete: async (source: string, line: number, column: number) => {
      if (!app.dataSource) {
        return { context: "unknown" as const, suggestions: [] };
      }
      return app.dataSource.getAutocomplete(source, line, column);
    },
  });

  // =========================================================================
  // Create AgentPanel (AI chat interface)
  // =========================================================================

  // Agent server URL - can be configured via window.AGENT_SERVER_URL
  // Empty string means same origin (relative URLs)
  const AGENT_SERVER_URL =
    (typeof window !== "undefined" &&
      (window as Window & { AGENT_SERVER_URL?: string }).AGENT_SERVER_URL) ||
    "";

  const agentPanel = new AgentPanel({
    panel: elements.chatPanel,
    header: elements.chatHeader,
    title: elements.chatTitle,
    clearBtn: elements.chatClearBtn,
    minimizeBtn: elements.chatMinimizeBtn,
    messages: elements.chatMessages,
    inputArea: elements.chatInputArea,
    input: elements.chatInput,
    sendBtn: elements.chatSendBtn,
    openBtn: elements.chatOpenBtn,
    getClient: () => app.dataSource?.client ?? null,
    getServerUrl: () => AGENT_SERVER_URL,
    onViewportRefresh: async () => {
      fetchViewportNow();
      // Refresh workbook name in case setDocumentTitle was called
      if (app.dataSource) {
        const name = await app.dataSource.client.getWorkbookName();
        if (name) {
          app.dataSource.setWorkbookName(name);
          workbookTitleEditor.setTitle(name);
        }
      }
    },
  });

  // =========================================================================
  // Create WorkbookTitleEditor
  // =========================================================================

  const workbookTitleEditor = new WorkbookTitleEditor({
    titleElement: elements.workbookTitle,
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
      colPixelOffsets: app.colPixelOffsets,
      rowPixelOffsets: app.rowPixelOffsets,
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
      hoveredFormulaRefIndex: app.hoveredFormulaRefIndex,
      discoveredRows: app.discoveredRows,
      isFillDragging: app.isFillDragging,
      fillPreviewRange: app.fillPreviewRange,
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
    updateScrollbars();
  }

  // =========================================================================
  // Scrollbar Management
  // =========================================================================

  /** Initialize scrollbar manager */
  function initScrollbars(): void {
    app.scrollbarManager = new ScrollbarManager(
      elements.canvasContainer,
      {
        getScrollX: () => app.scrollX,
        getScrollY: () => app.scrollY,
        setScrollX: (x) => { app.scrollX = x; },
        setScrollY: (y) => { app.scrollY = y; },
        getViewportWidth: () => elements.canvas.clientWidth - HEADER_WIDTH,
        getViewportHeight: () => elements.canvas.clientHeight - HEADER_HEIGHT,
        getContentWidth: () => {
          const colCount = app.sheetInfo?.colCount ?? 22;
          const { width } = calculateContentDimensions(colCount, 0, app.colWidths, app.rowHeights);
          return width;
        },
        getContentHeight: () => {
          // Use discovered rows for virtual scrolling
          const { height } = calculateContentDimensions(0, app.discoveredRows, app.colWidths, app.rowHeights);
          return height;
        },
        onScroll: () => {
          // Update discovered rows based on scroll position
          const viewportHeight = elements.canvas.clientHeight - HEADER_HEIGHT;
          const actualRows = app.sheetInfo?.rowCount ?? 100;
          app.discoveredRows = calculateDiscoveredRows(
            app.scrollY,
            viewportHeight,
            app.discoveredRows,
            actualRows
          );
          render();
          fetchViewportNow();
        },
      }
    );
    // Scrollbars are always visible
    app.scrollbarManager.setVisible(true);
  }

  /** Update scrollbar positions and thumb sizes */
  function updateScrollbars(): void {
    if (!app.scrollbarManager) return;

    // Update discovered rows based on current scroll
    const viewportHeight = elements.canvas.clientHeight - HEADER_HEIGHT;
    const actualRows = app.sheetInfo?.rowCount ?? 100;
    app.discoveredRows = calculateDiscoveredRows(
      app.scrollY,
      viewportHeight,
      app.discoveredRows,
      actualRows
    );
    app.scrollbarManager.update();
  }

  // =========================================================================
  // Formula bar update
  // =========================================================================

  function updateFormulaBar(): void {
    // Don't overwrite formula bar while user is actively editing
    if (cellEditor.isEditing() || formulaBarEditor.isEditingFormulaBar()) {
      if (app.selectedCell) {
        // Always show anchor cell reference (like Excel/Sheets)
        // For range selection, anchor is selectionStart; otherwise use selectedCell
        const anchor = app.selectionStart || app.selectedCell;
        const ref = colToLetter(anchor.col) + (anchor.row + 1);
        elements.cellReference.textContent = ref;
      }
      return;
    }

    if (!app.selectedCell) {
      elements.cellReference.textContent = "";
      elements.formulaInput.value = "";
      elements.formulaDisplay.textContent = "";
      elements.formulaDisplay.dataset.placeholder =
        "Select a cell to view or edit its value";
      return;
    }

    if (hasRangeSelection(app.selectionStart, app.selectionEnd)) {
      if (app.selectionStart) {
        // Show anchor cell reference (like Excel/Sheets)
        const ref = colToLetter(app.selectionStart.col) + (app.selectionStart.row + 1);
        elements.cellReference.textContent = ref;
        const anchorCell = getCellAt(
          app.selectionStart.col,
          app.selectionStart.row,
          app.cells
        );
        if (anchorCell) {
          const value = anchorCell.formula || anchorCell.value || "";
          elements.formulaInput.value = value;
          elements.formulaDisplay.textContent = value;
        } else {
          elements.formulaInput.value = "";
          elements.formulaDisplay.textContent = "";
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
        elements.formulaInput.value = formulaValue;
        elements.formulaDisplay.textContent = formulaValue;
      } else {
        elements.formulaInput.value = "";
        elements.formulaDisplay.textContent = "";
      }
    }
    elements.formulaDisplay.dataset.placeholder = "";

    // Show formula highlights when a formula cell is selected (not editing)
    // This allows users to see what cells a formula references just by clicking on it
    if (!cellEditor.isEditing() && !formulaBarEditor.isEditingFormulaBar()) {
      const cell = getCellAt(
        app.selectedCell.col,
        app.selectedCell.row,
        app.cells
      );
      const formulaValue = cell?.formula || "";
      if (formulaValue.startsWith("=")) {
        // Cell has a formula - show highlights
        updateFormulaHighlights(formulaValue);
      } else {
        // Not a formula cell - clear any existing highlights
        if (app.formulaHighlights.length > 0) {
          app.formulaHighlights = [];
          render();
        }
      }
    }

    // Update format controls to reflect current cell's format
    if (formatControlsRef) {
      formatControlsRef.updateForCurrentCell();
    }
  }

  // =========================================================================
  // Formula highlighting
  // =========================================================================

  /**
   * Convert ReferenceInfo from WASM to FormulaHighlight for rendering.
   * C++ provides resolved positions directly, eliminating viewport lookup race conditions.
   */
  function referenceToHighlight(
    ref: ReferenceInfo,
    colorIndex: number
  ): FormulaHighlight | null {
    switch (ref.type) {
      case "cell":
        // C++ provides col/row directly - no viewport lookup needed
        if (ref.col !== undefined && ref.row !== undefined) {
          return {
            type: "cell",
            colorIndex,
            col: ref.col,
            row: ref.row,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;

      case "range":
        // C++ provides startCol/startRow/endCol/endRow directly
        if (
          ref.startCol !== undefined &&
          ref.startRow !== undefined &&
          ref.endCol !== undefined &&
          ref.endRow !== undefined
        ) {
          return {
            type: "range",
            colorIndex,
            startCol: ref.startCol,
            startRow: ref.startRow,
            endCol: ref.endCol,
            endRow: ref.endRow,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;

      case "column":
        // C++ provides col position directly
        if (ref.col !== undefined) {
          return {
            type: "column",
            colorIndex,
            col: ref.col,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;

      case "row":
        // C++ provides row position directly
        if (ref.row !== undefined) {
          return {
            type: "row",
            colorIndex,
            row: ref.row,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;

      case "columnRange":
        // C++ provides startCol/endCol directly
        if (ref.startCol !== undefined && ref.endCol !== undefined) {
          return {
            type: "column",
            colorIndex,
            startCol: ref.startCol,
            endCol: ref.endCol,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;

      case "rowRange":
        // C++ provides startRow/endRow directly
        if (ref.startRow !== undefined && ref.endRow !== undefined) {
          return {
            type: "row",
            colorIndex,
            startRow: ref.startRow,
            endRow: ref.endRow,
            sourceStart: ref.sourceStart,
            sourceEnd: ref.sourceEnd,
          };
        }
        break;
    }
    return null;
  }

  /**
   * Update formula highlights based on formula text.
   * Called live as user types in formula bar.
   * C++ provides resolved positions directly, so no viewport lookup needed.
   * @param value The formula text
   * @param cursorPos Optional cursor position to restore after updating colored displays
   */
  async function updateFormulaHighlights(value: string, cursorPos?: number): Promise<void> {
    // Clear highlights if not editing or empty value
    if (!value || !value.startsWith("=")) {
      app.formulaHighlights = [];
      updateColoredDisplays(value, cursorPos);
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
      // C++ creates any referenced cells and returns positions directly
      const result = await app.dataSource.client.getReferencesFromPartial(value);

      if (result.error) {
        console.warn("Formula parse error:", result.error);
        app.formulaHighlights = [];
        updateColoredDisplays(value, cursorPos);
        render();
        return;
      }

      // Convert references to highlights - positions come from C++ directly
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
      updateColoredDisplays(value, cursorPos);
      render();
    } catch (e) {
      console.warn("Error updating formula highlights:", e);
      app.formulaHighlights = [];
      updateColoredDisplays(value, cursorPos);
      render();
    }
  }

  /**
   * Update the colored formula displays with current highlights.
   * Called after highlights are computed.
   * @param value The formula text
   * @param cursorPos Optional cursor position to restore (overrides EditingSession)
   */
  function updateColoredDisplays(value: string, cursorPos?: number): void {
    // Pass hoveredGridRefIndex to highlight formula text when grid highlight is hovered
    const coloredHtml = colorizeFormula(value, app.formulaHighlights, app.hoveredGridRefIndex);

    // Get cursor position: prefer explicit parameter, then EditingSession
    const sessionCursor = editingSession.getSelection();
    const targetCursor = cursorPos ?? sessionCursor.start;

    // Determine which element should have cursor restored based on EditingSession
    const activeEditor = editingSession.getActiveEditor();

    // Wrap innerHTML changes in selection suppression to prevent selectionchange
    // from corrupting EditingSession cursor state
    editingSession.withSuppressedSelectionChange(() => {
      // Update formula bar display
      elements.formulaDisplay.innerHTML = coloredHtml;

      // Update cell display
      elements.cellDisplay.innerHTML = coloredHtml;

      // Focus the appropriate editor and restore cursor
      // This is critical for reference insertion: the editor lost focus when
      // user clicked on the canvas, so we must refocus before setting cursor.
      if (editingSession.isActive()) {
        const targetElement = activeEditor === "formula"
          ? elements.formulaDisplay
          : elements.cellDisplay;

        // Use requestAnimationFrame to focus AFTER mouse events complete
        // (click/mouseup can steal focus back to canvas)
        requestAnimationFrame(() => {
          targetElement.focus();
          restoreCursorInElement(targetElement, targetCursor);
        });
      }
    });
  }

  /**
   * Restore cursor to text offset position in element.
   */
  function restoreCursorInElement(element: HTMLElement, offset: number): void {
    const selection = window.getSelection();
    if (!selection) return;

    const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT, null);
    let totalOffset = 0;
    let current = walker.nextNode();
    while (current) {
      const nodeLength = current.textContent?.length ?? 0;
      if (totalOffset + nodeLength >= offset) {
        const range = document.createRange();
        range.setStart(current, offset - totalOffset);
        range.collapse(true);
        selection.removeAllRanges();
        selection.addRange(range);
        return;
      }
      totalOffset += nodeLength;
      current = walker.nextNode();
    }

    // Offset is beyond content, place cursor at end
    const range = document.createRange();
    range.selectNodeContents(element);
    range.collapse(false);
    selection.removeAllRanges();
    selection.addRange(range);
  }

  // =========================================================================
  // Formula Reference Hover Handlers
  // =========================================================================

  /**
   * Set up hover handlers for formula reference spans in a container element.
   * Uses event delegation since spans are created dynamically.
   */
  function setupFormulaRefHover(container: HTMLElement): void {
    // Use mouseover/mouseout with delegation since spans change dynamically
    container.addEventListener("mouseover", (e) => {
      const target = e.target as HTMLElement;
      if (target.classList.contains("formula-ref")) {
        const refIndex = parseInt(target.dataset.refIndex ?? "-1", 10);
        if (refIndex >= 0 && app.hoveredFormulaRefIndex !== refIndex) {
          app.hoveredFormulaRefIndex = refIndex;
          render();
        }
      }
    });

    container.addEventListener("mouseout", (e) => {
      const target = e.target as HTMLElement;
      if (target.classList.contains("formula-ref")) {
        // Check if we're leaving to another formula-ref (don't clear if so)
        const related = e.relatedTarget as HTMLElement;
        if (!related || !related.classList?.contains("formula-ref")) {
          if (app.hoveredFormulaRefIndex !== -1) {
            app.hoveredFormulaRefIndex = -1;
            render();
          }
        }
      }
    });
  }

  // =========================================================================
  // Data fetching
  // =========================================================================

  async function fetchSheetInfo(): Promise<void> {
    if (!app.dataSource) return;
    try {
      app.sheetInfo = await app.dataSource.getSheetInfo();
      elements.sheetName.textContent = app.sheetInfo.name;
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
  // Create FocusManager
  // =========================================================================

  // FocusManager handles focus boundaries for the grid container.
  // It ensures clicks on canvas, scrollbars, or any element in the container
  // suppress blur commits during formula editing.
  const focusManager = new FocusManager({
    container: elements.canvasContainer,
    isEditingCell: () => app.uiStateMachine.isInState("CELL_EDITING"),
    isEditingFormulaBar: () => app.uiStateMachine.isInState("FORMULA_BAR_EDITING"),
  });

  // Helper to focus the canvas (used after editing commits)
  function focusCanvas(): void {
    elements.canvas.focus();
  }

  // =========================================================================
  // Create CellEditor
  // =========================================================================

  const cellEditor = new CellEditor({
    uiStateMachine: app.uiStateMachine,
    cellEditorContainer: elements.cellEditorContainer,
    cellEditorInput: elements.cellEditor,
    cellDisplay: elements.cellDisplay,
    formulaInput: elements.formulaInput,
    formulaDisplay: elements.formulaDisplay,
    focusManager,
    getSelectedCell: () => app.selectedCell,
    getSelectionStart: () => app.selectionStart,
    getSelectionEnd: () => app.selectionEnd,
    getSheetInfo: () => app.sheetInfo,
    getColWidths: () => app.colWidths,
    getRowHeights: () => app.rowHeights,
    getScrollX: () => app.scrollX,
    getScrollY: () => app.scrollY,
    getFormulaHighlights: () => app.formulaHighlights,
    getDiscoveredRows: () => app.discoveredRows,
    onFetchViewport: fetchViewport,
    onRender: render,
    onUpdateFormulaBar: updateFormulaBar,
    onSetSelection: setSelection,
    onUpdateFormulaHighlights: updateFormulaHighlights,
    onFocusCanvas: focusCanvas,
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

  // Position the cell editor overlay at a given cell (for visual feedback)
  function positionCellEditor(cell: Position): void {
    let cellX = HEADER_WIDTH - app.scrollX;
    for (let i = 0; i < cell.col; i++) {
      cellX += app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    let cellY = HEADER_HEIGHT - app.scrollY;
    for (let i = 0; i < cell.row; i++) {
      cellY += app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    }
    const cellWidth = app.colWidths.get(cell.col) ?? DEFAULT_COL_WIDTH;
    const cellHeight = app.rowHeights.get(cell.row) ?? DEFAULT_ROW_HEIGHT;
    elements.cellEditorContainer.style.left = cellX + "px";
    elements.cellEditorContainer.style.top = cellY + "px";
    elements.cellEditorContainer.style.width = cellWidth + "px";
    elements.cellEditorContainer.style.height = cellHeight + "px";
  }

  const formulaBarEditor = new FormulaBarEditor({
    uiStateMachine: app.uiStateMachine,
    formulaInput: elements.formulaInput,
    formulaDisplay: elements.formulaDisplay,
    cellEditorInput: elements.cellEditor,
    cellDisplay: elements.cellDisplay,
    focusManager,
    getSelectedCell: () => app.selectedCell,
    getSelectionStart: () => app.selectionStart,
    getSheetInfo: () => app.sheetInfo,
    getCells: () => app.cells,
    setCells: (cells) => {
      app.cells = cells;
    },
    getFormulaHighlights: () => app.formulaHighlights,
    onFetchViewport: fetchViewport,
    onRender: render,
    onUpdateFormulaBar: updateFormulaBar,
    onSetSelection: setSelection,
    onUpdateAstDebugPanel: (value) => astDebugPanel.update(value),
    onUpdateFormulaHighlights: updateFormulaHighlights,
    isEditing: () => cellEditor.isEditing(),
    onPositionCellEditor: positionCellEditor,
    onFocusCanvas: focusCanvas,
  });

  // Set formula bar container for autocomplete positioning
  formulaBarEditor.setFormulaBarContainer(elements.formulaBar);

  // =========================================================================
  // Create ClipboardManager
  // =========================================================================

  const clipboardManager = new ClipboardManager({
    getSelectionStart: () => app.selectionStart,
    getSelectionEnd: () => app.selectionEnd,
    getSelectedCell: () => app.selectedCell,
    getCells: () => app.cells,
    onFetchViewport: fetchViewportNow,
    onRender: render,
  });

  // =========================================================================
  // Create FormatControls
  // =========================================================================

  const formatControls = new FormatControls(
    {
      formatDropdown: elements.formatDropdown,
      formatDropdownBtn: elements.formatDropdownBtn,
      formatDropdownLabel: elements.formatDropdownLabel,
      formatDropdownMenu: elements.formatDropdownMenu,
      currencyBtn: elements.formatCurrencyBtn,
      percentBtn: elements.formatPercentBtn,
      decimalIncreaseBtn: elements.formatDecimalIncrease,
      decimalDecreaseBtn: elements.formatDecimalDecrease,
    },
    {
      getSelectedCell: () => app.selectedCell,
      getSelectedCellData: () => {
        if (!app.selectedCell) return null;
        return getCellAt(app.selectedCell.col, app.selectedCell.row, app.cells) ?? null;
      },
      requestRender: render,
      updateFormulaBar,
    }
  );
  formatControlsRef = formatControls;

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
    bottomBar: elements.bottomBar,
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
      workbookTitleEditor.setDataSource(dataSource);
      clipboardManager.setDataSource(dataSource);
      formatControls.setDataSource(dataSource);
      // Update the title display from the workbook name
      workbookTitleEditor.setTitle(dataSource.workbookName);
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
    formulaBarEditor,
    presenceBroadcaster,
    clipboardManager,
    scriptPanel,
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
    getFillHandleBounds: () => app.renderer.fillHandleBounds,

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

    getDiscoveredRows: () => app.discoveredRows,
    setDiscoveredRows: (v) => { app.discoveredRows = v; },

    getIsFillDragging: () => app.isFillDragging,
    setIsFillDragging: (v) => { app.isFillDragging = v; },
    getFillPreviewRange: () => app.fillPreviewRange,
    setFillPreviewRange: (v) => { app.fillPreviewRange = v; },

    getFormulaHighlights: () => app.formulaHighlights,
    getHoveredGridRefIndex: () => app.hoveredGridRefIndex,
    setHoveredGridRefIndex: (v) => { app.hoveredGridRefIndex = v; },
    getColPixelOffsets: () => app.colPixelOffsets,
    getRowPixelOffsets: () => app.rowPixelOffsets,
    updateFormulaBarHoverStyle: () => {
      // Re-colorize the formula displays with current hover state
      const value = elements.formulaInput.value;
      if (value && app.formulaHighlights.length > 0) {
        const coloredHtml = colorizeFormula(value, app.formulaHighlights, app.hoveredGridRefIndex);

        // Save cursor position from EditingSession before innerHTML change
        const cursorPos = editingSession.getSelection().start;
        const activeElement = document.activeElement;

        // Suppress selectionchange to avoid corrupting session state
        editingSession.withSuppressedSelectionChange(() => {
          elements.formulaDisplay.innerHTML = coloredHtml;
          elements.cellDisplay.innerHTML = coloredHtml;

          // Restore cursor if one of our editors has focus
          if (activeElement === elements.formulaDisplay) {
            restoreCursorInElement(elements.formulaDisplay, cursorPos);
          } else if (activeElement === elements.cellDisplay) {
            restoreCursorInElement(elements.cellDisplay, cursorPos);
          }
        });
      }
    },

    render,
    updateFormulaBar,
    clearFormulaHighlights: () => { app.formulaHighlights = []; },
    resizeCanvas,
    fetchViewportNow,
    toggleAstDebugPanel: () => astDebugPanel.toggle(elements.formulaInput.value),
    commitFormulaBarEdit: () => formulaBarEditor.commitFormulaBarEdit(),
    updateScrollbars,
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

      // Expose sync adapter on window for e2e testing
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (window as any)._syncAdapter = app.syncAdapter;

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

  // Re-render grid when theme changes
  window.addEventListener("themechange", () => {
    app.renderer.render();
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

    // Set up formula ref hover handlers (event delegation)
    setupFormulaRefHover(elements.formulaDisplay);
    setupFormulaRefHover(elements.cellDisplay);

    // Initialize scrollbars
    initScrollbars();

    // Connect focus manager to scrollbar for editor refocusing after scroll
    if (app.scrollbarManager) {
      app.scrollbarManager.setFocusManager(focusManager);
    }

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
    scriptPanel,
    agentPanel,
    workbookTitleEditor,
    clipboardManager,
    formatControls,

    openFile: () => fileLoader.openFile(),
    newFile: () => fileLoader.newFile(),
    exportAs: (format) => fileLoader.exportAs(format),

    init,
  };
}
