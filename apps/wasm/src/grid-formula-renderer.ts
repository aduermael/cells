// Grid Formula Renderer Module
// Handles rendering of formula reference highlights on the grid

import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  FORMULA_REF_COLORS,
  FORMULA_ERROR_COLOR,
  type FormulaHighlight,
} from "./grid-constants.js";
import { getColPixelX, getRowPixelY } from "./grid-utils.js";

/** State needed for formula highlight rendering */
export interface FormulaHighlightRendererState {
  scrollX: number;
  scrollY: number;
  colWidths: Map<number, number>;
  rowHeights: Map<number, number>;
  colPixelOffsets: Map<number, number>;
  rowPixelOffsets: Map<number, number>;
  formulaHighlights: FormulaHighlight[];
  /** Index of hovered formula reference (-1 = none) */
  hoveredFormulaRefIndex: number;
}

// Default fallback color (blue)
const DEFAULT_REF_COLOR = { border: "#4285f4", bg: "rgba(66, 133, 244, 0.15)" };

/**
 * Get the color for a formula highlight
 */
function getHighlightColor(
  highlight: FormulaHighlight
): { border: string; bg: string } {
  if (highlight.isError) {
    return { border: FORMULA_ERROR_COLOR.border, bg: FORMULA_ERROR_COLOR.bg };
  }
  const colorIdx = highlight.colorIndex % FORMULA_REF_COLORS.length;
  const color = FORMULA_REF_COLORS[colorIdx] ?? DEFAULT_REF_COLOR;
  return { border: color.border, bg: color.bg };
}

/**
 * Draw a single cell highlight
 */
function drawCellHighlight(
  ctx: CanvasRenderingContext2D,
  col: number,
  row: number,
  color: { border: string; bg: string },
  state: FormulaHighlightRendererState,
  viewWidth: number,
  viewHeight: number,
  isHovered: boolean
): void {
  const cellX = getColPixelX(col, state.scrollX, state.colPixelOffsets, state.colWidths);
  const cellY = getRowPixelY(row, state.scrollY, state.rowPixelOffsets, state.rowHeights);
  const cellW = state.colWidths.get(col) || DEFAULT_COL_WIDTH;
  const cellH = state.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;

  // Check if cell is visible
  if (
    cellX + cellW <= HEADER_WIDTH ||
    cellX >= viewWidth ||
    cellY + cellH <= HEADER_HEIGHT ||
    cellY >= viewHeight
  ) {
    return;
  }

  // Clip to visible area
  const clipX = Math.max(HEADER_WIDTH, cellX);
  const clipY = Math.max(HEADER_HEIGHT, cellY);
  const clipW = Math.min(cellW, cellX + cellW - clipX);
  const clipH = Math.min(cellH, cellY + cellH - clipY);

  // Draw fill (more opaque when hovered)
  ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
  ctx.fillRect(clipX, clipY, clipW, clipH);

  // Draw border (thicker when hovered)
  ctx.strokeStyle = color.border;
  ctx.lineWidth = isHovered ? 3 : 2;
  const inset = isHovered ? 1.5 : 1;
  ctx.strokeRect(clipX + inset, clipY + inset, clipW - inset * 2, clipH - inset * 2);
}

/**
 * Draw a range highlight (multiple cells)
 */
function drawRangeHighlight(
  ctx: CanvasRenderingContext2D,
  startCol: number,
  startRow: number,
  endCol: number,
  endRow: number,
  color: { border: string; bg: string },
  state: FormulaHighlightRendererState,
  viewWidth: number,
  viewHeight: number,
  isHovered: boolean
): void {
  // Normalize range (ensure start <= end)
  const minCol = Math.min(startCol, endCol);
  const maxCol = Math.max(startCol, endCol);
  const minRow = Math.min(startRow, endRow);
  const maxRow = Math.max(startRow, endRow);

  // Calculate range bounds
  const rangeX = getColPixelX(minCol, state.scrollX, state.colPixelOffsets, state.colWidths);
  const rangeY = getRowPixelY(minRow, state.scrollY, state.rowPixelOffsets, state.rowHeights);

  // Calculate total width and height
  let rangeW = 0;
  for (let i = minCol; i <= maxCol; i++) {
    rangeW += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
  }
  let rangeH = 0;
  for (let i = minRow; i <= maxRow; i++) {
    rangeH += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
  }

  // Check if range is visible
  if (
    rangeX + rangeW <= HEADER_WIDTH ||
    rangeX >= viewWidth ||
    rangeY + rangeH <= HEADER_HEIGHT ||
    rangeY >= viewHeight
  ) {
    return;
  }

  // Clip to visible area
  const clipX = Math.max(HEADER_WIDTH, rangeX);
  const clipY = Math.max(HEADER_HEIGHT, rangeY);
  const clipW = Math.min(rangeW, rangeX + rangeW - clipX);
  const clipH = Math.min(rangeH, rangeY + rangeH - clipY);

  // Draw fill (more opaque when hovered)
  ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
  ctx.fillRect(clipX, clipY, clipW, clipH);

  // Draw border (thicker when hovered)
  ctx.strokeStyle = color.border;
  ctx.lineWidth = isHovered ? 3 : 2;
  const inset = isHovered ? 1.5 : 1;
  ctx.strokeRect(clipX + inset, clipY + inset, clipW - inset * 2, clipH - inset * 2);
}

/**
 * Draw all formula reference highlights
 */
export function drawFormulaHighlights(
  ctx: CanvasRenderingContext2D,
  state: FormulaHighlightRendererState,
  viewWidth: number,
  viewHeight: number
): void {
  if (!state.formulaHighlights || state.formulaHighlights.length === 0) {
    return;
  }

  ctx.save();

  const hoveredIdx = state.hoveredFormulaRefIndex;

  for (let idx = 0; idx < state.formulaHighlights.length; idx++) {
    const highlight = state.formulaHighlights[idx];
    if (!highlight) continue;
    const color = getHighlightColor(highlight);
    const isHovered = idx === hoveredIdx;

    switch (highlight.type) {
      case "cell":
        if (highlight.col !== undefined && highlight.row !== undefined) {
          drawCellHighlight(
            ctx,
            highlight.col,
            highlight.row,
            color,
            state,
            viewWidth,
            viewHeight,
            isHovered
          );
        }
        break;

      case "range":
        if (
          highlight.startCol !== undefined &&
          highlight.startRow !== undefined &&
          highlight.endCol !== undefined &&
          highlight.endRow !== undefined
        ) {
          drawRangeHighlight(
            ctx,
            highlight.startCol,
            highlight.startRow,
            highlight.endCol,
            highlight.endRow,
            color,
            state,
            viewWidth,
            viewHeight,
            isHovered
          );
        }
        break;

      case "column":
        // Draw entire column highlight (from visible top to bottom)
        if (highlight.col !== undefined) {
          const colX = getColPixelX(highlight.col, state.scrollX, state.colPixelOffsets, state.colWidths);
          const colW = state.colWidths.get(highlight.col) || DEFAULT_COL_WIDTH;

          if (colX + colW > HEADER_WIDTH && colX < viewWidth) {
            const clipX = Math.max(HEADER_WIDTH, colX);
            const clipW = Math.min(colW, colX + colW - clipX);

            ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
            ctx.fillRect(clipX, HEADER_HEIGHT, clipW, viewHeight - HEADER_HEIGHT);

            ctx.strokeStyle = color.border;
            ctx.lineWidth = isHovered ? 3 : 2;
            const inset = isHovered ? 1.5 : 1;
            ctx.strokeRect(
              clipX + inset,
              HEADER_HEIGHT + inset,
              clipW - inset * 2,
              viewHeight - HEADER_HEIGHT - inset * 2
            );
          }
        }
        break;

      case "row":
        // Draw entire row highlight (from visible left to right)
        if (highlight.row !== undefined) {
          const rowY = getRowPixelY(highlight.row, state.scrollY, state.rowPixelOffsets, state.rowHeights);
          const rowH = state.rowHeights.get(highlight.row) || DEFAULT_ROW_HEIGHT;

          if (rowY + rowH > HEADER_HEIGHT && rowY < viewHeight) {
            const clipY = Math.max(HEADER_HEIGHT, rowY);
            const clipH = Math.min(rowH, rowY + rowH - clipY);

            ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
            ctx.fillRect(HEADER_WIDTH, clipY, viewWidth - HEADER_WIDTH, clipH);

            ctx.strokeStyle = color.border;
            ctx.lineWidth = isHovered ? 3 : 2;
            const inset = isHovered ? 1.5 : 1;
            ctx.strokeRect(
              HEADER_WIDTH + inset,
              clipY + inset,
              viewWidth - HEADER_WIDTH - inset * 2,
              clipH - inset * 2
            );
          }
        }
        break;
    }
  }

  ctx.restore();
}
