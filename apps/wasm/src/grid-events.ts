// =============================================================================
// Grid Events
// =============================================================================
//
// Mouse and keyboard event handling for the spreadsheet grid canvas.
// Translates raw DOM events into grid-level state changes.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Hit testing: convert pixel coordinates to cell/header/resize handle
// - Selection: single cell, range, column, row selection via mouse
// - Drag operations: column/row reordering, resize handles
// - Scroll handling: mouse wheel, touch scroll
// - Keyboard navigation: arrow keys, Tab, Enter
//
// State emission:
// - Emits GridEventState objects describing what changed
// - Does not directly modify App state (caller applies changes)
// - Enables decoupled testing of event logic
//
// =============================================================================

import {
    HEADER_HEIGHT,
    HEADER_WIDTH,
    DEFAULT_COL_WIDTH,
    DEFAULT_ROW_HEIGHT,
    getZoomFactor,
    getZoomedHeaderWidth,
    getZoomedHeaderHeight,
    getZoomedColWidth,
    getZoomedRowHeight,
} from "./grid-constants.js";
import type { FillHandleBounds } from "./grid-selection-renderer.js";
import type { SheetInfo, Position } from "./types.js";

// Resize handle detection width (pixels on each side of border)
const RESIZE_HANDLE_WIDTH = 6;

/** Column info for grid events */
interface ColumnInfo {
    id: string;
    pos: number;
    width: number;
    name: string;
}

/** Row info for grid events */
interface RowInfo {
    id: string;
    pos: number;
    height: number;
    name: string;
}

/** State changes emitted by the event handler */
export interface GridEventState {
    selectedCell?: Position | null;
    selectedColumn?: number | null;
    selectedRow?: number | null;
    scrollX?: number;
    scrollY?: number;
    isDraggingColumn?: boolean;
    isDraggingRow?: boolean;
    dragSourceIndex?: number;
    dragTargetIndex?: number;
    dragMouseX?: number;
    dragMouseY?: number;
    isResizing?: boolean;
    resizePreviewX?: number;
}

/** Options for GridEventHandler */
export interface GridEventHandlerOptions {
    onStateChange?: (state: GridEventState) => void;
    onRender?: () => void;
    onFetchViewport?: () => void;
    onStartEditing?: () => void;
    onResizeColumn?: (
        colIndex: number,
        colId: string | null,
        newWidth: number,
    ) => Promise<void>;
    onMoveColumn?: (
        colId: string,
        targetIndex: number,
        newSelectedCol: number,
    ) => Promise<void>;
    onMoveRow?: (
        rowId: string,
        targetIndex: number,
        newSelectedRow: number,
    ) => Promise<void>;
}

/** State references that can be set from the main application */
export interface GridEventHandlerState {
    sheetInfo?: SheetInfo | null;
    colWidths?: Map<number, number>;
    rowHeights?: Map<number, number>;
    columns?: ColumnInfo[];
    rows?: RowInfo[];
    scrollX?: number;
    scrollY?: number;
    selectedCell?: Position | null;
    selectedColumn?: number | null;
    selectedRow?: number | null;
    isEditing?: boolean;
    fillHandleBounds?: FillHandleBounds | null;
}

/**
 * GridEventHandler manages all user interactions with the grid
 */
export class GridEventHandler {
    canvas: HTMLCanvasElement;
    onStateChange: (state: GridEventState) => void;
    onRender: () => void;
    onFetchViewport: () => void;
    onStartEditing: () => void;
    onResizeColumn: (
        colIndex: number,
        colId: string | null,
        newWidth: number,
    ) => Promise<void>;
    onMoveColumn: (
        colId: string,
        targetIndex: number,
        newSelectedCol: number,
    ) => Promise<void>;
    onMoveRow: (
        rowId: string,
        targetIndex: number,
        newSelectedRow: number,
    ) => Promise<void>;

    // State references
    sheetInfo: SheetInfo | null = null;
    colWidths: Map<number, number> = new Map();
    rowHeights: Map<number, number> = new Map();
    columns: ColumnInfo[] = [];
    rows: RowInfo[] = [];
    scrollX = 0;
    scrollY = 0;
    selectedCell: Position | null = null;
    selectedColumn: number | null = null;
    selectedRow: number | null = null;
    fillHandleBounds: FillHandleBounds | null = null;

    // Internal state
    isEditing = false;
    isResizing = false;
    resizeColIndex = -1;
    resizeStartX = 0;
    resizeStartWidth = 0;
    resizePreviewX = 0;
    isDraggingColumn = false;
    isDraggingRow = false;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragMouseX = 0;
    dragMouseY = 0;

    constructor(
        canvas: HTMLCanvasElement,
        options: GridEventHandlerOptions = {},
    ) {
        this.canvas = canvas;
        this.onStateChange = options.onStateChange || (() => {});
        this.onRender = options.onRender || (() => {});
        this.onFetchViewport = options.onFetchViewport || (() => {});
        this.onStartEditing = options.onStartEditing || (() => {});
        this.onResizeColumn = options.onResizeColumn || (async () => {});
        this.onMoveColumn = options.onMoveColumn || (async () => {});
        this.onMoveRow = options.onMoveRow || (async () => {});

        this._bindEvents();
    }

    /**
     * Update state references from the main application
     */
    setStateRefs(state: GridEventHandlerState): void {
        Object.assign(this, state);
    }

    /**
     * Get column at X coordinate (zoom-aware)
     */
    getColAtX(x: number): number {
        const zoomedHeaderWidth = getZoomedHeaderWidth();
        const zoomedScrollX = Math.round(this.scrollX * getZoomFactor());
        if (x < zoomedHeaderWidth) return -1;
        let accX = zoomedHeaderWidth - zoomedScrollX;
        let col = 0;
        while (accX < x && col < (this.sheetInfo?.colCount || 1000)) {
            const baseWidth = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
            accX += getZoomedColWidth(baseWidth);
            if (accX > x) return col;
            col++;
        }
        return col;
    }

    /**
     * Get row at Y coordinate (zoom-aware)
     */
    getRowAtY(y: number): number {
        const zoomedHeaderHeight = getZoomedHeaderHeight();
        const zoomedScrollY = Math.round(this.scrollY * getZoomFactor());
        if (y < zoomedHeaderHeight) return -1;
        let accY = zoomedHeaderHeight - zoomedScrollY;
        let row = 0;
        while (accY < y && row < (this.sheetInfo?.rowCount || 1000)) {
            const baseHeight = this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
            accY += getZoomedRowHeight(baseHeight);
            if (accY > y) return row;
            row++;
        }
        return row;
    }

    /**
     * Check if mouse is over a column resize handle (zoom-aware)
     */
    getResizeHandleCol(mouseX: number): number {
        if (!this.sheetInfo) return -1;
        const zoomedHeaderWidth = getZoomedHeaderWidth();
        const zoomedScrollX = Math.round(this.scrollX * getZoomFactor());
        let x = zoomedHeaderWidth - zoomedScrollX;
        for (let col = 0; col < this.sheetInfo.colCount; col++) {
            const baseW = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
            const colW = getZoomedColWidth(baseW);
            const rightEdge = x + colW;
            if (
                mouseX >= rightEdge - RESIZE_HANDLE_WIDTH &&
                mouseX <= rightEdge + RESIZE_HANDLE_WIDTH
            ) {
                return col;
            }
            x = rightEdge;
        }
        return -1;
    }

    /**
     * Get column ID at position
     */
    getColumnId(colPos: number): string | null {
        for (const col of this.columns) {
            if (col.pos === colPos) return col.id;
        }
        return null;
    }

    /**
     * Get row ID at position
     */
    getRowId(rowPos: number): string | null {
        for (const row of this.rows) {
            if (row.pos === rowPos) return row.id;
        }
        return null;
    }

    /**
     * Get drop target column index from X coordinate
     */
    getDropTargetCol(mouseX: number): number {
        if (mouseX < HEADER_WIDTH) return 0;
        let x = HEADER_WIDTH - this.scrollX;
        for (let col = 0; col < (this.sheetInfo?.colCount || 1000); col++) {
            const colW = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
            const midX = x + colW / 2;
            if (mouseX < midX) return col;
            x += colW;
        }
        return this.sheetInfo?.colCount || 0;
    }

    /**
     * Get drop target row index from Y coordinate
     */
    getDropTargetRow(mouseY: number): number {
        if (mouseY < HEADER_HEIGHT) return 0;
        let y = HEADER_HEIGHT - this.scrollY;
        for (let row = 0; row < (this.sheetInfo?.rowCount || 1000); row++) {
            const rowH = this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
            const midY = y + rowH / 2;
            if (mouseY < midY) return row;
            y += rowH;
        }
        return this.sheetInfo?.rowCount || 0;
    }

    /**
     * Bind all event listeners
     */
    private _bindEvents(): void {
        this.canvas.addEventListener("wheel", this._handleWheel.bind(this), {
            passive: false,
        });
        this.canvas.addEventListener(
            "mousedown",
            this._handleMouseDown.bind(this),
        );
        this.canvas.addEventListener(
            "mousemove",
            this._handleMouseMove.bind(this),
        );
        this.canvas.addEventListener("mouseup", this._handleMouseUp.bind(this));
        this.canvas.addEventListener(
            "mouseleave",
            this._handleMouseLeave.bind(this),
        );
        this.canvas.addEventListener(
            "dblclick",
            this._handleDoubleClick.bind(this),
        );
        document.addEventListener("keydown", this._handleKeyDown.bind(this));
    }

    private _handleWheel(e: WheelEvent): void {
        if (!this.sheetInfo) return;
        e.preventDefault();

        // Get zoom factor for coordinate conversion
        const zoomFactor = getZoomFactor();

        // Convert screen viewport dimensions to logical coordinates
        const logicalViewportWidth = (this.canvas.clientWidth - getZoomedHeaderWidth()) / zoomFactor;
        const logicalViewportHeight = (this.canvas.clientHeight - getZoomedHeaderHeight()) / zoomFactor;

        // Max scroll in logical coordinates
        const maxScrollX = Math.max(
            0,
            this.sheetInfo.colCount * DEFAULT_COL_WIDTH - logicalViewportWidth,
        );
        const maxScrollY = Math.max(
            0,
            this.sheetInfo.rowCount * DEFAULT_ROW_HEIGHT - logicalViewportHeight,
        );

        // Convert wheel delta from screen pixels to logical pixels
        const logicalDeltaX = e.deltaX / zoomFactor;
        const logicalDeltaY = e.deltaY / zoomFactor;

        this.scrollX = Math.max(
            0,
            Math.min(maxScrollX, this.scrollX + logicalDeltaX),
        );
        this.scrollY = Math.max(
            0,
            Math.min(maxScrollY, this.scrollY + logicalDeltaY),
        );

        this.onStateChange({ scrollX: this.scrollX, scrollY: this.scrollY });
        this.onRender();
        this.onFetchViewport();
    }

    private _handleMouseDown(e: MouseEvent): void {
        if (!this.sheetInfo) return;
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;

        // Column resize
        if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
            const resizeCol = this.getResizeHandleCol(x);
            if (resizeCol >= 0) {
                this.isResizing = true;
                this.resizeColIndex = resizeCol;
                this.resizeStartX = e.clientX;
                this.resizeStartWidth =
                    this.colWidths.get(resizeCol) || DEFAULT_COL_WIDTH;

                let colX = HEADER_WIDTH - this.scrollX;
                for (let i = 0; i <= resizeCol; i++) {
                    colX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
                }
                this.resizePreviewX = colX;

                this.canvas.style.cursor = "col-resize";
                e.preventDefault();
                return;
            }
        }

        // Column header click (select + drag)
        if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
            const col = this.getColAtX(x);
            if (col >= 0) {
                this.selectedCell = null;
                this.selectedRow = null;
                this.selectedColumn = col;
                this.isDraggingColumn = true;
                this.dragSourceIndex = col;
                this.dragTargetIndex = col;
                this.dragMouseX = x;
                this.dragMouseY = y;

                this.canvas.style.cursor = "grab";
                this.onStateChange({
                    selectedCell: null,
                    selectedRow: null,
                    selectedColumn: col,
                    isDraggingColumn: true,
                    dragSourceIndex: col,
                    dragTargetIndex: col,
                    dragMouseX: x,
                    dragMouseY: y,
                });
                this.onRender();
                e.preventDefault();
                return;
            }
        }

        // Row header click (select + drag)
        if (x < HEADER_WIDTH && x > 0 && y > HEADER_HEIGHT) {
            const row = this.getRowAtY(y);
            if (row >= 0) {
                this.selectedCell = null;
                this.selectedColumn = null;
                this.selectedRow = row;
                this.isDraggingRow = true;
                this.dragSourceIndex = row;
                this.dragTargetIndex = row;
                this.dragMouseX = x;
                this.dragMouseY = y;

                this.canvas.style.cursor = "grab";
                this.onStateChange({
                    selectedCell: null,
                    selectedColumn: null,
                    selectedRow: row,
                    isDraggingRow: true,
                    dragSourceIndex: row,
                    dragTargetIndex: row,
                    dragMouseX: x,
                    dragMouseY: y,
                });
                this.onRender();
                e.preventDefault();
                return;
            }
        }

        // Fill handle click - don't change selection, just prepare for drag
        if (this.isPointInFillHandle(x, y)) {
            // TODO: Start fill drag (Phase 3)
            e.preventDefault();
            return;
        }

        // Cell selection
        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            this.selectedColumn = null;
            this.selectedRow = null;

            const col = this.getColAtX(x);
            const row = this.getRowAtY(y);

            if (col >= 0 && row >= 0) {
                this.selectedCell = { col, row };
                this.onStateChange({
                    selectedCell: { col, row },
                    selectedColumn: null,
                    selectedRow: null,
                });
                this.onRender();
            }
        }
    }

    private _handleMouseMove(e: MouseEvent): void {
        if (!this.sheetInfo) return;
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;

        if (this.isResizing) {
            const delta = e.clientX - this.resizeStartX;
            const newWidth = Math.max(
                20,
                Math.min(1000, this.resizeStartWidth + delta),
            );

            let colX = HEADER_WIDTH - this.scrollX;
            for (let i = 0; i < this.resizeColIndex; i++) {
                colX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
            }
            this.resizePreviewX = colX + newWidth;

            this.onStateChange({
                isResizing: true,
                resizePreviewX: this.resizePreviewX,
            });
            this.onRender();
        } else if (this.isDraggingColumn) {
            this.dragTargetIndex = this.getDropTargetCol(x);
            this.dragMouseX = x;
            this.dragMouseY = y;

            this.canvas.style.cursor = "grabbing";
            this.onStateChange({
                dragTargetIndex: this.dragTargetIndex,
                dragMouseX: x,
                dragMouseY: y,
            });
            this.onRender();
        } else if (this.isDraggingRow) {
            this.dragTargetIndex = this.getDropTargetRow(y);
            this.dragMouseX = x;
            this.dragMouseY = y;

            this.canvas.style.cursor = "grabbing";
            this.onStateChange({
                dragTargetIndex: this.dragTargetIndex,
                dragMouseX: x,
                dragMouseY: y,
            });
            this.onRender();
        } else {
            // Determine cursor based on position
            if (this.isPointInFillHandle(x, y)) {
                // Fill handle cursor (crosshair for drag-to-fill)
                this.canvas.style.cursor = "crosshair";
            } else if (y < HEADER_HEIGHT && y > 0 && x > HEADER_WIDTH) {
                // Resize cursor
                const resizeCol = this.getResizeHandleCol(x);
                this.canvas.style.cursor =
                    resizeCol >= 0 ? "col-resize" : "default";
            } else {
                this.canvas.style.cursor = "default";
            }
        }
    }

    /**
     * Check if a point is over the fill handle
     */
    isPointInFillHandle(x: number, y: number): boolean {
        if (!this.fillHandleBounds) return false;
        const { x: hx, y: hy, width, height } = this.fillHandleBounds;
        // Add padding for easier targeting
        const padding = 3;
        return (
            x >= hx - padding &&
            x <= hx + width + padding &&
            y >= hy - padding &&
            y <= hy + height + padding
        );
    }

    private async _handleMouseUp(e: MouseEvent): Promise<void> {
        if (this.isResizing) {
            const delta = e.clientX - this.resizeStartX;
            const newWidth = Math.max(
                20,
                Math.min(1000, this.resizeStartWidth + delta),
            );

            this.colWidths.set(this.resizeColIndex, newWidth);
            const colId = this.getColumnId(this.resizeColIndex);

            await this.onResizeColumn(this.resizeColIndex, colId, newWidth);

            this.isResizing = false;
            this.resizeColIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({ isResizing: false });
            this.onRender();
        } else if (this.isDraggingColumn) {
            if (
                this.dragSourceIndex !== this.dragTargetIndex &&
                this.dragSourceIndex !== this.dragTargetIndex - 1
            ) {
                const colId = this.getColumnId(this.dragSourceIndex);
                if (colId) {
                    const newSelectedCol =
                        this.dragTargetIndex > this.dragSourceIndex
                            ? this.dragTargetIndex - 1
                            : this.dragTargetIndex;
                    await this.onMoveColumn(
                        colId,
                        this.dragTargetIndex,
                        newSelectedCol,
                    );
                }
            }

            this.isDraggingColumn = false;
            this.dragSourceIndex = -1;
            this.dragTargetIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({
                isDraggingColumn: false,
                dragSourceIndex: -1,
                dragTargetIndex: -1,
            });
            this.onRender();
        } else if (this.isDraggingRow) {
            if (
                this.dragSourceIndex !== this.dragTargetIndex &&
                this.dragSourceIndex !== this.dragTargetIndex - 1
            ) {
                const rowId = this.getRowId(this.dragSourceIndex);
                if (rowId) {
                    const newSelectedRow =
                        this.dragTargetIndex > this.dragSourceIndex
                            ? this.dragTargetIndex - 1
                            : this.dragTargetIndex;
                    await this.onMoveRow(
                        rowId,
                        this.dragTargetIndex,
                        newSelectedRow,
                    );
                }
            }

            this.isDraggingRow = false;
            this.dragSourceIndex = -1;
            this.dragTargetIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({
                isDraggingRow: false,
                dragSourceIndex: -1,
                dragTargetIndex: -1,
            });
            this.onRender();
        }
    }

    private _handleMouseLeave(): void {
        if (this.isResizing) {
            this.isResizing = false;
            this.resizeColIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({ isResizing: false });
            this.onRender();
        } else if (this.isDraggingColumn) {
            this.isDraggingColumn = false;
            this.dragSourceIndex = -1;
            this.dragTargetIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({
                isDraggingColumn: false,
                dragSourceIndex: -1,
                dragTargetIndex: -1,
            });
            this.onRender();
        } else if (this.isDraggingRow) {
            this.isDraggingRow = false;
            this.dragSourceIndex = -1;
            this.dragTargetIndex = -1;
            this.canvas.style.cursor = "default";
            this.onStateChange({
                isDraggingRow: false,
                dragSourceIndex: -1,
                dragTargetIndex: -1,
            });
            this.onRender();
        }
    }

    private _handleDoubleClick(e: MouseEvent): void {
        if (!this.sheetInfo) return;
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;

        if (x > HEADER_WIDTH && y > HEADER_HEIGHT) {
            const col = this.getColAtX(x);
            const row = this.getRowAtY(y);

            if (col >= 0 && row >= 0) {
                this.selectedCell = { col, row };
                this.onStateChange({ selectedCell: { col, row } });
                this.onRender();
                this.onStartEditing();
            }
        }
    }

    private _handleKeyDown(e: KeyboardEvent): void {
        if (this.isEditing) return;
        if (!this.selectedCell || !this.sheetInfo) return;

        let newCol = this.selectedCell.col;
        let newRow = this.selectedCell.row;

        switch (e.key) {
            case "ArrowUp":
                newRow = Math.max(0, this.selectedCell.row - 1);
                break;
            case "ArrowDown":
                newRow = Math.min(
                    this.sheetInfo.rowCount - 1,
                    this.selectedCell.row + 1,
                );
                break;
            case "ArrowLeft":
                newCol = Math.max(0, this.selectedCell.col - 1);
                break;
            case "ArrowRight":
                newCol = Math.min(
                    this.sheetInfo.colCount - 1,
                    this.selectedCell.col + 1,
                );
                break;
            case "Tab":
                e.preventDefault();
                if (e.shiftKey) {
                    newCol = Math.max(0, this.selectedCell.col - 1);
                } else {
                    newCol = Math.min(
                        this.sheetInfo.colCount - 1,
                        this.selectedCell.col + 1,
                    );
                }
                break;
            case "Enter":
            case "F2":
                e.preventDefault();
                this.onStartEditing();
                return;
            default:
                return;
        }

        this.selectedCell = { col: newCol, row: newRow };

        // Scroll to keep selection visible
        let selX = HEADER_WIDTH;
        for (let i = 0; i < newCol; i++) {
            selX += this.colWidths.get(i) || DEFAULT_COL_WIDTH;
        }
        let selY = HEADER_HEIGHT;
        for (let i = 0; i < newRow; i++) {
            selY += this.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
        }
        const selW = this.colWidths.get(newCol) || DEFAULT_COL_WIDTH;
        const selH = this.rowHeights.get(newRow) || DEFAULT_ROW_HEIGHT;

        const viewWidth = this.canvas.clientWidth;
        const viewHeight = this.canvas.clientHeight;

        if (selX - this.scrollX < HEADER_WIDTH) {
            this.scrollX = selX - HEADER_WIDTH;
        } else if (selX + selW - this.scrollX > viewWidth) {
            this.scrollX = selX + selW - viewWidth;
        }

        if (selY - this.scrollY < HEADER_HEIGHT) {
            this.scrollY = selY - HEADER_HEIGHT;
        } else if (selY + selH - this.scrollY > viewHeight) {
            this.scrollY = selY + selH - viewHeight;
        }

        this.onStateChange({
            selectedCell: this.selectedCell,
            scrollX: this.scrollX,
            scrollY: this.scrollY,
        });
        this.onRender();
        this.onFetchViewport();
    }
}
