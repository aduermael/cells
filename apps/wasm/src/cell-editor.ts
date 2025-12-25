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
import { getNormalizedRange } from "./grid-utils";

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
 */
export class CellEditor {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private cellEditorInput: HTMLInputElement;
  private formulaInput: HTMLInputElement;
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

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    cellEditorInput: HTMLInputElement;
    formulaInput: HTMLInputElement;
    canvas: HTMLCanvasElement;
    getSelectedCell: () => Position | null;
    getSelectionStart: () => Position | null;
    getSelectionEnd: () => Position | null;
    getSheetInfo: () => SheetInfo | null;
    getColWidths: () => Map<number, number>;
    getRowHeights: () => Map<number, number>;
    getScrollX: () => number;
    getScrollY: () => number;
    onFetchViewport: () => Promise<void>;
    onRender: () => void;
    onUpdateFormulaBar: () => void;
    onSetSelection: (cell: Position, start: Position, end: Position) => void;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.cellEditorInput = config.cellEditorInput;
    this.formulaInput = config.formulaInput;
    this.canvas = config.canvas;
    this.getSelectedCell = config.getSelectedCell;
    this.getSelectionStart = config.getSelectionStart;
    this.getSelectionEnd = config.getSelectionEnd;
    this.getSheetInfo = config.getSheetInfo;
    this.getColWidths = config.getColWidths;
    this.getRowHeights = config.getRowHeights;
    this.getScrollX = config.getScrollX;
    this.getScrollY = config.getScrollY;
    this.onFetchViewport = config.onFetchViewport;
    this.onRender = config.onRender;
    this.onUpdateFormulaBar = config.onUpdateFormulaBar;
    this.onSetSelection = config.onSetSelection;

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
      if (focusCellEditor) {
        this.cellEditorInput.focus();
        // Place cursor at end (after the initial character)
        this.cellEditorInput.setSelectionRange(
          initialChar.length,
          initialChar.length
        );
      }
    } else if (mode === "append") {
      // Append mode: cursor at end of existing content
      this.cellEditorInput.value = initialValue;
      if (focusCellEditor) {
        this.cellEditorInput.focus();
        this.cellEditorInput.setSelectionRange(
          this.cellEditorInput.value.length,
          this.cellEditorInput.value.length
        );
      }
    } else {
      // Select mode: select all content (default for F2/Enter)
      this.cellEditorInput.value = initialValue;
      if (focusCellEditor) {
        this.cellEditorInput.focus();
        this.cellEditorInput.select();
      }
    }

    // Sync formula bar
    this.formulaInput.value = this.cellEditorInput.value;

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
    this.cellEditorInput.style.display = "none";
    this.cellEditorInput.value = "";
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

    const newValue = this.cellEditorInput.value;

    this.uiStateMachine.transition(UIEvent.COMMIT_CELL_EDIT);
    this.cellEditorInput.style.display = "none";

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

    this.cellEditorInput.style.left = cellX + "px";
    this.cellEditorInput.style.top = cellY + "px";
    this.cellEditorInput.style.width = cellWidth + "px";
    this.cellEditorInput.style.height = cellHeight + "px";
    this.cellEditorInput.style.display = "block";
  }

  /**
   * Set up event listeners on the cell editor input
   */
  private setupEventListeners(): void {
    this.cellEditorInput.addEventListener("keydown", (e) => {
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
        const cursorPos = this.cellEditorInput.selectionStart ?? 0;
        const textLen = this.cellEditorInput.value.length;
        const atStart = cursorPos === 0;
        const atEnd = cursorPos === textLen;

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

    this.cellEditorInput.addEventListener("blur", () => {
      if (this.isEditing()) {
        this.confirmEditing();
      }
    });

    // Live sync: cell editor -> formula bar + broadcast editing
    this.cellEditorInput.addEventListener("input", () => {
      if (this.isEditing()) {
        this.formulaInput.value = this.cellEditorInput.value;

        // Broadcast ephemeral editing state to peers
        const selectedCell = this.getSelectedCell();
        if (this.syncAdapter && selectedCell) {
          this.syncAdapter.setEditing(
            selectedCell.col,
            selectedCell.row,
            this.cellEditorInput.value
          );
        }
      }
    });
  }
}
