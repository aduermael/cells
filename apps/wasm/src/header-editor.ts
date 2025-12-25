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
import { colToLetter, getCellAt } from "./grid-utils";

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
 */
export class FormulaBarEditor {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private formulaInput: HTMLInputElement;
  private cellEditorInput: HTMLInputElement;
  private canvas: HTMLCanvasElement;

  // Nullable dependencies (set after construction)
  private dataSource: WasmDataSource | null = null;
  private syncAdapter: CppSyncAdapter | null = null;

  // =========================================================================
  // State accessors (provided by App)
  // =========================================================================

  private getSelectedCell: () => Position | null;
  private getSelectionStart: () => Position | null;
  private getSheetInfo: () => SheetInfo | null;
  private getCells: () => CellData[];
  private setCells: (cells: CellData[]) => void;

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
  private isEditing: () => boolean;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    formulaInput: HTMLInputElement;
    cellEditorInput: HTMLInputElement;
    canvas: HTMLCanvasElement;
    getSelectedCell: () => Position | null;
    getSelectionStart: () => Position | null;
    getSheetInfo: () => SheetInfo | null;
    getCells: () => CellData[];
    setCells: (cells: CellData[]) => void;
    onFetchViewport: () => Promise<void>;
    onRender: () => void;
    onUpdateFormulaBar: () => void;
    onSetSelection: (cell: Position, start: Position, end: Position) => void;
    onUpdateAstDebugPanel: (value: string) => void;
    isEditing: () => boolean;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.formulaInput = config.formulaInput;
    this.cellEditorInput = config.cellEditorInput;
    this.canvas = config.canvas;
    this.getSelectedCell = config.getSelectedCell;
    this.getSelectionStart = config.getSelectionStart;
    this.getSheetInfo = config.getSheetInfo;
    this.getCells = config.getCells;
    this.setCells = config.setCells;
    this.onFetchViewport = config.onFetchViewport;
    this.onRender = config.onRender;
    this.onUpdateFormulaBar = config.onUpdateFormulaBar;
    this.onSetSelection = config.onSetSelection;
    this.onUpdateAstDebugPanel = config.onUpdateAstDebugPanel;
    this.isEditing = config.isEditing;

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

  isEditingFormulaBar(): boolean {
    return this.uiStateMachine.isInState("FORMULA_BAR_EDITING");
  }

  // =========================================================================
  // Formula Bar Editing Operations
  // =========================================================================

  /**
   * Commit the current formula bar edit, saving changes
   */
  async commitFormulaBarEdit(): Promise<void> {
    // Use anchor cell (selectionStart) for editing, not selectedCell
    const editCell = this.getSelectionStart() || this.getSelectedCell();
    if (!editCell || !this.dataSource) return;

    // Hide cell editor if it's showing
    if (this.isEditing()) {
      this.uiStateMachine.transition(UIEvent.COMMIT_CELL_EDIT);
      this.cellEditorInput.style.display = "none";
    }

    const cells = this.getCells();
    let cell = getCellAt(editCell.col, editCell.row, cells);
    const newValue = this.formulaInput.value;

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

    this.uiStateMachine.transition(UIEvent.COMMIT_FORMULA_EDIT);
    // Hide cell editor if it was showing during formula bar editing
    this.cellEditorInput.style.display = "none";
    this.cellEditorInput.value = "";
    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }
    this.canvas.focus();
  }

  /**
   * Cancel the current formula bar edit, discarding changes
   */
  cancelFormulaBarEdit(): void {
    this.uiStateMachine.transition(UIEvent.CANCEL_FORMULA_EDIT);
    // Hide cell editor if it was showing during formula bar editing
    this.cellEditorInput.style.display = "none";
    this.cellEditorInput.value = "";
    // Clear ephemeral editing state
    if (this.syncAdapter) {
      this.syncAdapter.clearEditing();
    }
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
    this.canvas.focus();
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
    this.formulaInput.focus();
    this.formulaInput.select();
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Set up event listeners on the formula bar input
   */
  private setupEventListeners(): void {
    this.formulaInput.addEventListener("focus", () => {
      // If coming from cell editor, preserve its value in the formula bar
      const wasEditingCell = this.isEditing();
      const cellEditorValue = wasEditingCell
        ? this.cellEditorInput.value
        : null;

      this.uiStateMachine.transition(UIEvent.START_FORMULA_EDIT);

      // If we had a value from cell editor, ensure formula bar has it
      if (cellEditorValue !== null) {
        this.formulaInput.value = cellEditorValue;
      }

      // Keep cell editor visible and synced (for visual feedback on the cell)
      const selectedCell = this.getSelectedCell();
      if (wasEditingCell && selectedCell) {
        this.cellEditorInput.style.display = "block";
      }
    });

    this.formulaInput.addEventListener("keydown", (e) => {
      e.stopPropagation();
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

    this.formulaInput.addEventListener("blur", () => {
      // isEditingFormulaBar() is set to false by mousedown handler before blur fires
      // So this only commits when focus moves elsewhere (Tab to another element, etc.)
      const selectedCell = this.getSelectedCell();
      if (this.isEditingFormulaBar() && selectedCell) {
        this.commitFormulaBarEdit();
      }
    });

    // Live sync: update cell display while typing in formula bar
    this.formulaInput.addEventListener("input", () => {
      // Update AST debug panel live as user types
      this.onUpdateAstDebugPanel(this.formulaInput.value);

      if (!this.isEditingFormulaBar()) return;

      // Sync to cell editor if it's visible (bidirectional sync)
      if (this.cellEditorInput.style.display === "block") {
        this.cellEditorInput.value = this.formulaInput.value;
      }

      // Use anchor cell (selectionStart) for editing, not selectedCell
      const editCell = this.getSelectionStart() || this.getSelectedCell();
      if (!editCell) return;

      // Broadcast ephemeral editing state to peers
      if (this.syncAdapter) {
        this.syncAdapter.setEditing(
          editCell.col,
          editCell.row,
          this.formulaInput.value
        );
      }

      // Update local cell data for live preview (without saving to engine)
      const cells = this.getCells();
      let cell = getCellAt(editCell.col, editCell.row, cells);
      if (cell) {
        // Update existing cell's display value
        cell.value = this.formulaInput.value;
        cell.formula = this.formulaInput.value.startsWith("=")
          ? this.formulaInput.value
          : undefined;
      } else {
        // Create a temporary local cell for preview
        cells.push({
          id: "_temp_",
          col: editCell.col,
          row: editCell.row,
          value: this.formulaInput.value,
          type: "s",
        });
        this.setCells(cells);
      }
      this.onRender();
    });
  }
}
