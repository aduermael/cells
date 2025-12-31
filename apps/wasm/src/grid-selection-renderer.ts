// Grid Selection Renderer Module
// Handles rendering of cell and range selections

import type { Position } from "./types.js";
import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
  type NormalizedRange,
} from "./grid-constants.js";

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
  // Calculate range bounds
  let rangeX = HEADER_WIDTH - state.scrollX;
  for (let i = 0; i < range.minCol; i++) {
    rangeX += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let rangeY = HEADER_HEIGHT - state.scrollY;
  for (let i = 0; i < range.minRow; i++) {
    rangeY += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Calculate total width and height of range
  let rangeW = 0;
  for (let i = range.minCol; i <= range.maxCol; i++) {
    rangeW += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let rangeH = 0;
  for (let i = range.minRow; i <= range.maxRow; i++) {
    rangeH += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Get theme-aware colors
  const colors = getGridColors();

  // Draw range fill
  if (
    rangeX + rangeW > HEADER_WIDTH &&
    rangeX < viewWidth &&
    rangeY + rangeH > HEADER_HEIGHT &&
    rangeY < viewHeight
  ) {
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      Math.max(HEADER_WIDTH, rangeX),
      Math.max(HEADER_HEIGHT, rangeY),
      Math.min(rangeW, rangeX + rangeW - Math.max(HEADER_WIDTH, rangeX)),
      Math.min(rangeH, rangeY + rangeH - Math.max(HEADER_HEIGHT, rangeY))
    );

    // Draw range border (thinner than anchor cell)
    ctx.strokeStyle = colors.selectionBorder;
    ctx.lineWidth = 1;
    ctx.strokeRect(
      Math.max(HEADER_WIDTH, rangeX) + 0.5,
      Math.max(HEADER_HEIGHT, rangeY) + 0.5,
      Math.min(rangeW, rangeX + rangeW - Math.max(HEADER_WIDTH, rangeX)) - 1,
      Math.min(rangeH, rangeY + rangeH - Math.max(HEADER_HEIGHT, rangeY)) - 1
    );
  }

  // Draw anchor cell highlight (the cell where selection started)
  // This is only needed for multi-cell ranges to show the "active" cell
  if (hasRangeSelection(state) && state.selectionStart) {
    let anchorX = HEADER_WIDTH - state.scrollX;
    for (let i = 0; i < state.selectionStart.col; i++) {
      anchorX += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    }
    let anchorY = HEADER_HEIGHT - state.scrollY;
    for (let i = 0; i < state.selectionStart.row; i++) {
      anchorY += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    }
    const anchorW =
      state.colWidths.get(state.selectionStart.col) || DEFAULT_COL_WIDTH;
    const anchorH =
      state.rowHeights.get(state.selectionStart.row) || DEFAULT_ROW_HEIGHT;

    // Draw anchor cell with white background, glow, and border
    if (
      anchorX + anchorW > HEADER_WIDTH &&
      anchorX < viewWidth &&
      anchorY + anchorH > HEADER_HEIGHT &&
      anchorY < viewHeight
    ) {
      const clipX = Math.max(HEADER_WIDTH, anchorX);
      const clipY = Math.max(HEADER_HEIGHT, anchorY);
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

  let selX = HEADER_WIDTH - state.scrollX;
  for (let i = 0; i < state.selectedCell.col; i++) {
    selX += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let selY = HEADER_HEIGHT - state.scrollY;
  for (let i = 0; i < state.selectedCell.row; i++) {
    selY += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }
  const selW = state.colWidths.get(state.selectedCell.col) || DEFAULT_COL_WIDTH;
  const selH = state.rowHeights.get(state.selectedCell.row) || DEFAULT_ROW_HEIGHT;

  if (
    selX + selW > HEADER_WIDTH &&
    selX < viewWidth &&
    selY + selH > HEADER_HEIGHT &&
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
  let selX = HEADER_WIDTH - scrollX;
  for (let i = 0; i < selectedColumn; i++) {
    selX += colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  const selW = colWidths.get(selectedColumn) || DEFAULT_COL_WIDTH;

  if (selX + selW > HEADER_WIDTH && selX < viewWidth) {
    const colors = getGridColors();
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      Math.max(HEADER_WIDTH, selX),
      HEADER_HEIGHT,
      Math.min(selW, selX + selW - HEADER_WIDTH),
      viewHeight - HEADER_HEIGHT
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
  let selY = HEADER_HEIGHT - scrollY;
  for (let i = 0; i < selectedRow; i++) {
    selY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }
  const selH = rowHeights.get(selectedRow) || DEFAULT_ROW_HEIGHT;

  if (selY + selH > HEADER_HEIGHT && selY < viewHeight) {
    const colors = getGridColors();
    ctx.fillStyle = colors.selectionBg;
    ctx.fillRect(
      HEADER_WIDTH,
      Math.max(HEADER_HEIGHT, selY),
      viewWidth - HEADER_WIDTH,
      Math.min(selH, selY + selH - HEADER_HEIGHT)
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

  // Calculate position
  let x = HEADER_WIDTH - state.scrollX;
  for (let i = 0; i < minCol; i++) {
    x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let y = HEADER_HEIGHT - state.scrollY;
  for (let i = 0; i < minRow; i++) {
    y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Calculate size
  let width = 0;
  for (let i = minCol; i <= maxCol; i++) {
    width += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let height = 0;
  for (let i = minRow; i <= maxRow; i++) {
    height += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
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

  // Calculate fill handle position (bottom-right corner)
  const handleX = x + width - FILL_HANDLE_SIZE / 2;
  const handleY = y + height - FILL_HANDLE_SIZE / 2;

  // Check if fill handle is visible (within viewport and not in header)
  const vw = viewWidth ?? ctx.canvas.width;
  const vh = viewHeight ?? ctx.canvas.height;

  if (
    handleX + FILL_HANDLE_SIZE < HEADER_WIDTH ||
    handleX > vw ||
    handleY + FILL_HANDLE_SIZE < HEADER_HEIGHT ||
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
  // Calculate preview bounds
  let previewX = HEADER_WIDTH - state.scrollX;
  for (let i = 0; i < previewRange.minCol; i++) {
    previewX += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let previewY = HEADER_HEIGHT - state.scrollY;
  for (let i = 0; i < previewRange.minRow; i++) {
    previewY += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Calculate total width and height of preview
  let previewW = 0;
  for (let i = previewRange.minCol; i <= previewRange.maxCol; i++) {
    previewW += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let previewH = 0;
  for (let i = previewRange.minRow; i <= previewRange.maxRow; i++) {
    previewH += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Check if preview is visible
  if (
    previewX + previewW <= HEADER_WIDTH ||
    previewX >= viewWidth ||
    previewY + previewH <= HEADER_HEIGHT ||
    previewY >= viewHeight
  ) {
    return;
  }

  // Clip to data area
  const clipX = Math.max(HEADER_WIDTH, previewX);
  const clipY = Math.max(HEADER_HEIGHT, previewY);
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
