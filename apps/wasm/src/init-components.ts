// =============================================================================
// Component Creation and Wiring
// =============================================================================
//
// Factory functions for creating and configuring UI components. Handles the
// instantiation of editors, panels, and managers with their dependencies.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Create and configure UI components (editors, panels, managers)
// - Wire component callbacks and event handlers
// - Provide rendering helper functions
// - Set up formula bar updates
//
// =============================================================================

import type { App, DOMElements } from "./app";
import type { Position } from "./types";
import type { FormulaHighlight } from "./grid-renderer";
import { PresenceBroadcaster } from "./presence-broadcast";
import { AstDebugPanel } from "./ast-debug";
import { ScriptPanel } from "./script-panel";
import { AgentPanel } from "./agent-panel";
import { WorkbookTitleEditor } from "./workbook-title-editor";
import { FocusManager } from "./focus-manager";
import { CellEditor } from "./cell-editor";
import { ColumnHeaderEditor, FormulaBarEditor } from "./header-editor";
import { ClipboardManager } from "./clipboard";
import { FormatControls } from "./format-controls";
import { StyleControls } from "./style-controls";
import { NamedRangesDropdown } from "./named-ranges-dropdown";
import { SheetTabsManager } from "./sheet-tabs";
import { FileLoader } from "./file-loader";
import { AppEventManager } from "./app-events";
import { persistence } from "./persistence";
import { getCellAt, colToLetter, hasRangeSelection } from "./grid-utils";
import { colorizeFormula } from "./formula-colorizer.js";
import { editingSession } from "./editing-session";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  HEADER_WIDTH,
  HEADER_HEIGHT,
} from "./grid-renderer";
import {
  ScrollbarManager,
  calculateContentDimensions,
  calculateDiscoveredRows,
} from "./scrollbar.js";
import {
  referenceToHighlight,
  restoreCursorInElement,
  updateColoredDisplays,
  setupFormulaRefHover,
} from "./init-rendering";

// =============================================================================
// Types
// =============================================================================

/** Configuration for component creation */
export interface ComponentsConfig {
  app: App;
  elements: DOMElements;
  fetchViewportNow: () => void;
  fetchViewport: () => Promise<void>;
  fetchSheetInfo: () => Promise<void>;
  fetchSheets: () => Promise<void>;
  handleDataChanged: (changeType: import("./wasm-data-source").DataChangeType) => void;
  checkAutoJoinRoom: () => Promise<void>;
  clearRoomIdFromUrl: () => void;
}

/** Created components */
export interface Components {
  presenceBroadcaster: PresenceBroadcaster;
  astDebugPanel: AstDebugPanel;
  scriptPanel: ScriptPanel;
  agentPanel: AgentPanel;
  workbookTitleEditor: WorkbookTitleEditor;
  focusManager: FocusManager;
  cellEditor: CellEditor;
  columnHeaderEditor: ColumnHeaderEditor;
  formulaBarEditor: FormulaBarEditor;
  clipboardManager: ClipboardManager;
  formatControls: FormatControls;
  styleControls: StyleControls;
  sheetTabsManager: SheetTabsManager;
  fileLoader: FileLoader;
  eventManager: AppEventManager;
  // Functions
  render: () => void;
  renderPresenceOnly: () => void;
  resizeCanvas: () => void;
  updateFormulaBar: () => void;
  updateScrollbars: () => void;
  initScrollbars: () => void;
  setSelection: (cell: Position, start: Position, end: Position) => void;
  setDefaultSelection: () => void;
  updateFormulaHighlights: (value: string, cursorPos?: number) => Promise<void>;
  focusCanvas: () => void;
}

// =============================================================================
// Component Creation
// =============================================================================

/**
 * Create and wire all application components.
 */
export function createComponents(config: ComponentsConfig): Components {
  const {
    app,
    elements,
    fetchViewportNow,
    fetchViewport,
    fetchSheetInfo,
    fetchSheets,
    handleDataChanged,
    checkAutoJoinRoom,
    clearRoomIdFromUrl,
  } = config;

  // Forward references for controls (initialized later)
  let formatControlsRef: FormatControls | null = null;
  let styleControlsRef: StyleControls | null = null;

  // =========================================================================
  // Rendering Helpers
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
      spillRangeHighlight: app.spillRangeHighlight,
    });
  }

  // =========================================================================
  // Create PresenceBroadcaster
  // =========================================================================

  const presenceBroadcaster = new PresenceBroadcaster(app.renderer);

  // =========================================================================
  // Render Functions
  // =========================================================================

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

  function initScrollbars(): void {
    app.scrollbarManager = new ScrollbarManager(elements.canvasContainer, {
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
        const { height } = calculateContentDimensions(0, app.discoveredRows, app.colWidths, app.rowHeights);
        return height;
      },
      onScroll: () => {
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
    });
    app.scrollbarManager.setVisible(true);
  }

  function updateScrollbars(): void {
    if (!app.scrollbarManager) return;
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
  // Formula Bar Update
  // =========================================================================

  function getFormulaBarValue(
    cell: { formula?: string; value?: string; editValue?: string; display?: string; isSpilled?: boolean; masterFormula?: string } | null | undefined
  ): string {
    if (!cell) return "";
    // For spilled cells, show the master formula (grayed out)
    if (cell.isSpilled && cell.masterFormula) return cell.masterFormula;
    if (cell.formula) return cell.formula;
    // Use editValue for human-readable display (dates, percentages, etc.)
    // Fall back to raw value if editValue not available
    return cell.editValue ?? cell.value ?? "";
  }

  function updateFormulaBar(): void {
    if (cellEditor.isEditing() || formulaBarEditor.isEditingFormulaBar()) {
      if (app.selectedCell) {
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

    let isSpilledCell = false;
    if (hasRangeSelection(app.selectionStart, app.selectionEnd)) {
      if (app.selectionStart) {
        const ref = colToLetter(app.selectionStart.col) + (app.selectionStart.row + 1);
        elements.cellReference.textContent = ref;
        const anchorCell = getCellAt(app.selectionStart.col, app.selectionStart.row, app.cells);
        const value = getFormulaBarValue(anchorCell);
        elements.formulaInput.value = value;
        elements.formulaDisplay.textContent = value;
        isSpilledCell = anchorCell?.isSpilled === true;
      }
    } else {
      const ref = colToLetter(app.selectedCell.col) + (app.selectedCell.row + 1);
      elements.cellReference.textContent = ref;
      const cell = getCellAt(app.selectedCell.col, app.selectedCell.row, app.cells);
      const value = getFormulaBarValue(cell);
      elements.formulaInput.value = value;
      elements.formulaDisplay.textContent = value;
      isSpilledCell = cell?.isSpilled === true;
    }
    elements.formulaDisplay.dataset.placeholder = "";
    // Gray out formula bar for spilled cells (non-master cells in spill range)
    elements.formulaDisplay.classList.toggle("spilled-cell", isSpilledCell);

    if (!cellEditor.isEditing() && !formulaBarEditor.isEditingFormulaBar()) {
      const cell = getCellAt(app.selectedCell.col, app.selectedCell.row, app.cells);
      // Use master formula for spilled cells, otherwise use the cell's own formula
      const formulaValue = (cell?.isSpilled && cell?.masterFormula)
        ? cell.masterFormula
        : (cell?.formula || "");
      if (formulaValue.startsWith("=")) {
        updateFormulaHighlights(formulaValue);
      } else {
        if (app.formulaHighlights.length > 0) {
          app.formulaHighlights = [];
          render();
        }
      }
    }

    if (formatControlsRef) {
      formatControlsRef.updateForCurrentCell();
    }
    if (styleControlsRef) {
      styleControlsRef.updateForCurrentCell();
    }
  }

  // =========================================================================
  // Formula Highlighting
  // =========================================================================

  let highlightUpdateSeq = 0;

  async function updateFormulaHighlights(value: string, cursorPos?: number): Promise<void> {
    const thisSeq = ++highlightUpdateSeq;

    if (!value || !value.startsWith("=")) {
      app.formulaHighlights = [];
      updateColoredDisplays(value, cursorPos, elements, app);
      render();
      return;
    }

    if (!app.dataSource) {
      app.formulaHighlights = [];
      return;
    }

    try {
      const result = await app.dataSource.client.getReferencesFromPartial(value);
      if (thisSeq !== highlightUpdateSeq) return;

      if (result.error) {
        console.warn("Formula parse error:", result.error);
        app.formulaHighlights = [];
        updateColoredDisplays(value, cursorPos, elements, app);
        render();
        return;
      }

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
      updateColoredDisplays(value, cursorPos, elements, app);
      render();
    } catch (e) {
      if (thisSeq !== highlightUpdateSeq) return;
      console.warn("Error updating formula highlights:", e);
      app.formulaHighlights = [];
      updateColoredDisplays(value, cursorPos, elements, app);
      render();
    }
  }

  // =========================================================================
  // Selection Helpers
  // =========================================================================

  function setSelection(cell: Position, start: Position, end: Position): void {
    app.selectedCell = cell;
    app.selectionStart = start;
    app.selectionEnd = end;
    // Update spill range highlight (async, non-blocking)
    updateSpillRangeHighlight(cell);
  }

  async function updateSpillRangeHighlight(cell: Position): Promise<void> {
    if (!app.dataSource) {
      app.spillRangeHighlight = null;
      return;
    }
    try {
      const spillInfo = await app.dataSource.getSpillRangeAt(cell.col, cell.row);
      const oldHighlight = app.spillRangeHighlight;
      if (spillInfo.masterId) {
        app.spillRangeHighlight = {
          minCol: spillInfo.minCol!,
          maxCol: spillInfo.maxCol!,
          minRow: spillInfo.minRow!,
          maxRow: spillInfo.maxRow!,
          masterCol: spillInfo.masterCol!,
          masterRow: spillInfo.masterRow!,
        };
      } else {
        app.spillRangeHighlight = null;
      }
      // Re-render if highlight changed (async update after render)
      if (oldHighlight !== app.spillRangeHighlight) {
        render();
      }
    } catch {
      app.spillRangeHighlight = null;
    }
  }

  function setDefaultSelection(): void {
    app.selectedCell = { col: 0, row: 0 };
    app.selectionStart = { col: 0, row: 0 };
    app.selectionEnd = { col: 0, row: 0 };
    app.selectedColumn = -1;
    app.selectedRow = -1;
    // Update spill range highlight for default cell
    updateSpillRangeHighlight({ col: 0, row: 0 });
  }

  // =========================================================================
  // Create Panels
  // =========================================================================

  let fileLoaderRef: FileLoader;

  const astDebugPanel = new AstDebugPanel({
    panel: elements.astDebugPanel,
    errorsEl: elements.astErrors,
    treeEl: elements.astTree,
    ensureWasmClient: async () => fileLoaderRef.ensureWasmClient(),
  });

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
        return { success: false, error: "No data source available", instructions: 0 };
      }
      return app.dataSource.executeScript(script);
    },
    onScriptExecuted: async () => {
      fetchViewportNow();
      if (app.dataSource) {
        const name = await app.dataSource.client.getWorkbookName();
        if (name) {
          app.dataSource.setWorkbookName(name);
          workbookTitleEditor.setTitle(name);
        }
      }
    },
    tokenize: async (source: string) => {
      if (!app.dataSource) return [];
      return app.dataSource.tokenizeLuau(source);
    },
    getAutocomplete: async (source: string, line: number, column: number) => {
      if (!app.dataSource) return { context: "unknown" as const, suggestions: [] };
      return app.dataSource.getAutocomplete(source, line, column);
    },
  });

  const AGENT_SERVER_URL =
    (typeof window !== "undefined" &&
      (window as Window & { AGENT_SERVER_URL?: string }).AGENT_SERVER_URL) || "";

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
      if (app.dataSource) {
        const name = await app.dataSource.client.getWorkbookName();
        if (name) {
          app.dataSource.setWorkbookName(name);
          workbookTitleEditor.setTitle(name);
        }
      }
    },
  });

  const workbookTitleEditor = new WorkbookTitleEditor({
    titleElement: elements.workbookTitle,
  });

  // =========================================================================
  // Create Editors
  // =========================================================================

  const focusManager = new FocusManager({
    container: elements.canvasContainer,
    isEditingCell: () => app.uiStateMachine.isInState("CELL_EDITING"),
    isEditingFormulaBar: () => app.uiStateMachine.isInState("FORMULA_BAR_EDITING"),
  });

  function focusCanvas(): void {
    elements.canvas.focus();
  }

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
    getCellDataAt: (col, row) => getCellAt(col, row, app.cells) ?? null,
    onFetchViewport: fetchViewport,
    onRender: render,
    onUpdateFormulaBar: updateFormulaBar,
    onSetSelection: setSelection,
    onUpdateFormulaHighlights: updateFormulaHighlights,
    onFocusCanvas: focusCanvas,
  });

  const columnHeaderEditor = new ColumnHeaderEditor({
    uiStateMachine: app.uiStateMachine,
    columnHeaderEditorInput: elements.columnHeaderEditor,
    canvas: elements.canvas,
    getColWidths: () => app.colWidths,
    getColNames: () => app.colNames,
    getScrollX: () => app.scrollX,
    onRender: render,
    onSetColName: (colIndex, name) => {
      if (name) app.colNames.set(colIndex, name);
      else app.colNames.delete(colIndex);
    },
    onSetEditingColumnIndex: (index) => { app.editingColumnIndex = index; },
  });

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
    setCells: (cells) => { app.cells = cells; },
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

  formulaBarEditor.setFormulaBarContainer(elements.formulaBar);

  // =========================================================================
  // Create Managers
  // =========================================================================

  const clipboardManager = new ClipboardManager({
    getSelectionStart: () => app.selectionStart,
    getSelectionEnd: () => app.selectionEnd,
    getSelectedCell: () => app.selectedCell,
    getCells: () => app.cells,
    onFetchViewport: fetchViewportNow,
    onRender: render,
  });

  const formatControls = new FormatControls(
    {
      formatDropdown: elements.formatDropdown,
      formatDropdownBtn: elements.formatDropdownBtn,
      formatDropdownLabel: elements.formatDropdownLabel,
      formatDropdownMenu: elements.formatDropdownMenu,
      currencyDropdown: elements.currencyDropdown,
      currencyDropdownBtn: elements.currencyDropdownBtn,
      currencyDropdownLabel: elements.currencyDropdownLabel,
      currencyDropdownMenu: elements.currencyDropdownMenu,
      decimalIncreaseBtn: elements.formatDecimalIncrease,
      decimalDecreaseBtn: elements.formatDecimalDecrease,
      percentBtn: elements.formatPercentBtn,
      customFormatPanel: elements.customFormatPanel,
      customFormatInput: elements.customFormatInput,
      customFormatPreview: elements.customFormatPreview,
      customFormatError: elements.customFormatError,
      customFormatApplyBtn: elements.customFormatApplyBtn,
      customFormatCancelBtn: elements.customFormatCancelBtn,
    },
    {
      getSelectedCell: () => app.selectedCell,
      getSelectedCellData: () => {
        if (!app.selectedCell) return null;
        return getCellAt(app.selectedCell.col, app.selectedCell.row, app.cells) ?? null;
      },
      getSelectionRange: () => ({
        start: app.selectionStart,
        end: app.selectionEnd,
      }),
      getCellDataAt: (col, row) => getCellAt(col, row, app.cells) ?? null,
      requestRender: render,
      updateFormulaBar,
    }
  );
  formatControlsRef = formatControls;

  const styleControls = new StyleControls(
    {
      styleControls: elements.styleControls,
      boldBtn: elements.styleBoldBtn,
      italicBtn: elements.styleItalicBtn,
      underlineBtn: elements.styleUnderlineBtn,
      bgColorWrapper: elements.bgColorWrapper,
      bgColorBtn: elements.bgColorBtn,
      bgColorSwatch: elements.bgColorSwatch,
      bgColorPopup: elements.bgColorPopup,
      bgColorHexInput: elements.bgColorHexInput,
      textColorWrapper: elements.textColorWrapper,
      textColorBtn: elements.textColorBtn,
      textColorSwatch: elements.textColorSwatch,
      textColorPopup: elements.textColorPopup,
      textColorHexInput: elements.textColorHexInput,
      fontFamilyDropdown: elements.fontFamilyDropdown,
      fontFamilyBtn: elements.fontFamilyBtn,
      fontFamilyLabel: elements.fontFamilyLabel,
      fontFamilyMenu: elements.fontFamilyMenu,
      fontSizeDropdown: elements.fontSizeDropdown,
      fontSizeBtn: elements.fontSizeBtn,
      fontSizeLabel: elements.fontSizeLabel,
      fontSizeMenu: elements.fontSizeMenu,
      // Alignment controls
      hAlignGroup: elements.hAlignGroup,
      alignLeftBtn: elements.alignLeftBtn,
      alignCenterBtn: elements.alignCenterBtn,
      alignRightBtn: elements.alignRightBtn,
      vAlignGroup: elements.vAlignGroup,
      valignTopBtn: elements.valignTopBtn,
      valignMiddleBtn: elements.valignMiddleBtn,
      valignBottomBtn: elements.valignBottomBtn,
    },
    {
      getSelectedCell: () => app.selectedCell,
      getSelectedCellData: () => {
        if (!app.selectedCell) return null;
        return getCellAt(app.selectedCell.col, app.selectedCell.row, app.cells) ?? null;
      },
      getSelectionRange: () => ({
        start: app.selectionStart,
        end: app.selectionEnd,
      }),
      requestRender: render,
      updateFormulaBar,
    }
  );
  styleControlsRef = styleControls;

  elements.settingsBtn.addEventListener("click", () => {
    alert("Settings - Coming Soon!");
  });

  // Named ranges dropdown for quick insertion
  const namedRangesDropdown = new NamedRangesDropdown(
    elements.formulaBar,
    elements.cellRefWrapper,
    (name: string) => {
      // Insert named range into formula when selected
      if (cellEditor.isEditing()) {
        cellEditor.insertReferenceAtCursor(name);
      } else if (formulaBarEditor.isEditingFormulaBar()) {
        formulaBarEditor.insertReferenceAtCursor(name);
      } else {
        // Start editing with the named range as a formula
        const formulaValue = "=" + name;
        elements.formulaInput.value = formulaValue;
        elements.formulaDisplay.textContent = formulaValue;
        // Focus the formula display to start editing
        elements.formulaDisplay.focus();
        // Set cursor at end
        const selection = window.getSelection();
        if (selection) {
          const range = document.createRange();
          range.selectNodeContents(elements.formulaDisplay);
          range.collapse(false); // Collapse to end
          selection.removeAllRanges();
          selection.addRange(range);
        }
      }
    }
  );

  const sheetTabsManager = new SheetTabsManager({
    uiStateMachine: app.uiStateMachine,
    sheetTabsContainer: elements.sheetTabsContainer,
    addSheetBtn: elements.addSheetBtn,
    onSetActiveSheetIndex: (index) => {
      app.activeSheetIndex = index;
      presenceBroadcaster.setActiveSheetIndex(index);
    },
    onSetEditingSheetIndex: (index) => { app.editingSheetIndex = index; },
    onResetViewState: () => {
      app.resetViewportData();
      app.uiStateMachine.reset();
    },
  });

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
      styleControls.setDataSource(dataSource);
      namedRangesDropdown.setDataSource(dataSource);
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
    leaveCollaborationRoom: () => { if (app.roomManager) app.roomManager.leaveRoom(); },
    clearRoomIdFromUrl,
    resetSheetState: () => {
      sheetTabsManager.setSheets([]);
      sheetTabsManager.setActiveSheetIndex(0);
    },
  });

  fileLoaderRef = fileLoader;

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
    setSelectedCell: (cell) => {
      app.selectedCell = cell;
      if (cell) {
        updateSpillRangeHighlight(cell);
      } else {
        app.spillRangeHighlight = null;
      }
    },
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
      const value = elements.formulaInput.value;
      if (value && app.formulaHighlights.length > 0) {
        const coloredHtml = colorizeFormula(value, app.formulaHighlights, app.hoveredGridRefIndex);
        const cursor = editingSession.getSelection();
        const activeElement = document.activeElement;
        editingSession.withSuppressedSelectionChange(() => {
          elements.formulaDisplay.innerHTML = coloredHtml;
          elements.cellDisplay.innerHTML = coloredHtml;
          if (activeElement === elements.formulaDisplay) {
            restoreCursorInElement(elements.formulaDisplay, cursor.start, cursor.end);
          } else if (activeElement === elements.cellDisplay) {
            restoreCursorInElement(elements.cellDisplay, cursor.start, cursor.end);
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
  // Setup Formula Ref Hover and Theme
  // =========================================================================

  setupFormulaRefHover(elements.formulaDisplay, app, render);
  setupFormulaRefHover(elements.cellDisplay, app, render);

  window.addEventListener("themechange", () => {
    app.renderer.render();
  });

  // =========================================================================
  // Return Components
  // =========================================================================

  return {
    presenceBroadcaster,
    astDebugPanel,
    scriptPanel,
    agentPanel,
    workbookTitleEditor,
    focusManager,
    cellEditor,
    columnHeaderEditor,
    formulaBarEditor,
    clipboardManager,
    formatControls,
    styleControls,
    sheetTabsManager,
    fileLoader,
    eventManager,
    render,
    renderPresenceOnly,
    resizeCanvas,
    updateFormulaBar,
    updateScrollbars,
    initScrollbars,
    setSelection,
    setDefaultSelection,
    updateFormulaHighlights,
    focusCanvas,
  };
}
