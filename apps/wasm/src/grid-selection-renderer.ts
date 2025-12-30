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

      // Draw glow effect (2px spread shadow like formula bar)
      ctx.strokeStyle = "rgba(5, 134, 1, 0.15)";
      ctx.lineWidth = 4;
      ctx.strokeRect(clipX - 1, clipY - 1, clipW + 2, clipH + 2);

      // Cell background
      ctx.fillStyle = colors.cellBg;
      ctx.fillRect(clipX + 1, clipY + 1, clipW - 2, clipH - 2);

      // Main border for anchor cell
      ctx.strokeStyle = colors.selectionBorder;
      ctx.lineWidth = 2;
      ctx.strokeRect(clipX + 1, clipY + 1, clipW - 2, clipH - 2);
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
