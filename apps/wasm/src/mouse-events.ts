// =============================================================================
// Mouse Event Handlers
// =============================================================================
//
// Canvas mouse/pointer event handlers for the spreadsheet grid.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Mouse down: cell selection, resize start, drag start, fill handle
// - Mouse move: drag tracking, resize preview, selection extension
// - Mouse up: commit resize/drag operations, finalize selection
// - Double click: enter cell/header editing mode
// - Context menu: right-click menu with cut/copy/paste, insert/delete
//
// All handlers delegate state changes to UIStateMachine and update App state
// via the config callbacks.
//
// =============================================================================

import { UIEvent } from "./ui-state";
import type { SheetInfo, Position } from "./types";
import {
    HEADER_WIDTH,
    HEADER_HEIGHT,
    DEFAULT_COL_WIDTH,
    DEFAULT_ROW_HEIGHT,
    getZoomedHeaderWidth,
    getZoomedHeaderHeight,
    getZoomedColWidth,
    getZoomedRowHeight,
    getZoomFactor,
} from "./grid-constants";
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
    colToLetter,
    gridToScreen,
} from "./grid-utils";
import {
    showContextMenu,
    type ContextMenuEntry,
    type ContextType,
} from "./context-menu";
import { editingSession } from "./editing-session";
import type { AppEventManagerConfig } from "./app-events";
import {
    hitTestFormulaHighlight,
    getCursorForHitResult,
    createDragState,
    calculateResizedRange,
    calculateMovedRange,
    rangeToA1Notation,
    type FormulaRangeDragState,
} from "./formula-interaction";

// =============================================================================
// Mouse Event Handler Mixin
// =============================================================================

/**
 * Mixin class containing all mouse/pointer event handlers.
 * Designed to be used with AppEventManager via composition.
 */
export class MouseEventHandlers {
    protected config: AppEventManagerConfig;

    /** Last inserted reference during formula editing (for Shift+click/drag range building) */
    protected lastFormulaRef: FormulaRefState | null = null;

    /** Start position for drag-selecting a range during formula editing */
    protected formulaDragStart: Position | null = null;

    /** Original selection bounds when fill drag started */
    protected fillDragOriginalBounds: {
        minCol: number;
        maxCol: number;
        minRow: number;
        maxRow: number;
    } | null = null;

    /** Starting mouse position for fill drag */
    protected fillDragStartX = 0;
    protected fillDragStartY = 0;

    /** Pointer ID captured during drag operations */
    protected capturedPointerId: number | null = null;

    /** Formula range drag state for move/resize operations */
    protected formulaRangeDragState: FormulaRangeDragState | null = null;

    /** RAF batching for wheel events to coalesce rapid scroll events */
    protected pendingWheelDeltaX = 0;
    protected pendingWheelDeltaY = 0;
    protected wheelRafPending = false;

    constructor(config: AppEventManagerConfig) {
        this.config = config;
    }

    // =========================================================================
    // Coordinate Conversion
    // =========================================================================

    /**
     * Convert screen coordinates to canvas coordinates.
     * With proper zoom (not CSS transform), screen and canvas coordinates are 1:1
     * since zoom is applied to rendering dimensions, not the canvas element.
     * @param e Mouse event
     * @returns Canvas-relative x, y coordinates
     */
    protected getCanvasCoords(e: MouseEvent): { x: number; y: number } {
        const rect = this.config.canvas.getBoundingClientRect();
        // No zoom adjustment needed - coordinates are 1:1 with proper zoom
        return {
            x: e.clientX - rect.left,
            y: e.clientY - rect.top,
        };
    }


    // =========================================================================
    // Formula Reference Insertion Helpers
    // =========================================================================

    /**
     * Check if we're currently in formula editing mode
     */
    protected isInFormulaEditingMode(): boolean {
        const { cellEditor, formulaBarEditor } = this.config;
        const inFormulaMode =
            cellEditor.isFormulaMode() || formulaBarEditor.isFormulaMode();

        if (!inFormulaMode) {
            this.lastFormulaRef = null;
            this.formulaDragStart = null;
        }

        return inFormulaMode;
    }

    /**
     * Get the active formula display element
     */
    protected getActiveFormulaDisplay(): HTMLElement | null {
        const { cellEditor, formulaBarEditor } = this.config;
        if (cellEditor.isFormulaMode()) {
            return cellEditor.getDisplayElement();
        } else if (formulaBarEditor.isFormulaMode()) {
            return formulaBarEditor.getDisplayElement();
        }
        return null;
    }

    /**
     * Check if a sheet name needs quoting (contains spaces, quotes, !, or [)
     */
    protected sheetNameNeedsQuotes(name: string): boolean {
        return /[ '!\[]/.test(name);
    }

    /**
     * Format a sheet name with proper quoting for A1 notation.
     * Returns "SheetName!" or "'Sheet Name'!" depending on whether quoting is needed.
     */
    protected formatSheetPrefix(name: string): string {
        if (!this.sheetNameNeedsQuotes(name)) {
            return name + "!";
        }
        // Quote the name and escape single quotes by doubling them
        const escapedName = name.replace(/'/g, "''");
        return `'${escapedName}'!`;
    }

    /**
     * Get the sheet prefix for a cross-sheet reference, if needed.
     * Returns empty string if on the same sheet as the formula origin.
     * Returns "SheetName!" or "'Sheet Name'!" if on a different sheet.
     */
    protected getCrossSheetPrefix(): string {
        const { uiStateMachine, getSheetInfo } = this.config;

        // Get the origin sheet index (where formula editing started)
        const originSheetIndex = editingSession.getOriginSheetIndex();
        if (originSheetIndex < 0) {
            // Not tracking cross-sheet editing, no prefix needed
            return "";
        }

        // Get the current sheet index
        const currentSheetIndex = uiStateMachine.getActiveSheet();
        if (currentSheetIndex === originSheetIndex) {
            // Same sheet as origin, no prefix needed
            return "";
        }

        // Different sheet - need to add sheet prefix
        const sheetInfo = getSheetInfo();
        if (!sheetInfo) {
            return "";
        }

        return this.formatSheetPrefix(sheetInfo.name);
    }

    /**
     * Insert a reference into the active formula editor.
     * Automatically adds sheet prefix if clicking on a different sheet during cross-sheet formula editing.
     */
    protected insertFormulaReference(ref: string, position: Position): void {
        const { cellEditor, formulaBarEditor, render } = this.config;

        // Add sheet prefix if on a different sheet
        const sheetPrefix = this.getCrossSheetPrefix();
        const fullRef = sheetPrefix + ref;

        const activeEditor = editingSession.getActiveEditor();
        const cursorStart = editingSession.getSelection().start;

        if (activeEditor === "cell") {
            cellEditor.insertReferenceAtCursor(fullRef);
            cellEditor.getDisplayElement().focus();
        } else {
            formulaBarEditor.insertReferenceAtCursor(fullRef);
            formulaBarEditor.getDisplayElement().focus();
        }

        this.lastFormulaRef = {
            position,
            cursorStart,
            cursorEnd: cursorStart + fullRef.length,
            sheetPrefix,
        };

        render();
    }

    /**
     * Insert a column or row reference.
     * Automatically adds sheet prefix if clicking on a different sheet during cross-sheet formula editing.
     */
    protected insertColumnOrRowReference(ref: string): void {
        const { cellEditor, formulaBarEditor, render } = this.config;

        // Add sheet prefix if on a different sheet
        const sheetPrefix = this.getCrossSheetPrefix();
        const fullRef = sheetPrefix + ref;

        const activeEditor = editingSession.getActiveEditor();

        if (activeEditor === "cell") {
            cellEditor.insertReferenceAtCursor(fullRef);
            cellEditor.getDisplayElement().focus();
        } else {
            formulaBarEditor.insertReferenceAtCursor(fullRef);
            formulaBarEditor.getDisplayElement().focus();
        }

        render();
    }

    /**
     * Replace the last inserted reference with a range.
     * Preserves the sheet prefix from the original reference for cross-sheet ranges.
     */
    protected replaceLastRefWithRange(endCol: number, endRow: number): void {
        const { cellEditor, formulaBarEditor, render } = this.config;

        if (!this.lastFormulaRef) return;

        const startCol = colToLetter(this.lastFormulaRef.position.col);
        const startRow = this.lastFormulaRef.position.row + 1;
        const endColLetter = colToLetter(endCol);
        const endRowNum = endRow + 1;
        // Include the sheet prefix (e.g., "Sheet2!A1:B3")
        const rangeRef = `${this.lastFormulaRef.sheetPrefix}${startCol}${startRow}:${endColLetter}${endRowNum}`;

        const activeEditor = editingSession.getActiveEditor();

        if (activeEditor === "cell") {
            cellEditor.replaceReferenceAtPosition(
                this.lastFormulaRef.cursorStart,
                this.lastFormulaRef.cursorEnd,
                rangeRef,
            );
        } else {
            formulaBarEditor.replaceReferenceAtPosition(
                this.lastFormulaRef.cursorStart,
                this.lastFormulaRef.cursorEnd,
                rangeRef,
            );
        }

        this.lastFormulaRef.cursorEnd =
            this.lastFormulaRef.cursorStart + rangeRef.length;

        render();
    }

    // =========================================================================
    // Fill Handle Detection & Helpers
    // =========================================================================

    /**
     * Get column at X using midpoint snapping for fill handle
     */
    protected getColAtXMidpoint(
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
     * Get row at Y using midpoint snapping for fill handle
     */
    protected getRowAtYMidpoint(
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
    protected isPointInFillHandle(x: number, y: number): boolean {
        const bounds = this.config.getFillHandleBounds();
        if (!bounds) return false;
        const { x: hx, y: hy, width, height } = bounds;
        const padding = 3;
        return (
            x >= hx - padding &&
            x <= hx + width + padding &&
            y >= hy - padding &&
            y <= hy + height + padding
        );
    }

    // =========================================================================
    // Formula Range Drag Cancellation
    // =========================================================================

    /**
     * Cancel the current formula range drag operation and restore the original reference.
     * Called when Escape is pressed during drag.
     */
    cancelFormulaRangeDrag(): void {
        if (!this.formulaRangeDragState) return;

        const { canvas, cellEditor, formulaBarEditor, render } = this.config;
        const dragState = this.formulaRangeDragState;

        // Restore original reference text (use the stored original text to preserve $ markers)
        const activeEditor = cellEditor.isEditing() ? cellEditor : formulaBarEditor;
        activeEditor.replaceReferenceAtPosition(
            dragState.sourcePosition.start,
            dragState.sourcePosition.end,
            dragState.originalRefText
        );

        // Clear drag state
        this.formulaRangeDragState = null;
        if (this.capturedPointerId !== null) {
            canvas.releasePointerCapture(this.capturedPointerId);
            this.capturedPointerId = null;
        }
        canvas.style.cursor = "default";

        // Focus back on the active formula editor
        const display = this.getActiveFormulaDisplay();
        if (display) {
            display.focus();
        }
        render();
    }

    /**
     * Check if a formula range drag is currently active.
     */
    isFormulaRangeDragging(): boolean {
        return this.formulaRangeDragState !== null;
    }

    // =========================================================================
    // Canvas Event Setup
    // =========================================================================

    setupCanvasEvents(): void {
        const { canvas } = this.config;

        canvas.addEventListener("wheel", (e) => this.handleWheel(e), {
            passive: false,
        });

        canvas.addEventListener("pointerdown", (e) => this.handleMouseDown(e));
        canvas.addEventListener("pointermove", (e) => this.handleMouseMove(e));
        canvas.addEventListener("pointerup", (e) => this.handleMouseUp(e));
        canvas.addEventListener("pointerleave", () => this.handleMouseLeave());
        canvas.addEventListener("dblclick", (e) => this.handleDblClick(e));

        canvas.addEventListener("contextmenu", (e) =>
            this.handleContextMenu(e),
        );

        // Handle Escape key during formula range drag
        document.addEventListener("keydown", (e) => {
            if (e.key === "Escape" && this.formulaRangeDragState) {
                e.preventDefault();
                this.cancelFormulaRangeDrag();
            }
        });
    }

    // =========================================================================
    // Wheel Handler
    // =========================================================================

    handleWheel(e: WheelEvent): void {
        const { getSheetInfo } = this.config;
        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;
        e.preventDefault();

        // Get zoom factor for coordinate conversion
        const zoomFactor = getZoomFactor();

        // Accumulate wheel deltas (converted to logical pixels)
        this.pendingWheelDeltaX += e.deltaX / zoomFactor;
        this.pendingWheelDeltaY += e.deltaY / zoomFactor;

        // Use RAF batching to coalesce rapid wheel events (e.g., inertia scrolling)
        // This prevents overwhelming the system with scroll updates
        if (!this.wheelRafPending) {
            this.wheelRafPending = true;
            requestAnimationFrame(() => {
                this.wheelRafPending = false;
                this.processWheel();
            });
        }
    }

    /** Process accumulated wheel deltas - called once per animation frame */
    protected processWheel(): void {
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
            isScrollbarDragging,
        } = this.config;

        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;

        // Get accumulated deltas and reset
        const logicalDeltaX = this.pendingWheelDeltaX;
        const logicalDeltaY = this.pendingWheelDeltaY;
        this.pendingWheelDeltaX = 0;
        this.pendingWheelDeltaY = 0;

        // Get zoom factor for coordinate conversion
        const zoomFactor = getZoomFactor();

        // Convert screen viewport dimensions to logical coordinates
        const logicalViewportWidth = (canvas.clientWidth - getZoomedHeaderWidth()) / zoomFactor;
        const logicalViewportHeight = (canvas.clientHeight - getZoomedHeaderHeight()) / zoomFactor;

        // Max scroll in logical coordinates
        const { getColWidths, getRowHeights } = this.config;
        const colWidths = getColWidths();
        let contentWidth = sheetInfo.colCount * DEFAULT_COL_WIDTH;
        // Adjust for any custom column widths
        for (const [col, customWidth] of colWidths) {
            if (col < sheetInfo.colCount) {
                contentWidth += customWidth - DEFAULT_COL_WIDTH;
            }
        }
        const maxScrollX = Math.max(
            0,
            contentWidth - logicalViewportWidth,
        );

        const newScrollY = getScrollY() + logicalDeltaY;

        const effectiveRowCount = Math.max(
            sheetInfo.rowCount,
            getDiscoveredRows(),
        );

        // Check if we need to expand discovered rows (using logical coordinates)
        const visibleBottomRow = Math.ceil(
            (newScrollY + logicalViewportHeight) / DEFAULT_ROW_HEIGHT,
        );
        if (visibleBottomRow > getDiscoveredRows()) {
            const newDiscovered = Math.min(
                1_000_000,
                Math.max(visibleBottomRow + 100, sheetInfo.rowCount),
            );
            setDiscoveredRows(newDiscovered);
        }

        // Calculate actual content height
        const rowHeights = getRowHeights();
        let contentHeight = effectiveRowCount * DEFAULT_ROW_HEIGHT;
        // Adjust for any custom row heights
        for (const [row, customHeight] of rowHeights) {
            if (row < effectiveRowCount) {
                contentHeight += customHeight - DEFAULT_ROW_HEIGHT;
            }
        }
        const maxScrollY = Math.max(
            0,
            contentHeight - logicalViewportHeight,
        );

        setScrollX(Math.max(0, Math.min(maxScrollX, getScrollX() + logicalDeltaX)));
        setScrollY(Math.max(0, Math.min(maxScrollY, newScrollY)));

        render();
        // Skip expensive WASM data fetch if scrollbar is being dragged.
        // The scrollbar drag handler uses lightweight preview mode, and wheel
        // events should respect that to avoid triggering fetch storms on large files.
        // The scrollbar's mouseup handler will trigger a final fetch when drag ends.
        if (!isScrollbarDragging?.()) {
            fetchViewportNow();
        }
        updateScrollbars();
    }

    // =========================================================================
    // Mouse Down Handler
    // =========================================================================

    handleMouseDown(e: PointerEvent): void {
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
            formulaBarEditor,
            commitFormulaBarEdit,
        } = this.config;

        // Clear any lingering text selection from other elements (e.g., title editor)
        const selection = window.getSelection();
        if (selection && selection.rangeCount > 0) {
            selection.removeAllRanges();
        }

        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;

        const { x, y } = this.getCanvasCoords(e);
        const scrollX = getScrollX();
        const scrollY = getScrollY();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        // Column resize - use zoomed dimensions for header area check
        const zoomedHeaderHeight = getZoomedHeaderHeight();
        const zoomedHeaderWidth = getZoomedHeaderWidth();
        if (y < zoomedHeaderHeight && y > 0 && x > zoomedHeaderWidth) {
            const resizeCol = getResizeHandleCol(
                x,
                scrollX,
                colWidths,
                sheetInfo,
            );
            if (resizeCol >= 0) {
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

                // Calculate zoomed preview position using centralized helper
                // gridToScreen gives us the left edge of the column, we need the right edge
                const { x: colLeftX } = gridToScreen(
                    resizeCol + 1,  // Column after the one being resized
                    0,
                    scrollX,
                    scrollY,
                    colWidths,
                    rowHeights
                );
                setResizePreviewX(colLeftX);

                canvas.style.cursor = "col-resize";
                e.preventDefault();
                return;
            }
        }

        // Column header click - use zoomed dimensions
        if (y < zoomedHeaderHeight && y > 0 && x > zoomedHeaderWidth) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            if (col >= 0) {
                if (this.isInFormulaEditingMode()) {
                    const colLetter = colToLetter(col);
                    const ref = `${colLetter}:${colLetter}`;
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

                setPendingDragColumn(true);
                setPendingDragStartX(x);
                setPendingDragStartY(y);
                setDragSourceIndex(col);
                setDragTargetIndex(col);

                clearFormulaHighlights();
                render();
                updateFormulaBar(); // Update toolbar to show column style
                e.preventDefault();
                return;
            }
        }

        // Row resize - use zoomed dimensions for header area check
        if (x < zoomedHeaderWidth && x > 0 && y > zoomedHeaderHeight) {
            const resizeRow = getResizeHandleRow(
                y,
                scrollY,
                rowHeights,
                sheetInfo,
            );
            if (resizeRow >= 0) {
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

                // Calculate zoomed preview position using centralized helper
                // gridToScreen gives us the top edge of the row, we need the bottom edge
                const { y: rowTopY } = gridToScreen(
                    0,
                    resizeRow + 1,  // Row after the one being resized
                    scrollX,
                    scrollY,
                    colWidths,
                    rowHeights
                );
                setResizePreviewY(rowTopY);

                canvas.style.cursor = "row-resize";
                e.preventDefault();
                return;
            }
        }

        // Row header click - use zoomed dimensions
        if (x < zoomedHeaderWidth && x > 0 && y > zoomedHeaderHeight) {
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows)
            );
            if (row >= 0) {
                if (this.isInFormulaEditingMode()) {
                    const rowNum = row + 1;
                    const ref = `${rowNum}:${rowNum}`;
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

                setPendingDragRow(true);
                setPendingDragStartX(x);
                setPendingDragStartY(y);
                setDragSourceIndex(row);
                setDragTargetIndex(row);

                clearFormulaHighlights();
                render();
                updateFormulaBar(); // Update toolbar to show row style
                e.preventDefault();
                return;
            }
        }

        // Formula highlight interaction (resize/move) - check before fill handle
        if (this.isInFormulaEditingMode() && x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const { getFormulaHighlights, getColPixelOffsets, getRowPixelOffsets } = this.config;
            const highlights = getFormulaHighlights();
            if (highlights.length > 0) {
                const hitResult = hitTestFormulaHighlight(
                    x,
                    y,
                    {
                        scrollX,
                        scrollY,
                        colWidths,
                        rowHeights,
                        colPixelOffsets: getColPixelOffsets(),
                        rowPixelOffsets: getRowPixelOffsets(),
                        formulaHighlights: highlights,
                        hoveredFormulaRefIndex: -1,
                        isFormulaEditing: true,
                    },
                    canvas.width,
                    canvas.height
                );

                if (hitResult) {
                    const highlight = highlights[hitResult.highlightIndex];
                    if (highlight) {
                        const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
                        const discoveredRows = this.config.getDiscoveredRows();
                        const row = getRowAtY(
                            y,
                            scrollY,
                            rowHeights,
                            Math.max(sheetInfo.rowCount, discoveredRows)
                        );

                        if (col >= 0 && row >= 0) {
                            // Get original reference text for preserving $ markers
                            const activeEditor = cellEditor.isEditing() ? cellEditor : formulaBarEditor;
                            const formulaText = activeEditor.getValue();
                            const originalRefText = formulaText.slice(highlight.sourceStart, highlight.sourceEnd);

                            const dragState = createDragState(hitResult, highlight, { col, row }, originalRefText);
                            if (dragState) {
                                this.formulaRangeDragState = dragState;
                                canvas.setPointerCapture(e.pointerId);
                                this.capturedPointerId = e.pointerId;
                                canvas.style.cursor = getCursorForHitResult(hitResult, true);
                                e.preventDefault();
                                return;
                            }
                        }
                    }
                }
            }
        }

        // Fill handle click
        if (this.isPointInFillHandle(x, y)) {
            const selStart = getSelectionStart();
            const { getSelectionEnd, setIsFillDragging } = this.config;
            const selEnd = getSelectionEnd();
            if (selStart && selEnd) {
                const startFillDrag = () => {
                    setIsFillDragging(true);
                    this.fillDragOriginalBounds = {
                        minCol: Math.min(selStart.col, selEnd.col),
                        maxCol: Math.max(selStart.col, selEnd.col),
                        minRow: Math.min(selStart.row, selEnd.row),
                        maxRow: Math.max(selStart.row, selEnd.row),
                    };
                    this.fillDragStartX = x;
                    this.fillDragStartY = y;
                    canvas.style.cursor = "crosshair";
                };

                // Commit any active editing before starting fill drag
                if (cellEditor.isEditing()) {
                    cellEditor.confirmEditing().then(startFillDrag);
                } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
                    commitFormulaBarEdit().then(startFillDrag);
                } else {
                    startFillDrag();
                }
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
                Math.max(sheetInfo.rowCount, discoveredRows)
            );

            if (col >= 0 && row >= 0) {
                if (this.isInFormulaEditingMode()) {
                    if (e.shiftKey && this.lastFormulaRef) {
                        this.replaceLastRefWithRange(col, row);
                    } else {
                        const colLetter = colToLetter(col);
                        const rowNum = row + 1;
                        const ref = `${colLetter}${rowNum}`;
                        this.insertFormulaReference(ref, { col, row });
                        this.formulaDragStart = { col, row };
                    }
                    e.preventDefault();
                    return;
                }

                setSelectedColumn(null);
                setSelectedRow(null);

                const selStart = getSelectionStart();
                const isShiftClick = e.shiftKey && selStart;

                const pointerId = e.pointerId;

                const applySelection = () => {
                    if (isShiftClick && selStart) {
                        setSelectionEnd({ col, row });
                        setSelectedCell({ col, row });
                    } else {
                        setSelectedCell({ col, row });
                        setSelectionStart({ col, row });
                        setSelectionEnd({ col, row });
                    }
                    uiStateMachine.transition(UIEvent.START_SELECTING, {
                        selectedCell: { col, row },
                        selectionStart: isShiftClick ? selStart : { col, row },
                        selectionEnd: { col, row },
                    });
                    canvas.setPointerCapture(pointerId);
                    this.capturedPointerId = pointerId;
                    render();
                    updateFormulaBar();
                    canvas.focus();
                };

                if (cellEditor.isEditing()) {
                    cellEditor.confirmEditing().then(applySelection);
                } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
                    uiStateMachine.transition(UIEvent.COMMIT_FORMULA_EDIT);
                    commitFormulaBarEdit().then(applySelection);
                } else {
                    applySelection();
                }
            }
        }
    }

    // =========================================================================
    // Mouse Move Handler
    // =========================================================================

    handleMouseMove(e: PointerEvent): void {
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

        const { x, y } = this.getCanvasCoords(e);
        const scrollX = getScrollX();
        const scrollY = getScrollY();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        // Handle fill handle drag
        const { getIsFillDragging, getFillPreviewRange, setFillPreviewRange } = this.config;
        if (getIsFillDragging() && this.fillDragOriginalBounds) {
            const discoveredRows = this.config.getDiscoveredRows();
            const maxRows = Math.max(sheetInfo.rowCount, discoveredRows);
            const bounds = this.fillDragOriginalBounds;

            const dx = Math.abs(x - this.fillDragStartX);
            const dy = Math.abs(y - this.fillDragStartY);
            const threshold = 3;

            if (dx > threshold || dy > threshold) {
                const axis = dx > dy ? "x" : "y";

                let newMinCol: number,
                    newMaxCol: number,
                    newMinRow: number,
                    newMaxRow: number;

                if (axis === "x") {
                    newMinRow = bounds.minRow;
                    newMaxRow = bounds.maxRow;

                    const targetCol = this.getColAtXMidpoint(
                        x,
                        scrollX,
                        colWidths,
                        sheetInfo.colCount,
                    );

                    if (targetCol < bounds.minCol) {
                        newMinCol = targetCol;
                        newMaxCol = bounds.maxCol;
                    } else {
                        newMinCol = bounds.minCol;
                        newMaxCol = Math.max(bounds.minCol, targetCol);
                    }
                } else {
                    newMinCol = bounds.minCol;
                    newMaxCol = bounds.maxCol;

                    const targetRow = this.getRowAtYMidpoint(
                        y,
                        scrollY,
                        rowHeights,
                        maxRows,
                    );

                    if (targetRow < bounds.minRow) {
                        newMinRow = targetRow;
                        newMaxRow = bounds.maxRow;
                    } else {
                        newMinRow = bounds.minRow;
                        newMaxRow = Math.max(bounds.minRow, targetRow);
                    }
                }

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

        // Handle formula range manipulation (move/resize) during drag
        if (this.formulaRangeDragState && x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const { cellEditor, formulaBarEditor } = this.config;
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows)
            );

            if (col >= 0 && row >= 0) {
                const dragState = this.formulaRangeDragState;
                let newRange: { startCol: number; startRow: number; endCol: number; endRow: number };

                if (dragState.action === "resize") {
                    newRange = calculateResizedRange(dragState, { col, row });
                } else {
                    newRange = calculateMovedRange(dragState, { col, row });
                }

                // Generate new reference text, preserving $ markers and sheet prefix from original
                const newRef = rangeToA1Notation(
                    newRange.startCol,
                    newRange.startRow,
                    newRange.endCol,
                    newRange.endRow,
                    dragState.absoluteMarkers,
                    dragState.sheetPrefix
                );

                // Update formula text using replaceReferenceAtPosition
                const activeEditor = cellEditor.isEditing() ? cellEditor : formulaBarEditor;
                activeEditor.replaceReferenceAtPosition(
                    dragState.sourcePosition.start,
                    dragState.sourcePosition.end,
                    newRef
                );

                // Update source position for next iteration (reference length may have changed)
                dragState.sourcePosition.end = dragState.sourcePosition.start + newRef.length;
            }
            return;
        }

        // Handle drag selection during formula editing
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
                Math.max(sheetInfo.rowCount, discoveredRows)
            );
            if (col >= 0 && row >= 0) {
                if (
                    col !== this.formulaDragStart.col ||
                    row !== this.formulaDragStart.row
                ) {
                    if (this.lastFormulaRef) {
                        this.replaceLastRefWithRange(col, row);
                    }
                }
            }
            return;
        }

        // Check if pending drag should become actual drag
        if (getPendingDragColumn()) {
            const dx = Math.abs(x - getPendingDragStartX());
            const dy = Math.abs(y - getPendingDragStartY());
            if (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD) {
                if (cellEditor.isEditing()) {
                    cellEditor.confirmEditing();
                } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
                    commitFormulaBarEdit();
                }
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
                if (cellEditor.isEditing()) {
                    cellEditor.confirmEditing();
                } else if (uiStateMachine.isInState("FORMULA_BAR_EDITING")) {
                    commitFormulaBarEdit();
                }
                setPendingDragRow(false);
                uiStateMachine.transition(UIEvent.START_ROW_DRAG);
                setDragMouseX(x);
                setDragMouseY(y);
                canvas.style.cursor = "grabbing";
            }
        }

        // Range selection drag
        const isPointerCaptured = this.capturedPointerId !== null;
        const inGridArea = x > HEADER_WIDTH && y > HEADER_HEIGHT;
        if (uiStateMachine.isInState("SELECTING") && (inGridArea || isPointerCaptured)) {
            const clampedX = Math.max(HEADER_WIDTH + 1, x);
            const clampedY = Math.max(HEADER_HEIGHT + 1, y);

            const col = getColAtX(clampedX, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = this.config.getDiscoveredRows();
            const row = getRowAtY(
                clampedY,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows)
            );
            if (col >= 0 && row >= 0) {
                const selEnd = getSelectionEnd();
                if (!selEnd || selEnd.col !== col || selEnd.row !== row) {
                    setSelectionEnd({ col, row });
                    setSelectedCell({ col, row });
                    render();
                    updateFormulaBar();
                }
            }
            presenceBroadcaster.broadcastMousePosition(x, y);
            return;
        }

        if (uiStateMachine.isInState("COLUMN_RESIZING")) {
            // Delta is in screen (zoomed) pixels - the preview should move
            // directly by this amount since we're in screen coordinates
            const delta = e.clientX - getResizeStartX();

            // Calculate zoomed preview position using centralized helper
            // gridToScreen gives us the left edge of the column
            const { x: colX } = gridToScreen(
                getResizeColIndex(),
                0,
                scrollX,
                scrollY,
                colWidths,
                rowHeights
            );
            // Add the zoomed start width plus the delta (delta is already in screen pixels)
            setResizePreviewX(colX + getZoomedColWidth(getResizeStartWidth()) + delta);

            render();
        } else if (uiStateMachine.isInState("ROW_RESIZING")) {
            // Delta is in screen (zoomed) pixels
            const delta = e.clientY - getResizeStartY();

            // Calculate zoomed preview position using centralized helper
            // gridToScreen gives us the top edge of the row
            const { y: rowY } = gridToScreen(
                0,
                getResizeRowIndex(),
                scrollX,
                scrollY,
                colWidths,
                rowHeights
            );
            // Add the zoomed start height plus the delta (delta is already in screen pixels)
            setResizePreviewY(rowY + getZoomedRowHeight(getResizeStartHeight()) + delta);

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
            // Determine cursor based on position - use zoomed dimensions
            const zoomedHeaderHeight = getZoomedHeaderHeight();
            const zoomedHeaderWidth = getZoomedHeaderWidth();

            // Check for formula highlight interaction zones when in formula editing mode
            let formulaHitResult = null;
            if (this.isInFormulaEditingMode()) {
                const { getFormulaHighlights, getColPixelOffsets, getRowPixelOffsets, getHoveredGridRefIndex } = this.config;
                const highlights = getFormulaHighlights();
                if (highlights.length > 0) {
                    formulaHitResult = hitTestFormulaHighlight(
                        x,
                        y,
                        {
                            scrollX,
                            scrollY,
                            colWidths,
                            rowHeights,
                            colPixelOffsets: getColPixelOffsets(),
                            rowPixelOffsets: getRowPixelOffsets(),
                            formulaHighlights: highlights,
                            hoveredFormulaRefIndex: getHoveredGridRefIndex(),
                            isFormulaEditing: true,
                        },
                        canvas.width,
                        canvas.height
                    );
                }
            }

            if (formulaHitResult) {
                // Formula highlight interaction takes priority
                canvas.style.cursor = getCursorForHitResult(formulaHitResult);
            } else if (this.isPointInFillHandle(x, y)) {
                canvas.style.cursor = "crosshair";
            } else if (y < zoomedHeaderHeight && y > 0 && x > zoomedHeaderWidth) {
                const resizeCol = getResizeHandleCol(
                    x,
                    scrollX,
                    colWidths,
                    sheetInfo,
                );
                canvas.style.cursor = resizeCol >= 0 ? "col-resize" : "default";
            } else if (x < zoomedHeaderWidth && x > 0 && y > zoomedHeaderHeight) {
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

        // Broadcast mouse position for collaboration - use zoomed dimensions
        const zoomedHeaderWidthBroadcast = getZoomedHeaderWidth();
        const zoomedHeaderHeightBroadcast = getZoomedHeaderHeight();
        if (x > zoomedHeaderWidthBroadcast && y > zoomedHeaderHeightBroadcast) {
            presenceBroadcaster.broadcastMousePosition(x, y);
        }
    }

    // =========================================================================
    // Mouse Up Handler
    // =========================================================================

    async handleMouseUp(e: PointerEvent): Promise<void> {
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

        // End fill handle drag
        const { getIsFillDragging, setIsFillDragging, getFillPreviewRange, setFillPreviewRange, setSelectionStart, setSelectionEnd } = this.config;
        if (getIsFillDragging() && dataSource) {
            const original = this.fillDragOriginalBounds;
            const preview = getFillPreviewRange();
            if (original && preview) {
                const targetMinCol = preview.minCol;
                const targetMinRow = preview.minRow;
                const targetMaxCol = preview.maxCol;
                const targetMaxRow = preview.maxRow;

                dataSource.fillRange(
                    original.minCol, original.minRow,
                    original.maxCol, original.maxRow,
                    targetMinCol, targetMinRow,
                    targetMaxCol, targetMaxRow
                ).then(() => {
                    setSelectionStart({ col: targetMinCol, row: targetMinRow });
                    setSelectionEnd({ col: targetMaxCol, row: targetMaxRow });
                    render();
                    updateFormulaBar();
                }).catch((err) => {
                    console.error("Error filling range:", err);
                });
            }
            setIsFillDragging(false);
            setFillPreviewRange(null);
            this.fillDragOriginalBounds = null;
            canvas.style.cursor = "default";
            render();
            updateFormulaBar();
        }

        // End formula range manipulation (move/resize)
        if (this.formulaRangeDragState) {
            this.formulaRangeDragState = null;
            if (this.capturedPointerId !== null) {
                canvas.releasePointerCapture(this.capturedPointerId);
                this.capturedPointerId = null;
            }
            canvas.style.cursor = "default";
            // Focus back on the active formula editor
            const display = this.getActiveFormulaDisplay();
            if (display) {
                display.focus();
            }
        }

        // End formula drag selection
        if (this.formulaDragStart) {
            this.formulaDragStart = null;
            const display = this.getActiveFormulaDisplay();
            if (display) {
                display.focus();
            }
        }

        // End range selection
        if (uiStateMachine.isInState("SELECTING")) {
            uiStateMachine.transition(UIEvent.STOP_SELECTING);
            if (this.capturedPointerId !== null) {
                canvas.releasePointerCapture(this.capturedPointerId);
                this.capturedPointerId = null;
            }
            const selStart = getSelectionStart();
            if (selStart) {
                setSelectedCell({ col: selStart.col, row: selStart.row });
            }
            render();
            updateFormulaBar();
        }

        // Clear pending drag states
        if (getPendingDragColumn()) {
            setPendingDragColumn(false);
            setDragSourceIndex(-1);
            setDragTargetIndex(-1);
        }
        if (getPendingDragRow()) {
            setPendingDragRow(false);
            setDragSourceIndex(-1);
            setDragTargetIndex(-1);
        }

        if (uiStateMachine.isInState("COLUMN_RESIZING") && dataSource) {
            const delta = e.clientX - getResizeStartX();
            const newWidth = Math.max(
                20,
                Math.min(1000, getResizeStartWidth() + delta),
            );
            const colIndexToResize = getResizeColIndex();

            colWidths.set(colIndexToResize, newWidth);

            uiStateMachine.transition(UIEvent.END_COLUMN_RESIZE);
            setResizeColIndex(-1);
            canvas.style.cursor = "default";
            render();

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

            rowHeights.set(rowIndexToResize, newHeight);

            uiStateMachine.transition(UIEvent.END_ROW_RESIZE);
            setResizeRowIndex(-1);
            canvas.style.cursor = "default";
            render();

            try {
                await dataSource.resizeRowByPos(rowIndexToResize, newHeight);
            } catch (err) {
                console.error("Error resizing row:", err);
            }
        } else if (uiStateMachine.isInState("COLUMN_DRAGGING") && dataSource) {
            const sourceIdx = getDragSourceIndex();
            const targetIdx = getDragTargetIndex();
            const colId = getColumnId(sourceIdx, getColumns());

            uiStateMachine.transition(UIEvent.END_COLUMN_DRAG);
            setDragSourceIndex(-1);
            setDragTargetIndex(-1);
            canvas.style.cursor = "default";

            if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
                if (targetIdx > sourceIdx) {
                    setSelectedColumn(targetIdx - 1);
                } else {
                    setSelectedColumn(targetIdx);
                }
            }
            render();

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

            uiStateMachine.transition(UIEvent.END_ROW_DRAG);
            setDragSourceIndex(-1);
            setDragTargetIndex(-1);
            canvas.style.cursor = "default";

            if (sourceIdx !== targetIdx && sourceIdx !== targetIdx - 1) {
                if (targetIdx > sourceIdx) {
                    setSelectedRow(targetIdx - 1);
                } else {
                    setSelectedRow(targetIdx);
                }
            }
            render();

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

    // =========================================================================
    // Mouse Leave Handler
    // =========================================================================

    handleMouseLeave(): void {
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

        // End range selection on mouse leave (unless pointer is captured)
        if (uiStateMachine.isInState("SELECTING") && this.capturedPointerId === null) {
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

        presenceBroadcaster.clearMousePosition();
    }

    // =========================================================================
    // Double Click Handler
    // =========================================================================

    handleDblClick(e: MouseEvent): void {
        const {
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
            getGridRenderer,
            getDataSource,
        } = this.config;

        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;

        const { x, y } = this.getCanvasCoords(e);
        const scrollX = getScrollX();
        const scrollY = getScrollY();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        // Double-click on column resize handle: auto-fit column width
        const zoomedHeaderHeight = getZoomedHeaderHeight();
        const zoomedHeaderWidth = getZoomedHeaderWidth();
        if (y < zoomedHeaderHeight && y > 0 && x > zoomedHeaderWidth) {
            const resizeCol = getResizeHandleCol(
                x,
                scrollX,
                colWidths,
                sheetInfo,
            );
            if (resizeCol >= 0) {
                // Auto-fit the column to the left of the resize handle
                const gridRenderer = getGridRenderer();
                const dataSource = getDataSource();
                const optimalWidth = gridRenderer.calculateAutoFitWidth(resizeCol);
                if (optimalWidth !== null && dataSource) {
                    // Update local state immediately for responsive UI
                    colWidths.set(resizeCol, optimalWidth);
                    render();
                    // Persist to CRDT
                    dataSource.resizeColumnByPos(resizeCol, optimalWidth).catch((err) => {
                        console.error("Error auto-fitting column:", err);
                    });
                }
                e.preventDefault();
                return;
            }
        }

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
                Math.max(sheetInfo.rowCount, discoveredRows)
            );

            if (col >= 0 && row >= 0) {
                setSelectedCell({ col, row });
                setSelectionStart({ col, row });
                setSelectionEnd({ col, row });
                render();
                updateFormulaBar();
                cellEditor.startEditing({ mode: "append" });
            }
        }
    }

    // =========================================================================
    // Context Menu Handler
    // =========================================================================

    handleContextMenu(e: MouseEvent): void {
        e.preventDefault();

        const {
            getSheetInfo,
            getScrollX,
            getScrollY,
            getColWidths,
            getRowHeights,
        } = this.config;

        const sheetInfo = getSheetInfo();
        if (!sheetInfo) return;

        const { x, y } = this.getCanvasCoords(e);
        const scrollX = getScrollX();
        const scrollY = getScrollY();
        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        const context = this.getContextAtPosition(
            x,
            y,
            scrollX,
            scrollY,
            colWidths,
            rowHeights,
            sheetInfo,
        );

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

        if (x <= HEADER_WIDTH && y <= HEADER_HEIGHT) {
            return { type: "corner" };
        }

        if (y <= HEADER_HEIGHT && x > HEADER_WIDTH) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            if (col >= 0) {
                const columns = getColumns();
                const colId = getColumnId(col, columns);
                return { type: "column-header", col, colId };
            }
            return { type: "empty" };
        }

        if (x <= HEADER_WIDTH && y > HEADER_HEIGHT) {
            const discoveredRows = getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows)
            );
            if (row >= 0) {
                const rows = getRows();
                const rowId = getRowId(row, rows);
                return { type: "row-header", row, rowId };
            }
            return { type: "empty" };
        }

        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const col = getColAtX(x, scrollX, colWidths, sheetInfo.colCount);
            const discoveredRows = getDiscoveredRows();
            const row = getRowAtY(
                y,
                scrollY,
                rowHeights,
                Math.max(sheetInfo.rowCount, discoveredRows)
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
        const { getDataSource, fetchViewportNow, render, getGridRenderer, getColWidths } = this.config;

        switch (context.type) {
            case "column-header":
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
                    label: "Auto-fit column width",
                    action: async () => {
                        const ds = getDataSource();
                        if (!ds) return;
                        const gridRenderer = getGridRenderer();
                        const colWidths = getColWidths();
                        const optimalWidth = gridRenderer.calculateAutoFitWidth(context.col);
                        if (optimalWidth !== null) {
                            // Update local state immediately for responsive UI
                            colWidths.set(context.col, optimalWidth);
                            render();
                            // Persist to CRDT
                            await ds.resizeColumnByPos(context.col, optimalWidth);
                        }
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
                const {
                    clipboardManager,
                    setSelectedCell,
                    setSelectionStart,
                    setSelectionEnd,
                } = this.config;

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
                items.push({
                    label: "Select all",
                    action: () => {
                        console.log("Select all");
                    },
                    disabled: true,
                });
                break;

            case "empty":
                break;
        }

        return items;
    }

    // =========================================================================
    // Formula Highlight Hover
    // =========================================================================

    /**
     * Check if mouse is over a formula highlight and update hover state
     */
    checkFormulaHighlightHover(mouseX: number, mouseY: number): void {
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

        const getColX = (col: number): number => {
            const offset = colPixelOffsets.get(col);
            if (offset !== undefined) return offset - scrollX + HEADER_WIDTH;
            let x = HEADER_WIDTH - scrollX;
            for (let i = 0; i < col; i++) {
                x += colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            return x;
        };

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
                continue;
            }

            if (mouseX >= minX && mouseX <= maxX && mouseY >= minY && mouseY <= maxY) {
                hoveredIdx = idx;
                break;
            }
        }

        if (hoveredIdx !== getHoveredGridRefIndex()) {
            setHoveredGridRefIndex(hoveredIdx);
            render();
            this.config.updateFormulaBarHoverStyle();
        }
    }
}

// =============================================================================
// Types
// =============================================================================

/** Tracks the last inserted formula reference for range building */
interface FormulaRefState {
    position: Position;
    cursorStart: number;
    cursorEnd: number;
    /** Sheet prefix (e.g., "Sheet2!" or "'My Sheet'!") for cross-sheet references */
    sheetPrefix: string;
}
