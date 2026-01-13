// Grid Selection Renderer Module
// Handles rendering of cell and range selections

import type { Position } from "./types.js";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
  SPILL_RANGE_COLOR,
  getZoomFactor,
  getZoomedColWidth,
  getZoomedRowHeight,
  getZoomedHeaderWidth,
  getZoomedHeaderHeight,
  type NormalizedRange,
  type SpillRangeHighlight,
} from "./grid-constants.js";

// Fill handle size in pixels
const FILL_HANDLE_SIZE = 6;

/**
 * Helper to get zoomed scroll X value.
 * Scroll values are stored in unzoomed (base) coordinates but need to be
 * converted to zoomed coordinates for proper rendering at non-100% zoom.
 */
function getZoomedScrollX(scrollX: number): number {
  return Math.round(scrollX * getZoomFactor());
}

/**
 * Helper to get zoomed scroll Y value.
 */
function getZoomedScrollY(scrollY: number): number {
  return Math.round(scrollY * getZoomFactor());
}

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

  // Calculate range bounds using zoomed dimensions
  let rangeX = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
  for (let i = 0; i < range.minCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    rangeX += getZoomedColWidth(baseWidth);
  }
  let rangeY = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
  for (let i = 0; i < range.minRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    rangeY += getZoomedRowHeight(baseHeight);
  }

  // Calculate total width and height of range using zoomed dimensions
  let rangeW = 0;
  for (let i = range.minCol; i <= range.maxCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    rangeW += getZoomedColWidth(baseWidth);
  }
  let rangeH = 0;
  for (let i = range.minRow; i <= range.maxRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    rangeH += getZoomedRowHeight(baseHeight);
  }

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
    ctx.strokeStyle = colors.selectionBorder;
    ctx.lineWidth = 1;
    ctx.strokeRect(
      Math.max(zoomedHeaderWidth, rangeX) + 0.5,
      Math.max(zoomedHeaderHeight, rangeY) + 0.5,
      Math.min(rangeW, rangeX + rangeW - Math.max(zoomedHeaderWidth, rangeX)) - 1,
      Math.min(rangeH, rangeY + rangeH - Math.max(zoomedHeaderHeight, rangeY)) - 1
    );
  }

  // Draw anchor cell highlight (the cell where selection started)
  // This is only needed for multi-cell ranges to show the "active" cell
  if (hasRangeSelection(state) && state.selectionStart) {
    let anchorX = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
    for (let i = 0; i < state.selectionStart.col; i++) {
      const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
      anchorX += getZoomedColWidth(baseWidth);
    }
    let anchorY = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
    for (let i = 0; i < state.selectionStart.row; i++) {
      const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      anchorY += getZoomedRowHeight(baseHeight);
    }
    const anchorBaseW =
      state.colWidths.get(state.selectionStart.col) || DEFAULT_COL_WIDTH;
    const anchorBaseH =
      state.rowHeights.get(state.selectionStart.row) || DEFAULT_ROW_HEIGHT;
    const anchorW = getZoomedColWidth(anchorBaseW);
    const anchorH = getZoomedRowHeight(anchorBaseH);

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
      ctx.strokeRect(clipX - 0.5, clipY - 0.5, clipW + 1, clipH + 1);

      // Main border for anchor cell (1px to match range border)
      ctx.strokeStyle = colors.selectionBorder;
      ctx.lineWidth = 1;
      ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW - 1, clipH - 1);
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

  let selX = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
  for (let i = 0; i < state.selectedCell.col; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    selX += getZoomedColWidth(baseWidth);
  }
  let selY = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
  for (let i = 0; i < state.selectedCell.row; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    selY += getZoomedRowHeight(baseHeight);
  }
  const selBaseW = state.colWidths.get(state.selectedCell.col) || DEFAULT_COL_WIDTH;
  const selBaseH = state.rowHeights.get(state.selectedCell.row) || DEFAULT_ROW_HEIGHT;
  const selW = getZoomedColWidth(selBaseW);
  const selH = getZoomedRowHeight(selBaseH);

  if (
    selX + selW > zoomedHeaderWidth &&
    selX < viewWidth &&
    selY + selH > zoomedHeaderHeight &&
    selY < viewHeight
  ) {
    const colors = getGridColors();

    // Draw glow effect (2px spread shadow like formula bar)
    ctx.strokeStyle = "rgba(5, 134, 1, 0.15)";
    ctx.lineWidth = 4;
    ctx.strokeRect(selX - 1, selY - 1, selW + 2, selH + 2);

    // Draw selection fill
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(selX, selY, selW, selH);

    // Draw main border
    ctx.strokeStyle = colors.selectionBorder;
    ctx.lineWidth = 2;
    ctx.strokeRect(selX + 1, selY + 1, selW - 2, selH - 2);
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

  let selX = zoomedHeaderWidth - getZoomedScrollX(scrollX);
  for (let i = 0; i < selectedColumn; i++) {
    const baseWidth = colWidths.get(i) || DEFAULT_COL_WIDTH;
    selX += getZoomedColWidth(baseWidth);
  }
  const selBaseW = colWidths.get(selectedColumn) || DEFAULT_COL_WIDTH;
  const selW = getZoomedColWidth(selBaseW);

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

  let selY = zoomedHeaderHeight - getZoomedScrollY(scrollY);
  for (let i = 0; i < selectedRow; i++) {
    const baseHeight = rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    selY += getZoomedRowHeight(baseHeight);
  }
  const selBaseH = rowHeights.get(selectedRow) || DEFAULT_ROW_HEIGHT;
  const selH = getZoomedRowHeight(selBaseH);

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
): { x: number; y: number; width: number; height: number } | null {
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

  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Calculate position using zoomed dimensions
  let x = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
  for (let i = 0; i < minCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    x += getZoomedColWidth(baseWidth);
  }
  let y = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
  for (let i = 0; i < minRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    y += getZoomedRowHeight(baseHeight);
  }

  // Calculate size using zoomed dimensions
  let width = 0;
  for (let i = minCol; i <= maxCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    width += getZoomedColWidth(baseWidth);
  }
  let height = 0;
  for (let i = minRow; i <= maxRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    height += getZoomedRowHeight(baseHeight);
  }

  return { x, y, width, height };
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

  // Calculate preview bounds using zoomed dimensions
  let previewX = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
  for (let i = 0; i < previewRange.minCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    previewX += getZoomedColWidth(baseWidth);
  }
  let previewY = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
  for (let i = 0; i < previewRange.minRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    previewY += getZoomedRowHeight(baseHeight);
  }

  // Calculate total width and height of preview using zoomed dimensions
  let previewW = 0;
  for (let i = previewRange.minCol; i <= previewRange.maxCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    previewW += getZoomedColWidth(baseWidth);
  }
  let previewH = 0;
  for (let i = previewRange.minRow; i <= previewRange.maxRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    previewH += getZoomedRowHeight(baseHeight);
  }

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

  // Draw dashed border
  ctx.save();
  ctx.strokeStyle = colors.selectionBorder;
  ctx.lineWidth = 2;
  ctx.setLineDash([4, 4]); // 4px dash, 4px gap
  ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW - 1, clipH - 1);
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

  // Calculate spill range bounds using zoomed dimensions
  let rangeX = zoomedHeaderWidth - getZoomedScrollX(state.scrollX);
  for (let i = 0; i < spillRange.minCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    rangeX += getZoomedColWidth(baseWidth);
  }
  let rangeY = zoomedHeaderHeight - getZoomedScrollY(state.scrollY);
  for (let i = 0; i < spillRange.minRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    rangeY += getZoomedRowHeight(baseHeight);
  }

  // Calculate total width and height of spill range using zoomed dimensions
  let rangeW = 0;
  for (let i = spillRange.minCol; i <= spillRange.maxCol; i++) {
    const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    rangeW += getZoomedColWidth(baseWidth);
  }
  let rangeH = 0;
  for (let i = spillRange.minRow; i <= spillRange.maxRow; i++) {
    const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    rangeH += getZoomedRowHeight(baseHeight);
  }

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

  // Draw blue border (like Excel's spill range indicator)
  ctx.strokeStyle = SPILL_RANGE_COLOR.border;
  ctx.lineWidth = 2;
  ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW - 1, clipH - 1);

  ctx.restore();
}
