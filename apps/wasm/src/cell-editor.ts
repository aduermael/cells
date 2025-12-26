// Cell Editor - In-place cell editing functionality
// Handles starting, committing, and canceling cell edits, as well as
// navigation after editing and formula bar synchronization.

import type { WasmDataSource } from "./wasm-data-source";
import type { CppSyncAdapter } from "./cpp-sync-adapter";
import type { UIStateMachine } from "./ui-state";
import { UIEvent } from "./ui-state";
import {
  HEADER_WIDTH,
  HEADER_HEIGHT,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
} from "./grid-renderer";
import type { Position, SheetInfo } from "./types";
import type { FormulaHighlight } from "./grid-constants";
import { getNormalizedRange } from "./grid-utils";
import {
  colorizeFormula,
  getPlainText,
  getCursorPosition,
  setCursorPosition,
} from "./formula-colorizer.js";

// =============================================================================
// Types
// =============================================================================

/** Edit mode for starting cell editing */
export type EditMode = "append" | "replace" | "select";

/** Options for starting cell editing */
export interface StartEditingOptions {
  /** Whether to focus the cell editor (default: true) */
  focusCellEditor?: boolean;
  /** Edit mode: append, replace, or select (default: select) */
  mode?: EditMode;
  /** Initial character for replace mode */
  initialChar?: string;
}

/** Callback for after edit completion (navigation/render) */
export type AfterEditCallback = () => void;

// =============================================================================
// CellEditor Class
// =============================================================================

/**
 * CellEditor manages in-place cell editing functionality.
 *
 * Responsibilities:
 * - Starting cell edit (getting/creating cell, positioning editor)
 * - Committing cell edits (updating/deleting cell)
 * - Canceling cell edits
 * - Handling keyboard navigation after edit
 * - Syncing with formula bar and collaboration
 * - Color-coded formula reference display
 */
export class CellEditor {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private cellEditorContainer: HTMLElement;
  private cellEditorInput: HTMLInputElement; // Hidden input for value storage
  private cellDisplay: HTMLElement; // Contenteditable for colored display
  private formulaInput: HTMLInputElement;
  private formulaDisplay: HTMLElement;
  private canvas: HTMLCanvasElement;

  // Nullable dependencies (set after construction)
  private dataSource: WasmDataSource | null = null;
  private syncAdapter: CppSyncAdapter | null = null;

  // =========================================================================
  // State accessors (provided by App)
  // =========================================================================

  private getSelectedCell: () => Position | null;
  private getSelectionStart: () => Position | null;
  private getSelectionEnd: () => Position | null;
  private getSheetInfo: () => SheetInfo | null;
  private getColWidths: () => Map<number, number>;
  private getRowHeights: () => Map<number, number>;
  private getScrollX: () => number;
  private getScrollY: () => number;
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
  private onUpdateFormulaHighlights: (value: string) => void;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    cellEditorContainer: HTMLElement;
    cellEditorInput: HTMLInputElement;
    cellDisplay: HTMLElement;
    formulaInput: HTMLInputElement;
    formulaDisplay: HTMLElement;
    canvas: HTMLCanvasElement;
    getSelectedCell: () => Position | null;
    getSelectionStart: () => Position | null;
    getSelectionEnd: () => Position | null;
    getSheetInfo: () => SheetInfo | null;
    getColWidths: () => Map<number, number>;
    getRowHeights: () => Map<number, number>;
    getScrollX: () => number;
    getScrollY: () => number;
    getFormulaHighlights: () => FormulaHighlight[];
    onFetchViewport: () => Promise<void>;
    onRender: () => void;
    onUpdateFormulaBar: () => void;
    onSetSelection: (cell: Position, start: Position, end: Position) => void;
    onUpdateFormulaHighlights: (value: string) => void;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.cellEditorContainer = config.cellEditorContainer;
    this.cellEditorInput = config.cellEditorInput;
    this.cellDisplay = config.cellDisplay;
    this.formulaInput = config.formulaInput;
    this.formulaDisplay = config.formulaDisplay;
    this.canvas = config.canvas;
    this.getSelectedCell = config.getSelectedCell;
    this.getSelectionStart = config.getSelectionStart;
    this.getSelectionEnd = config.getSelectionEnd;
    this.getSheetInfo = config.getSheetInfo;
    this.getColWidths = config.getColWidths;
    this.getRowHeights = config.getRowHeights;
    this.getScrollX = config.getScrollX;
    this.getScrollY = config.getScrollY;
    this.getFormulaHighlights = config.getFormulaHighlights;
    this.onFetchViewport = config.onFetchViewport;
    this.onRender = config.onRender;
    this.onUpdateFormulaBar = config.onUpdateFormulaBar;
    this.onSetSelection = config.onSetSelection;
    this.onUpdateFormulaHighlights = config.onUpdateFormulaHighlights;

    this.setupEventListeners();
  }

  // =========================================================================
  // Configuration
  // =========================================================================

  setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
  }

  setSyncAdapter(adapter: CppSyncAdapter | null): void {
    this.syncAdapter = adapter;
  }

  // =========================================================================
  // State Helpers
  // =========================================================================

  isEditing(): boolean {
    return this.uiStateMachine.isInState("CELL_EDITING");
  }

  /**
   * Check if currently editing a formula (value starts with '=')
   */
  isFormulaMode(): boolean {
    if (!this.isEditing()) return false;
    return this.getValue().startsWith("=");
  }

  /**
   * Get the current cell value (plain text from contenteditable)
   */
  getValue(): string {
    return getPlainText(this.cellDisplay);
  }

  /**
   * Set the cell value with color highlighting
   */
  setValue(value: string): void {
    this.cellEditorInput.value = value;
    this.updateColoredDisplay();
  }

  /**
   * Update the colored display based on current value and highlights
   */
  updateColoredDisplay(): void {
    const value = this.cellEditorInput.value;
    const highlights = this.getFormulaHighlights();

    // Get cursor position before update
    const cursorPos = getCursorPosition(this.cellDisplay);

    // Apply colored HTML
    this.cellDisplay.innerHTML = colorizeFormula(value, highlights);

    // Restore cursor position
    setCursorPosition(this.cellDisplay, cursorPos.start);

    // Also update formula display
    this.formulaDisplay.innerHTML = colorizeFormula(value, highlights);
  }

  /**
   * Insert a cell reference at the current cursor position
   * @param ref The reference to insert (e.g., "A1", "B:B", "3:3")
   */
  insertReferenceAtCursor(ref: string): void {
    if (!this.isEditing()) return;

    const cursorPos = getCursorPosition(this.cellDisplay);
    const value = this.getValue();
    const start = cursorPos.start;
    const end = cursorPos.end;

    // Insert the reference at cursor position, replacing any selection
    const before = value.slice(0, start);
    const after = value.slice(end);
    const newValue = before + ref + after;

    // Update values
    this.cellEditorInput.value = newValue;

    // Update formula highlights (async, will call updateColoredDisplay via callback)
    this.onUpdateFormulaHighlights(newValue);

    // Move cursor to after the inserted reference
    const newPos = start + ref.length;
    requestAnimationFrame(() => {
      setCursorPosition(this.cellDisplay, newPos);
    });

    // Sync with formula bar
    this.formulaInput.value = newValue;

    // Broadcast editing state
    const selectedCell = this.getSelectedCell();
    if (this.syncAdapter && selectedCell) {
      this.syncAdapter.setEditing(selectedCell.col, selectedCell.row, newValue);
    }
  }

  /**
   * Get the display element (contenteditable)
   */
  getDisplayElement(): HTMLElement {
    return this.cellDisplay;
  }

  /**
   * Get the input element for direct manipulation
   */
  getInputElement(): HTMLInputElement {
    return this.cellEditorInput;
  }

  // =========================================================================
  // Cell Editing Operations
  // =========================================================================

  /**
   * Start editing the currently selected cell
   *
   * Edit modes:
   * - 'append': Double-click - cursor at end of existing content
   * - 'replace': Single-click + type - clears content and starts fresh
   * - 'select': F2/Enter - selects all content
   */
  async startEditing(options: StartEditingOptions = {}): Promise<void> {
    const { focusCellEditor = true, mode = "select", initialChar = "" } = options;
    const selectedCell = this.getSelectedCell();
    if (!selectedCell || this.isEditing() || !this.dataSource) return;

    // Get or create cell - single call returns ID and value
    let cellId: string | null = null;
    let initialValue = "";

    try {
      const result = await this.dataSource.getOrCreateCellAt(
        selectedCell.col,
        selectedCell.row
      );
      cellId = result.id;
      initialValue = result.formula || result.value || "";
      // If cell was created, refresh viewport to include it
      if (!result.existed) {
        await this.onFetchViewport();
      }
    } catch (e) {
      console.error("Error getting/creating cell:", e);
      return;
    }

    // Transition to editing state with context (cellId stored in state machine)
    this.uiStateMachine.transition(UIEvent.START_CELL_EDIT, {
      cellId,
      col: selectedCell.col,
      row: selectedCell.row,
      initialValue,
    });

    // Position the editor
    this.positionEditor(selectedCell);

    // Set value and cursor position based on mode
    if (mode === "replace") {
      // Replace mode: start with the initial character (clears existing content)
      this.cellEditorInput.value = initialChar;
      this.cellDisplay.textContent = initialChar;
      if (focusCellEditor) {
        this.cellDisplay.focus();
        // Place cursor at end (after the initial character)
        setCursorPosition(this.cellDisplay, initialChar.length);
      }
    } else if (mode === "append") {
      // Append mode: cursor at end of existing content
      this.cellEditorInput.value = initialValue;
      this.cellDisplay.textContent = initialValue;
      if (focusCellEditor) {
        this.cellDisplay.focus();
        setCursorPosition(this.cellDisplay, initialValue.length);
      }
    } else {
      // Select mode: select all content (default for F2/Enter)
      this.cellEditorInput.value = initialValue;
      this.cellDisplay.textContent = initialValue;
      if (focusCellEditor) {
        this.cellDisplay.focus();
        // Select all text in contenteditable
        const selection = window.getSelection();
        if (selection) {
          const range = document.createRange();
          range.selectNodeContents(this.cellDisplay);
          selection.removeAllRanges();
          selection.addRange(range);
        }
      }
    }

    // Sync formula bar
    this.formulaInput.value = this.cellEditorInput.value;
    this.formulaDisplay.textContent = this.cellEditorInput.value;

    // Show formula highlights for the initial value
    // (input event doesn't fire when value is set programmatically)
    this.onUpdateFormulaHighlights(this.cellEditorInput.value);

    // Broadcast initial editing state to peers
    if (this.syncAdapter && selectedCell) {
      this.syncAdapter.setEditing(
        selectedCell.col,
        selectedCell.row,
        this.cellEditorInput.value
      );
    }
  }

  /**
   * Cancel the current cell edit, discarding changes
   */
  cancelEditing(): void {
    if (!this.isEditing()) return;
    this.uiStateMachine.transition(UIEvent.CANCEL_CELL_EDIT);
    this.cellEditorContainer.style.display = "none";
    this.cellEditorInput.value = "";
    this.cellDisplay.innerHTML = "";
    // Clear formula highlights
    this.onUpdateFormulaHighlights("");
    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }
    this.canvas.focus();
  }

  /**
   * Commit the current cell edit, saving changes
   */
  async confirmEditing(): Promise<void> {
    if (!this.isEditing() || !this.dataSource) return;

    // Get cellId from state machine context before transitioning
    const context = this.uiStateMachine.getStateContext();
    const cellId = context.cellId as string | undefined;
    if (!cellId) return;

    const newValue = this.getValue();

    this.uiStateMachine.transition(UIEvent.COMMIT_CELL_EDIT);
    this.cellEditorContainer.style.display = "none";
    this.cellEditorInput.value = "";
    this.cellDisplay.innerHTML = "";

    // Clear formula highlights
    this.onUpdateFormulaHighlights("");

    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }

    try {
      if (newValue === "" || newValue.trim() === "") {
        // Delete cell when content is completely cleared
        await this.dataSource.deleteCell(cellId);
      } else {
        await this.dataSource.updateCell(cellId, newValue);
      }
      // Listener handles refresh automatically
    } catch (e) {
      console.error("Error updating cell:", e);
    }
  }

  /**
   * Delete all cells in the current range selection
   */
  async deleteRangeCells(): Promise<void> {
    if (!this.dataSource) return;

    const range = getNormalizedRange(
      this.getSelectionStart(),
      this.getSelectionEnd()
    );
    if (!range) return;

    try {
      // Delete cells at each position - deleteCellAt is a no-op if cell doesn't exist
      for (let col = range.minCol; col <= range.maxCol; col++) {
        for (let row = range.minRow; row <= range.maxRow; row++) {
          await this.dataSource.deleteCellAt(col, row);
        }
      }
    } catch (e) {
      console.error("Error deleting range cells:", e);
    }
  }

  // =========================================================================
  // Navigation Helpers
  // =========================================================================

  /**
   * Move selection after edit (Enter key)
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
   * Move selection after edit (Tab key)
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
  }

  /**
   * Move selection after edit (Arrow key)
   * @param key Arrow key direction
   */
  navigateAfterArrow(key: "ArrowUp" | "ArrowDown" | "ArrowLeft" | "ArrowRight"): void {
    const selectedCell = this.getSelectedCell();
    const sheetInfo = this.getSheetInfo();
    if (!selectedCell || !sheetInfo) return;

    let newCol = selectedCell.col;
    let newRow = selectedCell.row;

    if (key === "ArrowUp") {
      newRow = Math.max(0, selectedCell.row - 1);
    } else if (key === "ArrowDown") {
      newRow = Math.min(sheetInfo.rowCount - 1, selectedCell.row + 1);
    } else if (key === "ArrowLeft") {
      newCol = Math.max(0, selectedCell.col - 1);
    } else if (key === "ArrowRight") {
      newCol = Math.min(sheetInfo.colCount - 1, selectedCell.col + 1);
    }

    const newPos = { col: newCol, row: newRow };
    this.onSetSelection(newPos, newPos, newPos);
    this.onRender();
    this.onUpdateFormulaBar();
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Position the cell editor over the selected cell
   */
  private positionEditor(cell: Position): void {
    const scrollX = this.getScrollX();
    const scrollY = this.getScrollY();
    const colWidths = this.getColWidths();
    const rowHeights = this.getRowHeights();

    let cellX = HEADER_WIDTH - scrollX;
    for (let i = 0; i < cell.col; i++) {
      cellX += colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    let cellY = HEADER_HEIGHT - scrollY;
    for (let i = 0; i < cell.row; i++) {
      cellY += rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    }

    const cellWidth = colWidths.get(cell.col) ?? DEFAULT_COL_WIDTH;
    const cellHeight = rowHeights.get(cell.row) ?? DEFAULT_ROW_HEIGHT;

    this.cellEditorContainer.style.left = cellX + "px";
    this.cellEditorContainer.style.top = cellY + "px";
    this.cellEditorContainer.style.width = cellWidth + "px";
    this.cellEditorContainer.style.height = cellHeight + "px";
    this.cellEditorContainer.style.display = "block";
  }

  /**
   * Set up event listeners on the cell editor contenteditable
   */
  private setupEventListeners(): void {
    // Keyboard events on contenteditable
    this.cellDisplay.addEventListener("keydown", (e) => {
      e.stopPropagation();
      if (e.key === "Escape") {
        e.preventDefault();
        this.cancelEditing();
      } else if (e.key === "Enter") {
        e.preventDefault();
        this.confirmEditing().then(() => {
          this.navigateAfterEnter(e.shiftKey);
        });
      } else if (e.key === "Tab") {
        e.preventDefault();
        this.confirmEditing().then(() => {
          this.navigateAfterTab(e.shiftKey);
        });
      } else if (
        e.key === "ArrowUp" ||
        e.key === "ArrowDown" ||
        e.key === "ArrowLeft" ||
        e.key === "ArrowRight"
      ) {
        // Arrow keys during editing: check if cursor is at boundary
        const cursorPos = getCursorPosition(this.cellDisplay);
        const textLen = this.getValue().length;
        const atStart = cursorPos.start === 0;
        const atEnd = cursorPos.start === textLen;

        // Only commit and navigate if at boundary in the direction of movement
        if (
          (e.key === "ArrowLeft" && atStart) ||
          (e.key === "ArrowRight" && atEnd) ||
          e.key === "ArrowUp" ||
          e.key === "ArrowDown"
        ) {
          e.preventDefault();
          this.confirmEditing().then(() => {
            this.navigateAfterArrow(
              e.key as "ArrowUp" | "ArrowDown" | "ArrowLeft" | "ArrowRight"
            );
          });
        }
        // Otherwise, let the arrow key work normally within the text field
      }
    });

    // Blur commits the edit
    this.cellDisplay.addEventListener("blur", () => {
      if (this.isEditing()) {
        this.confirmEditing();
      }
    });

    // Live sync: cell editor -> formula bar + formula highlights + broadcast editing
    this.cellDisplay.addEventListener("input", () => {
      if (this.isEditing()) {
        const value = getPlainText(this.cellDisplay);
        this.cellEditorInput.value = value;
        this.formulaInput.value = value;

        // Update formula highlights for live feedback while typing formulas
        this.onUpdateFormulaHighlights(value);

        // Broadcast ephemeral editing state to peers
        const selectedCell = this.getSelectedCell();
        if (this.syncAdapter && selectedCell) {
          this.syncAdapter.setEditing(selectedCell.col, selectedCell.row, value);
        }
      }
    });

    // Prevent paste from including formatting
    this.cellDisplay.addEventListener("paste", (e) => {
      e.preventDefault();
      const text = e.clipboardData?.getData("text/plain") ?? "";
      document.execCommand("insertText", false, text);
    });
  }
}
