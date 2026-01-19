// Grid Selection Renderer Module
// Handles rendering of cell and range selections

import type { Position } from "./types.js";
import {
  getGridColors,
  SPILL_RANGE_COLOR,
  getZoomedHeaderWidth,
  getZoomedHeaderHeight,
  type NormalizedRange,
  type SpillRangeHighlight,
} from "./grid-constants.js";
import {
  getCellBounds,
  getRangeBounds,
  type CellBounds,
} from "./grid-utils.js";

// Fill handle size in pixels
const FILL_HANDLE_SIZE = 6;

/** State needed for selection rendering */
export interface SelectionRendererState {
  scrollX: number;
  scrollY: number;
  colWidths: Map<number, number>;
  rowHeights: Map<number, number>;
  selectionStart: Position | null;
  selectionEnd: Position | null;
  selectedCell: Position | null;
}

/** Position and bounds of the fill handle for hit testing */
export interface FillHandleBounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

/**
 * Check if we have a multi-cell range selection
 */
export function hasRangeSelection(state: SelectionRendererState): boolean {
  if (!state.selectionStart || !state.selectionEnd) return false;
  return (
    state.selectionStart.col !== state.selectionEnd.col ||
    state.selectionStart.row !== state.selectionEnd.row
  );
}

/**
 * Draw range selection (fill, border, anchor cell)
 */
export function drawRangeSelection(
  ctx: CanvasRenderingContext2D,
  state: SelectionRendererState,
  range: NormalizedRange,
  viewWidth: number,
  viewHeight: number
): void {
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper to calculate range bounds
  const rangeBounds = getRangeBounds(
    range.minCol,
    range.minRow,
    range.maxCol,
    range.maxRow,
    state.scrollX,
    state.scrollY,
    state.colWidths,
    state.rowHeights
  );

  const { x: rangeX, y: rangeY, width: rangeW, height: rangeH } = rangeBounds;

  // Get theme-aware colors
  const colors = getGridColors();

  // Draw range fill
  if (
    rangeX + rangeW > zoomedHeaderWidth &&
    rangeX < viewWidth &&
    rangeY + rangeH > zoomedHeaderHeight &&
    rangeY < viewHeight
  ) {
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      Math.max(zoomedHeaderWidth, rangeX),
      Math.max(zoomedHeaderHeight, rangeY),
      Math.min(rangeW, rangeX + rangeW - Math.max(zoomedHeaderWidth, rangeX)),
      Math.min(rangeH, rangeY + rangeH - Math.max(zoomedHeaderHeight, rangeY))
    );

    // Draw range border (thinner than anchor cell)
    // Border should align with cell borders, which are drawn centered at cell edges
    ctx.strokeStyle = colors.selectionBorder;
    ctx.lineWidth = 1;
    ctx.strokeRect(
      Math.max(zoomedHeaderWidth, rangeX) + 0.5,
      Math.max(zoomedHeaderHeight, rangeY) + 0.5,
      Math.min(rangeW, rangeX + rangeW - Math.max(zoomedHeaderWidth, rangeX)),
      Math.min(rangeH, rangeY + rangeH - Math.max(zoomedHeaderHeight, rangeY))
    );
  }

  // Draw anchor cell highlight (the cell where selection started)
  // This is only needed for multi-cell ranges to show the "active" cell
  if (hasRangeSelection(state) && state.selectionStart) {
    // Use centralized helper for anchor cell bounds
    const anchorBounds = getCellBounds(
      state.selectionStart.col,
      state.selectionStart.row,
      state.scrollX,
      state.scrollY,
      state.colWidths,
      state.rowHeights
    );

    const { x: anchorX, y: anchorY, width: anchorW, height: anchorH } = anchorBounds;

    // Draw anchor cell with white background, glow, and border
    if (
      anchorX + anchorW > zoomedHeaderWidth &&
      anchorX < viewWidth &&
      anchorY + anchorH > zoomedHeaderHeight &&
      anchorY < viewHeight
    ) {
      const clipX = Math.max(zoomedHeaderWidth, anchorX);
      const clipY = Math.max(zoomedHeaderHeight, anchorY);
      const clipW = Math.min(anchorW, anchorX + anchorW - clipX);
      const clipH = Math.min(anchorH, anchorY + anchorH - clipY);

      // Draw glow effect (subtle shadow)
      ctx.strokeStyle = "rgba(5, 134, 1, 0.15)";
      ctx.lineWidth = 3;
      ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW, clipH);

      // Main border for anchor cell (1px to match range border)
      ctx.strokeStyle = colors.selectionBorder;
      ctx.lineWidth = 1;
      ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW, clipH);
    }
  }
}

/**
 * Draw single cell selection (fallback when no range)
 */
export function drawSingleCellSelection(
  ctx: CanvasRenderingContext2D,
  state: SelectionRendererState,
  viewWidth: number,
  viewHeight: number
): void {
  if (!state.selectedCell) return;

  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper for cell bounds
  const { x: selX, y: selY, width: selW, height: selH } = getCellBounds(
    state.selectedCell.col,
    state.selectedCell.row,
    state.scrollX,
    state.scrollY,
    state.colWidths,
    state.rowHeights
  );

  if (
    selX + selW > zoomedHeaderWidth &&
    selX < viewWidth &&
    selY + selH > zoomedHeaderHeight &&
    selY < viewHeight
  ) {
    const colors = getGridColors();

    // Draw glow effect (spread shadow around cell edges)
    ctx.strokeStyle = "rgba(5, 134, 1, 0.15)";
    ctx.lineWidth = 4;
    ctx.strokeRect(selX + 0.5, selY + 0.5, selW, selH);

    // Draw selection fill
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(selX, selY, selW, selH);

    // Draw main border (aligned with cell borders)
    ctx.strokeStyle = colors.selectionBorder;
    ctx.lineWidth = 2;
    ctx.strokeRect(selX + 0.5, selY + 0.5, selW, selH);
  }
}

/**
 * Draw column selection (highlight entire column)
 */
export function drawColumnSelection(
  ctx: CanvasRenderingContext2D,
  scrollX: number,
  colWidths: Map<number, number>,
  selectedColumn: number,
  viewWidth: number,
  viewHeight: number
): void {
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper for cell bounds (row 0, but we only need X and width)
  const emptyRowHeights = new Map<number, number>();
  const { x: selX, width: selW } = getCellBounds(
    selectedColumn,
    0, // row doesn't matter for X position
    scrollX,
    0, // scrollY doesn't matter for column
    colWidths,
    emptyRowHeights
  );

  if (selX + selW > zoomedHeaderWidth && selX < viewWidth) {
    const colors = getGridColors();
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      Math.max(zoomedHeaderWidth, selX),
      zoomedHeaderHeight,
      Math.min(selW, selX + selW - zoomedHeaderWidth),
      viewHeight - zoomedHeaderHeight
    );
  }
}

/**
 * Draw row selection (highlight entire row)
 */
export function drawRowSelection(
  ctx: CanvasRenderingContext2D,
  scrollY: number,
  rowHeights: Map<number, number>,
  selectedRow: number,
  viewWidth: number,
  viewHeight: number
): void {
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper for cell bounds (col 0, but we only need Y and height)
  const emptyColWidths = new Map<number, number>();
  const { y: selY, height: selH } = getCellBounds(
    0, // col doesn't matter for Y position
    selectedRow,
    0, // scrollX doesn't matter for row
    scrollY,
    emptyColWidths,
    rowHeights
  );

  if (selY + selH > zoomedHeaderHeight && selY < viewHeight) {
    const colors = getGridColors();
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      zoomedHeaderWidth,
      Math.max(zoomedHeaderHeight, selY),
      viewWidth - zoomedHeaderWidth,
      Math.min(selH, selY + selH - zoomedHeaderHeight)
    );
  }
}

/**
 * Calculate the bounds of the selection for fill handle positioning
 */
export function getSelectionBounds(
  state: SelectionRendererState,
  range?: NormalizedRange
): CellBounds | null {
  let minCol: number, maxCol: number, minRow: number, maxRow: number;

  if (range) {
    minCol = range.minCol;
    maxCol = range.maxCol;
    minRow = range.minRow;
    maxRow = range.maxRow;
  } else if (state.selectedCell) {
    minCol = maxCol = state.selectedCell.col;
    minRow = maxRow = state.selectedCell.row;
  } else {
    return null;
  }

  // Use centralized helper for range bounds
  return getRangeBounds(
    minCol,
    minRow,
    maxCol,
    maxRow,
    state.scrollX,
    state.scrollY,
    state.colWidths,
    state.rowHeights
  );
}

/**
 * Draw the fill handle (small square at bottom-right of selection)
 * Returns the bounds for hit testing, or null if not visible
 */
export function drawFillHandle(
  ctx: CanvasRenderingContext2D,
  state: SelectionRendererState,
  range?: NormalizedRange,
  viewWidth?: number,
  viewHeight?: number
): FillHandleBounds | null {
  const bounds = getSelectionBounds(state, range);
  if (!bounds) return null;

  const { x, y, width, height } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Calculate fill handle position (bottom-right corner)
  const handleX = x + width - FILL_HANDLE_SIZE / 2;
  const handleY = y + height - FILL_HANDLE_SIZE / 2;

  // Check if fill handle is visible (within viewport and not in header)
  const vw = viewWidth ?? ctx.canvas.width;
  const vh = viewHeight ?? ctx.canvas.height;

  if (
    handleX + FILL_HANDLE_SIZE < zoomedHeaderWidth ||
    handleX > vw ||
    handleY + FILL_HANDLE_SIZE < zoomedHeaderHeight ||
    handleY > vh
  ) {
    return null;
  }

  // Draw fill handle
  const colors = getGridColors();
  ctx.fillStyle = colors.selectionBorder;
  ctx.fillRect(handleX, handleY, FILL_HANDLE_SIZE, FILL_HANDLE_SIZE);

  return {
    x: handleX,
    y: handleY,
    width: FILL_HANDLE_SIZE,
    height: FILL_HANDLE_SIZE,
  };
}

/**
 * Check if a point is within the fill handle bounds
 */
export function isPointInFillHandle(
  point: Position,
  fillHandleBounds: FillHandleBounds | null
): boolean {
  if (!fillHandleBounds) return false;
  const { x, y, width, height } = fillHandleBounds;
  // Add some padding for easier targeting
  const padding = 3;
  return (
    point.col >= x - padding &&
    point.col <= x + width + padding &&
    point.row >= y - padding &&
    point.row <= y + height + padding
  );
}

/** Fill preview range type */
export interface FillPreviewRange {
  minCol: number;
  maxCol: number;
  minRow: number;
  maxRow: number;
}

/**
 * Draw the fill preview (dashed border showing target range during fill drag)
 * This shows the area that will be filled when the user releases the mouse.
 */
export function drawFillPreview(
  ctx: CanvasRenderingContext2D,
  state: SelectionRendererState,
  previewRange: FillPreviewRange,
  viewWidth: number,
  viewHeight: number
): void {
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper for preview bounds
  const previewBounds = getRangeBounds(
    previewRange.minCol,
    previewRange.minRow,
    previewRange.maxCol,
    previewRange.maxRow,
    state.scrollX,
    state.scrollY,
    state.colWidths,
    state.rowHeights
  );

  const { x: previewX, y: previewY, width: previewW, height: previewH } = previewBounds;

  // Check if preview is visible
  if (
    previewX + previewW <= zoomedHeaderWidth ||
    previewX >= viewWidth ||
    previewY + previewH <= zoomedHeaderHeight ||
    previewY >= viewHeight
  ) {
    return;
  }

  // Clip to data area
  const clipX = Math.max(zoomedHeaderWidth, previewX);
  const clipY = Math.max(zoomedHeaderHeight, previewY);
  const clipW = Math.min(previewW, previewX + previewW - clipX);
  const clipH = Math.min(previewH, previewY + previewH - clipY);

  const colors = getGridColors();

  // Draw dashed border (aligned with cell borders)
  ctx.save();
  ctx.strokeStyle = colors.selectionBorder;
  ctx.lineWidth = 2;
  ctx.setLineDash([4, 4]); // 4px dash, 4px gap
  ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW, clipH);
  ctx.restore();
}

/**
 * Draw spill range highlight when selected cell is part of a spill range.
 * Uses a blue border (like Excel) to show the entire spill area.
 */
export function drawSpillRangeHighlight(
  ctx: CanvasRenderingContext2D,
  state: SelectionRendererState,
  spillRange: SpillRangeHighlight,
  viewWidth: number,
  viewHeight: number
): void {
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Use centralized helper for spill range bounds
  const spillBounds = getRangeBounds(
    spillRange.minCol,
    spillRange.minRow,
    spillRange.maxCol,
    spillRange.maxRow,
    state.scrollX,
    state.scrollY,
    state.colWidths,
    state.rowHeights
  );

  const { x: rangeX, y: rangeY, width: rangeW, height: rangeH } = spillBounds;

  // Check if spill range is visible
  if (
    rangeX + rangeW <= zoomedHeaderWidth ||
    rangeX >= viewWidth ||
    rangeY + rangeH <= zoomedHeaderHeight ||
    rangeY >= viewHeight
  ) {
    return;
  }

  // Clip to data area
  const clipX = Math.max(zoomedHeaderWidth, rangeX);
  const clipY = Math.max(zoomedHeaderHeight, rangeY);
  const clipW = Math.min(rangeW, rangeX + rangeW - clipX);
  const clipH = Math.min(rangeH, rangeY + rangeH - clipY);

  ctx.save();

  // Draw subtle background fill
  ctx.fillStyle = SPILL_RANGE_COLOR.bg;
  ctx.fillRect(clipX, clipY, clipW, clipH);

  // Draw blue border (like Excel's spill range indicator, aligned with cell borders)
  ctx.strokeStyle = SPILL_RANGE_COLOR.border;
  ctx.lineWidth = 2;
  ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW, clipH);

  ctx.restore();
}
