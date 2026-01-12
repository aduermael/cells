// =============================================================================
// Grid Renderer
// =============================================================================
//
// Canvas rendering for the spreadsheet grid. Draws cells, headers, selection,
// formula highlights, and remote user presence indicators.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Render cell content (text, numbers, formatted values)
// - Draw column/row headers with resize handles
// - Draw selection highlighting (cell, range, column, row)
// - Draw fill handle and fill preview during drag-fill
// - Render formula reference highlights during editing
// - Show remote user cursors and selections (collaboration)
//
// Architecture:
// - GridRenderer coordinates sub-renderers (headers, selection, presence)
// - Uses viewport culling to only draw visible cells
// - Supports frozen rows/columns (not yet implemented)
// - Theme-aware colors via getGridColors()
//
// =============================================================================

import type { SheetInfo, CellData, Position } from "./types.js";
import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  CELL_PADDING,
  getGridColors,
  type GridColors,
  type NormalizedRange,
  type RemotePresenceRender,
  type GridRendererState,
  type FormulaHighlight,
} from "./grid-constants.js";
import { drawRemotePresence } from "./grid-presence-renderer.js";
import {
  drawRangeSelection,
  drawSingleCellSelection,
  drawColumnSelection,
  drawRowSelection,
  drawFillHandle,
  drawFillPreview,
  drawSpillRangeHighlight,
  type FillHandleBounds,
} from "./grid-selection-renderer.js";
import {
  drawColumnHeaders,
  drawRowHeaders,
  getDragAdjustedColX as getColX,
  getDragAdjustedRowY as getRowY,
  colToLetter,
  getColumnHeaderText,
  type HeaderRendererState,
} from "./grid-header-renderer.js";
import { drawFormulaHighlights } from "./grid-formula-renderer.js";

// Re-export constants and types for backwards compatibility
export {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  CELL_PADDING,
  PRIMARY_COLOR,
  SECONDARY_COLOR,
  COLORS,
  getGridColors,
  FORMULA_REF_COLORS,
  FORMULA_ERROR_COLOR,
  SPILL_RANGE_COLOR,
  type GridColors,
  type NormalizedRange,
  type RemotePresenceRender,
  type GridRendererState,
  type FormulaHighlight,
  type SpillRangeHighlight,
} from "./grid-constants.js";

/**
 * GridRenderer handles all canvas drawing operations for the spreadsheet
 */
export class GridRenderer {
  canvas: HTMLCanvasElement;
  ctx: CanvasRenderingContext2D;

  // State references (set by setStateRefs)
  sheetInfo: SheetInfo | null = null;
  cells: CellData[] = [];
  columns: Array<{ id: string; pos: number; width: number; name: string }> = [];
  rows: Array<{ id: string; pos: number; height: number; name: string }> = [];
  colWidths: Map<number, number> = new Map();
  rowHeights: Map<number, number> = new Map();
  colNames: Map<number, string> = new Map();
  /** Pre-computed column pixel offsets for O(1) lookups */
  colPixelOffsets: Map<number, number> = new Map();
  /** Pre-computed row pixel offsets for O(1) lookups */
  rowPixelOffsets: Map<number, number> = new Map();
  scrollX = 0;
  scrollY = 0;
  selectedCell: Position | null = null;
  selectedColumn: number | null = null;
  selectedRow: number | null = null;
  selectionStart: Position | null = null;
  selectionEnd: Position | null = null;

  // Drag state
  isDraggingColumn = false;
  isDraggingRow = false;
  dragSourceIndex = -1;
  dragTargetIndex = -1;
  dragMouseX = 0;
  dragMouseY = 0;

  // Resize state
  isResizing = false;
  resizePreviewX = 0;
  isResizingRow = false;
  resizePreviewY = 0;

  // Editing state
  editingColumnIndex = -1;

  // Remote presence state
  remotePresence: RemotePresenceRender[] = [];

  // Virtual scrolling: discovered row count (for drawing rows beyond sheetInfo.rowCount)
  discoveredRows = 100;

  // Theme-aware colors (updated on each render)
  colors: GridColors = getGridColors();

  // Formula reference highlights state
  formulaHighlights: FormulaHighlight[] = [];

  // Index of hovered formula reference (-1 = none)
  hoveredFormulaRefIndex = -1;

  // Fill handle bounds (for hit testing in grid events)
  fillHandleBounds: FillHandleBounds | null = null;

  // Fill handle drag state
  isFillDragging = false;
  fillPreviewRange: { minCol: number; maxCol: number; minRow: number; maxRow: number } | null = null;

  // Spill range highlight state (for dynamic array formulas)
  spillRangeHighlight: { minCol: number; maxCol: number; minRow: number; maxRow: number; masterCol: number; masterRow: number } | null = null;

  // Zoom scale (100 = 100%, range 10-400)
  private _zoomScale = 100;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    const ctx = canvas.getContext("2d");
    if (!ctx) {
      throw new Error("Failed to get 2D rendering context");
    }
    this.ctx = ctx;
  }

  /** Update state references from the main application */
  setStateRefs(state: GridRendererState): void {
    Object.assign(this, state);
    // If sheetInfo has a zoom level, sync it
    if (state.sheetInfo?.zoomScale !== undefined) {
      this.setZoomScale(state.sheetInfo.zoomScale);
    }
  }

  /** Get the current zoom scale (10-400) */
  getZoomScale(): number {
    return this._zoomScale;
  }

  /**
   * Set the zoom scale (10-400)
   * Applies CSS transform to the canvas for visual zoom
   */
  setZoomScale(scale: number): void {
    // Clamp to valid range
    scale = Math.max(10, Math.min(400, scale));
    if (this._zoomScale === scale) return;

    this._zoomScale = scale;

    // Apply CSS transform for zoom effect
    // transform-origin is top-left so zoom expands to bottom-right
    const factor = scale / 100;
    this.canvas.style.transformOrigin = "top left";
    this.canvas.style.transform = `scale(${factor})`;
  }

  /**
   * Convert screen coordinates to canvas coordinates accounting for zoom
   * Use this when processing mouse events
   */
  screenToCanvas(screenX: number, screenY: number): { x: number; y: number } {
    const factor = this._zoomScale / 100;
    return {
      x: screenX / factor,
      y: screenY / factor,
    };
  }

  /** Resize the canvas to fit its container */
  resizeCanvas(): void {
    const container = this.canvas.parentElement;
    if (!container) return;

    const dpr = window.devicePixelRatio || 1;
    this.canvas.width = container.clientWidth * dpr;
    this.canvas.height = container.clientHeight * dpr;
    this.canvas.style.width = container.clientWidth + "px";
    this.canvas.style.height = container.clientHeight + "px";
    this.ctx.scale(dpr, dpr);
    this.render();
  }

  /** Convert column index to Excel-style letter */
  colToLetter(col: number): string {
    return colToLetter(col);
  }

  /** Get display text for a column header */
  getColumnHeaderText(col: number): string {
    return getColumnHeaderText(col, this.colNames);
  }

  /** Get normalized range selection (min/max coordinates) */
  getNormalizedRange(): NormalizedRange | null {
    if (!this.selectionStart || !this.selectionEnd) return null;
    return {
      minCol: Math.min(this.selectionStart.col, this.selectionEnd.col),
      maxCol: Math.max(this.selectionStart.col, this.selectionEnd.col),
      minRow: Math.min(this.selectionStart.row, this.selectionEnd.row),
      maxRow: Math.max(this.selectionStart.row, this.selectionEnd.row),
    };
  }

  /** Check if we have a multi-cell range selection */
  hasRangeSelection(): boolean {
    if (!this.selectionStart || !this.selectionEnd) return false;
    return (
      this.selectionStart.col !== this.selectionEnd.col ||
      this.selectionStart.row !== this.selectionEnd.row
    );
  }

  /** Get state object for header renderer functions */
  private _getHeaderState(): HeaderRendererState {
    return {
      sheetInfo: this.sheetInfo,
      scrollX: this.scrollX,
      scrollY: this.scrollY,
      colWidths: this.colWidths,
      rowHeights: this.rowHeights,
      colNames: this.colNames,
      colPixelOffsets: this.colPixelOffsets,
      rowPixelOffsets: this.rowPixelOffsets,
      selectedColumn: this.selectedColumn,
      selectedRow: this.selectedRow,
      selectedCell: this.selectedCell,
      isDraggingColumn: this.isDraggingColumn,
      isDraggingRow: this.isDraggingRow,
      dragSourceIndex: this.dragSourceIndex,
      dragTargetIndex: this.dragTargetIndex,
      editingColumnIndex: this.editingColumnIndex,
      discoveredRows: this.discoveredRows,
    };
  }

  /** Get visual X position for column during drag */
  getDragAdjustedColX(col: number): number {
    return getColX(col, this._getHeaderState());
  }

  /** Get visual Y position for row during drag */
  getDragAdjustedRowY(row: number): number {
    return getRowY(row, this._getHeaderState());
  }

  /** Main render function - draws the entire grid */
  render(): void {
    if (!this.sheetInfo) return;

    const container = this.canvas.parentElement;
    if (!container) return;

    // Refresh theme colors at start of each render
    this.colors = getGridColors();

    const viewWidth = container.clientWidth;
    const viewHeight = container.clientHeight;
    const ctx = this.ctx;

    ctx.clearRect(0, 0, viewWidth, viewHeight);

    // Draw cells area (clipped)
    ctx.save();
    ctx.beginPath();
    ctx.rect(HEADER_WIDTH, HEADER_HEIGHT, viewWidth - HEADER_WIDTH, viewHeight - HEADER_HEIGHT);
    ctx.clip();

    // Fill cell background explicitly (ensures correct theme color on theme switch)
    ctx.fillStyle = this.colors.cellBg;
    ctx.fillRect(HEADER_WIDTH, HEADER_HEIGHT, viewWidth - HEADER_WIDTH, viewHeight - HEADER_HEIGHT);

    const headerState = this._getHeaderState();
    const colHasMoved =
      this.isDraggingColumn &&
      this.dragTargetIndex !== this.dragSourceIndex &&
      this.dragTargetIndex !== this.dragSourceIndex + 1;
    const rowHasMoved =
      this.isDraggingRow &&
      this.dragTargetIndex !== this.dragSourceIndex &&
      this.dragTargetIndex !== this.dragSourceIndex + 1;

    // Cell background colors (drawn before grid lines so lines appear on top)
    this._drawCellBackgrounds(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);

    // Grid lines
    this._drawGridLines(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);

    // Cell values
    this._drawCellValues(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);

    // Formula reference highlights (drawn before selection so selection appears on top)
    if (this.formulaHighlights.length > 0) {
      const formulaState = {
        scrollX: this.scrollX,
        scrollY: this.scrollY,
        colWidths: this.colWidths,
        rowHeights: this.rowHeights,
        colPixelOffsets: this.colPixelOffsets,
        rowPixelOffsets: this.rowPixelOffsets,
        formulaHighlights: this.formulaHighlights,
        hoveredFormulaRefIndex: this.hoveredFormulaRefIndex,
      };
      drawFormulaHighlights(ctx, formulaState, viewWidth, viewHeight);
    }

    // Spill range highlight (drawn before selection so it appears behind)
    if (this.spillRangeHighlight) {
      const spillState = {
        scrollX: this.scrollX,
        scrollY: this.scrollY,
        colWidths: this.colWidths,
        rowHeights: this.rowHeights,
        selectionStart: this.selectionStart,
        selectionEnd: this.selectionEnd,
        selectedCell: this.selectedCell,
      };
      drawSpillRangeHighlight(ctx, spillState, this.spillRangeHighlight, viewWidth, viewHeight);
    }

    // Column/row selection highlights
    if (this.selectedColumn !== null && !this.isDraggingColumn) {
      drawColumnSelection(ctx, this.scrollX, this.colWidths, this.selectedColumn, viewWidth, viewHeight);
    }
    if (this.selectedRow !== null && !this.isDraggingRow) {
      drawRowSelection(ctx, this.scrollY, this.rowHeights, this.selectedRow, viewWidth, viewHeight);
    }

    // Cell/Range selection
    const range = this.getNormalizedRange();
    const selState = {
      scrollX: this.scrollX,
      scrollY: this.scrollY,
      colWidths: this.colWidths,
      rowHeights: this.rowHeights,
      selectionStart: this.selectionStart,
      selectionEnd: this.selectionEnd,
      selectedCell: this.selectedCell,
    };
    if (range) {
      drawRangeSelection(ctx, selState, range, viewWidth, viewHeight);
      // Don't show fill handle during fill drag (it's being dragged)
      if (!this.isFillDragging) {
        this.fillHandleBounds = drawFillHandle(ctx, selState, range, viewWidth, viewHeight);
      } else {
        this.fillHandleBounds = null;
      }
    } else if (this.selectedCell) {
      drawSingleCellSelection(ctx, selState, viewWidth, viewHeight);
      // Don't show fill handle during fill drag
      if (!this.isFillDragging) {
        this.fillHandleBounds = drawFillHandle(ctx, selState, undefined, viewWidth, viewHeight);
      } else {
        this.fillHandleBounds = null;
      }
    } else {
      this.fillHandleBounds = null;
    }

    // Draw fill preview (dashed border during fill handle drag)
    if (this.isFillDragging && this.fillPreviewRange) {
      drawFillPreview(ctx, selState, this.fillPreviewRange, viewWidth, viewHeight);
    }

    ctx.restore();

    // Draw headers
    drawColumnHeaders(ctx, headerState, viewWidth, colHasMoved, range);
    drawRowHeaders(ctx, headerState, viewHeight, rowHasMoved, range);

    // Corner and header borders
    ctx.fillStyle = this.colors.cornerBg;
    ctx.fillRect(0, 0, HEADER_WIDTH, HEADER_HEIGHT);

    ctx.strokeStyle = this.colors.headerBorder;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, HEADER_HEIGHT + 0.5);
    ctx.lineTo(viewWidth, HEADER_HEIGHT + 0.5);
    ctx.moveTo(HEADER_WIDTH + 0.5, 0);
    ctx.lineTo(HEADER_WIDTH + 0.5, viewHeight);
    ctx.stroke();

    // Draw freeze pane separator lines
    this._drawFreezePaneSeparators(ctx, viewWidth, viewHeight);
  }

  /** Draw thick separator lines to indicate freeze pane boundaries */
  private _drawFreezePaneSeparators(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    viewHeight: number
  ): void {
    if (!this.sheetInfo) return;

    const freezeCol = this.sheetInfo.freezeCol || 0;
    const freezeRow = this.sheetInfo.freezeRow || 0;

    if (freezeCol === 0 && freezeRow === 0) return;

    // Calculate freeze pane boundaries
    // Frozen columns start at HEADER_WIDTH (column header area)
    let freezeColX = HEADER_WIDTH;
    for (let col = 0; col < freezeCol; col++) {
      freezeColX += this.colWidths.get(col) || DEFAULT_COL_WIDTH;
    }

    // Frozen rows start at HEADER_HEIGHT (row header area)
    let freezeRowY = HEADER_HEIGHT;
    for (let row = 0; row < freezeRow; row++) {
      freezeRowY += this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    }

    // Draw thick separator lines
    ctx.save();
    ctx.strokeStyle = this.colors.headerBorder;
    ctx.lineWidth = 2;

    if (freezeCol > 0) {
      // Vertical separator after frozen columns
      ctx.beginPath();
      ctx.moveTo(freezeColX + 0.5, HEADER_HEIGHT);
      ctx.lineTo(freezeColX + 0.5, viewHeight);
      ctx.stroke();
    }

    if (freezeRow > 0) {
      // Horizontal separator after frozen rows
      ctx.beginPath();
      ctx.moveTo(HEADER_WIDTH, freezeRowY + 0.5);
      ctx.lineTo(viewWidth, freezeRowY + 0.5);
      ctx.stroke();
    }

    ctx.restore();
  }

  /** Draw cell background colors */
  private _drawCellBackgrounds(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    viewHeight: number,
    colHasMoved: boolean,
    rowHasMoved: boolean,
    headerState: HeaderRendererState
  ): void {
    for (const cell of this.cells) {
      if (colHasMoved && cell.col === this.dragSourceIndex) continue;
      if (rowHasMoved && cell.row === this.dragSourceIndex) continue;

      // Skip merged cells (not anchors) - their background is drawn by the anchor
      if (cell.isMergedCell) continue;

      const bgColor = cell.style?.bgColor;
      if (!bgColor) continue;

      const cellX = getColX(cell.col, headerState);
      const cellY = getRowY(cell.row, headerState);

      // Calculate width/height (possibly spanning multiple cells for merged regions)
      let totalWidth = 0;
      let totalHeight = 0;
      if (cell.isMergeAnchor && cell.mergeColSpan && cell.mergeRowSpan) {
        // Sum up widths for all columns in the merge
        for (let c = 0; c < cell.mergeColSpan; c++) {
          totalWidth += this.colWidths.get(cell.col + c) || DEFAULT_COL_WIDTH;
        }
        // Sum up heights for all rows in the merge
        for (let r = 0; r < cell.mergeRowSpan; r++) {
          totalHeight += this.rowHeights.get(cell.row + r) || DEFAULT_ROW_HEIGHT;
        }
      } else {
        totalWidth = this.colWidths.get(cell.col) || DEFAULT_COL_WIDTH;
        totalHeight = this.rowHeights.get(cell.row) || DEFAULT_ROW_HEIGHT;
      }

      if (cellX + totalWidth < HEADER_WIDTH || cellX > viewWidth) continue;
      if (cellY + totalHeight < HEADER_HEIGHT || cellY > viewHeight) continue;

      ctx.fillStyle = bgColor;
      ctx.fillRect(cellX + 1, cellY + 1, totalWidth - 1, totalHeight - 1);
    }
  }

  /** Draw grid lines */
  private _drawGridLines(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    viewHeight: number,
    colHasMoved: boolean,
    rowHasMoved: boolean,
    headerState: HeaderRendererState
  ): void {
    if (!this.sheetInfo) return;

    // Respect showGridLines sheet property
    if (this.sheetInfo.showGridLines === false) return;

    ctx.strokeStyle = this.colors.gridLine;
    ctx.lineWidth = 1;

    // Calculate visible column range (approximate, then refine)
    const startCol = Math.max(0, Math.floor(this.scrollX / DEFAULT_COL_WIDTH) - 1);
    const endCol = Math.min(
      this.sheetInfo.colCount,
      startCol + Math.ceil(viewWidth / DEFAULT_COL_WIDTH) + 2
    );

    // Vertical lines - only iterate through visible columns
    for (let col = startCol; col <= endCol; col++) {
      if (colHasMoved && col === this.dragSourceIndex) continue;
      const lineX = getColX(col, headerState) + 0.5;
      if (lineX >= HEADER_WIDTH && lineX < viewWidth) {
        ctx.beginPath();
        ctx.moveTo(lineX, HEADER_HEIGHT);
        ctx.lineTo(lineX, viewHeight);
        ctx.stroke();
      }
    }

    // Calculate visible row range
    const rowCount = Math.max(this.sheetInfo.rowCount, this.discoveredRows);
    const startRow = Math.max(0, Math.floor(this.scrollY / DEFAULT_ROW_HEIGHT) - 1);
    const endRow = Math.min(
      rowCount,
      startRow + Math.ceil(viewHeight / DEFAULT_ROW_HEIGHT) + 2
    );

    // Horizontal lines - only iterate through visible rows
    for (let row = startRow; row <= endRow; row++) {
      if (rowHasMoved && row === this.dragSourceIndex) continue;
      const lineY = getRowY(row, headerState) + 0.5;
      if (lineY >= HEADER_HEIGHT && lineY < viewHeight) {
        ctx.beginPath();
        ctx.moveTo(HEADER_WIDTH, lineY);
        ctx.lineTo(viewWidth, lineY);
        ctx.stroke();
      }
    }
  }

  /** Draw cell values */
  private _drawCellValues(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    viewHeight: number,
    colHasMoved: boolean,
    rowHasMoved: boolean,
    headerState: HeaderRendererState
  ): void {
    for (const cell of this.cells) {
      if (colHasMoved && cell.col === this.dragSourceIndex) continue;
      if (rowHasMoved && cell.row === this.dragSourceIndex) continue;

      // Skip merged cells (not anchors) - their content is drawn by the anchor
      if (cell.isMergedCell) continue;

      const cellX = getColX(cell.col, headerState);
      const cellY = getRowY(cell.row, headerState);

      // Calculate width/height (possibly spanning multiple cells for merged regions)
      let colWidth = 0;
      let rowHeight = 0;
      if (cell.isMergeAnchor && cell.mergeColSpan && cell.mergeRowSpan) {
        // Sum up widths for all columns in the merge
        for (let c = 0; c < cell.mergeColSpan; c++) {
          colWidth += this.colWidths.get(cell.col + c) || DEFAULT_COL_WIDTH;
        }
        // Sum up heights for all rows in the merge
        for (let r = 0; r < cell.mergeRowSpan; r++) {
          rowHeight += this.rowHeights.get(cell.row + r) || DEFAULT_ROW_HEIGHT;
        }
      } else {
        colWidth = this.colWidths.get(cell.col) || DEFAULT_COL_WIDTH;
        rowHeight = this.rowHeights.get(cell.row) || DEFAULT_ROW_HEIGHT;
      }

      if (cellX + colWidth < HEADER_WIDTH || cellX > viewWidth) continue;
      if (cellY + rowHeight < HEADER_HEIGHT || cellY > viewHeight) continue;

      const displayValue = cell.display || cell.value || "";
      const style = cell.style;

      ctx.save();
      ctx.beginPath();
      ctx.rect(cellX + 1, cellY + 1, colWidth - 2, rowHeight - 2);
      ctx.clip();

      // Set text color
      ctx.fillStyle = style?.textColor || this.colors.cellText;

      // Build font string with style properties
      let fontStyle = "";
      if (style?.italic) fontStyle += "italic ";
      if (style?.bold) fontStyle += "bold ";
      const fontSize = style?.fontSize || 13;
      const fontFamily = style?.fontFamily || '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
      ctx.font = fontStyle ? `${fontStyle}${fontSize}px ${fontFamily}` : `${fontSize}px ${fontFamily}`;

      // Set horizontal alignment
      // Content area is inset by 1px on each side for grid lines
      const hAlign = style?.hAlign || "left";
      let textX: number;
      if (hAlign === "center") {
        ctx.textAlign = "center";
        // Center within the content area (between grid lines: cellX+1 to cellX+colWidth-1)
        // Round to nearest pixel for crisp rendering
        textX = Math.round(cellX + colWidth / 2);
      } else if (hAlign === "right") {
        ctx.textAlign = "right";
        textX = cellX + colWidth - CELL_PADDING;
      } else {
        ctx.textAlign = "left";
        textX = cellX + CELL_PADDING;
      }

      // Set vertical alignment (default matches CellStyle in C++)
      const vAlign = style?.vAlign || "bottom";
      let textY: number;
      if (vAlign === "top") {
        ctx.textBaseline = "top";
        textY = cellY + 2; // Small padding from top
      } else if (vAlign === "bottom") {
        ctx.textBaseline = "bottom";
        textY = cellY + rowHeight - 2; // Small padding from bottom
      } else {
        ctx.textBaseline = "middle";
        textY = cellY + rowHeight / 2;
      }

      ctx.fillText(displayValue, textX, textY);

      // Draw underline if enabled
      if (style?.underline) {
        const textMetrics = ctx.measureText(displayValue);
        let underlineX: number;
        if (hAlign === "center") {
          underlineX = textX - textMetrics.width / 2;
        } else if (hAlign === "right") {
          underlineX = textX - textMetrics.width;
        } else {
          underlineX = textX;
        }
        // Adjust Y position based on vertical alignment
        let underlineY: number;
        if (vAlign === "top") {
          underlineY = textY + fontSize + 1;
        } else if (vAlign === "bottom") {
          underlineY = textY + 1;
        } else {
          underlineY = textY + fontSize / 2 + 1;
        }
        ctx.beginPath();
        ctx.strokeStyle = style?.textColor || this.colors.cellText;
        ctx.lineWidth = 1;
        ctx.moveTo(underlineX, underlineY);
        ctx.lineTo(underlineX + textMetrics.width, underlineY);
        ctx.stroke();
      }

      ctx.restore();
    }
  }

  /** Draw the resize preview line */
  drawResizePreview(): void {
    const container = this.canvas.parentElement;
    if (!container) return;

    const viewWidth = container.clientWidth;
    const viewHeight = container.clientHeight;
    const ctx = this.ctx;

    ctx.save();
    ctx.strokeStyle = this.colors.selectionBorder;
    ctx.lineWidth = 2;
    ctx.setLineDash([4, 4]);

    if (this.isResizing) {
      ctx.beginPath();
      ctx.moveTo(this.resizePreviewX + 0.5, 0);
      ctx.lineTo(this.resizePreviewX + 0.5, viewHeight);
      ctx.stroke();
    }

    if (this.isResizingRow) {
      ctx.beginPath();
      ctx.moveTo(0, this.resizePreviewY + 0.5);
      ctx.lineTo(viewWidth, this.resizePreviewY + 0.5);
      ctx.stroke();
    }

    ctx.setLineDash([]);
    ctx.restore();
  }

  /** Draw the drag ghost for column/row reordering */
  drawDragGhost(): void {
    if (!this.isDraggingColumn && !this.isDraggingRow) return;

    const container = this.canvas.parentElement;
    if (!container) return;

    const viewWidth = container.clientWidth;
    const viewHeight = container.clientHeight;
    const ctx = this.ctx;

    ctx.save();
    ctx.globalAlpha = 0.6;

    if (this.isDraggingColumn) {
      this._drawColumnDragGhost(ctx, viewWidth, viewHeight);
    } else if (this.isDraggingRow) {
      this._drawRowDragGhost(ctx, viewWidth, viewHeight);
    }

    ctx.restore();
  }

  private _drawColumnDragGhost(
    ctx: CanvasRenderingContext2D,
    _viewWidth: number,
    viewHeight: number
  ): void {
    const colW = this.colWidths.get(this.dragSourceIndex) || DEFAULT_COL_WIDTH;
    const ghostX = this.dragMouseX - colW / 2;

    ctx.fillStyle = this.colors.selectionBorder;
    ctx.fillRect(ghostX, 0, colW, HEADER_HEIGHT);

    ctx.fillStyle = "#fff";
    ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(this.getColumnHeaderText(this.dragSourceIndex), ghostX + colW / 2, HEADER_HEIGHT / 2);

    ctx.fillStyle = this.colors.selectionBg;
    ctx.fillRect(ghostX, HEADER_HEIGHT, colW, viewHeight - HEADER_HEIGHT);

    ctx.strokeStyle = this.colors.selectionBorder;
    ctx.lineWidth = 2;
    ctx.strokeRect(ghostX, 0, colW, viewHeight);
  }

  private _drawRowDragGhost(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    _viewHeight: number
  ): void {
    const rowH = this.rowHeights.get(this.dragSourceIndex) || DEFAULT_ROW_HEIGHT;
    const ghostY = this.dragMouseY - rowH / 2;

    ctx.fillStyle = this.colors.selectionBorder;
    ctx.fillRect(0, ghostY, HEADER_WIDTH, rowH);

    ctx.fillStyle = "#fff";
    ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(String(this.dragSourceIndex + 1), HEADER_WIDTH / 2, ghostY + rowH / 2);

    ctx.fillStyle = this.colors.selectionBg;
    ctx.fillRect(HEADER_WIDTH, ghostY, viewWidth - HEADER_WIDTH, rowH);

    ctx.strokeStyle = this.colors.selectionBorder;
    ctx.lineWidth = 2;
    ctx.strokeRect(0, ghostY, viewWidth, rowH);
  }

  /** Draw remote user presence (cursors and selections) */
  drawRemotePresence(): void {
    if (!this.sheetInfo || this.remotePresence.length === 0) return;

    drawRemotePresence(
      this.ctx,
      this.canvas,
      {
        scrollX: this.scrollX,
        scrollY: this.scrollY,
        colWidths: this.colWidths,
        rowHeights: this.rowHeights,
      },
      this.remotePresence
    );
  }
}
