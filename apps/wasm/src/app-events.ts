// App Events - Canvas and document event handlers
// This module sets up all event listeners for canvas interactions,
// keyboard navigation, and window resize.

import { UIEvent, type UIStateMachine } from "./ui-state";
import type { WasmDataSource } from "./wasm-data-source";
import type { CppSyncAdapter } from "./cpp-sync-adapter";
import type { SheetInfo, Position } from "./types";
import {
  HEADER_WIDTH,
  HEADER_HEIGHT,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
} from "./grid-renderer";
import {
  getColAtX,
  getRowAtY,
  getResizeHandleCol,
  getResizeHandleRow,
  getDropTargetCol,
  getDropTargetRow,
  getColumnId,
  getRowId,
  DRAG_THRESHOLD,
  hasRangeSelection,
} from "./grid-utils";
import type { CellEditor } from "./cell-editor";
import type { ColumnHeaderEditor, FormulaBarEditor } from "./header-editor";
import type { PresenceBroadcaster } from "./presence-broadcast";
import { colToLetter } from "./grid-utils";

// =============================================================================
// Types
// =============================================================================

/** Configuration for AppEventManager */
export interface AppEventManagerConfig {
  canvas: HTMLCanvasElement;
  uiStateMachine: UIStateMachine;
  cellEditor: CellEditor;
  columnHeaderEditor: ColumnHeaderEditor;
  formulaBarEditor: FormulaBarEditor;
  presenceBroadcaster: PresenceBroadcaster;
  formulaInput: HTMLInputElement;

  // State accessors
  getSheetInfo: () => SheetInfo | null;
  getSelectedCell: () => Position | null;
  getSelectionStart: () => Position | null;
  getSelectionEnd: () => Position | null;
  getScrollX: () => number;
  getScrollY: () => number;
  getColWidths: () => Map<number, number>;
  getRowHeights: () => Map<number, number>;
  getColumns: () => Array<{ id: string; pos: number }>;
  getRows: () => Array<{ id: string; pos: number }>;
  getDataSource: () => WasmDataSource | null;
  getSyncAdapter: () => CppSyncAdapter | null;

  // Resize state accessors/mutators
  getResizeColIndex: () => number;
  setResizeColIndex: (v: number) => void;
  getResizeStartX: () => number;
  setResizeStartX: (v: number) => void;
  getResizeStartWidth: () => number;
  setResizeStartWidth: (v: number) => void;
  getResizePreviewX: () => number;
  setResizePreviewX: (v: number) => void;
  getResizeRowIndex: () => number;
  setResizeRowIndex: (v: number) => void;
  getResizeStartY: () => number;
  setResizeStartY: (v: number) => void;
  getResizeStartHeight: () => number;
  setResizeStartHeight: (v: number) => void;
  getResizePreviewY: () => number;
  setResizePreviewY: (v: number) => void;

  // Drag state accessors/mutators
  getDragSourceIndex: () => number;
  setDragSourceIndex: (v: number) => void;
  getDragTargetIndex: () => number;
  setDragTargetIndex: (v: number) => void;
  getDragMouseX: () => number;
  setDragMouseX: (v: number) => void;
  getDragMouseY: () => number;
  setDragMouseY: (v: number) => void;
  getPendingDragColumn: () => boolean;
  setPendingDragColumn: (v: boolean) => void;
  getPendingDragRow: () => boolean;
  setPendingDragRow: (v: boolean) => void;
  getPendingDragStartX: () => number;
  setPendingDragStartX: (v: number) => void;
  getPendingDragStartY: () => number;
  setPendingDragStartY: (v: number) => void;

  // Selection mutators
  setSelectedCell: (cell: Position | null) => void;
  setSelectedColumn: (col: number | null) => void;
  setSelectedRow: (row: number | null) => void;
  setSelectionStart: (pos: Position | null) => void;
  setSelectionEnd: (pos: Position | null) => void;
  setSelection: (cell: Position, start: Position, end: Position) => void;

  // Scroll mutators
  setScrollX: (v: number) => void;
  setScrollY: (v: number) => void;

  // Callbacks
  render: () => void;
  updateFormulaBar: () => void;
  clearFormulaHighlights: () => void;
  resizeCanvas: () => void;
  fetchViewportNow: () => void;
  toggleAstDebugPanel: () => void;
  commitFormulaBarEdit: () => Promise<void>;
}

// =============================================================================
// AppEventManager Class
// =============================================================================

/**
 * AppEventManager sets up and manages all event listeners for canvas
 * and document interactions.
 *
 * Responsibilities:
 * - Canvas wheel/scroll handling
 * - Mouse interactions (selection, resize, drag)
 * - Keyboard navigation and editing triggers
 * - Window resize handling
 */
/** Tracks the last inserted formula reference for range building */
interface FormulaRefState {
  /** Cell position */
  position: Position;
  /** Cursor position in input where reference starts */
  cursorStart: number;
  /** Cursor position in input where reference ends */
  cursorEnd: number;
}

export class AppEventManager {
  private config: AppEventManagerConfig;

  /** Last inserted reference during formula editing (for Shift+click/drag range building) */
  private lastFormulaRef: FormulaRefState | null = null;

  /** Start position for drag-selecting a range during formula editing */
  private formulaDragStart: Position | null = null;

  constructor(config: AppEventManagerConfig) {
    this.config = config;
  }

  // =========================================================================
  // Setup
  // =========================================================================

  /**
   * Set up all event listeners
   */
  setupEventListeners(): void {
    this.setupCanvasEvents();
    this.setupKeyboardEvents();
    this.setupWindowEvents();
  }

  // =========================================================================
  // Formula Reference Insertion Helpers
  // =========================================================================

  /**
   * Check if we're currently in formula editing mode (editing a formula in cell or formula bar)
   */
  private isInFormulaEditingMode(): boolean {
    const { cellEditor, formulaBarEditor } = this.config;
    const inFormulaMode = cellEditor.isFormulaMode() || formulaBarEditor.isFormulaMode();

    // Clear the formula ref state if not in formula mode
    if (!inFormulaMode) {
      this.lastFormulaRef = null;
      this.formulaDragStart = null;
    }

    return inFormulaMode;
  }

  /**
   * Get the active formula input element
   */
  private getActiveFormulaInput(): HTMLInputElement | null {
    const { cellEditor, formulaBarEditor } = this.config;
    if (cellEditor.isFormulaMode()) {
      return cellEditor.getInputElement();
    } else if (formulaBarEditor.isFormulaMode()) {
      return formulaBarEditor.getInputElement();
    }
    return null;
  }

  /**
   * Insert a reference into the active formula editor and track cursor positions
   */
  private insertFormulaReference(ref: string, position: Position): void {
    const { cellEditor, formulaBarEditor, render } = this.config;

    const input = this.getActiveFormulaInput();
    if (!input) return;

    const cursorStart = input.selectionStart ?? input.value.length;

    if (cellEditor.isFormulaMode()) {
      cellEditor.insertReferenceAtCursor(ref);
      cellEditor.getInputElement().focus();
    } else if (formulaBarEditor.isFormulaMode()) {
      formulaBarEditor.insertReferenceAtCursor(ref);
      formulaBarEditor.getInputElement().focus();
    }

    // Track this reference for potential shift+click range extension
    this.lastFormulaRef = {
      position,
      cursorStart,
      cursorEnd: cursorStart + ref.length,
    };

    render();
  }

  /**
   * Insert a column or row reference (doesn't track for range extension)
   */
  private insertColumnOrRowReference(ref: string): void {
    const { cellEditor, formulaBarEditor, render } = this.config;

    if (cellEditor.isFormulaMode()) {
      cellEditor.insertReferenceAtCursor(ref);
      cellEditor.getInputElement().focus();
    } else if (formulaBarEditor.isFormulaMode()) {
      formulaBarEditor.insertReferenceAtCursor(ref);
      formulaBarEditor.getInputElement().focus();
    }

    render();
  }

  /**
   * Replace the last inserted reference with a range
   */
  private replaceLastRefWithRange(endCol: number, endRow: number): void {
    const { cellEditor, formulaBarEditor, render } = this.config;

    if (!this.lastFormulaRef) return;

    const input = this.getActiveFormulaInput();
    if (!input) return;

    const startCol = colToLetter(this.lastFormulaRef.position.col);
    const startRow = this.lastFormulaRef.position.row + 1;
    const endColLetter = colToLetter(endCol);
    const endRowNum = endRow + 1;
    const rangeRef = `${startCol}${startRow}:${endColLetter}${endRowNum}`;

    // Select and replace the previous reference
    const before = input.value.slice(0, this.lastFormulaRef.cursorStart);
    const after = input.value.slice(this.lastFormulaRef.cursorEnd);
    input.value = before + rangeRef + after;

    // Update cursor position
    const newCursorEnd = this.lastFormulaRef.cursorStart + rangeRef.length;
    input.setSelectionRange(newCursorEnd, newCursorEnd);

    // Sync with the other editor
    if (cellEditor.isFormulaMode()) {
      const formulaInput = formulaBarEditor.getInputElement();
      formulaInput.value = input.value;
    } else if (formulaBarEditor.isFormulaMode()) {
      const cellInput = cellEditor.getInputElement();
      if (cellInput.style.display === "block") {
        cellInput.value = input.value;
      }
    }

    // Trigger highlight update by dispatching an input event
    input.dispatchEvent(new Event("input", { bubbles: true }));

    // Update tracked reference to the new range
    this.lastFormulaRef.cursorEnd = newCursorEnd;

    input.focus();
    render();
  }

  // =========================================================================
  // Canvas Events
  // =========================================================================

  private setupCanvasEvents(): void {
    const { canvas } = this.config;

    // Wheel/scroll
    canvas.addEventListener(
      "wheel",
      (e) => this.handleWheel(e),
      { passive: false }
    );

    // Mouse interactions
    canvas.addEventListener("mousedown", (e) => this.handleMouseDown(e));
    canvas.addEventListener("mousemove", (e) => this.handleMouseMove(e));
    canvas.addEventListener("mouseup", (e) => this.handleMouseUp(e));
    canvas.addEventListener("mouseleave", () => this.handleMouseLeave());
    canvas.addEventListener("dblclick", (e) => this.handleDblClick(e));
  }

  private handleWheel(e: WheelEvent): void {
    const { getSheetInfo, getScrollX, getScrollY, setScrollX, setScrollY, render, fetchViewportNow, canvas } = this.config;

    const sheetInfo = getSheetInfo();
    if (!sheetInfo) return;
    e.preventDefault();

    const maxScrollX = Math.max(
      0,
      sheetInfo.colCount * DEFAULT_COL_WIDTH - canvas.clientWidth + HEADER_WIDTH
    );
    const maxScrollY = Math.max(
      0,
      sheetInfo.rowCount * DEFAULT_ROW_HEIGHT - canvas.clientHeight + HEADER_HEIGHT
    );

    setScrollX(Math.max(0, Math.min(maxScrollX, getScrollX() + e.deltaX)));
    setScrollY(Math.max(0, Math.min(maxScrollY, getScrollY() + e.deltaY)));

    render();
    fetchViewportNow();
  }

  private handleMouseDown(e: MouseEvent): void {
    const {
      canvas, uiStateMachine, getSheetInfo, getScrollX, getScrollY, getColWidths, getRowHeights,
      setSelectedCell, setSelectedColumn, setSelectedRow, setSelectionStart, setSelectionEnd,
      getSelectionStart,
      setResizeColIndex, setResizeStartX, setResizeStartWidth, setResizePreviewX,
      setResizeRowIndex, setResizeStartY, setResizeStartHeight, setResizePreviewY,
      setPendingDragColumn, setPendingDragRow, setPendingDragStartX, setPendingDragStartY,
      setDragSourceIndex, setDragTargetIndex,
      render, updateFormulaBar, clearFormulaHighlights, cellEditor, commitFormulaBarEdit
    } = this.config;

    const sheetInfo = getSheetInfo();
    if (!sheetInfo) return;

    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const scrollX = getScrollX();
    const scrollY = getScrollY();
    const colWidths = getColWidths();
    const rowHeights = getRowHeights();

    // Column resize
    if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
      const resizeCol = getResizeHandleCol(x, scrollX, colWidths, sheetInfo);
      if (resizeCol >= 0) {
        // Commit any active edit before starting resize
        if (cellEditor.isEditing()) {
          cellEditor.confirmEditing();
        } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
          commitFormulaBarEdit();
        }
        uiStateMachine.transition(UIEvent.START_COLUMN_RESIZE);
        setResizeColIndex(resizeCol);
        setResizeStartX(e.clientX);
        setResizeStartWidth(colWidths.get(resizeCol) || DEFAULT_COL_WIDTH);

        let colX = HEADER_WIDTH - scrollX;
        for (let i = 0; i <= resizeCol; i++) {
          colX += colWidths.get(i) || DEFAULT_COL_WIDTH;
        }
        setResizePreviewX(colX);

        canvas.style.cursor = "col-resize";
        e.preventDefault();
        return;
      }
    }

    // Column header click (select, pending drag, or insert reference)
    if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      if (col >= 0) {
        // Check if in formula editing mode - insert column reference instead of selecting
        if (this.isInFormulaEditingMode()) {
          const colLetter = colToLetter(col);
          const ref = `${colLetter}:${colLetter}`; // Column reference like "B:B"
          // Column references don't support range extension, clear tracking
          this.lastFormulaRef = null;
          this.formulaDragStart = null;
          this.insertColumnOrRowReference(ref);
          e.preventDefault();
          return;
        }

        setSelectedCell(null);
        setSelectedRow(null);
        setSelectedColumn(col);
        setSelectionStart(null);
        setSelectionEnd(null);

        // Start pending drag (actual drag starts after threshold)
        setPendingDragColumn(true);
        setPendingDragStartX(x);
        setPendingDragStartY(y);
        setDragSourceIndex(col);
        setDragTargetIndex(col);

        clearFormulaHighlights();
        render();
        e.preventDefault();
        return;
      }
    }

    // Row resize
    if (x < HEADER_WIDTH && x > 0 && y > HEADER_HEIGHT) {
      const resizeRow = getResizeHandleRow(y, scrollY, rowHeights, sheetInfo);
      if (resizeRow >= 0) {
        // Commit any active edit before starting resize
        if (cellEditor.isEditing()) {
          cellEditor.confirmEditing();
        } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
          commitFormulaBarEdit();
        }
        uiStateMachine.transition(UIEvent.START_ROW_RESIZE);
        setResizeRowIndex(resizeRow);
        setResizeStartY(e.clientY);
        setResizeStartHeight(rowHeights.get(resizeRow) || DEFAULT_ROW_HEIGHT);

        let rowY = HEADER_HEIGHT - scrollY;
        for (let i = 0; i <= resizeRow; i++) {
          rowY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
        }
        setResizePreviewY(rowY);

        canvas.style.cursor = "row-resize";
        e.preventDefault();
        return;
      }
    }

    // Row header click (select, pending drag, or insert reference)
    if (x < HEADER_WIDTH && x > 0 && y > HEADER_HEIGHT) {
      const row = getRowAtY(y, scrollY, rowHeights, sheetInfo.rowCount);
      if (row >= 0) {
        // Check if in formula editing mode - insert row reference instead of selecting
        if (this.isInFormulaEditingMode()) {
          const rowNum = row + 1; // 1-based row number
          const ref = `${rowNum}:${rowNum}`; // Row reference like "3:3"
          // Row references don't support range extension, clear tracking
          this.lastFormulaRef = null;
          this.formulaDragStart = null;
          this.insertColumnOrRowReference(ref);
          e.preventDefault();
          return;
        }

        setSelectedCell(null);
        setSelectedColumn(null);
        setSelectedRow(row);
        setSelectionStart(null);
        setSelectionEnd(null);

        // Start pending drag (actual drag starts after threshold)
        setPendingDragRow(true);
        setPendingDragStartX(x);
        setPendingDragStartY(y);
        setDragSourceIndex(row);
        setDragTargetIndex(row);

        clearFormulaHighlights();
        render();
        e.preventDefault();
        return;
      }
    }

    // Cell selection or insert reference
    if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      const row = getRowAtY(y, scrollY, rowHeights, sheetInfo.rowCount);

      if (col >= 0 && row >= 0) {
        // Check if in formula editing mode - insert cell reference instead of selecting
        if (this.isInFormulaEditingMode()) {
          // Shift+click replaces the last reference with a range
          if (e.shiftKey && this.lastFormulaRef) {
            this.replaceLastRefWithRange(col, row);
          } else {
            // Insert single cell reference and track for drag/shift+click
            const colLetter = colToLetter(col);
            const rowNum = row + 1;
            const ref = `${colLetter}${rowNum}`;
            this.insertFormulaReference(ref, { col, row });
            // Start tracking for potential drag selection
            this.formulaDragStart = { col, row };
          }
          e.preventDefault();
          return;
        }

        setSelectedColumn(null);
        setSelectedRow(null);

        const selStart = getSelectionStart();
        const isShiftClick = e.shiftKey && selStart;

        // Helper to apply selection
        const applySelection = () => {
          if (isShiftClick && selStart) {
            // Extend selection from existing anchor
            setSelectionEnd({ col, row });
            setSelectedCell({ col, row });
          } else {
            // New selection
            setSelectedCell({ col, row });
            setSelectionStart({ col, row });
            setSelectionEnd({ col, row });
          }
          // Pass selection context to state machine
          uiStateMachine.transition(UIEvent.START_SELECTING, {
            selectedCell: { col, row },
            selectionStart: isShiftClick ? selStart : { col, row },
            selectionEnd: { col, row },
          });
          render();
          updateFormulaBar();
          canvas.focus();
        };

        // If editing cell editor, commit first then select new cell
        if (cellEditor.isEditing()) {
          cellEditor.confirmEditing().then(applySelection);
        } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
          // Mark as not editing BEFORE commit to prevent blur handler double-commit
          uiStateMachine.transition(UIEvent.COMMIT_FORMULA_EDIT);
          commitFormulaBarEdit().then(applySelection);
        } else {
          applySelection();
        }
      }
    }
  }

  private handleMouseMove(e: MouseEvent): void {
    const {
      canvas, uiStateMachine, cellEditor, getSheetInfo, getScrollX, getScrollY, getColWidths, getRowHeights,
      getPendingDragColumn, setPendingDragColumn, getPendingDragRow, setPendingDragRow,
      getPendingDragStartX, getPendingDragStartY,
      setDragMouseX, setDragMouseY, setDragTargetIndex,
      getResizeColIndex, getResizeStartX, getResizeStartWidth, setResizePreviewX,
      getResizeRowIndex, getResizeStartY, getResizeStartHeight, setResizePreviewY,
      getSelectionEnd, setSelectionEnd, setSelectedCell,
      render, updateFormulaBar, presenceBroadcaster, commitFormulaBarEdit
    } = this.config;

    const sheetInfo = getSheetInfo();
    if (!sheetInfo) return;

    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const scrollX = getScrollX();
    const scrollY = getScrollY();
    const colWidths = getColWidths();
    const rowHeights = getRowHeights();

    // Handle drag selection during formula editing (click+drag to select range)
    if (this.formulaDragStart && this.isInFormulaEditingMode() && x > HEADER_WIDTH && y > HEADER_HEIGHT) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      const row = getRowAtY(y, scrollY, rowHeights, sheetInfo.rowCount);
      if (col >= 0 && row >= 0) {
        // Only update if moved to a different cell
        if (col !== this.formulaDragStart.col || row !== this.formulaDragStart.row) {
          // Replace the last reference with a range from drag start to current position
          if (this.lastFormulaRef) {
            this.replaceLastRefWithRange(col, row);
          }
        }
      }
      return; // Don't process other handlers during formula drag
    }

    // Check if pending drag should become actual drag (threshold exceeded)
    if (getPendingDragColumn()) {
      const dx = Math.abs(x - getPendingDragStartX());
      const dy = Math.abs(y - getPendingDragStartY());
      if (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD) {
        // Commit any active edit before starting drag
        if (cellEditor.isEditing()) {
          cellEditor.confirmEditing();
        } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
          commitFormulaBarEdit();
        }
        // Threshold exceeded - start actual drag
        setPendingDragColumn(false);
        uiStateMachine.transition(UIEvent.START_COLUMN_DRAG);
        setDragMouseX(x);
        setDragMouseY(y);
        canvas.style.cursor = "grabbing";
      }
    }

    if (getPendingDragRow()) {
      const dx = Math.abs(x - getPendingDragStartX());
      const dy = Math.abs(y - getPendingDragStartY());
      if (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD) {
        // Commit any active edit before starting drag
        if (cellEditor.isEditing()) {
          cellEditor.confirmEditing();
        } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
          commitFormulaBarEdit();
        }
        // Threshold exceeded - start actual drag
        setPendingDragRow(false);
        uiStateMachine.transition(UIEvent.START_ROW_DRAG);
        setDragMouseX(x);
        setDragMouseY(y);
        canvas.style.cursor = "grabbing";
      }
    }

    // Range selection drag
    if (uiStateMachine.isInState("SELECTING") && x > HEADER_WIDTH && y > HEADER_HEIGHT) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      const row = getRowAtY(y, scrollY, rowHeights, sheetInfo.rowCount);
      if (col >= 0 && row >= 0) {
        const selEnd = getSelectionEnd();
        // Only update if position changed
        if (!selEnd || selEnd.col !== col || selEnd.row !== row) {
          setSelectionEnd({ col, row });
          // Update selectedCell to the current end position (for formula bar display)
          setSelectedCell({ col, row });
          render();
          updateFormulaBar();
        }
      }
      // Broadcast mouse position even during range selection
      presenceBroadcaster.broadcastMousePosition(x, y);
      return; // Don't process other mouse move handlers during range selection
    }

    if (uiStateMachine.isInState("COLUMN_RESIZING")) {
      const delta = e.clientX - getResizeStartX();
      const newWidth = Math.max(20, Math.min(1000, getResizeStartWidth() + delta));

      let colX = HEADER_WIDTH - scrollX;
      for (let i = 0; i < getResizeColIndex(); i++) {
        colX += colWidths.get(i) || DEFAULT_COL_WIDTH;
      }
      setResizePreviewX(colX + newWidth);

      render();
    } else if (uiStateMachine.isInState("ROW_RESIZING")) {
      const delta = e.clientY - getResizeStartY();
      const newHeight = Math.max(16, Math.min(500, getResizeStartHeight() + delta));

      let rowY = HEADER_HEIGHT - scrollY;
      for (let i = 0; i < getResizeRowIndex(); i++) {
        rowY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      }
      setResizePreviewY(rowY + newHeight);

      render();
    } else if (uiStateMachine.isInState("COLUMN_DRAGGING")) {
      setDragTargetIndex(getDropTargetCol(x, scrollX, colWidths, sheetInfo));
      setDragMouseX(x);
      setDragMouseY(y);

      canvas.style.cursor = "grabbing";
      render();
    } else if (uiStateMachine.isInState("ROW_DRAGGING")) {
      setDragTargetIndex(getDropTargetRow(y, scrollY, rowHeights, sheetInfo));
      setDragMouseX(x);
      setDragMouseY(y);

      canvas.style.cursor = "grabbing";
      render();
    } else {
      // Resize cursor
      if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
        const resizeCol = getResizeHandleCol(x, scrollX, colWidths, sheetInfo);
        canvas.style.cursor = resizeCol >= 0 ? "col-resize" : "default";
      } else if (x < HEADER_WIDTH && x > 0 && y > HEADER_HEIGHT) {
        const resizeRow = getResizeHandleRow(y, scrollY, rowHeights, sheetInfo);
        canvas.style.cursor = resizeRow >= 0 ? "row-resize" : "default";
      } else {
        canvas.style.cursor = "default";
      }
    }

    // Broadcast mouse position for collaboration (throttled)
    // Only broadcast when inside data area
    if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
      presenceBroadcaster.broadcastMousePosition(x, y);
    }
  }

  private async handleMouseUp(e: MouseEvent): Promise<void> {
    const {
      canvas, uiStateMachine, getDataSource,
      getResizeStartX, getResizeStartWidth, getResizeColIndex, setResizeColIndex,
      getResizeStartY, getResizeStartHeight, getResizeRowIndex, setResizeRowIndex,
      getDragSourceIndex, setDragSourceIndex, getDragTargetIndex, setDragTargetIndex,
      getPendingDragColumn, setPendingDragColumn, getPendingDragRow, setPendingDragRow,
      getColWidths, getRowHeights, getColumns, getRows,
      getSelectionStart, setSelectedCell, setSelectedColumn, setSelectedRow,
      render, updateFormulaBar
    } = this.config;

    const dataSource = getDataSource();
    const colWidths = getColWidths();
    const rowHeights = getRowHeights();

    // End formula drag selection (if active)
    if (this.formulaDragStart) {
      this.formulaDragStart = null;
      // Refocus the input element after drag
      const input = this.getActiveFormulaInput();
      if (input) {
        input.focus();
      }
    }

    // End range selection
    if (uiStateMachine.isInState("SELECTING")) {
      uiStateMachine.transition(UIEvent.STOP_SELECTING);
      // Keep selectionStart and selectionEnd as they are
      // selectedCell should be set to selectionStart (the anchor)
      const selStart = getSelectionStart();
      if (selStart) {
        setSelectedCell({ col: selStart.col, row: selStart.row });
      }
      render();
      updateFormulaBar();
    }

    // Clear pending drag states (click without drag)
    if (getPendingDragColumn()) {
      setPendingDragColumn(false);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      // Keep selectedColumn - column stays selected after click
    }
    if (getPendingDragRow()) {
      setPendingDragRow(false);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      // Keep selectedRow - row stays selected after click
    }

    if (uiStateMachine.isInState("COLUMN_RESIZING") && dataSource) {
      const delta = e.clientX - getResizeStartX();
      const newWidth = Math.max(20, Math.min(1000, getResizeStartWidth() + delta));
      const colIndexToResize = getResizeColIndex();

      // Optimistic update for smooth UX
      colWidths.set(colIndexToResize, newWidth);

      // Transition state BEFORE async call to avoid race conditions
      uiStateMachine.transition(UIEvent.END_COLUMN_RESIZE);
      setResizeColIndex(-1);
      canvas.style.cursor = "default";
      render();

      // Persist to WASM (listener handles refresh)
      try {
        await dataSource.resizeColumnByPos(colIndexToResize, newWidth);
      } catch (err) {
        console.error("Error resizing column:", err);
      }
    } else if (uiStateMachine.isInState("ROW_RESIZING") && dataSource) {
      const delta = e.clientY - getResizeStartY();
      const newHeight = Math.max(16, Math.min(500, getResizeStartHeight() + delta));
      const rowIndexToResize = getResizeRowIndex();

      // Optimistic update for smooth UX
      rowHeights.set(rowIndexToResize, newHeight);

      // Transition state BEFORE async call to avoid race conditions
      uiStateMachine.transition(UIEvent.END_ROW_RESIZE);
      setResizeRowIndex(-1);
      canvas.style.cursor = "default";
      render();

      // Persist to WASM (listener handles refresh)
      try {
        await dataSource.resizeRowByPos(rowIndexToResize, newHeight);
      } catch (err) {
        console.error("Error resizing row:", err);
      }
    } else if (uiStateMachine.isInState("COLUMN_DRAGGING") && dataSource) {
      const sourceIdx = getDragSourceIndex();
      const targetIdx = getDragTargetIndex();
      const colId = getColumnId(sourceIdx, getColumns());

      // Transition state BEFORE async call to avoid race conditions
      uiStateMachine.transition(UIEvent.END_COLUMN_DRAG);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      canvas.style.cursor = "default";

      // Update selected column to its new position (optimistic)
      if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
        if (targetIdx > sourceIdx) {
          setSelectedColumn(targetIdx - 1);
        } else {
          setSelectedColumn(targetIdx);
        }
      }
      render();

      // Persist to WASM (listener handles refresh)
      if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
        try {
          if (colId) {
            await dataSource.moveColumn(colId, targetIdx);
          } else {
            await dataSource.shiftColumnsForEmptyMove(sourceIdx, targetIdx);
          }
        } catch (err) {
          console.error("Error moving column:", err);
        }
      }
    } else if (uiStateMachine.isInState("ROW_DRAGGING") && dataSource) {
      const sourceIdx = getDragSourceIndex();
      const targetIdx = getDragTargetIndex();
      const rowId = getRowId(sourceIdx, getRows());

      // Transition state BEFORE async call to avoid race conditions
      uiStateMachine.transition(UIEvent.END_ROW_DRAG);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      canvas.style.cursor = "default";

      // Update selected row to its new position (optimistic)
      if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
        if (targetIdx > sourceIdx) {
          setSelectedRow(targetIdx - 1);
        } else {
          setSelectedRow(targetIdx);
        }
      }
      render();

      // Persist to WASM (listener handles refresh)
      if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
        try {
          if (rowId) {
            await dataSource.moveRow(rowId, targetIdx);
          } else {
            await dataSource.shiftRowsForEmptyMove(sourceIdx, targetIdx);
          }
        } catch (err) {
          console.error("Error moving row:", err);
        }
      }
    }
  }

  private handleMouseLeave(): void {
    const {
      canvas, uiStateMachine, presenceBroadcaster,
      setPendingDragColumn, setPendingDragRow,
      setResizeColIndex, setResizeRowIndex,
      setDragSourceIndex, setDragTargetIndex,
      getSelectionStart, setSelectedCell,
      render, updateFormulaBar
    } = this.config;

    // Clear pending drag states
    setPendingDragColumn(false);
    setPendingDragRow(false);

    // End range selection on mouse leave
    if (uiStateMachine.isInState("SELECTING")) {
      uiStateMachine.transition(UIEvent.STOP_SELECTING);
      const selStart = getSelectionStart();
      if (selStart) {
        setSelectedCell({ col: selStart.col, row: selStart.row });
      }
      render();
      updateFormulaBar();
    }

    if (uiStateMachine.isInState("COLUMN_RESIZING")) {
      uiStateMachine.transition(UIEvent.CANCEL_COLUMN_RESIZE);
      setResizeColIndex(-1);
      canvas.style.cursor = "default";
      render();
    } else if (uiStateMachine.isInState("ROW_RESIZING")) {
      uiStateMachine.transition(UIEvent.CANCEL_ROW_RESIZE);
      setResizeRowIndex(-1);
      canvas.style.cursor = "default";
      render();
    } else if (uiStateMachine.isInState("COLUMN_DRAGGING")) {
      uiStateMachine.transition(UIEvent.CANCEL_COLUMN_DRAG);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      canvas.style.cursor = "default";
      render();
    } else if (uiStateMachine.isInState("ROW_DRAGGING")) {
      uiStateMachine.transition(UIEvent.CANCEL_ROW_DRAG);
      setDragSourceIndex(-1);
      setDragTargetIndex(-1);
      canvas.style.cursor = "default";
      render();
    }

    // Clear mouse position for collaboration when leaving canvas
    presenceBroadcaster.clearMousePosition();
  }

  private handleDblClick(e: MouseEvent): void {
    const {
      canvas, getSheetInfo, getScrollX, getScrollY, getColWidths, getRowHeights,
      setSelectedCell, setSelectionStart, setSelectionEnd,
      render, updateFormulaBar, cellEditor, columnHeaderEditor
    } = this.config;

    const sheetInfo = getSheetInfo();
    if (!sheetInfo) return;

    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const scrollX = getScrollX();
    const scrollY = getScrollY();
    const colWidths = getColWidths();
    const rowHeights = getRowHeights();

    // Double-click on column header: start column renaming
    if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      if (col >= 0) {
        columnHeaderEditor.startEditingColumnHeader(col);
        e.preventDefault();
        return;
      }
    }

    // Double-click on cell: start cell editing
    if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
      const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
      const row = getRowAtY(y, scrollY, rowHeights, sheetInfo.rowCount);

      if (col >= 0 && row >= 0) {
        setSelectedCell({ col, row });
        // Clear range selection on double-click (start editing single cell)
        setSelectionStart({ col, row });
        setSelectionEnd({ col, row });
        render();
        updateFormulaBar();
        // Double-click enters append mode (cursor at end of content)
        cellEditor.startEditing({ mode: "append" });
      }
    }
  }

  // =========================================================================
  // Keyboard Events
  // =========================================================================

  private setupKeyboardEvents(): void {
    document.addEventListener("keydown", (e) => this.handleKeyDown(e));
    document.addEventListener("keyup", (e) => this.handleKeyUp(e));
  }

  private handleKeyDown(e: KeyboardEvent): void {
    const {
      uiStateMachine, cellEditor, getSheetInfo, getSelectedCell,
      getSelectionStart, getSelectionEnd, setSelectionStart, setSelectionEnd,
      setSelectedCell, getScrollX, getScrollY, setScrollX, setScrollY,
      getColWidths, getRowHeights, canvas,
      render, updateFormulaBar, fetchViewportNow, toggleAstDebugPanel
    } = this.config;

    // Update modifier state for the state machine
    uiStateMachine.updateModifiersFromEvent(e);

    // Ctrl+Shift+D toggles AST debug panel (works even during editing)
    if (e.ctrlKey && e.shiftKey && e.key === "D") {
      e.preventDefault();
      toggleAstDebugPanel();
      return;
    }

    if (
      cellEditor.isEditing() ||
      uiStateMachine.isInState("FORMULA_BAR_EDITING") ||
      uiStateMachine.isInState("COLUMN_HEADER_EDITING")
    ) {
      return;
    }

    const selectedCell = getSelectedCell();
    const sheetInfo = getSheetInfo();
    if (!selectedCell || !sheetInfo) return;

    // Determine current end position for range extension
    const selEnd = getSelectionEnd();
    const currentEnd = selEnd || selectedCell;
    let newCol = currentEnd.col;
    let newRow = currentEnd.row;
    let handled = false;
    let isExtendingSelection = e.shiftKey;

    const colWidths = getColWidths();
    const rowHeights = getRowHeights();

    switch (e.key) {
      case "ArrowUp":
        newRow = Math.max(0, currentEnd.row - 1);
        handled = true;
        break;
      case "ArrowDown":
        newRow = Math.min(sheetInfo.rowCount - 1, currentEnd.row + 1);
        handled = true;
        break;
      case "ArrowLeft":
        newCol = Math.max(0, currentEnd.col - 1);
        handled = true;
        break;
      case "ArrowRight":
        newCol = Math.min(sheetInfo.colCount - 1, currentEnd.col + 1);
        handled = true;
        break;
      case "Tab":
        e.preventDefault();
        isExtendingSelection = false; // Tab doesn't extend selection
        if (e.shiftKey) {
          newCol = Math.max(0, selectedCell.col - 1);
        } else {
          newCol = Math.min(sheetInfo.colCount - 1, selectedCell.col + 1);
        }
        newRow = selectedCell.row;
        handled = true;
        break;
      case "F2":
        // F2 enters edit mode
        e.preventDefault();
        cellEditor.startEditing({ mode: "select" });
        return;
      case "Enter":
        // Enter just moves down (Shift+Enter moves up) - no edit mode
        e.preventDefault();
        isExtendingSelection = false; // Enter/Shift+Enter doesn't extend selection
        if (e.shiftKey) {
          newRow = Math.max(0, selectedCell.row - 1);
        } else {
          newRow = Math.min(sheetInfo.rowCount - 1, selectedCell.row + 1);
        }
        newCol = selectedCell.col;
        handled = true;
        break;
      case "Delete":
      case "Backspace":
        e.preventDefault();
        if (hasRangeSelection(getSelectionStart(), getSelectionEnd())) {
          // Delete all cells in range
          cellEditor.deleteRangeCells();
        } else {
          // Single cell - clear and start editing empty
          cellEditor.startEditing({ mode: "replace", initialChar: "" });
        }
        return;
      case "Escape":
        // Escape clears range selection (collapses to single cell)
        if (hasRangeSelection(getSelectionStart(), getSelectionEnd())) {
          e.preventDefault();
          const selStart = getSelectionStart();
          if (selStart) {
            setSelectionEnd({ col: selStart.col, row: selStart.row });
            setSelectedCell({ col: selStart.col, row: selStart.row });
          }
          render();
          updateFormulaBar();
        }
        return;
      default:
        // Check for printable character - start replace mode
        // Single printable character (not control keys, not with Ctrl/Meta)
        if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
          e.preventDefault();
          cellEditor.startEditing({ mode: "replace", initialChar: e.key });
          return;
        }
        return;
    }

    if (handled) {
      e.preventDefault();
    }

    if (isExtendingSelection) {
      // Extend selection: keep anchor, update end
      let selStart = getSelectionStart();
      if (!selStart) {
        selStart = { col: selectedCell.col, row: selectedCell.row };
        setSelectionStart(selStart);
      }
      setSelectionEnd({ col: newCol, row: newRow });
      setSelectedCell({ col: newCol, row: newRow });
    } else {
      // Move selection: collapse to single cell
      setSelectedCell({ col: newCol, row: newRow });
      setSelectionStart({ col: newCol, row: newRow });
      setSelectionEnd({ col: newCol, row: newRow });
    }

    // Scroll to keep selection visible
    let selX = HEADER_WIDTH;
    for (let i = 0; i < newCol; i++) {
      selX += colWidths.get(i) || DEFAULT_COL_WIDTH;
    }
    let selY = HEADER_HEIGHT;
    for (let i = 0; i < newRow; i++) {
      selY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    }
    const selW = colWidths.get(newCol) || DEFAULT_COL_WIDTH;
    const selH = rowHeights.get(newRow) || DEFAULT_ROW_HEIGHT;

    const viewWidth = canvas.clientWidth;
    const viewHeight = canvas.clientHeight;
    let scrollX = getScrollX();
    let scrollY = getScrollY();

    if (selX - scrollX < HEADER_WIDTH) {
      scrollX = selX - HEADER_WIDTH;
    } else if (selX + selW - scrollX > viewWidth) {
      scrollX = selX + selW - viewWidth;
    }

    if (selY - scrollY < HEADER_HEIGHT) {
      scrollY = selY - HEADER_HEIGHT;
    } else if (selY + selH - scrollY > viewHeight) {
      scrollY = selY + selH - viewHeight;
    }

    setScrollX(scrollX);
    setScrollY(scrollY);

    render();
    updateFormulaBar();
    fetchViewportNow();
  }

  private handleKeyUp(e: KeyboardEvent): void {
    this.config.uiStateMachine.updateModifiersFromEvent(e);
  }

  // =========================================================================
  // Window Events
  // =========================================================================

  private setupWindowEvents(): void {
    window.addEventListener("resize", () => {
      this.config.resizeCanvas();
      this.config.fetchViewportNow();
    });
  }
}
