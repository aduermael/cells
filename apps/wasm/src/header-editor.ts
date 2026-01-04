// Header Editor - Column header editing and formula bar editing
// Handles renaming column headers and editing cells via the formula bar.

import type { WasmDataSource } from "./wasm-data-source";
import type { CppSyncAdapter } from "./cpp-sync-adapter";
import type { UIStateMachine } from "./ui-state";
import { UIEvent } from "./ui-state";
import {
  HEADER_WIDTH,
  HEADER_HEIGHT,
  DEFAULT_COL_WIDTH,
} from "./grid-renderer";
import type { Position, SheetInfo, CellData } from "./types";
import type { FormulaHighlight } from "./grid-constants";
import { colToLetter, getCellAt } from "./grid-utils";
import {
  colorizeFormula,
  getPlainText,
  getCursorPosition,
  setCursorPosition,
} from "./formula-colorizer.js";
import type { FocusManager } from "./focus-manager";
import { FormulaAutocomplete } from "./formula-autocomplete";
import { editingSession } from "./editing-session";

// =============================================================================
// ColumnHeaderEditor Class
// =============================================================================

/**
 * ColumnHeaderEditor manages column header renaming functionality.
 *
 * Responsibilities:
 * - Starting column header edit (positioning editor)
 * - Committing column header edits (renaming column)
 * - Canceling column header edits
 */
export class ColumnHeaderEditor {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private columnHeaderEditorInput: HTMLInputElement;
  private canvas: HTMLCanvasElement;

  // Nullable dependencies (set after construction)
  private dataSource: WasmDataSource | null = null;

  // =========================================================================
  // State
  // =========================================================================

  /** Index of column currently being edited (-1 for none) */
  private editingColumnIndex: number = -1;

  // =========================================================================
  // State accessors (provided by App)
  // =========================================================================

  private getColWidths: () => Map<number, number>;
  private getColNames: () => Map<number, string>;
  private getScrollX: () => number;

  // =========================================================================
  // Callbacks
  // =========================================================================

  private onRender: () => void;
  private onSetColName: (colIndex: number, name: string | null) => void;
  private onSetEditingColumnIndex: (index: number) => void;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    columnHeaderEditorInput: HTMLInputElement;
    canvas: HTMLCanvasElement;
    getColWidths: () => Map<number, number>;
    getColNames: () => Map<number, string>;
    getScrollX: () => number;
    onRender: () => void;
    onSetColName: (colIndex: number, name: string | null) => void;
    onSetEditingColumnIndex: (index: number) => void;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.columnHeaderEditorInput = config.columnHeaderEditorInput;
    this.canvas = config.canvas;
    this.getColWidths = config.getColWidths;
    this.getColNames = config.getColNames;
    this.getScrollX = config.getScrollX;
    this.onRender = config.onRender;
    this.onSetColName = config.onSetColName;
    this.onSetEditingColumnIndex = config.onSetEditingColumnIndex;

    this.setupEventListeners();
  }

  // =========================================================================
  // Configuration
  // =========================================================================

  setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
  }

  // =========================================================================
  // State Helpers
  // =========================================================================

  isEditingColumnHeader(): boolean {
    return this.uiStateMachine.isInState("COLUMN_HEADER_EDITING");
  }

  getEditingColumnIndex(): number {
    return this.editingColumnIndex;
  }

  // =========================================================================
  // Column Header Editing Operations
  // =========================================================================

  /**
   * Start editing a column header
   */
  startEditingColumnHeader(colIndex: number): void {
    if (this.isEditingColumnHeader() || !this.dataSource) return;

    this.uiStateMachine.transition(UIEvent.START_COLUMN_HEADER_EDIT);
    this.editingColumnIndex = colIndex;
    this.onSetEditingColumnIndex(colIndex);
    console.log(
      "startEditingColumnHeader: editingColumnIndex =",
      this.editingColumnIndex,
      "state =",
      this.uiStateMachine.getState()
    );
    this.onRender(); // Redraw to hide the column header text

    // Position the editor
    this.positionEditor(colIndex);

    // Set the current column name (custom or generate from letter)
    const currentName = this.getColNames().get(colIndex) || "";
    this.columnHeaderEditorInput.value = currentName;
    this.columnHeaderEditorInput.placeholder = colToLetter(colIndex);
    this.columnHeaderEditorInput.focus();
    this.columnHeaderEditorInput.select();
  }

  /**
   * Cancel the current column header edit
   */
  cancelEditingColumnHeader(): void {
    if (!this.isEditingColumnHeader()) return;
    this.uiStateMachine.transition(UIEvent.CANCEL_COLUMN_HEADER_EDIT);
    this.editingColumnIndex = -1;
    this.onSetEditingColumnIndex(-1);
    this.columnHeaderEditorInput.style.display = "none";
    this.columnHeaderEditorInput.value = "";
    this.canvas.focus();
  }

  /**
   * Commit the current column header edit
   */
  async confirmEditingColumnHeader(): Promise<void> {
    if (
      !this.isEditingColumnHeader() ||
      this.editingColumnIndex < 0 ||
      !this.dataSource
    )
      return;

    const newName = this.columnHeaderEditorInput.value.trim();
    const savedColIndex = this.editingColumnIndex;

    this.uiStateMachine.transition(UIEvent.COMMIT_COLUMN_HEADER_EDIT);
    this.editingColumnIndex = -1;
    this.onSetEditingColumnIndex(-1);
    this.columnHeaderEditorInput.style.display = "none";

    try {
      // Always use ByPos - engine handles creation if needed
      await this.dataSource.renameColumnByPos(savedColIndex, newName);
      // Update local cache optimistically (listener will refresh from source)
      this.onSetColName(savedColIndex, newName || null);
      // Listener handles refresh automatically
    } catch (e) {
      console.error("Error renaming column:", e);
    }

    this.canvas.focus();
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Position the column header editor over the specified column
   */
  private positionEditor(colIndex: number): void {
    const scrollX = this.getScrollX();
    const colWidths = this.getColWidths();

    let headerX = HEADER_WIDTH - scrollX;
    for (let i = 0; i < colIndex; i++) {
      headerX += colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    const headerW = colWidths.get(colIndex) ?? DEFAULT_COL_WIDTH;

    this.columnHeaderEditorInput.style.left = headerX + "px";
    this.columnHeaderEditorInput.style.top = "0px";
    this.columnHeaderEditorInput.style.width = headerW + "px";
    this.columnHeaderEditorInput.style.height = HEADER_HEIGHT + "px";
    this.columnHeaderEditorInput.style.display = "block";
  }

  /**
   * Set up event listeners on the column header editor input
   */
  private setupEventListeners(): void {
    this.columnHeaderEditorInput.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (e.key === "Escape") {
        e.preventDefault();
        this.cancelEditingColumnHeader();
      } else if (e.key === "Enter") {
        e.preventDefault();
        this.confirmEditingColumnHeader();
      } else if (e.key === "Tab") {
        e.preventDefault();
        this.confirmEditingColumnHeader();
      }
    });

    this.columnHeaderEditorInput.addEventListener("blur", () => {
      if (this.isEditingColumnHeader()) {
        this.confirmEditingColumnHeader();
      }
    });
  }
}

// =============================================================================
// FormulaBarEditor Class
// =============================================================================

/**
 * FormulaBarEditor manages formula bar editing functionality.
 *
 * Responsibilities:
 * - Handling formula bar focus/blur
 * - Committing formula bar edits (create/update/delete cell)
 * - Canceling formula bar edits
 * - Live preview of edits
 * - Syncing with cell editor and collaboration
 * - Color-coded formula reference display
 */
export class FormulaBarEditor {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private formulaInput: HTMLInputElement; // Hidden input for value storage
  private formulaDisplay: HTMLElement; // Contenteditable for colored display
  private cellEditorInput: HTMLInputElement;
  private cellDisplay: HTMLElement; // Contenteditable for cell editor colors
  private focusManager: FocusManager;

  // Nullable dependencies (set after construction)
  private dataSource: WasmDataSource | null = null;
  private syncAdapter: CppSyncAdapter | null = null;

  // Formula function autocomplete
  private formulaAutocomplete: FormulaAutocomplete | null = null;
  private formulaBarContainer: HTMLElement | null = null;

  // =========================================================================
  // State accessors (provided by App)
  // =========================================================================

  private getSelectedCell: () => Position | null;
  private getSelectionStart: () => Position | null;
  private getSheetInfo: () => SheetInfo | null;
  private getCells: () => CellData[];
  private setCells: (cells: CellData[]) => void;
  private getFormulaHighlights: () => FormulaHighlight[];

  // =========================================================================
  // Callbacks
  // =========================================================================

  private onFetchViewport: () => Promise<void>;
  private onRender: () => void;
  private onUpdateFormulaBar: () => void;
  private onSetSelection: (
    cell: Position,
    start: Position,
    end: Position
  ) => void;
  private onUpdateAstDebugPanel: (value: string) => void;
  private onUpdateFormulaHighlights: (value: string, cursorPos?: number) => void;
  private isEditing: () => boolean;
  private onPositionCellEditor: (cell: Position) => void;
  private onFocusCanvas: () => void;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    formulaInput: HTMLInputElement;
    formulaDisplay: HTMLElement;
    cellEditorInput: HTMLInputElement;
    cellDisplay: HTMLElement;
    focusManager: FocusManager;
    getSelectedCell: () => Position | null;
    getSelectionStart: () => Position | null;
    getSheetInfo: () => SheetInfo | null;
    getCells: () => CellData[];
    setCells: (cells: CellData[]) => void;
    getFormulaHighlights: () => FormulaHighlight[];
    onFetchViewport: () => Promise<void>;
    onRender: () => void;
    onUpdateFormulaBar: () => void;
    onSetSelection: (cell: Position, start: Position, end: Position) => void;
    onUpdateAstDebugPanel: (value: string) => void;
    onUpdateFormulaHighlights: (value: string, cursorPos?: number) => void;
    isEditing: () => boolean;
    onPositionCellEditor: (cell: Position) => void;
    onFocusCanvas: () => void;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.formulaInput = config.formulaInput;
    this.formulaDisplay = config.formulaDisplay;
    this.cellEditorInput = config.cellEditorInput;
    this.cellDisplay = config.cellDisplay;
    this.focusManager = config.focusManager;
    this.getSelectedCell = config.getSelectedCell;
    this.getSelectionStart = config.getSelectionStart;
    this.getSheetInfo = config.getSheetInfo;
    this.getCells = config.getCells;
    this.setCells = config.setCells;
    this.getFormulaHighlights = config.getFormulaHighlights;
    this.onFetchViewport = config.onFetchViewport;
    this.onRender = config.onRender;
    this.onUpdateFormulaBar = config.onUpdateFormulaBar;
    this.onSetSelection = config.onSetSelection;
    this.onUpdateAstDebugPanel = config.onUpdateAstDebugPanel;
    this.onUpdateFormulaHighlights = config.onUpdateFormulaHighlights;
    this.isEditing = config.isEditing;
    this.onPositionCellEditor = config.onPositionCellEditor;
    this.onFocusCanvas = config.onFocusCanvas;

    this.setupEventListeners();
  }

  // =========================================================================
  // Configuration
  // =========================================================================

  setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
    // Initialize formula autocomplete when dataSource is available
    this.initFormulaAutocomplete();
  }

  setSyncAdapter(adapter: CppSyncAdapter | null): void {
    this.syncAdapter = adapter;
  }

  /**
   * Set the formula bar container element for autocomplete positioning.
   * Must be called before autocomplete can work.
   */
  setFormulaBarContainer(container: HTMLElement): void {
    this.formulaBarContainer = container;
    this.initFormulaAutocomplete();
  }

  /**
   * Initialize formula autocomplete if all dependencies are available.
   */
  private initFormulaAutocomplete(): void {
    // Need both dataSource and container
    if (!this.dataSource || !this.formulaBarContainer) return;
    // Already initialized
    if (this.formulaAutocomplete) return;

    this.formulaAutocomplete = new FormulaAutocomplete(
      this.formulaBarContainer,
      this.dataSource,
      (functionName: string) => this.insertFunctionName(functionName)
    );
    // Use the contenteditable display for positioning, not the hidden input
    this.formulaAutocomplete.setInputElement(this.formulaDisplay as HTMLInputElement);
  }

  /**
   * Insert a function name at the current cursor position.
   * Called when user selects a function from autocomplete.
   */
  private insertFunctionName(functionName: string): void {
    const cursorPos = editingSession.getSelection().start;

    // Find the prefix we need to replace
    const prefix = this.formulaAutocomplete?.getPrefix() || "";
    const prefixStart = cursorPos - prefix.length;

    // Build new value with function inserted (replace prefix with function name + open paren)
    const insertText = functionName + "(";
    const newCursorPos = editingSession.replaceRange(prefixStart, cursorPos, insertText);

    // Update DOM elements
    const newValue = editingSession.getValue();
    this.formulaInput.value = newValue;
    this.updateColoredDisplay();

    // Use setTimeout to ensure DOM updates before setting cursor
    setTimeout(() => {
      setCursorPosition(this.formulaDisplay, newCursorPos);
      this.formulaDisplay.focus();
      // Sync cell editor display
      this.cellEditorInput.value = newValue;
      this.cellDisplay.innerHTML = this.formulaDisplay.innerHTML;
      this.onUpdateFormulaHighlights(newValue);
    }, 0);
  }

  // =========================================================================
  // State Helpers
  // =========================================================================

  isEditingFormulaBar(): boolean {
    return this.uiStateMachine.isInState("FORMULA_BAR_EDITING");
  }

  /**
   * Check if currently editing a formula (value starts with '=')
   */
  isFormulaMode(): boolean {
    if (!this.isEditingFormulaBar()) return false;
    return this.getValue().startsWith("=");
  }

  /**
   * Get the current formula value.
   * Uses EditingSession when active, otherwise reads from DOM.
   */
  getValue(): string {
    if (editingSession.isActive()) {
      return editingSession.getValue();
    }
    return getPlainText(this.formulaDisplay);
  }

  /**
   * Set the formula value with color highlighting.
   * Updates EditingSession and DOM elements.
   */
  setValue(value: string): void {
    if (editingSession.isActive()) {
      editingSession.setValue(value);
    }
    this.formulaInput.value = value;
    this.updateColoredDisplay();
  }

  /**
   * Update the colored display based on current value and highlights
   */
  updateColoredDisplay(): void {
    const value = this.formulaInput.value;
    const highlights = this.getFormulaHighlights();

    // Use EditingSession as source of truth for cursor position
    const cursorPos = editingSession.getSelection();

    // Wrap innerHTML changes in selection suppression
    editingSession.withSuppressedSelectionChange(() => {
      // Apply colored HTML
      this.formulaDisplay.innerHTML = colorizeFormula(value, highlights);

      // Restore cursor/selection position from session
      if (document.activeElement === this.formulaDisplay) {
        setCursorPosition(this.formulaDisplay, cursorPos.start, cursorPos.end);
      }

      // Also update cell display if visible
      if (this.cellDisplay.parentElement?.style.display !== "none") {
        this.cellDisplay.innerHTML = colorizeFormula(value, highlights);
      }
    });
  }

  /**
   * Insert a cell reference at the current cursor position
   * @param ref The reference to insert (e.g., "A1", "B:B", "3:3")
   */
  insertReferenceAtCursor(ref: string): void {
    if (!this.isEditingFormulaBar()) return;

    // Use EditingSession for cursor position - this is the single source of truth
    // and persists correctly across focus changes
    const newCursorPos = editingSession.insertAtCursor(ref);
    const newValue = editingSession.getValue();

    // Only update hidden inputs - these don't cause visual changes
    // DO NOT update display elements (formulaDisplay/cellDisplay) here!
    // Async colorization will update them with properly colored HTML.
    // This prevents flicker from plain text -> colored text transition.
    this.formulaInput.value = newValue;
    this.cellEditorInput.value = newValue;

    // Update formula highlights (async) - this will update display elements
    // with colored HTML and restore cursor position atomically
    this.onUpdateFormulaHighlights(newValue, newCursorPos);

    // Broadcast editing state
    const editCell = this.getSelectionStart() || this.getSelectedCell();
    if (this.syncAdapter && editCell) {
      this.syncAdapter.setEditing(editCell.col, editCell.row, newValue);
    }
  }

  /**
   * Replace a reference at the given position with a new range reference
   */
  replaceReferenceAtPosition(startPos: number, endPos: number, newRef: string): void {
    if (!this.isEditingFormulaBar()) return;

    // Use EditingSession to replace the range
    const newCursorPos = editingSession.replaceRange(startPos, endPos, newRef);
    const newValue = editingSession.getValue();

    // Only update hidden inputs - these don't cause visual changes
    // DO NOT update display elements here - let async colorization handle it
    this.formulaInput.value = newValue;
    this.cellEditorInput.value = newValue;

    // Update formula highlights (async) - this will update display elements
    // with colored HTML and restore cursor position atomically
    this.onUpdateFormulaHighlights(newValue, newCursorPos);

    // Broadcast editing state
    const editCell = this.getSelectionStart() || this.getSelectedCell();
    if (this.syncAdapter && editCell) {
      this.syncAdapter.setEditing(editCell.col, editCell.row, newValue);
    }
  }

  /**
   * Get the last known value (for external access).
   * Delegates to EditingSession.
   */
  getLastKnownValue(): string {
    return editingSession.getValue();
  }

  /**
   * Get the last known cursor position (for external access).
   * Delegates to EditingSession.
   */
  getLastKnownCursorPos(): { start: number; end: number } {
    return editingSession.getSelection();
  }

  /**
   * Get the display element (contenteditable)
   */
  getDisplayElement(): HTMLElement {
    return this.formulaDisplay;
  }

  /**
   * Get the input element for direct manipulation
   */
  getInputElement(): HTMLInputElement {
    return this.formulaInput;
  }

  // =========================================================================
  // Formula Bar Editing Operations
  // =========================================================================

  /**
   * Commit the current formula bar edit, saving changes
   */
  async commitFormulaBarEdit(): Promise<void> {
    // Hide autocomplete
    this.formulaAutocomplete?.hide();

    // Use anchor cell (selectionStart) for editing, not selectedCell
    const editCell = this.getSelectionStart() || this.getSelectedCell();
    if (!editCell || !this.dataSource) return;

    // Hide cell editor if it's showing
    if (this.isEditing()) {
      this.uiStateMachine.transition(UIEvent.COMMIT_CELL_EDIT);
      const container = this.cellEditorInput.parentElement;
      if (container) container.style.display = "none";
    }

    const newValue = this.getValue();

    const cells = this.getCells();
    let cell = getCellAt(editCell.col, editCell.row, cells);

    // Check if cell is the temp preview cell (not a real cell in the backend)
    const isTemp = cell && cell.id === "_temp_";
    if (isTemp) {
      // Remove temp cell from local array - it will be replaced by real data
      const tempIdx = cells.findIndex((c) => c.id === "_temp_");
      if (tempIdx !== -1) {
        cells.splice(tempIdx, 1);
        this.setCells(cells);
      }
      cell = undefined; // Treat as no cell exists
    }

    try {
      if (newValue === "" || newValue.trim() === "") {
        // Delete cell when content is completely cleared
        if (cell) {
          await this.dataSource.deleteCell(cell.id);
        }
        // If no cell exists and value is empty, nothing to do
      } else if (!cell) {
        // Create cell if it doesn't exist and value is non-empty
        await this.dataSource.createCell(editCell.col, editCell.row, newValue);
      } else {
        await this.dataSource.updateCell(cell.id, newValue);
      }
      // Listener handles refresh automatically
    } catch (e) {
      console.error("Error updating cell from formula bar:", e);
    }

    this.cleanupAfterEdit();
  }

  /**
   * Cleanup formula bar state after edit
   */
  private cleanupAfterEdit(): void {
    // Clear EditingSession
    editingSession.clear();

    this.uiStateMachine.transition(UIEvent.COMMIT_FORMULA_EDIT);
    // Hide cell editor if it was showing during formula bar editing
    const container = this.cellEditorInput.parentElement;
    if (container) container.style.display = "none";
    this.cellEditorInput.value = "";
    this.cellDisplay.innerHTML = "";
    // Clear formula display
    this.formulaDisplay.innerHTML = "";
    this.formulaInput.value = "";
    // Clear formula highlights
    this.onUpdateFormulaHighlights("");
    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }
    // Clear active editor from focus manager
    this.focusManager.setActiveEditor(null);
    this.onFocusCanvas();
  }

  /**
   * Cancel the current formula bar edit, discarding changes
   */
  cancelFormulaBarEdit(): void {
    // Hide autocomplete
    this.formulaAutocomplete?.hide();

    // Clear EditingSession
    editingSession.clear();

    this.uiStateMachine.transition(UIEvent.CANCEL_FORMULA_EDIT);
    // Hide cell editor if it was showing during formula bar editing
    const container = this.cellEditorInput.parentElement;
    if (container) container.style.display = "none";
    this.cellEditorInput.value = "";
    this.cellDisplay.innerHTML = "";
    // Clear formula highlights
    this.onUpdateFormulaHighlights("");
    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }
    // Clear active editor from focus manager
    this.focusManager.setActiveEditor(null);
    // Remove any temp cell created during live preview
    const cells = this.getCells();
    const tempIdx = cells.findIndex((c) => c.id === "_temp_");
    if (tempIdx !== -1) {
      cells.splice(tempIdx, 1);
      this.setCells(cells);
    }
    // Refresh to restore original values
    this.onFetchViewport().then(() => {
      this.onRender();
      this.onUpdateFormulaBar();
    });
    this.onFocusCanvas();
  }

  /**
   * Move selection after formula bar edit (Enter key)
   * @param shiftKey Whether shift was held (moves up instead of down)
   */
  navigateAfterEnter(shiftKey: boolean): void {
    const selectedCell = this.getSelectedCell();
    const sheetInfo = this.getSheetInfo();
    if (!selectedCell || !sheetInfo) return;

    // Enter moves down, Shift+Enter moves up
    const newRow = shiftKey
      ? Math.max(0, selectedCell.row - 1)
      : Math.min(sheetInfo.rowCount - 1, selectedCell.row + 1);

    const newPos = { col: selectedCell.col, row: newRow };
    this.onSetSelection(newPos, newPos, newPos);
    this.onRender();
    this.onUpdateFormulaBar();
  }

  /**
   * Move selection after formula bar edit (Tab key)
   * @param shiftKey Whether shift was held (moves left instead of right)
   */
  navigateAfterTab(shiftKey: boolean): void {
    const selectedCell = this.getSelectedCell();
    const sheetInfo = this.getSheetInfo();
    if (!selectedCell || !sheetInfo) return;

    // Tab moves right, Shift+Tab moves left
    const newCol = shiftKey
      ? Math.max(0, selectedCell.col - 1)
      : Math.min(sheetInfo.colCount - 1, selectedCell.col + 1);

    const newPos = { col: newCol, row: selectedCell.row };
    this.onSetSelection(newPos, newPos, newPos);
    this.onRender();
    this.onUpdateFormulaBar();
    this.formulaDisplay.focus();
    // Select all text in contenteditable
    const selection = window.getSelection();
    if (selection) {
      const range = document.createRange();
      range.selectNodeContents(this.formulaDisplay);
      selection.removeAllRanges();
      selection.addRange(range);
    }
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Set up event listeners on the formula bar contenteditable
   */
  private setupEventListeners(): void {
    // NOTE: Capture-phase mousedown is now handled by FocusManager on the container,
    // which covers canvas, scrollbars, and all other elements within the grid.

    // Focus on contenteditable starts formula editing
    this.formulaDisplay.addEventListener("focus", () => {
      // If already editing formula bar (e.g., refocusing after inserting reference),
      // just sync the cell editor overlay and restore cursor from session
      if (this.isEditingFormulaBar()) {
        const selectedCell = this.getSelectedCell();
        if (selectedCell) {
          this.cellEditorInput.value = this.formulaInput.value;
          this.cellDisplay.innerHTML = this.formulaDisplay.innerHTML;
        }
        // Restore cursor position from session after focus
        const selection = editingSession.getSelection();
        setCursorPosition(this.formulaDisplay, selection.start, selection.end);
        return;
      }

      // If coming from cell editor with an active EditingSession, use that
      const wasEditingCell = this.isEditing();
      const sessionActive = editingSession.isActive();

      this.uiStateMachine.transition(UIEvent.START_FORMULA_EDIT);

      // Register this editor as active with focus manager
      this.focusManager.setActiveEditor(this.formulaDisplay);

      // If EditingSession is already active (from CellEditor), sync from it
      if (sessionActive) {
        const value = editingSession.getValue();
        this.formulaInput.value = value;
        this.updateColoredDisplay();
      } else if (wasEditingCell) {
        // Session not active but was editing cell - start new session as "formula"
        // (this is a fallback case - normally session would already be active)
        const cellEditorValue = getPlainText(this.cellDisplay);
        const selectedCell = this.getSelectedCell();
        const sheetInfo = this.getSheetInfo();
        const sheetId = sheetInfo?.name ?? "default";
        if (selectedCell) {
          editingSession.start(sheetId, selectedCell.col, selectedCell.row, cellEditorValue, "formula");
        }
        this.formulaInput.value = cellEditorValue;
        this.updateColoredDisplay();
      } else {
        // Starting fresh edit from formula bar - start new session as "formula"
        const currentValue = this.formulaInput.value;
        const selectedCell = this.getSelectedCell();
        const sheetInfo = this.getSheetInfo();
        const sheetId = sheetInfo?.name ?? "default";
        if (selectedCell) {
          editingSession.start(sheetId, selectedCell.col, selectedCell.row, currentValue, "formula");
        }
      }

      // Show cell editor for visual feedback on the cell while editing
      const selectedCell = this.getSelectedCell();
      if (selectedCell) {
        // Position and show the cell editor overlay
        this.onPositionCellEditor(selectedCell);
        const container = this.cellEditorInput.parentElement;
        if (container) container.style.display = "block";
        this.cellEditorInput.value = this.formulaInput.value;
        this.cellDisplay.innerHTML = this.formulaDisplay.innerHTML;
      }

      // Show formula highlights for the current value when starting to edit
      // (ensures highlights are shown even if coming from selection mode)
      this.onUpdateFormulaHighlights(this.formulaInput.value);
    });

    // Keyboard events on contenteditable
    this.formulaDisplay.addEventListener("keydown", (e) => {
      e.stopPropagation();

      // Let autocomplete handle navigation keys first
      if (this.formulaAutocomplete?.handleKeyDown(e)) {
        return;
      }

      if (e.key === "Escape") {
        e.preventDefault();
        this.cancelFormulaBarEdit();
      } else if (e.key === "Enter") {
        e.preventDefault();
        this.commitFormulaBarEdit().then(() => {
          this.navigateAfterEnter(e.shiftKey);
        });
      } else if (e.key === "Tab") {
        e.preventDefault();
        this.commitFormulaBarEdit().then(() => {
          this.navigateAfterTab(e.shiftKey);
        });
      }
    });

    // Blur commits the edit - use FocusManager to check if blur should be suppressed
    this.formulaDisplay.addEventListener("blur", (e) => {
      // Use FocusManager to check if we should suppress this blur
      // This handles clicks on canvas, scrollbars, or any element in the container
      if (this.focusManager.shouldSuppressBlur(e.relatedTarget)) {
        this.focusManager.consumeSuppressFlag();
        return;
      }

      const selectedCell = this.getSelectedCell();
      if (this.isEditingFormulaBar() && selectedCell) {
        this.commitFormulaBarEdit();
      }
    });

    // Live sync: update cell display while typing in formula bar
    this.formulaDisplay.addEventListener("input", () => {
      if (this.isEditingFormulaBar()) {
        // Get plain text value from contenteditable
        const value = getPlainText(this.formulaDisplay);
        const cursorPos = getCursorPosition(this.formulaDisplay);

        // Sync value and cursor to EditingSession
        editingSession.setValue(value);
        editingSession.setCursor(cursorPos.start, cursorPos.end);

        this.formulaInput.value = value;

        // Update formula autocomplete
        if (this.formulaAutocomplete) {
          this.formulaAutocomplete.update(value, cursorPos.start);
        }

        // Update AST debug panel live as user types
        this.onUpdateAstDebugPanel(value);

        // Update formula reference highlights live as user types
        this.onUpdateFormulaHighlights(value);

        // Sync to cell editor if it's visible (bidirectional sync)
        const container = this.cellEditorInput.parentElement;
        if (container?.style.display !== "none") {
          this.cellEditorInput.value = value;
          // Cell display will be updated via updateColoredDisplay callback
        }

        // Use anchor cell (selectionStart) for editing, not selectedCell
        const editCell = this.getSelectionStart() || this.getSelectedCell();
        if (!editCell) return;

        // Broadcast ephemeral editing state to peers
        if (this.syncAdapter) {
          this.syncAdapter.setEditing(editCell.col, editCell.row, value);
        }

        // Update local cell data for live preview (without saving to engine)
        const cells = this.getCells();
        let cell = getCellAt(editCell.col, editCell.row, cells);
        if (cell) {
          // Update existing cell's display value
          cell.value = value;
          cell.formula = value.startsWith("=") ? value : undefined;
        } else {
          // Create a temporary local cell for preview
          cells.push({
            id: "_temp_",
            col: editCell.col,
            row: editCell.row,
            value: value,
            type: "s",
          });
          this.setCells(cells);
        }
        this.onRender();
      }
    });

    // Track cursor position on selection changes (arrow keys, mouse clicks in editor)
    document.addEventListener("selectionchange", () => {
      // Skip if suppressed (during colorization innerHTML changes)
      if (editingSession.shouldSuppressSelectionChange()) return;

      if (this.isEditingFormulaBar() && document.activeElement === this.formulaDisplay) {
        const cursorPos = getCursorPosition(this.formulaDisplay);
        editingSession.setCursor(cursorPos.start, cursorPos.end);
      }
    });

    // Prevent paste from including formatting
    this.formulaDisplay.addEventListener("paste", (e) => {
      e.preventDefault();
      const text = e.clipboardData?.getData("text/plain") ?? "";
      document.execCommand("insertText", false, text);
    });
  }
}
