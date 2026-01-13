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

import type { SheetInfo, CellData, Position, BorderStyle } from "./types.js";
import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
  getZoomedHeaderHeight,
  getZoomedHeaderWidth,
  getZoomedColWidth,
  getZoomedRowHeight,
  getZoomedCellPadding,
  getZoomedFontSize,
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
  getFrozenColWidth,
  getFrozenRowHeight,
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
  // Track if zoom has been initialized from sheetInfo (only sync once)
  private _zoomInitialized = false;

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
    // Only sync zoom from sheetInfo on first call (initial load)
    // After that, UI-modified zoom should persist
    if (!this._zoomInitialized && state.sheetInfo?.zoomScale !== undefined) {
      this.setZoomScale(state.sheetInfo.zoomScale);
      this._zoomInitialized = true;
    }
  }

  /** Get the current zoom scale (10-400) */
  getZoomScale(): number {
    return this._zoomScale;
  }

  /**
   * Set the zoom scale (10-400)
   * Zoom is applied by scaling dimensions during rendering, not CSS transform.
   * This allows crisp rendering at any zoom level.
   */
  setZoomScale(scale: number): void {
    // Clamp to valid range
    scale = Math.max(10, Math.min(400, scale));
    if (this._zoomScale === scale) return;

    this._zoomScale = scale;

    // Clear any CSS transform (old approach)
    this.canvas.style.transform = "";
    this.canvas.style.transformOrigin = "";

    // Re-render with new zoom scale
    this.render();
  }

  /** Get the zoom factor (1.0 = 100%) for calculations */
  getZoomFactor(): number {
    return this._zoomScale / 100;
  }

  /**
   * Convert screen coordinates to canvas coordinates.
   * With proper zoom (not CSS transform), screen and canvas coordinates are the same
   * since zoom is applied to rendering dimensions, not the canvas element.
   */
  screenToCanvas(screenX: number, screenY: number): { x: number; y: number } {
    // No conversion needed with proper zoom - coordinates are 1:1
    return { x: screenX, y: screenY };
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
      zoomFactor: this.getZoomFactor(),
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
    const zoomFactor = this.getZoomFactor();

    // Calculate zoomed header dimensions
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);

    ctx.clearRect(0, 0, viewWidth, viewHeight);

    // Calculate frozen pane boundaries (using zoomed dimensions)
    const freezeCol = this.sheetInfo.freezeCol || 0;
    const freezeRow = this.sheetInfo.freezeRow || 0;
    const frozenColWidth = getFrozenColWidth(freezeCol, this.colWidths, zoomFactor);
    const frozenRowHeight = getFrozenRowHeight(freezeRow, this.rowHeights, zoomFactor);

    // The freeze boundary positions (where frozen content ends)
    const freezeX = zoomedHeaderWidth + frozenColWidth;
    const freezeY = zoomedHeaderHeight + frozenRowHeight;

    const headerState = this._getHeaderState();
    const colHasMoved =
      this.isDraggingColumn &&
      this.dragTargetIndex !== this.dragSourceIndex &&
      this.dragTargetIndex !== this.dragSourceIndex + 1;
    const rowHasMoved =
      this.isDraggingRow &&
      this.dragTargetIndex !== this.dragSourceIndex &&
      this.dragTargetIndex !== this.dragSourceIndex + 1;

    // Fill cell background explicitly (ensures correct theme color on theme switch)
    ctx.fillStyle = this.colors.cellBg;
    ctx.fillRect(zoomedHeaderWidth, zoomedHeaderHeight, viewWidth - zoomedHeaderWidth, viewHeight - zoomedHeaderHeight);

    // Render the four quadrants of the freeze pane layout:
    // - Q1 (bottom-right): Scrollable in both X and Y
    // - Q2 (top-right): Frozen rows, scrollable columns (scrolls in X only)
    // - Q3 (bottom-left): Frozen columns, scrollable rows (scrolls in Y only)
    // - Q4 (top-left): Fully frozen corner (no scrolling)
    //
    // We render in order Q1 -> Q2 -> Q3 -> Q4 so frozen content is drawn on top.

    // === Q1: Scrollable content (bottom-right) ===
    ctx.save();
    ctx.beginPath();
    ctx.rect(freezeX, freezeY, viewWidth - freezeX, viewHeight - freezeY);
    ctx.clip();

    this._drawCellBackgrounds(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
    this._drawGridLines(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
    this._drawCellBorders(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
    this._drawCellValues(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
    ctx.restore();

    // === Q2: Frozen rows (top-right) ===
    if (freezeRow > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(freezeX, zoomedHeaderHeight, viewWidth - freezeX, frozenRowHeight);
      ctx.clip();

      ctx.fillStyle = this.colors.cellBg;
      ctx.fillRect(freezeX, zoomedHeaderHeight, viewWidth - freezeX, frozenRowHeight);

      this._drawCellBackgrounds(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawGridLines(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellBorders(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellValues(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      ctx.restore();
    }

    // === Q3: Frozen columns (bottom-left) ===
    if (freezeCol > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(zoomedHeaderWidth, freezeY, frozenColWidth, viewHeight - freezeY);
      ctx.clip();

      ctx.fillStyle = this.colors.cellBg;
      ctx.fillRect(zoomedHeaderWidth, freezeY, frozenColWidth, viewHeight - freezeY);

      this._drawCellBackgrounds(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawGridLines(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellBorders(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellValues(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      ctx.restore();
    }

    // === Q4: Frozen corner (top-left) ===
    if (freezeCol > 0 && freezeRow > 0) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(zoomedHeaderWidth, zoomedHeaderHeight, frozenColWidth, frozenRowHeight);
      ctx.clip();

      ctx.fillStyle = this.colors.cellBg;
      ctx.fillRect(zoomedHeaderWidth, zoomedHeaderHeight, frozenColWidth, frozenRowHeight);

      this._drawCellBackgrounds(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawGridLines(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellBorders(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      this._drawCellValues(ctx, viewWidth, viewHeight, colHasMoved, rowHasMoved, headerState);
      ctx.restore();
    }

    // === Draw selection and other overlays (clipped to entire grid area) ===
    ctx.save();
    ctx.beginPath();
    ctx.rect(zoomedHeaderWidth, zoomedHeaderHeight, viewWidth - zoomedHeaderWidth, viewHeight - zoomedHeaderHeight);
    ctx.clip();

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
        zoomFactor,
      };
      drawSpillRangeHighlight(ctx, spillState, this.spillRangeHighlight, viewWidth, viewHeight);
    }

    // Column/row selection highlights
    if (this.selectedColumn !== null && !this.isDraggingColumn) {
      drawColumnSelection(ctx, this.scrollX, this.colWidths, this.selectedColumn, viewWidth, viewHeight, zoomFactor);
    }
    if (this.selectedRow !== null && !this.isDraggingRow) {
      drawRowSelection(ctx, this.scrollY, this.rowHeights, this.selectedRow, viewWidth, viewHeight, zoomFactor);
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
      zoomFactor,
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
    ctx.fillRect(0, 0, zoomedHeaderWidth, zoomedHeaderHeight);

    ctx.strokeStyle = this.colors.headerBorder;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, zoomedHeaderHeight + 0.5);
    ctx.lineTo(viewWidth, zoomedHeaderHeight + 0.5);
    ctx.moveTo(zoomedHeaderWidth + 0.5, 0);
    ctx.lineTo(zoomedHeaderWidth + 0.5, viewHeight);
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

    const zoomFactor = this.getZoomFactor();
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);

    // Calculate freeze pane boundaries
    // Frozen columns start at zoomedHeaderWidth (column header area)
    let freezeColX = zoomedHeaderWidth;
    for (let col = 0; col < freezeCol; col++) {
      const baseWidth = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
      freezeColX += getZoomedColWidth(baseWidth, zoomFactor);
    }

    // Frozen rows start at zoomedHeaderHeight (row header area)
    let freezeRowY = zoomedHeaderHeight;
    for (let row = 0; row < freezeRow; row++) {
      const baseHeight = this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
      freezeRowY += getZoomedRowHeight(baseHeight, zoomFactor);
    }

    // Draw thick separator lines
    ctx.save();
    ctx.strokeStyle = this.colors.headerBorder;
    ctx.lineWidth = 2;

    if (freezeCol > 0) {
      // Vertical separator after frozen columns
      ctx.beginPath();
      ctx.moveTo(freezeColX + 0.5, zoomedHeaderHeight);
      ctx.lineTo(freezeColX + 0.5, viewHeight);
      ctx.stroke();
    }

    if (freezeRow > 0) {
      // Horizontal separator after frozen rows
      ctx.beginPath();
      ctx.moveTo(zoomedHeaderWidth, freezeRowY + 0.5);
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
    const zoomFactor = headerState.zoomFactor;
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);

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
          const baseW = this.colWidths.get(cell.col + c) || DEFAULT_COL_WIDTH;
          totalWidth += getZoomedColWidth(baseW, zoomFactor);
        }
        // Sum up heights for all rows in the merge
        for (let r = 0; r < cell.mergeRowSpan; r++) {
          const baseH = this.rowHeights.get(cell.row + r) || DEFAULT_ROW_HEIGHT;
          totalHeight += getZoomedRowHeight(baseH, zoomFactor);
        }
      } else {
        const baseW = this.colWidths.get(cell.col) || DEFAULT_COL_WIDTH;
        const baseH = this.rowHeights.get(cell.row) || DEFAULT_ROW_HEIGHT;
        totalWidth = getZoomedColWidth(baseW, zoomFactor);
        totalHeight = getZoomedRowHeight(baseH, zoomFactor);
      }

      if (cellX + totalWidth < zoomedHeaderWidth || cellX > viewWidth) continue;
      if (cellY + totalHeight < zoomedHeaderHeight || cellY > viewHeight) continue;

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

    const zoomFactor = headerState.zoomFactor;
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);
    const zoomedColWidth = getZoomedColWidth(DEFAULT_COL_WIDTH, zoomFactor);
    const zoomedRowHeight = getZoomedRowHeight(DEFAULT_ROW_HEIGHT, zoomFactor);

    ctx.strokeStyle = this.colors.gridLine;
    ctx.lineWidth = 1;

    // Calculate visible column range (approximate, then refine)
    const startCol = Math.max(0, Math.floor(this.scrollX / DEFAULT_COL_WIDTH) - 1);
    const endCol = Math.min(
      this.sheetInfo.colCount,
      startCol + Math.ceil(viewWidth / zoomedColWidth) + 2
    );

    // Vertical lines - only iterate through visible columns
    for (let col = startCol; col <= endCol; col++) {
      if (colHasMoved && col === this.dragSourceIndex) continue;
      const lineX = getColX(col, headerState) + 0.5;
      if (lineX >= zoomedHeaderWidth && lineX < viewWidth) {
        ctx.beginPath();
        ctx.moveTo(lineX, zoomedHeaderHeight);
        ctx.lineTo(lineX, viewHeight);
        ctx.stroke();
      }
    }

    // Calculate visible row range
    const rowCount = Math.max(this.sheetInfo.rowCount, this.discoveredRows);
    const startRow = Math.max(0, Math.floor(this.scrollY / DEFAULT_ROW_HEIGHT) - 1);
    const endRow = Math.min(
      rowCount,
      startRow + Math.ceil(viewHeight / zoomedRowHeight) + 2
    );

    // Horizontal lines - only iterate through visible rows
    for (let row = startRow; row <= endRow; row++) {
      if (rowHasMoved && row === this.dragSourceIndex) continue;
      const lineY = getRowY(row, headerState) + 0.5;
      if (lineY >= zoomedHeaderHeight && lineY < viewHeight) {
        ctx.beginPath();
        ctx.moveTo(zoomedHeaderWidth, lineY);
        ctx.lineTo(viewWidth, lineY);
        ctx.stroke();
      }
    }
  }

  /**
   * Represents a unique edge between two cells.
   * For a horizontal edge, col/row refers to the cell above, col2/row2 to the cell below.
   * For a vertical edge, col/row refers to the cell to the left, col2/row2 to the cell to the right.
   */
  private _edgeKey(orientation: "h" | "v", col: number, row: number): string {
    // For horizontal edges: edge is between row-1 and row (top of row = bottom of row-1)
    // For vertical edges: edge is between col-1 and col (left of col = right of col-1)
    return `${orientation}:${col}:${row}`;
  }

  /**
   * Border priority for deduplication - higher value wins.
   * Priority is based on: thickness first, then darkness of color.
   */
  private _getBorderPriority(edge: { style: BorderStyle; color: string }): number {
    const width = this._getBorderWidth(edge.style);
    // Calculate darkness from color (lower RGB sum = darker = higher priority)
    let darkness = 0;
    if (edge.color && edge.color.startsWith("#") && edge.color.length >= 7) {
      const r = parseInt(edge.color.slice(1, 3), 16);
      const g = parseInt(edge.color.slice(3, 5), 16);
      const b = parseInt(edge.color.slice(5, 7), 16);
      // Invert so darker colors have higher darkness value (max 765)
      darkness = 765 - (r + g + b);
    } else {
      // Default black has maximum darkness
      darkness = 765;
    }
    // Combine: width is most important, darkness is tiebreaker
    // width * 1000 ensures width always wins over color
    return width * 1000 + darkness;
  }

  /** Build a map of border edges, keeping only the highest priority border for each edge */
  private _buildBorderEdgeMap(
    colHasMoved: boolean,
    rowHasMoved: boolean
  ): Map<string, { style: BorderStyle; color: string }> {
    const edgeMap = new Map<string, { style: BorderStyle; color: string }>();

    for (const cell of this.cells) {
      if (colHasMoved && cell.col === this.dragSourceIndex) continue;
      if (rowHasMoved && cell.row === this.dragSourceIndex) continue;

      const border = cell.style?.border;
      if (!border) continue;

      // Determine cell dimensions (for merge anchors)
      let colSpan = 1;
      let rowSpan = 1;
      if (cell.isMergeAnchor && cell.mergeColSpan && cell.mergeRowSpan) {
        colSpan = cell.mergeColSpan;
        rowSpan = cell.mergeRowSpan;
      }

      // Top border - horizontal edge at the top of this cell
      if (border.top && border.top.style !== "none") {
        const key = this._edgeKey("h", cell.col, cell.row);
        const priority = this._getBorderPriority(border.top);
        const existing = edgeMap.get(key);
        if (!existing || priority > this._getBorderPriority(existing)) {
          edgeMap.set(key, { style: border.top.style, color: border.top.color || "#000000" });
        }
      }

      // Bottom border - horizontal edge at the bottom of this cell (top of next row)
      if (border.bottom && border.bottom.style !== "none") {
        const key = this._edgeKey("h", cell.col, cell.row + rowSpan);
        const priority = this._getBorderPriority(border.bottom);
        const existing = edgeMap.get(key);
        if (!existing || priority > this._getBorderPriority(existing)) {
          edgeMap.set(key, { style: border.bottom.style, color: border.bottom.color || "#000000" });
        }
      }

      // Left border - vertical edge at the left of this cell
      if (border.left && border.left.style !== "none") {
        const key = this._edgeKey("v", cell.col, cell.row);
        const priority = this._getBorderPriority(border.left);
        const existing = edgeMap.get(key);
        if (!existing || priority > this._getBorderPriority(existing)) {
          edgeMap.set(key, { style: border.left.style, color: border.left.color || "#000000" });
        }
      }

      // Right border - vertical edge at the right of this cell (left of next col)
      if (border.right && border.right.style !== "none") {
        const key = this._edgeKey("v", cell.col + colSpan, cell.row);
        const priority = this._getBorderPriority(border.right);
        const existing = edgeMap.get(key);
        if (!existing || priority > this._getBorderPriority(existing)) {
          edgeMap.set(key, { style: border.right.style, color: border.right.color || "#000000" });
        }
      }
    }

    return edgeMap;
  }

  /** Get line width for a border style */
  private _getBorderWidth(style: BorderStyle): number {
    switch (style) {
      case "thin":
      case "dashed":
      case "dotted":
      case "hair":
        return 1;
      case "medium":
      case "mediumDashed":
      case "dashDot":
      case "mediumDashDot":
      case "dashDotDot":
      case "mediumDashDotDot":
      case "slantDashDot":
        return 2;
      case "thick":
        return 3;
      case "double":
        return 3; // For double, we draw two lines
      default:
        return 0;
    }
  }

  /** Set line dash pattern for a border style */
  private _setBorderDash(ctx: CanvasRenderingContext2D, style: BorderStyle): void {
    switch (style) {
      case "dashed":
      case "mediumDashed":
        ctx.setLineDash([4, 2]);
        break;
      case "dotted":
        ctx.setLineDash([1, 1]);
        break;
      case "dashDot":
      case "mediumDashDot":
        ctx.setLineDash([4, 2, 1, 2]);
        break;
      case "dashDotDot":
      case "mediumDashDotDot":
        ctx.setLineDash([4, 2, 1, 2, 1, 2]);
        break;
      case "hair":
        ctx.setLineDash([1, 1]);
        break;
      default:
        ctx.setLineDash([]);
    }
  }

  /**
   * Draw cell borders using edge deduplication.
   *
   * When adjacent cells both have borders on a shared edge (e.g., cell A has bottom border,
   * cell B below has top border), we only draw one border to avoid double-thickness lines.
   * The border with higher priority (thicker, then darker) wins.
   *
   * Borders are drawn centered on cell edges for proper alignment.
   */
  private _drawCellBorders(
    ctx: CanvasRenderingContext2D,
    viewWidth: number,
    viewHeight: number,
    colHasMoved: boolean,
    rowHasMoved: boolean,
    headerState: HeaderRendererState
  ): void {
    // Build deduplicated edge map
    const edgeMap = this._buildBorderEdgeMap(colHasMoved, rowHasMoved);

    if (edgeMap.size === 0) return;

    const zoomFactor = headerState.zoomFactor;
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);

    // Draw each unique edge once
    for (const [key, edge] of edgeMap) {
      const parts = key.split(":");
      const orientation = parts[0];
      const col = parseInt(parts[1] || "0", 10);
      const row = parseInt(parts[2] || "0", 10);

      const width = this._getBorderWidth(edge.style);
      ctx.strokeStyle = edge.color;
      ctx.lineWidth = width;
      this._setBorderDash(ctx, edge.style);

      if (orientation === "h") {
        // Horizontal edge: draw at the boundary between row-1 and row
        // Edge position is at the top of 'row' (or bottom of 'row-1')
        const y = getRowY(row, headerState);

        // Skip if outside visible area
        if (y < zoomedHeaderHeight - width || y > viewHeight + width) continue;

        // For horizontal edges, we need to determine the span
        // Find the cell that owns this edge to determine its column span
        let startX = getColX(col, headerState);
        const baseColW = this.colWidths.get(col) || DEFAULT_COL_WIDTH;
        let endX = startX + getZoomedColWidth(baseColW, zoomFactor);

        // Check if this edge spans multiple columns (from merged cells)
        // The edge key uses the anchor cell's column, but we need to find the actual span
        for (const cell of this.cells) {
          if (cell.col !== col) continue;
          const cellRowSpan = cell.isMergeAnchor && cell.mergeRowSpan ? cell.mergeRowSpan : 1;
          const cellColSpan = cell.isMergeAnchor && cell.mergeColSpan ? cell.mergeColSpan : 1;

          // Check if this cell's top or bottom edge matches our edge
          if (cell.row === row || cell.row + cellRowSpan === row) {
            // This cell owns this edge - calculate full span
            endX = startX;
            for (let c = 0; c < cellColSpan; c++) {
              const w = this.colWidths.get(col + c) || DEFAULT_COL_WIDTH;
              endX += getZoomedColWidth(w, zoomFactor);
            }
            break;
          }
        }

        // Clamp to visible area
        startX = Math.max(startX, zoomedHeaderWidth);
        endX = Math.min(endX, viewWidth);

        if (startX >= endX) continue;

        ctx.beginPath();
        // Center the line on the edge (add 0.5 for crisp 1px lines)
        const lineY = Math.round(y) + 0.5;
        ctx.moveTo(startX, lineY);
        ctx.lineTo(endX, lineY);
        ctx.stroke();
      } else {
        // Vertical edge: draw at the boundary between col-1 and col
        // Edge position is at the left of 'col' (or right of 'col-1')
        const x = getColX(col, headerState);

        // Skip if outside visible area
        if (x < zoomedHeaderWidth - width || x > viewWidth + width) continue;

        // For vertical edges, determine the row span
        let startY = getRowY(row, headerState);
        const baseRowH = this.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
        let endY = startY + getZoomedRowHeight(baseRowH, zoomFactor);

        // Check if this edge spans multiple rows (from merged cells)
        for (const cell of this.cells) {
          if (cell.row !== row) continue;
          const cellColSpan = cell.isMergeAnchor && cell.mergeColSpan ? cell.mergeColSpan : 1;
          const cellRowSpan = cell.isMergeAnchor && cell.mergeRowSpan ? cell.mergeRowSpan : 1;

          // Check if this cell's left or right edge matches our edge
          if (cell.col === col || cell.col + cellColSpan === col) {
            // This cell owns this edge - calculate full span
            endY = startY;
            for (let r = 0; r < cellRowSpan; r++) {
              const h = this.rowHeights.get(row + r) || DEFAULT_ROW_HEIGHT;
              endY += getZoomedRowHeight(h, zoomFactor);
            }
            break;
          }
        }

        // Clamp to visible area
        startY = Math.max(startY, zoomedHeaderHeight);
        endY = Math.min(endY, viewHeight);

        if (startY >= endY) continue;

        ctx.beginPath();
        // Center the line on the edge (add 0.5 for crisp 1px lines)
        const lineX = Math.round(x) + 0.5;
        ctx.moveTo(lineX, startY);
        ctx.lineTo(lineX, endY);
        ctx.stroke();
      }
    }

    // Reset line dash
    ctx.setLineDash([]);
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
    const zoomFactor = headerState.zoomFactor;
    const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
    const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);
    const zoomedCellPadding = getZoomedCellPadding(zoomFactor);

    // Build a lookup set of cells with content for overflow checking
    // Key format: "col,row"
    const cellsWithContent = new Set<string>();
    for (const cell of this.cells) {
      // Cell has content if it has a value/display/formula, or is part of a merge
      if (cell.value || cell.display || cell.formula || cell.isMergedCell || cell.isMergeAnchor) {
        cellsWithContent.add(`${cell.col},${cell.row}`);
      }
    }

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
          const baseW = this.colWidths.get(cell.col + c) || DEFAULT_COL_WIDTH;
          colWidth += getZoomedColWidth(baseW, zoomFactor);
        }
        // Sum up heights for all rows in the merge
        for (let r = 0; r < cell.mergeRowSpan; r++) {
          const baseH = this.rowHeights.get(cell.row + r) || DEFAULT_ROW_HEIGHT;
          rowHeight += getZoomedRowHeight(baseH, zoomFactor);
        }
      } else {
        const baseW = this.colWidths.get(cell.col) || DEFAULT_COL_WIDTH;
        const baseH = this.rowHeights.get(cell.row) || DEFAULT_ROW_HEIGHT;
        colWidth = getZoomedColWidth(baseW, zoomFactor);
        rowHeight = getZoomedRowHeight(baseH, zoomFactor);
      }

      if (cellX + colWidth < zoomedHeaderWidth || cellX > viewWidth) continue;
      if (cellY + rowHeight < zoomedHeaderHeight || cellY > viewHeight) continue;

      const displayValue = cell.display || cell.value || "";
      const style = cell.style;

      // Set text color
      ctx.fillStyle = style?.textColor || this.colors.cellText;

      // Build font string with style properties (zoom font size)
      let fontStyle = "";
      if (style?.italic) fontStyle += "italic ";
      if (style?.bold) fontStyle += "bold ";
      const baseFontSize = style?.fontSize || 13;
      const zoomedFontSize = getZoomedFontSize(baseFontSize, zoomFactor);
      const fontFamily = style?.fontFamily || '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
      ctx.font = fontStyle ? `${fontStyle}${zoomedFontSize}px ${fontFamily}` : `${zoomedFontSize}px ${fontFamily}`;

      // Measure text width for overflow detection
      const textWidth = ctx.measureText(displayValue).width;
      const availableWidth = colWidth - 2 * zoomedCellPadding;

      // Set horizontal alignment
      // When hAlign is not set (undefined), use "general" alignment:
      // - numbers, dates, formulas with numeric results: right-aligned
      // - text, boolean, errors: left-aligned
      let hAlign: string;
      if (style?.hAlign) {
        hAlign = style.hAlign;
      } else {
        // "General" alignment - right for numbers/dates, left for text
        const type = cell.type;
        if (type === "n" || type === "d" || type === "t") {
          // number, date, datetime - right align
          hAlign = "right";
        } else if (type === "f") {
          // formula - check if display is numeric (not an error/text result)
          const display = cell.display || "";
          // Numeric formula results look like numbers (possibly with currency/percent)
          // Text results would have non-numeric characters
          const isNumericResult = display !== "" && !isNaN(parseFloat(display.replace(/[$,%()]/g, "")));
          hAlign = isNumericResult ? "right" : "left";
        } else {
          // string, boolean, error - left align
          hAlign = "left";
        }
      }

      // Calculate overflow clip region (extends into empty neighbor cells)
      let clipStartCol = cell.col;
      let clipEndCol = cell.col;

      // Only calculate overflow if text is wider than available space
      // and the cell is not a merge anchor (merged cells have their own sizing)
      if (textWidth > availableWidth && !cell.isMergeAnchor && displayValue) {
        const overflowNeeded = textWidth - availableWidth;

        if (hAlign === "left") {
          // Overflow to the right
          let extendedWidth = 0;
          for (let c = cell.col + 1; c < (this.sheetInfo?.colCount || 100); c++) {
            if (cellsWithContent.has(`${c},${cell.row}`)) break;
            const baseNeighborWidth = this.colWidths.get(c) || DEFAULT_COL_WIDTH;
            extendedWidth += getZoomedColWidth(baseNeighborWidth, zoomFactor);
            clipEndCol = c;
            if (extendedWidth >= overflowNeeded) break;
          }
        } else if (hAlign === "right") {
          // Overflow to the left
          let extendedWidth = 0;
          for (let c = cell.col - 1; c >= 0; c--) {
            if (cellsWithContent.has(`${c},${cell.row}`)) break;
            const baseNeighborWidth = this.colWidths.get(c) || DEFAULT_COL_WIDTH;
            extendedWidth += getZoomedColWidth(baseNeighborWidth, zoomFactor);
            clipStartCol = c;
            if (extendedWidth >= overflowNeeded) break;
          }
        } else if (hAlign === "center") {
          // Overflow to both sides equally
          const halfOverflow = overflowNeeded / 2;
          // Extend right
          let rightExtend = 0;
          for (let c = cell.col + 1; c < (this.sheetInfo?.colCount || 100); c++) {
            if (cellsWithContent.has(`${c},${cell.row}`)) break;
            const baseNeighborWidth = this.colWidths.get(c) || DEFAULT_COL_WIDTH;
            rightExtend += getZoomedColWidth(baseNeighborWidth, zoomFactor);
            clipEndCol = c;
            if (rightExtend >= halfOverflow) break;
          }
          // Extend left
          let leftExtend = 0;
          for (let c = cell.col - 1; c >= 0; c--) {
            if (cellsWithContent.has(`${c},${cell.row}`)) break;
            const baseNeighborWidth = this.colWidths.get(c) || DEFAULT_COL_WIDTH;
            leftExtend += getZoomedColWidth(baseNeighborWidth, zoomFactor);
            clipStartCol = c;
            if (leftExtend >= halfOverflow) break;
          }
        }
      }

      // Calculate the extended clip region
      const clipX = getColX(clipStartCol, headerState);
      let clipWidth = 0;
      for (let c = clipStartCol; c <= clipEndCol; c++) {
        const baseW = this.colWidths.get(c) || DEFAULT_COL_WIDTH;
        clipWidth += getZoomedColWidth(baseW, zoomFactor);
      }

      ctx.save();
      ctx.beginPath();
      ctx.rect(clipX + 1, cellY + 1, clipWidth - 2, rowHeight - 2);
      ctx.clip();

      // Set horizontal alignment and calculate text X position
      let textX: number;
      if (hAlign === "center") {
        ctx.textAlign = "center";
        // Center within the original cell (between grid lines)
        textX = Math.round(cellX + colWidth / 2);
      } else if (hAlign === "right") {
        ctx.textAlign = "right";
        textX = cellX + colWidth - zoomedCellPadding;
      } else {
        ctx.textAlign = "left";
        textX = cellX + zoomedCellPadding;
      }

      // Set vertical alignment (default matches CellStyle in C++)
      const vAlign = style?.vAlign || "bottom";
      const zoomedVertPadding = Math.round(2 * zoomFactor);
      let textY: number;
      if (vAlign === "top") {
        ctx.textBaseline = "top";
        textY = cellY + zoomedVertPadding; // Small padding from top
      } else if (vAlign === "bottom") {
        ctx.textBaseline = "bottom";
        textY = cellY + rowHeight - zoomedVertPadding; // Small padding from bottom
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
          underlineY = textY + zoomedFontSize + 1;
        } else if (vAlign === "bottom") {
          underlineY = textY + 1;
        } else {
          underlineY = textY + zoomedFontSize / 2 + 1;
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
