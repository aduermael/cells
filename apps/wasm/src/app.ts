// =============================================================================
// Application State Manager
// =============================================================================
//
// Central state manager holding all application state, DOM references, and
// the UI state machine. Created once by init.ts and passed to all components.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Hold DOM element references (canvas, toolbar buttons, dialogs)
// - Manage grid state (columns, rows, selection, scroll position)
// - Coordinate between GridRenderer and data source
// - Track resize/drag contexts for column/row operations
// - Provide unified access to all UI components
//
// State categories:
// - DOM references: canvas, buttons, inputs, containers
// - Grid state: columns, rows, columnWidths, rowHeights
// - Selection: selectedCell, rangeSelection, fillOperation
// - Interaction: resizeContext, dragContext, pendingDrag
//
// =============================================================================

import type { CellsClient } from "./client";
import type {
  SheetInfo,
  CellData,
  ColumnInfo,
  RowInfo,
  Position,
} from "./types";
import { GridRenderer } from "./grid-renderer";
import type { FormulaHighlight } from "./grid-constants";
import { WasmDataSource } from "./wasm-data-source";
import { createUIStateMachine, UIState, type UIStateMachine } from "./ui-state";
import { CollabUI } from "./collab-ui";
import { CppSyncAdapter } from "./cpp-sync-adapter";
import { RoomManager } from "./room-url";
import type { ScrollbarManager } from "./scrollbar";

// =============================================================================
// Types
// =============================================================================

/** Sheet data for the sheet tabs */
export interface SheetData {
  index: number;
  name: string;
  active: boolean;
}

/** Resize context for column/row resizing */
export interface ResizeContext {
  index: number;
  startPos: number; // startX for columns, startY for rows
  startSize: number; // startWidth or startHeight
  previewPos: number; // previewX or previewY
}

/** Drag context for column/row dragging */
export interface DragContext {
  sourceIndex: number;
  targetIndex: number;
  mouseX: number;
  mouseY: number;
}

/** Pending drag context (before threshold is exceeded) */
export interface PendingDragContext {
  column: boolean;
  row: boolean;
  startX: number;
  startY: number;
}

/** DOM element references */
export interface DOMElements {
  canvas: HTMLCanvasElement;
  loading: HTMLElement;
  error: HTMLElement;
  sheetName: HTMLElement;
  workbookTitle: HTMLElement;
  dropZone: HTMLElement;
  emptyState: HTMLElement;
  fileInput: HTMLInputElement;
  cellEditorContainer: HTMLElement;
  cellEditor: HTMLInputElement;
  cellDisplay: HTMLElement;
  columnHeaderEditor: HTMLInputElement;
  formattingToolbar: HTMLElement;
  formulaBar: HTMLElement;
  cellRefWrapper: HTMLElement;
  cellReference: HTMLElement;
  formulaInput: HTMLInputElement;
  formulaDisplay: HTMLElement;
  scriptPanelBtn: HTMLElement;
  scriptPanel: HTMLElement;
  scriptLineNumbers: HTMLElement;
  scriptEditor: HTMLTextAreaElement;
  scriptEditorBackdrop: HTMLPreElement;
  scriptEditorHighlight: HTMLElement;
  scriptRunBtn: HTMLElement;
  scriptStatus: HTMLElement;
  scriptPanelResizeHandle: HTMLElement;
  scriptConsole: HTMLElement;
  scriptConsoleContent: HTMLElement;
  scriptConsoleCloseBtn: HTMLElement;
  scriptConsoleClearBtn: HTMLElement;
  scriptConsoleResizeHandle: HTMLElement;
  bottomBar: HTMLElement;
  sheetTabsContainer: HTMLElement;
  addSheetBtn: HTMLButtonElement;
  astDebugPanel: HTMLElement;
  astErrors: HTMLElement;
  astTree: HTMLElement;
  collabUIContainer: HTMLElement;
  canvasContainer: HTMLElement;
  // Chat panel
  chatPanel: HTMLElement;
  chatHeader: HTMLElement;
  chatTitle: HTMLElement;
  chatClearBtn: HTMLButtonElement;
  chatMinimizeBtn: HTMLButtonElement;
  chatMessages: HTMLElement;
  chatInputArea: HTMLElement;
  chatInput: HTMLTextAreaElement;
  chatSendBtn: HTMLButtonElement;
  chatOpenBtn: HTMLButtonElement;
  // Format controls
  formatControls: HTMLElement;
  formatDropdown: HTMLElement;
  formatDropdownBtn: HTMLButtonElement;
  formatDropdownLabel: HTMLElement;
  formatDropdownMenu: HTMLElement;
  currencyDropdown: HTMLElement;
  currencyDropdownBtn: HTMLButtonElement;
  currencyDropdownLabel: HTMLElement;
  currencyDropdownMenu: HTMLElement;
  formatDecimalIncrease: HTMLButtonElement;
  formatDecimalDecrease: HTMLButtonElement;
  formatPercentBtn: HTMLButtonElement;
  // Custom format panel
  customFormatPanel: HTMLElement;
  customFormatInput: HTMLInputElement;
  customFormatPreview: HTMLElement;
  customFormatError: HTMLElement;
  customFormatApplyBtn: HTMLButtonElement;
  customFormatCancelBtn: HTMLButtonElement;
  // Style controls
  styleControls: HTMLElement;
  styleBoldBtn: HTMLButtonElement;
  styleItalicBtn: HTMLButtonElement;
  styleUnderlineBtn: HTMLButtonElement;
  styleWrapTextBtn: HTMLButtonElement;
  bgColorWrapper: HTMLElement;
  bgColorBtn: HTMLButtonElement;
  bgColorSwatch: HTMLElement;
  bgColorPopup: HTMLElement;
  bgColorHexInput: HTMLInputElement;
  bgThemePalette: HTMLElement;
  textColorWrapper: HTMLElement;
  textColorBtn: HTMLButtonElement;
  textColorSwatch: HTMLElement;
  textColorPopup: HTMLElement;
  textColorHexInput: HTMLInputElement;
  textThemePalette: HTMLElement;
  // Font controls
  fontFamilyDropdown: HTMLElement;
  fontFamilyBtn: HTMLButtonElement;
  fontFamilyLabel: HTMLElement;
  fontFamilyMenu: HTMLElement;
  fontSizeDropdown: HTMLElement;
  fontSizeBtn: HTMLButtonElement;
  fontSizeLabel: HTMLElement;
  fontSizeMenu: HTMLElement;
  // Alignment controls
  hAlignGroup: HTMLElement;
  alignLeftBtn: HTMLButtonElement;
  alignCenterBtn: HTMLButtonElement;
  alignRightBtn: HTMLButtonElement;
  vAlignGroup: HTMLElement;
  valignTopBtn: HTMLButtonElement;
  valignMiddleBtn: HTMLButtonElement;
  valignBottomBtn: HTMLButtonElement;
  // Merge controls
  mergeDropdown: HTMLElement;
  mergeBtn: HTMLButtonElement;
  mergeAllBtn: HTMLButtonElement;
  mergeHorizontalBtn: HTMLButtonElement;
  unmergeBtn: HTMLButtonElement;
  // Border controls
  borderDropdown: HTMLElement;
  borderDropdownMenu: HTMLElement;
  borderBtn: HTMLButtonElement;
  borderStyleIndicator: SVGElement;
  borderAllBtn: HTMLButtonElement;
  borderOuterBtn: HTMLButtonElement;
  borderTopBtn: HTMLButtonElement;
  borderBottomBtn: HTMLButtonElement;
  borderLeftBtn: HTMLButtonElement;
  borderRightBtn: HTMLButtonElement;
  borderNoneBtn: HTMLButtonElement;
  borderStyleGrid: HTMLElement;
  borderColorPalette: HTMLElement;
  borderColorHexInput: HTMLInputElement;
  // Cell Styles gallery
  cellStylesBtn: HTMLButtonElement;
  cellStylesGallery: HTMLElement;
}

// =============================================================================
// App Class
// =============================================================================

/**
 * App - Main application state container
 *
 * Centralizes all application state that was previously global variables
 * in index.html. Provides:
 * - State storage (cells, columns, rows, selection, scroll, etc.)
 * - State machine helpers (isEditing, isResizing, etc.)
 * - DOM element references
 * - Core object references (renderer, dataSource, client, etc.)
 */
export class App {
  // =========================================================================
  // DOM Elements
  // =========================================================================

  readonly elements: DOMElements;

  // =========================================================================
  // Core Objects
  // =========================================================================

  readonly renderer: GridRenderer;
  readonly uiStateMachine: UIStateMachine;
  readonly collabUI: CollabUI;

  // Nullable core objects (set during initialization)
  dataSource: WasmDataSource | null = null;
  wasmClient: CellsClient | null = null;
  syncAdapter: CppSyncAdapter | null = null;
  roomManager: RoomManager | null = null;
  scrollbarManager: ScrollbarManager | null = null;

  // =========================================================================
  // Application State Flags
  // =========================================================================

  hasFileLoaded: boolean = false;
  collaborationInitialized: boolean = false;
  collaborationInitializing: boolean = false;
  astDebugPanelVisible: boolean = false;

  /**
   * Flag indicating viewport data is stale and being refreshed.
   * When true, presence rendering should be deferred to avoid cursor offset issues.
   * This handles the race between dimension syncs and cursor rendering.
   */
  viewportRefreshPending: boolean = false;

  // =========================================================================
  // Sheet/Viewport State
  // =========================================================================

  sheetInfo: SheetInfo | null = null;
  cells: CellData[] = [];
  columns: ColumnInfo[] = [];
  rows: RowInfo[] = [];
  /** Style ranges for rendering backgrounds on empty cells */
  styleRanges: Array<{
    startCol: number;
    startRow: number;
    endCol: number;
    endRow: number;
    style: { bgColor?: string; textColor?: string };
  }> = [];

  /** Axis styles for rendering full column/row backgrounds */
  axisStyles: Array<{
    type: "column" | "row";
    position: number;
    style: { bgColor?: string; textColor?: string };
  }> = [];

  // Scroll position
  scrollX: number = 0;
  scrollY: number = 0;

  // Virtual scrolling: discovered row count (expands as user scrolls down)
  discoveredRows: number = 100;

  // =========================================================================
  // Selection State
  // =========================================================================

  /** Currently selected cell (for single-cell selection) */
  selectedCell: Position | null = null;

  /** Currently selected column header (-1 for none) */
  selectedColumn: number = -1;

  /** Currently selected row header (-1 for none) */
  selectedRow: number = -1;

  /** Selection anchor (start point for range selection) */
  selectionStart: Position | null = null;

  /** Selection end point (current position for range selection) */
  selectionEnd: Position | null = null;

  // =========================================================================
  // Editing State
  // =========================================================================

  /** Column index being header-edited (-1 for none) */
  editingColumnIndex: number = -1;

  /** Sheet index being tab-edited (-1 for none) */
  editingSheetIndex: number = -1;

  // =========================================================================
  // Column/Row Resize State
  // =========================================================================

  resizeColIndex: number = -1;
  resizeStartX: number = 0;
  resizeStartWidth: number = 0;
  resizePreviewX: number = 0;

  resizeRowIndex: number = -1;
  resizeStartY: number = 0;
  resizeStartHeight: number = 0;
  resizePreviewY: number = 0;

  // =========================================================================
  // Column/Row Drag State
  // =========================================================================

  dragSourceIndex: number = -1;
  dragTargetIndex: number = -1;
  dragMouseX: number = 0;
  dragMouseY: number = 0;

  // Pending drag (before threshold)
  pendingDragColumn: boolean = false;
  pendingDragRow: boolean = false;
  pendingDragStartX: number = 0;
  pendingDragStartY: number = 0;

  // =========================================================================
  // Size Caches
  // =========================================================================

  /** Column widths (position -> width) */
  colWidths: Map<number, number> = new Map();

  /** Row heights (position -> height) */
  rowHeights: Map<number, number> = new Map();

  /** Column pixel offsets (position -> X pixel offset) - pre-computed by WASM ViewportIndex */
  colPixelOffsets: Map<number, number> = new Map();

  /** Row pixel offsets (position -> Y pixel offset) - pre-computed by WASM ViewportIndex */
  rowPixelOffsets: Map<number, number> = new Map();

  /** Column names cache (position -> custom name) */
  colNames: Map<number, string> = new Map();

  // =========================================================================
  // Sheet State
  // =========================================================================

  /** All sheets in the workbook */
  sheets: SheetData[] = [];

  /** Index of active sheet */
  activeSheetIndex: number = 0;

  /** Reference to sheet tab context menu element */
  sheetTabContextMenu: HTMLElement | null = null;

  /** Sheet tab drag context */
  dragSheetIndex: number = -1;
  dragSheetTargetIndex: number = -1;

  // =========================================================================
  // Fill Handle Drag State
  // =========================================================================

  /** Whether currently dragging the fill handle */
  isFillDragging: boolean = false;

  /** Fill preview range (shown with dashed border during fill drag) */
  fillPreviewRange: { minCol: number; maxCol: number; minRow: number; maxRow: number } | null = null;

  // =========================================================================
  // Spill Range Highlight State
  // =========================================================================

  /** Spill range highlight for dynamic array formulas */
  spillRangeHighlight: { minCol: number; maxCol: number; minRow: number; maxRow: number; masterCol: number; masterRow: number } | null = null;

  // =========================================================================
  // Formula Highlighting State
  // =========================================================================

  /** Formula reference highlights for the grid */
  formulaHighlights: FormulaHighlight[] = [];

  /** Index of hovered reference in formula bar (-1 = none) */
  hoveredFormulaRefIndex: number = -1;

  /** Index of hovered reference from grid highlight (-1 = none) */
  hoveredGridRefIndex: number = -1;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(elements: DOMElements) {
    this.elements = elements;

    // Initialize renderer
    this.renderer = new GridRenderer(elements.canvas);

    // Initialize UI state machine
    this.uiStateMachine = createUIStateMachine({ debug: false });

    // Initialize collaboration UI
    this.collabUI = new CollabUI({ container: elements.collabUIContainer });

    // Register state change listener for debugging
    this.uiStateMachine.onStateChange(() => {
      // Debug logging available via uiStateMachine debug option
      // Future: could centralize render/updateFormulaBar calls here
    });
  }

  // =========================================================================
  // State Machine Helpers
  // =========================================================================

  /** Check if currently editing a cell */
  isEditing(): boolean {
    return this.uiStateMachine.isInState(UIState.CELL_EDITING);
  }

  /** Check if currently editing the formula bar */
  isEditingFormulaBar(): boolean {
    return this.uiStateMachine.isInState(UIState.FORMULA_BAR_EDITING);
  }

  /** Check if currently editing a column header */
  isEditingColumnHeader(): boolean {
    return this.uiStateMachine.isInState(UIState.COLUMN_HEADER_EDITING);
  }

  /** Check if currently editing a sheet tab */
  isEditingSheetTab(): boolean {
    return this.uiStateMachine.isInState(UIState.SHEET_TAB_EDITING);
  }

  /** Check if currently resizing a column */
  isResizing(): boolean {
    return this.uiStateMachine.isInState(UIState.COLUMN_RESIZING);
  }

  /** Check if currently resizing a row */
  isResizingRow(): boolean {
    return this.uiStateMachine.isInState(UIState.ROW_RESIZING);
  }

  /** Check if currently dragging a column */
  isDraggingColumn(): boolean {
    return this.uiStateMachine.isInState(UIState.COLUMN_DRAGGING);
  }

  /** Check if currently dragging a row */
  isDraggingRow(): boolean {
    return this.uiStateMachine.isInState(UIState.ROW_DRAGGING);
  }

  /** Check if currently selecting a range */
  isSelectingRange(): boolean {
    return this.uiStateMachine.isInState(UIState.SELECTING);
  }

  /** Check if currently dragging a sheet tab */
  isDraggingSheetTab(): boolean {
    return this.uiStateMachine.isInState(UIState.SHEET_TAB_DRAGGING);
  }

  // =========================================================================
  // State Reset Methods
  // =========================================================================

  /** Reset selection state */
  resetSelection(): void {
    this.selectedCell = null;
    this.selectedColumn = -1;
    this.selectedRow = -1;
    this.selectionStart = null;
    this.selectionEnd = null;
  }

  /** Reset resize state */
  resetResizeState(): void {
    this.resizeColIndex = -1;
    this.resizeStartX = 0;
    this.resizeStartWidth = 0;
    this.resizePreviewX = 0;
    this.resizeRowIndex = -1;
    this.resizeStartY = 0;
    this.resizeStartHeight = 0;
    this.resizePreviewY = 0;
  }

  /** Reset drag state */
  resetDragState(): void {
    this.dragSourceIndex = -1;
    this.dragTargetIndex = -1;
    this.dragMouseX = 0;
    this.dragMouseY = 0;
    this.pendingDragColumn = false;
    this.pendingDragRow = false;
    this.pendingDragStartX = 0;
    this.pendingDragStartY = 0;
  }

  /** Reset sheet tab drag state */
  resetSheetDragState(): void {
    this.dragSheetIndex = -1;
    this.dragSheetTargetIndex = -1;
  }

  /** Reset all viewport data when switching sheets or loading new file */
  resetViewportData(): void {
    this.cells = [];
    this.columns = [];
    this.rows = [];
    this.colWidths.clear();
    this.rowHeights.clear();
    this.colPixelOffsets.clear();
    this.rowPixelOffsets.clear();
    this.colNames.clear();
    this.scrollX = 0;
    this.scrollY = 0;
    this.discoveredRows = 100;
    this.resetSelection();
    this.formulaHighlights = [];
    this.hoveredFormulaRefIndex = -1;
    this.hoveredGridRefIndex = -1;
  }

  /** Clear formula highlights (when editing stops) */
  clearFormulaHighlights(): void {
    this.formulaHighlights = [];
    this.hoveredFormulaRefIndex = -1;
    this.hoveredGridRefIndex = -1;
  }
}

// =============================================================================
// Factory Function
// =============================================================================

/**
 * Create an App instance with DOM elements retrieved by ID
 * @returns App instance
 * @throws Error if any required DOM element is not found
 */
export function createApp(): App {
  const getElement = <T extends HTMLElement>(id: string): T => {
    const el = document.getElementById(id);
    if (!el) {
      throw new Error(`Required DOM element not found: #${id}`);
    }
    return el as T;
  };

  const getSvgElement = (id: string): SVGElement => {
    const el = document.getElementById(id);
    if (!el) {
      throw new Error(`Required DOM element not found: #${id}`);
    }
    return el as unknown as SVGElement;
  };

  const elements: DOMElements = {
    canvas: getElement<HTMLCanvasElement>("grid"),
    loading: getElement("loading"),
    error: getElement("error"),
    sheetName: getElement("sheet-name"),
    workbookTitle: getElement("workbook-title"),
    dropZone: getElement("drop-zone"),
    emptyState: getElement("empty-state"),
    fileInput: getElement<HTMLInputElement>("file-input"),
    cellEditorContainer: getElement("cell-editor-container"),
    cellEditor: getElement<HTMLInputElement>("cell-editor"),
    cellDisplay: getElement("cell-display"),
    columnHeaderEditor: getElement<HTMLInputElement>("column-header-editor"),
    formattingToolbar: getElement("formatting-toolbar"),
    formulaBar: getElement("formula-bar"),
    cellRefWrapper: getElement("cell-ref-wrapper"),
    cellReference: getElement("cell-reference"),
    formulaInput: getElement<HTMLInputElement>("formula-input"),
    formulaDisplay: getElement("formula-display"),
    scriptPanelBtn: getElement("script-panel-btn"),
    scriptPanel: getElement("script-panel"),
    scriptLineNumbers: getElement("script-line-numbers"),
    scriptEditor: getElement<HTMLTextAreaElement>("script-editor"),
    scriptEditorBackdrop: getElement<HTMLPreElement>("script-editor-backdrop"),
    scriptEditorHighlight: getElement("script-editor-highlight"),
    scriptRunBtn: getElement("script-run-btn"),
    scriptStatus: getElement("script-status"),
    scriptPanelResizeHandle: getElement("script-panel-resize-handle"),
    scriptConsole: getElement("script-console"),
    scriptConsoleContent: getElement("script-console-content"),
    scriptConsoleCloseBtn: getElement("script-console-close-btn"),
    scriptConsoleClearBtn: getElement("script-console-clear-btn"),
    scriptConsoleResizeHandle: getElement("script-console-resize-handle"),
    bottomBar: getElement("bottom-bar"),
    sheetTabsContainer: getElement("sheet-tabs-container"),
    addSheetBtn: getElement<HTMLButtonElement>("add-sheet-btn"),
    astDebugPanel: getElement("ast-debug-panel"),
    astErrors: getElement("ast-errors"),
    astTree: getElement("ast-tree"),
    collabUIContainer: getElement("collab-ui-container"),
    canvasContainer: getElement("canvas-container"),
    // Chat panel
    chatPanel: getElement("chat-panel"),
    chatHeader: getElement("chat-header"),
    chatTitle: getElement("chat-title"),
    chatClearBtn: getElement<HTMLButtonElement>("chat-clear-btn"),
    chatMinimizeBtn: getElement<HTMLButtonElement>("chat-minimize-btn"),
    chatMessages: getElement("chat-messages"),
    chatInputArea: getElement("chat-input-area"),
    chatInput: getElement<HTMLTextAreaElement>("chat-input"),
    chatSendBtn: getElement<HTMLButtonElement>("chat-send"),
    chatOpenBtn: getElement<HTMLButtonElement>("chat-open-btn"),
    // Format controls
    formatControls: getElement("format-controls"),
    formatDropdown: getElement("format-dropdown"),
    formatDropdownBtn: getElement<HTMLButtonElement>("format-dropdown-btn"),
    formatDropdownLabel: getElement("format-dropdown-label"),
    formatDropdownMenu: document.querySelector("#format-dropdown .dropdown-menu") as HTMLElement,
    currencyDropdown: getElement("currency-dropdown"),
    currencyDropdownBtn: getElement<HTMLButtonElement>("currency-dropdown-btn"),
    currencyDropdownLabel: getElement("currency-dropdown-label"),
    currencyDropdownMenu: document.querySelector("#currency-dropdown .dropdown-menu") as HTMLElement,
    formatDecimalIncrease: getElement<HTMLButtonElement>("format-decimal-increase"),
    formatDecimalDecrease: getElement<HTMLButtonElement>("format-decimal-decrease"),
    formatPercentBtn: getElement<HTMLButtonElement>("format-percent-btn"),
    // Custom format panel
    customFormatPanel: getElement("custom-format-panel"),
    customFormatInput: getElement<HTMLInputElement>("custom-format-input"),
    customFormatPreview: getElement("custom-format-preview"),
    customFormatError: getElement("custom-format-error"),
    customFormatApplyBtn: getElement<HTMLButtonElement>("custom-format-apply"),
    customFormatCancelBtn: getElement<HTMLButtonElement>("custom-format-cancel"),
    // Style controls
    styleControls: getElement("style-controls"),
    styleBoldBtn: getElement<HTMLButtonElement>("style-bold-btn"),
    styleItalicBtn: getElement<HTMLButtonElement>("style-italic-btn"),
    styleUnderlineBtn: getElement<HTMLButtonElement>("style-underline-btn"),
    styleWrapTextBtn: getElement<HTMLButtonElement>("style-wrap-text-btn"),
    bgColorWrapper: getElement("bg-color-wrapper"),
    bgColorBtn: getElement<HTMLButtonElement>("style-bg-color-btn"),
    bgColorSwatch: getElement("bg-color-swatch"),
    bgColorPopup: getElement("bg-color-popup"),
    bgColorHexInput: getElement<HTMLInputElement>("bg-color-hex"),
    bgThemePalette: getElement("bg-theme-palette"),
    textColorWrapper: getElement("text-color-wrapper"),
    textColorBtn: getElement<HTMLButtonElement>("style-text-color-btn"),
    textColorSwatch: getElement("text-color-swatch"),
    textColorPopup: getElement("text-color-popup"),
    textColorHexInput: getElement<HTMLInputElement>("text-color-hex"),
    textThemePalette: getElement("text-theme-palette"),
    // Font controls
    fontFamilyDropdown: getElement("font-family-dropdown"),
    fontFamilyBtn: getElement<HTMLButtonElement>("font-family-btn"),
    fontFamilyLabel: getElement("font-family-label"),
    fontFamilyMenu: document.querySelector("#font-family-dropdown .dropdown-menu") as HTMLElement,
    fontSizeDropdown: getElement("font-size-dropdown"),
    fontSizeBtn: getElement<HTMLButtonElement>("font-size-btn"),
    fontSizeLabel: getElement("font-size-label"),
    fontSizeMenu: document.querySelector("#font-size-dropdown .dropdown-menu") as HTMLElement,
    // Alignment controls
    hAlignGroup: getElement("h-align-group"),
    alignLeftBtn: getElement<HTMLButtonElement>("align-left-btn"),
    alignCenterBtn: getElement<HTMLButtonElement>("align-center-btn"),
    alignRightBtn: getElement<HTMLButtonElement>("align-right-btn"),
    vAlignGroup: getElement("v-align-group"),
    valignTopBtn: getElement<HTMLButtonElement>("valign-top-btn"),
    valignMiddleBtn: getElement<HTMLButtonElement>("valign-middle-btn"),
    valignBottomBtn: getElement<HTMLButtonElement>("valign-bottom-btn"),
    // Merge controls
    mergeDropdown: getElement("merge-dropdown"),
    mergeBtn: getElement<HTMLButtonElement>("merge-btn"),
    mergeAllBtn: getElement<HTMLButtonElement>("merge-all-btn"),
    mergeHorizontalBtn: getElement<HTMLButtonElement>("merge-horizontal-btn"),
    unmergeBtn: getElement<HTMLButtonElement>("unmerge-btn"),
    // Border controls
    borderDropdown: getElement("border-dropdown"),
    borderDropdownMenu: getElement("border-dropdown").querySelector(".dropdown-menu") as HTMLElement,
    borderBtn: getElement<HTMLButtonElement>("border-btn"),
    borderStyleIndicator: getSvgElement("border-style-indicator"),
    borderAllBtn: getElement<HTMLButtonElement>("border-all-btn"),
    borderOuterBtn: getElement<HTMLButtonElement>("border-outer-btn"),
    borderTopBtn: getElement<HTMLButtonElement>("border-top-btn"),
    borderBottomBtn: getElement<HTMLButtonElement>("border-bottom-btn"),
    borderLeftBtn: getElement<HTMLButtonElement>("border-left-btn"),
    borderRightBtn: getElement<HTMLButtonElement>("border-right-btn"),
    borderNoneBtn: getElement<HTMLButtonElement>("border-none-btn"),
    borderStyleGrid: getElement("border-style-grid"),
    borderColorPalette: getElement("border-color-palette"),
    borderColorHexInput: getElement<HTMLInputElement>("border-color-hex"),
    // Cell Styles gallery
    cellStylesBtn: getElement<HTMLButtonElement>("cell-styles-btn"),
    cellStylesGallery: getElement("cell-styles-gallery"),
  };

  return new App(elements);
}
