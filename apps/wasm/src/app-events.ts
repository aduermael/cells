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
import type { ClipboardManager } from "./clipboard";
import type { ScriptPanel } from "./script-panel";
import { colToLetter } from "./grid-utils";
import {
    showContextMenu,
    type ContextMenuEntry,
    type ContextType,
} from "./context-menu";

// =============================================================================
// Types
// =============================================================================

/** Configuration for AppEventManager */
/** Fill handle bounds for hit testing */
export interface FillHandleBounds {
    x: number;
    y: number;
    width: number;
    height: number;
}

export interface AppEventManagerConfig {
    canvas: HTMLCanvasElement;
    uiStateMachine: UIStateMachine;
    cellEditor: CellEditor;
    columnHeaderEditor: ColumnHeaderEditor;
    formulaBarEditor: FormulaBarEditor;
    presenceBroadcaster: PresenceBroadcaster;
    clipboardManager: ClipboardManager;
    scriptPanel: ScriptPanel;
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
    getFillHandleBounds: () => FillHandleBounds | null;

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

    // Virtual scrolling
    getDiscoveredRows: () => number;
    setDiscoveredRows: (v: number) => void;

    // Fill drag state accessors/mutators
    getIsFillDragging: () => boolean;
    setIsFillDragging: (v: boolean) => void;
    getFillPreviewRange: () => { minCol: number; maxCol: number; minRow: number; maxRow: number } | null;
    setFillPreviewRange: (v: { minCol: number; maxCol: number; minRow: number; maxRow: number } | null) => void;

    // Formula highlight hover
    getFormulaHighlights: () => import("./grid-constants").FormulaHighlight[];
    getHoveredGridRefIndex: () => number;
    setHoveredGridRefIndex: (v: number) => void;
    getColPixelOffsets: () => Map<number, number>;
    getRowPixelOffsets: () => Map<number, number>;
    updateFormulaBarHoverStyle: () => void;

    // Callbacks
    render: () => void;
    updateFormulaBar: () => void;
    clearFormulaHighlights: () => void;
    resizeCanvas: () => void;
    fetchViewportNow: () => void;
    toggleAstDebugPanel: () => void;
    commitFormulaBarEdit: () => Promise<void>;
    updateScrollbars: () => void;
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

    /** Original selection bounds when fill drag started (to preserve dimensions on non-dragged axis) */
    private fillDragOriginalBounds: {
        minCol: number;
        maxCol: number;
        minRow: number;
        maxRow: number;
    } | null = null;

    /** Starting mouse position for fill drag (to determine axis dynamically) */
    private fillDragStartX = 0;
    private fillDragStartY = 0;

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
        const inFormulaMode =
            cellEditor.isFormulaMode() || formulaBarEditor.isFormulaMode();

        // Clear the formula ref state if not in formula mode
        if (!inFormulaMode) {
            this.lastFormulaRef = null;
            this.formulaDragStart = null;
        }

        return inFormulaMode;
    }

    /**
     * Get the active formula display element (contenteditable for focus)
     */
    private getActiveFormulaDisplay(): HTMLElement | null {
        const { cellEditor, formulaBarEditor } = this.config;
        if (cellEditor.isFormulaMode()) {
            return cellEditor.getDisplayElement();
        } else if (formulaBarEditor.isFormulaMode()) {
            return formulaBarEditor.getDisplayElement();
        }
        return null;
    }

    /**
     * Insert a reference into the active formula editor and track cursor positions
     */
    private insertFormulaReference(ref: string, position: Position): void {
        const { cellEditor, formulaBarEditor, render } = this.config;

        // Get cursor position from the editor's tracking BEFORE insertion
        let cursorStart = 0;

        if (cellEditor.isFormulaMode()) {
            cursorStart = cellEditor.getLastKnownCursorPos().start;
            cellEditor.insertReferenceAtCursor(ref);
            cellEditor.getDisplayElement().focus();
        } else if (formulaBarEditor.isFormulaMode()) {
            cursorStart = formulaBarEditor.getLastKnownCursorPos().start;
            formulaBarEditor.insertReferenceAtCursor(ref);
            formulaBarEditor.getDisplayElement().focus();
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
            cellEditor.getDisplayElement().focus();
        } else if (formulaBarEditor.isFormulaMode()) {
            formulaBarEditor.insertReferenceAtCursor(ref);
            formulaBarEditor.getDisplayElement().focus();
        }

        render();
    }

    /**
     * Replace the last inserted reference with a range
     */
    private replaceLastRefWithRange(endCol: number, endRow: number): void {
        const { cellEditor, formulaBarEditor, render } = this.config;

        if (!this.lastFormulaRef) return;

        const startCol = colToLetter(this.lastFormulaRef.position.col);
        const startRow = this.lastFormulaRef.position.row + 1;
        const endColLetter = colToLetter(endCol);
        const endRowNum = endRow + 1;
        const rangeRef = `${startCol}${startRow}:${endColLetter}${endRowNum}`;

        // Use the editor's replaceReferenceAtPosition method which handles all updates
        if (cellEditor.isFormulaMode()) {
            cellEditor.replaceReferenceAtPosition(
                this.lastFormulaRef.cursorStart,
                this.lastFormulaRef.cursorEnd,
                rangeRef,
            );
        } else if (formulaBarEditor.isFormulaMode()) {
            formulaBarEditor.replaceReferenceAtPosition(
                this.lastFormulaRef.cursorStart,
                this.lastFormulaRef.cursorEnd,
                rangeRef,
            );
        }

        // Update tracked reference to the new range end position
        this.lastFormulaRef.cursorEnd =
            this.lastFormulaRef.cursorStart + rangeRef.length;

        render();
    }

    // =========================================================================
    // Fill Handle Detection & Helpers
    // =========================================================================

    /**
     * Get column at X coordinate using midpoint snapping.
     * Returns the rightmost column whose midpoint the cursor has passed.
     * Used for fill handle: column is added when cursor crosses its center.
     */
    private getColAtXMidpoint(
        x: number,
        scrollX: number,
        colWidths: Map<number, number>,
        colCount: number,
    ): number {
        if (x < HEADER_WIDTH) return 0;
        let accX = HEADER_WIDTH - scrollX;
        let committedCol = 0;
        for (let col = 0; col < colCount; col++) {
            const colW = colWidths.get(col) || DEFAULT_COL_WIDTH;
            const midpoint = accX + colW / 2;
            if (x >= midpoint) {
                committedCol = col;
            }
            accX += colW;
        }
        return committedCol;
    }

    /**
     * Get row at Y coordinate using midpoint snapping.
     * Returns the bottommost row whose midpoint the cursor has passed.
     * Used for fill handle: row is added when cursor crosses its center.
     */
    private getRowAtYMidpoint(
        y: number,
        scrollY: number,
        rowHeights: Map<number, number>,
        rowCount: number,
    ): number {
        if (y < HEADER_HEIGHT) return 0;
        let accY = HEADER_HEIGHT - scrollY;
        let committedRow = 0;
        for (let row = 0; row < rowCount; row++) {
            const rowH = rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
            const midpoint = accY + rowH / 2;
            if (y >= midpoint) {
                committedRow = row;
            }
            accY += rowH;
        }
        return committedRow;
    }

    /**
     * Check if a point is over the fill handle
     */
    private isPointInFillHandle(x: number, y: number): boolean {
        const bounds = this.config.getFillHandleBounds();
        if (!bounds) return false;
        const { x: hx, y: hy, width, height } = bounds;
        // Add padding for easier targeting
        const padding = 3;
        return (
            x >= hx - padding &&
            x <= hx + width + padding &&
            y >= hy - padding &&
            y <= hy + height + padding
        );
    }

    // =========================================================================
    // Canvas Events
    // =========================================================================

    private setupCanvasEvents(): void {
        const { canvas } = this.config;

        // Wheel/scroll
        canvas.addEventListener("wheel", (e) => this.handleWheel(e), {
            passive: false,
        });

        // Mouse interactions
        canvas.addEventListener("mousedown", (e) => this.handleMouseDown(e));
        canvas.addEventListener("mousemove", (e) => this.handleMouseMove(e));
        canvas.addEventListener("mouseup", (e) => this.handleMouseUp(e));
        canvas.addEventListener("mouseleave", () => this.handleMouseLeave());
        canvas.addEventListener("dblclick", (e) => this.handleDblClick(e));

        // Context menu (right-click)
        canvas.addEventListener("contextmenu", (e) =>
            this.handleContextMenu(e),
        );
    }

    private handleWheel(e: WheelEvent): void {
        const {
            getSheetInfo,
            getScrollX,
            getScrollY,
            setScrollX,
            setScrollY,
            getDiscoveredRows,
            setDiscoveredRows,
            render,
            fetchViewportNow,
            updateScrollbars,
            canvas,
        } = this.config;

        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;
        e.preventDefault();

        const maxScrollX = Math.max(
            0,
            sheetInfo.colCount * DEFAULT_COL_WIDTH -
                canvas.clientWidth +
                HEADER_WIDTH,
        );

        // Calculate max scroll based on actual sheet size (not just discovered rows)
        // This allows scrolling through the entire file immediately
        const viewportHeight = canvas.clientHeight - HEADER_HEIGHT;
        const newScrollY = getScrollY() + e.deltaY;

        // Use the larger of sheetInfo.rowCount or discoveredRows for scroll limits
        // This ensures we can scroll through all data rows
        const effectiveRowCount = Math.max(
            sheetInfo.rowCount,
            getDiscoveredRows(),
        );

        // Update discoveredRows to match where the user is scrolling
        const visibleBottomRow = Math.ceil(
            (newScrollY + viewportHeight) / DEFAULT_ROW_HEIGHT,
        );
        if (visibleBottomRow > getDiscoveredRows()) {
            // Expand discoveredRows to at least cover visible area plus buffer
            const newDiscovered = Math.min(
                1_000_000,
                Math.max(visibleBottomRow + 100, sheetInfo.rowCount),
            );
            setDiscoveredRows(newDiscovered);
        }

        const maxScrollY = Math.max(
            0,
            effectiveRowCount * DEFAULT_ROW_HEIGHT - viewportHeight,
        );

        setScrollX(Math.max(0, Math.min(maxScrollX, getScrollX() + e.deltaX)));
        setScrollY(Math.max(0, Math.min(maxScrollY, newScrollY)));

        render();
        fetchViewportNow();
        updateScrollbars();
    }

    private handleMouseDown(e: MouseEvent): void {
        const {
            canvas,
            uiStateMachine,
            getSheetInfo,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
            setSelectedCell,
            setSelectedColumn,
            setSelectedRow,
            setSelectionStart,
            setSelectionEnd,
            getSelectionStart,
            setResizeColIndex,
            setResizeStartX,
            setResizeStartWidth,
            setResizePreviewX,
            setResizeRowIndex,
            setResizeStartY,
            setResizeStartHeight,
            setResizePreviewY,
            setPendingDragColumn,
            setPendingDragRow,
            setPendingDragStartX,
            setPendingDragStartY,
            setDragSourceIndex,
            setDragTargetIndex,
            render,
            updateFormulaBar,
            clearFormulaHighlights,
            cellEditor,
            commitFormulaBarEdit,
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
            const resizeCol = getResizeHandleCol(
                x,
                scrollX,
                colWidths,
                sheetInfo,
            );
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
                setResizeStartWidth(
                    colWidths.get(resizeCol) || DEFAULT_COL_WIDTH,
                );

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
            const resizeRow = getResizeHandleRow(
                y,
                scrollY,
                rowHeights,
                sheetInfo,
            );
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
                setResizeStartHeight(
                    rowHeights.get(resizeRow) || DEFAULT_ROW_HEIGHT,
                );

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
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );
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

        // Fill handle click - start fill drag
        if (this.isPointInFillHandle(x, y)) {
            const selStart = getSelectionStart();
            const { getSelectionEnd, setIsFillDragging } = this.config;
            const selEnd = getSelectionEnd();
            if (selStart && selEnd) {
                setIsFillDragging(true);
                // Store original selection bounds to preserve dimensions on non-dragged axis
                this.fillDragOriginalBounds = {
                    minCol: Math.min(selStart.col, selEnd.col),
                    maxCol: Math.max(selStart.col, selEnd.col),
                    minRow: Math.min(selStart.row, selEnd.row),
                    maxRow: Math.max(selStart.row, selEnd.row),
                };
                this.fillDragStartX = x;
                this.fillDragStartY = y;
                canvas.style.cursor = "crosshair";
            }
            e.preventDefault();
            return;
        }

        // Cell selection or insert reference
        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );

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
            canvas,
            uiStateMachine,
            cellEditor,
            getSheetInfo,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
            getPendingDragColumn,
            setPendingDragColumn,
            getPendingDragRow,
            setPendingDragRow,
            getPendingDragStartX,
            getPendingDragStartY,
            setDragMouseX,
            setDragMouseY,
            setDragTargetIndex,
            getResizeColIndex,
            getResizeStartX,
            getResizeStartWidth,
            setResizePreviewX,
            getResizeRowIndex,
            getResizeStartY,
            getResizeStartHeight,
            setResizePreviewY,
            getSelectionEnd,
            setSelectionEnd,
            setSelectedCell,
            render,
            updateFormulaBar,
            presenceBroadcaster,
            commitFormulaBarEdit,
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

        // Handle fill handle drag (show preview on single axis, dynamic axis based on delta)
        const { getIsFillDragging, getFillPreviewRange, setFillPreviewRange } = this.config;
        if (getIsFillDragging() && this.fillDragOriginalBounds) {
            const discoveredRows = this.config.getDiscoveredRows();
            const maxRows = Math.max(sheetInfo.rowCount, discoveredRows);
            const bounds = this.fillDragOriginalBounds;

            // Calculate delta from drag start to determine axis dynamically
            const dx = Math.abs(x - this.fillDragStartX);
            const dy = Math.abs(y - this.fillDragStartY);
            const threshold = 3; // minimum pixels before any axis is active

            if (dx > threshold || dy > threshold) {
                // Determine axis based on which delta is currently larger
                const axis = dx > dy ? "x" : "y";

                let newMinCol: number,
                    newMaxCol: number,
                    newMinRow: number,
                    newMaxRow: number;

                if (axis === "x") {
                    // Horizontal drag - preserve original row bounds, extend/shrink columns
                    newMinRow = bounds.minRow;
                    newMaxRow = bounds.maxRow;

                    // Find target column using midpoint snapping
                    const targetCol = this.getColAtXMidpoint(
                        x,
                        scrollX,
                        colWidths,
                        sheetInfo.colCount,
                    );

                    // Determine direction based on target position relative to original bounds
                    if (targetCol < bounds.minCol) {
                        // Extending/shrinking left from minCol
                        newMinCol = targetCol;
                        newMaxCol = bounds.maxCol;
                    } else {
                        // Extending/shrinking right from maxCol (can shrink within original)
                        newMinCol = bounds.minCol;
                        newMaxCol = Math.max(bounds.minCol, targetCol); // At least keep minCol
                    }
                } else {
                    // Vertical drag - preserve original column bounds, extend/shrink rows
                    newMinCol = bounds.minCol;
                    newMaxCol = bounds.maxCol;

                    // Find target row using midpoint snapping
                    const targetRow = this.getRowAtYMidpoint(
                        y,
                        scrollY,
                        rowHeights,
                        maxRows,
                    );

                    // Determine direction based on target position relative to original bounds
                    if (targetRow < bounds.minRow) {
                        // Extending/shrinking up from minRow
                        newMinRow = targetRow;
                        newMaxRow = bounds.maxRow;
                    } else {
                        // Extending/shrinking down from maxRow (can shrink within original)
                        newMinRow = bounds.minRow;
                        newMaxRow = Math.max(bounds.minRow, targetRow); // At least keep minRow
                    }
                }

                // Only update preview if actually changed
                const currentPreview = getFillPreviewRange();
                if (
                    !currentPreview ||
                    newMinCol !== currentPreview.minCol ||
                    newMaxCol !== currentPreview.maxCol ||
                    newMinRow !== currentPreview.minRow ||
                    newMaxRow !== currentPreview.maxRow
                ) {
                    setFillPreviewRange({
                        minCol: newMinCol,
                        maxCol: newMaxCol,
                        minRow: newMinRow,
                        maxRow: newMaxRow,
                    });
                    render();
                }
            }
            return;
        }

        // Handle drag selection during formula editing (click+drag to select range)
        if (
            this.formulaDragStart &&
            this.isInFormulaEditingMode() &&
            x > HEADER_WIDTH &&
            y > HEADER_HEIGHT
        ) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );
            if (col >= 0 && row >= 0) {
                // Only update if moved to a different cell
                if (
                    col !== this.formulaDragStart.col ||
                    row !== this.formulaDragStart.row
                ) {
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
        if (
            uiStateMachine.isInState("SELECTING") &&
            x > HEADER_WIDTH &&
            y > HEADER_HEIGHT
        ) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );
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
            const newWidth = Math.max(
                20,
                Math.min(1000, getResizeStartWidth() + delta),
            );

            let colX = HEADER_WIDTH - scrollX;
            for (let i = 0; i < getResizeColIndex(); i++) {
                colX += colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            setResizePreviewX(colX + newWidth);

            render();
        } else if (uiStateMachine.isInState("ROW_RESIZING")) {
            const delta = e.clientY - getResizeStartY();
            const newHeight = Math.max(
                16,
                Math.min(500, getResizeStartHeight() + delta),
            );

            let rowY = HEADER_HEIGHT - scrollY;
            for (let i = 0; i < getResizeRowIndex(); i++) {
                rowY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            setResizePreviewY(rowY + newHeight);

            render();
        } else if (uiStateMachine.isInState("COLUMN_DRAGGING")) {
            setDragTargetIndex(
                getDropTargetCol(x, scrollX, colWidths, sheetInfo),
            );
            setDragMouseX(x);
            setDragMouseY(y);

            canvas.style.cursor = "grabbing";
            render();
        } else if (uiStateMachine.isInState("ROW_DRAGGING")) {
            setDragTargetIndex(
                getDropTargetRow(y, scrollY, rowHeights, sheetInfo),
            );
            setDragMouseX(x);
            setDragMouseY(y);

            canvas.style.cursor = "grabbing";
            render();
        } else {
            // Determine cursor based on position
            if (this.isPointInFillHandle(x, y)) {
                // Fill handle cursor (crosshair for drag-to-fill)
                canvas.style.cursor = "crosshair";
            } else if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
                // Resize cursor for column header
                const resizeCol = getResizeHandleCol(
                    x,
                    scrollX,
                    colWidths,
                    sheetInfo,
                );
                canvas.style.cursor = resizeCol >= 0 ? "col-resize" : "default";
            } else if (x < HEADER_WIDTH && x > 0 && y > HEADER_HEIGHT) {
                // Resize cursor for row header
                const resizeRow = getResizeHandleRow(
                    y,
                    scrollY,
                    rowHeights,
                    sheetInfo,
                );
                canvas.style.cursor = resizeRow >= 0 ? "row-resize" : "default";
            } else {
                canvas.style.cursor = "default";
            }
        }

        // Check for formula highlight hover
        this.checkFormulaHighlightHover(x, y);

        // Broadcast mouse position for collaboration (throttled)
        // Only broadcast when inside data area
        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            presenceBroadcaster.broadcastMousePosition(x, y);
        }
    }

    private async handleMouseUp(e: MouseEvent): Promise<void> {
        const {
            canvas,
            uiStateMachine,
            getDataSource,
            getResizeStartX,
            getResizeStartWidth,
            getResizeColIndex,
            setResizeColIndex,
            getResizeStartY,
            getResizeStartHeight,
            getResizeRowIndex,
            setResizeRowIndex,
            getDragSourceIndex,
            setDragSourceIndex,
            getDragTargetIndex,
            setDragTargetIndex,
            getPendingDragColumn,
            setPendingDragColumn,
            getPendingDragRow,
            setPendingDragRow,
            getColWidths,
            getRowHeights,
            getColumns,
            getRows,
            getSelectionStart,
            setSelectedCell,
            setSelectedColumn,
            setSelectedRow,
            render,
            updateFormulaBar,
        } = this.config;

        const dataSource = getDataSource();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        // End fill handle drag (if active)
        const { getIsFillDragging, setIsFillDragging, getFillPreviewRange, setFillPreviewRange, setSelectionStart, setSelectionEnd } = this.config;
        if (getIsFillDragging() && dataSource) {
            // Get the original selection bounds (source) and the preview range (target)
            const original = this.fillDragOriginalBounds;
            const preview = getFillPreviewRange();
            if (original && preview) {
                // Capture preview values before clearing state
                const targetMinCol = preview.minCol;
                const targetMinRow = preview.minRow;
                const targetMaxCol = preview.maxCol;
                const targetMaxRow = preview.maxRow;

                // Call fillRange to extrapolate values from source to target
                dataSource.fillRange(
                    original.minCol, original.minRow,
                    original.maxCol, original.maxRow,
                    targetMinCol, targetMinRow,
                    targetMaxCol, targetMaxRow
                ).then(() => {
                    // Update selection to the filled range
                    setSelectionStart({ col: targetMinCol, row: targetMinRow });
                    setSelectionEnd({ col: targetMaxCol, row: targetMaxRow });
                    render();
                    updateFormulaBar();
                }).catch((err) => {
                    console.error("Error filling range:", err);
                });
            }
            // Clear fill drag state
            setIsFillDragging(false);
            setFillPreviewRange(null);
            this.fillDragOriginalBounds = null;
            canvas.style.cursor = "default";
            render();
            updateFormulaBar();
        }

        // End formula drag selection (if active)
        if (this.formulaDragStart) {
            this.formulaDragStart = null;
            // Refocus the display element (contenteditable) after drag/click
            const display = this.getActiveFormulaDisplay();
            if (display) {
                display.focus();
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
            const newWidth = Math.max(
                20,
                Math.min(1000, getResizeStartWidth() + delta),
            );
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
            const newHeight = Math.max(
                16,
                Math.min(500, getResizeStartHeight() + delta),
            );
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
                        await dataSource.shiftColumnsForEmptyMove(
                            sourceIdx,
                            targetIdx,
                        );
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
                        await dataSource.shiftRowsForEmptyMove(
                            sourceIdx,
                            targetIdx,
                        );
                    }
                } catch (err) {
                    console.error("Error moving row:", err);
                }
            }
        }
    }

    private handleMouseLeave(): void {
        const {
            canvas,
            uiStateMachine,
            presenceBroadcaster,
            setPendingDragColumn,
            setPendingDragRow,
            setResizeColIndex,
            setResizeRowIndex,
            setDragSourceIndex,
            setDragTargetIndex,
            getSelectionStart,
            setSelectedCell,
            render,
            updateFormulaBar,
            getIsFillDragging,
            setIsFillDragging,
            setFillPreviewRange,
        } = this.config;

        // Clear fill drag state
        if (getIsFillDragging()) {
            setIsFillDragging(false);
            setFillPreviewRange(null);
            this.fillDragOriginalBounds = null;
            canvas.style.cursor = "default";
        }

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
            canvas,
            getSheetInfo,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
            setSelectedCell,
            setSelectionStart,
            setSelectionEnd,
            render,
            updateFormulaBar,
            cellEditor,
            columnHeaderEditor,
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
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );

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
    // Context Menu
    // =========================================================================

    private handleContextMenu(e: MouseEvent): void {
        e.preventDefault();

        const {
            canvas,
            getSheetInfo,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
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

        // Determine what was right-clicked
        const context = this.getContextAtPosition(
            x,
            y,
            scrollX,
            scrollY,
            colWidths,
            rowHeights,
            sheetInfo,
        );

        // Build menu items based on context
        const items = this.buildContextMenuItems(context);

        if (items.length > 0) {
            showContextMenu(e.clientX, e.clientY, items);
        }
    }

    /**
     * Determine what element is at the given position
     */
    private getContextAtPosition(
        x: number,
        y: number,
        scrollX: number,
        scrollY: number,
        colWidths: Map<number, number>,
        rowHeights: Map<number, number>,
        sheetInfo: SheetInfo,
    ): ContextType {
        const { getColumns, getRows, getDiscoveredRows } = this.config;

        // Corner (top-left area)
        if (x <= HEADER_WIDTH && y <= HEADER_HEIGHT) {
            return { type: "corner" };
        }

        // Column header
        if (y <= HEADER_HEIGHT && x > HEADER_WIDTH) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            if (col >= 0) {
                const columns = getColumns();
                const colId = getColumnId(col, columns);
                return { type: "column-header", col, colId };
            }
            return { type: "empty" };
        }

        // Row header
        if (x <= HEADER_WIDTH && y > HEADER_HEIGHT) {
            const discoveredRows = getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );
            if (row >= 0) {
                const rows = getRows();
                const rowId = getRowId(row, rows);
                return { type: "row-header", row, rowId };
            }
            return { type: "empty" };
        }

        // Cell area
        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows),
            );
            if (col >= 0 && row >= 0) {
                const columns = getColumns();
                const rows = getRows();
                const colId = getColumnId(col, columns);
                const rowId = getRowId(row, rows);
                return { type: "cell", col, row, colId, rowId };
            }
        }

        return { type: "empty" };
    }

    /**
     * Build context menu items based on what was right-clicked
     */
    private buildContextMenuItems(context: ContextType): ContextMenuEntry[] {
        const items: ContextMenuEntry[] = [];
        const { getDataSource, fetchViewportNow, render } = this.config;

        switch (context.type) {
            case "column-header":
                // Column header context menu
                items.push({
                    label: "Insert column left",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds) return;
                        await ds.insertColumnAt(context.col, true);
                        fetchViewportNow();
                        render();
                    },
                });
                items.push({
                    label: "Insert column right",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds) return;
                        await ds.insertColumnAt(context.col, false);
                        fetchViewportNow();
                        render();
                    },
                });
                items.push({ type: "separator" });
                items.push({
                    label: "Delete column",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds || !context.colId) return;
                        await ds.deleteColumnById(context.colId);
                        fetchViewportNow();
                        render();
                    },
                    danger: true,
                });
                break;

            case "row-header":
                // Row header context menu
                items.push({
                    label: "Insert row above",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds) return;
                        await ds.insertRowAt(context.row, true);
                        fetchViewportNow();
                        render();
                    },
                });
                items.push({
                    label: "Insert row below",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds) return;
                        await ds.insertRowAt(context.row, false);
                        fetchViewportNow();
                        render();
                    },
                });
                items.push({ type: "separator" });
                items.push({
                    label: "Delete row",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds || !context.rowId) return;
                        await ds.deleteRowById(context.rowId);
                        fetchViewportNow();
                        render();
                    },
                    danger: true,
                });
                break;

            case "cell": {
                // Cell context menu with clipboard operations
                const {
                    clipboardManager,
                    setSelectedCell,
                    setSelectionStart,
                    setSelectionEnd,
                } = this.config;

                // Select the cell that was right-clicked
                const selectCell = () => {
                    setSelectedCell({ col: context.col, row: context.row });
                    setSelectionStart({ col: context.col, row: context.row });
                    setSelectionEnd({ col: context.col, row: context.row });
                    render();
                };

                items.push({
                    label: "Cut",
                    shortcut: "⌘X",
                    action: () => {
                        selectCell();
                        clipboardManager.cut();
                    },
                });
                items.push({
                    label: "Copy",
                    shortcut: "⌘C",
                    action: () => {
                        selectCell();
                        clipboardManager.copy();
                    },
                });
                items.push({
                    label: "Paste",
                    shortcut: "⌘V",
                    action: () => {
                        selectCell();
                        clipboardManager.paste();
                    },
                });
                break;
            }

            case "corner":
                // Select all - placeholder
                items.push({
                    label: "Select all",
                    action: () => {
                        console.log("Select all");
                    },
                    disabled: true,
                });
                break;

            case "empty":
                // No menu for empty areas
                break;
        }

        return items;
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
            uiStateMachine,
            cellEditor,
            getSheetInfo,
            getSelectedCell,
            getSelectionStart,
            getSelectionEnd,
            setSelectionStart,
            setSelectionEnd,
            setSelectedCell,
            getScrollX,
            getScrollY,
            setScrollX,
            setScrollY,
            getColWidths,
            getRowHeights,
            canvas,
            render,
            updateFormulaBar,
            fetchViewportNow,
            toggleAstDebugPanel,
        } = this.config;

        // Update modifier state for the state machine
        uiStateMachine.updateModifiersFromEvent(e);

        // Ctrl+Shift+D toggles AST debug panel (works even during editing)
        if (e.ctrlKey && e.shiftKey && e.key === "D") {
            e.preventDefault();
            toggleAstDebugPanel();
            return;
        }

        // Clipboard shortcuts (Cmd/Ctrl+C/V/X) - only when NOT editing
        // These must be checked before the editing state check below
        const isMod = e.metaKey || e.ctrlKey;
        const { scriptPanel } = this.config;

        // Check if there's text selected outside the canvas (e.g., in chat panel)
        // If so, let the browser handle copy/cut natively
        const selection = window.getSelection();
        const hasTextSelection = selection && selection.toString().length > 0;
        const selectionInCanvas = selection?.anchorNode?.parentElement?.closest("canvas");

        if (
            isMod &&
            !cellEditor.isEditing() &&
            !uiStateMachine.isInState("FORMULA_BAR_EDITING") &&
            !uiStateMachine.isInState("COLUMN_HEADER_EDITING") &&
            !scriptPanel.isEditorFocused() &&
            !(hasTextSelection && !selectionInCanvas) // Let browser handle text selection copies
        ) {
            const { clipboardManager } = this.config;
            switch (e.key.toLowerCase()) {
                case "c":
                    e.preventDefault();
                    clipboardManager.copy();
                    return;
                case "x":
                    e.preventDefault();
                    clipboardManager.cut();
                    return;
                case "v":
                    e.preventDefault();
                    clipboardManager.paste();
                    return;
            }
        }

        if (
            cellEditor.isEditing() ||
            uiStateMachine.isInState("FORMULA_BAR_EDITING") ||
            uiStateMachine.isInState("COLUMN_HEADER_EDITING")
        ) {
            return;
        }

        // Ignore keyboard events when focus is on other editable elements
        // (like the workbook title or other inputs outside the grid)
        const activeEl = document.activeElement;
        if (activeEl && activeEl !== canvas) {
            const tagName = activeEl.tagName.toLowerCase();
            const isContentEditable = (activeEl as HTMLElement)
                .isContentEditable;
            if (
                tagName === "input" ||
                tagName === "textarea" ||
                isContentEditable
            ) {
                return;
            }
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
            case "ArrowDown": {
                const discoveredRows = this.config.getDiscoveredRows();
                newRow = Math.min(
                    Math.max(sheetInfo.rowCount, discoveredRows) - 1,
                    currentEnd.row + 1,
                );
                handled = true;
                break;
            }
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
                    newCol = Math.min(
                        sheetInfo.colCount - 1,
                        selectedCell.col + 1,
                    );
                }
                newRow = selectedCell.row;
                handled = true;
                break;
            case "F2":
                // F2 enters edit mode
                e.preventDefault();
                cellEditor.startEditing({ mode: "select" });
                return;
            case "Enter": {
                // Enter just moves down (Shift+Enter moves up) - no edit mode
                e.preventDefault();
                isExtendingSelection = false; // Enter/Shift+Enter doesn't extend selection
                const discoveredRows = this.config.getDiscoveredRows();
                const maxRow = Math.max(sheetInfo.rowCount, discoveredRows) - 1;
                if (e.shiftKey) {
                    newRow = Math.max(0, selectedCell.row - 1);
                } else {
                    newRow = Math.min(maxRow, selectedCell.row + 1);
                }
                newCol = selectedCell.col;
                handled = true;
                break;
            }
            case "Delete":
            case "Backspace":
                e.preventDefault();
                if (hasRangeSelection(getSelectionStart(), getSelectionEnd())) {
                    // Delete all cells in range
                    cellEditor.deleteRangeCells();
                } else {
                    // Single cell - clear and start editing empty
                    cellEditor.startEditing({
                        mode: "replace",
                        initialChar: "",
                    });
                }
                return;
            case "Escape":
                // Escape clears range selection (collapses to single cell)
                if (hasRangeSelection(getSelectionStart(), getSelectionEnd())) {
                    e.preventDefault();
                    const selStart = getSelectionStart();
                    if (selStart) {
                        setSelectionEnd({
                            col: selStart.col,
                            row: selStart.row,
                        });
                        setSelectedCell({
                            col: selStart.col,
                            row: selStart.row,
                        });
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
                    // For range selections, collapse to anchor cell before editing
                    // (like Excel - typing replaces anchor cell content)
                    const selStart = getSelectionStart();
                    if (selStart && hasRangeSelection(selStart, getSelectionEnd())) {
                        setSelectedCell(selStart);
                        setSelectionStart(selStart);
                        setSelectionEnd(selStart);
                        render();
                    }
                    cellEditor.startEditing({
                        mode: "replace",
                        initialChar: e.key,
                    });
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
            // Keep selectedCell at anchor (not end) so typing edits the anchor cell
            setSelectedCell({ col: selStart.col, row: selStart.row });
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
    // Formula Highlight Hover
    // =========================================================================

    /**
     * Check if mouse is over a formula highlight and update hover state.
     * Uses pixel bounds of each highlight to determine hit.
     */
    private checkFormulaHighlightHover(mouseX: number, mouseY: number): void {
        const {
            getFormulaHighlights,
            getHoveredGridRefIndex,
            setHoveredGridRefIndex,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
            getColPixelOffsets,
            getRowPixelOffsets,
            render,
        } = this.config;

        const highlights = getFormulaHighlights();
        if (highlights.length === 0) {
            // No highlights - clear hover state if set
            if (getHoveredGridRefIndex() !== -1) {
                setHoveredGridRefIndex(-1);
                render();
            }
            return;
        }

        const scrollX = getScrollX();
        const scrollY = getScrollY();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();
        const colPixelOffsets = getColPixelOffsets();
        const rowPixelOffsets = getRowPixelOffsets();

        // Helper to get pixel X for a column
        const getColX = (col: number): number => {
            const offset = colPixelOffsets.get(col);
            if (offset !== undefined) return offset - scrollX + HEADER_WIDTH;
            let x = HEADER_WIDTH - scrollX;
            for (let i = 0; i < col; i++) {
                x += colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            return x;
        };

        // Helper to get pixel Y for a row
        const getRowY = (row: number): number => {
            const offset = rowPixelOffsets.get(row);
            if (offset !== undefined) return offset - scrollY + HEADER_HEIGHT;
            let y = HEADER_HEIGHT - scrollY;
            for (let i = 0; i < row; i++) {
                y += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
            }
            return y;
        };

        let hoveredIdx = -1;

        for (let idx = 0; idx < highlights.length; idx++) {
            const h = highlights[idx];
            if (!h) continue;

            let minX: number, minY: number, maxX: number, maxY: number;

            if (h.type === "cell" && h.col !== undefined && h.row !== undefined) {
                minX = getColX(h.col);
                minY = getRowY(h.row);
                maxX = minX + (colWidths.get(h.col) || DEFAULT_COL_WIDTH);
                maxY = minY + (rowHeights.get(h.row) || DEFAULT_ROW_HEIGHT);
            } else if (
                h.type === "range" &&
                h.startCol !== undefined &&
                h.startRow !== undefined &&
                h.endCol !== undefined &&
                h.endRow !== undefined
            ) {
                const startCol = Math.min(h.startCol, h.endCol);
                const endCol = Math.max(h.startCol, h.endCol);
                const startRow = Math.min(h.startRow, h.endRow);
                const endRow = Math.max(h.startRow, h.endRow);

                minX = getColX(startCol);
                minY = getRowY(startRow);
                maxX = getColX(endCol) + (colWidths.get(endCol) || DEFAULT_COL_WIDTH);
                maxY = getRowY(endRow) + (rowHeights.get(endRow) || DEFAULT_ROW_HEIGHT);
            } else {
                // Column/row highlights - skip for now (full column/row)
                continue;
            }

            // Hit test
            if (mouseX >= minX && mouseX <= maxX && mouseY >= minY && mouseY <= maxY) {
                hoveredIdx = idx;
                break; // First hit wins (highlights are drawn in order)
            }
        }

        // Update state if changed
        if (hoveredIdx !== getHoveredGridRefIndex()) {
            setHoveredGridRefIndex(hoveredIdx);
            render();
            this.config.updateFormulaBarHoverStyle();
        }
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
