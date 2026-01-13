// Grid Formula Renderer Module
// Handles rendering of formula reference highlights on the grid

import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  FORMULA_REF_COLORS,
  FORMULA_ERROR_COLOR,
  getZoomedHeaderWidth,
  getZoomedHeaderHeight,
  getZoomedColWidth,
  getZoomedRowHeight,
  type FormulaHighlight,
} from "./grid-constants.js";
import { getCellBounds, getRangeBounds } from "./grid-utils.js";

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
 * Helper to get column X position and width in zoomed screen coordinates
 */
function getColumnBounds(
  col: number,
  scrollX: number,
  colWidths: Map<number, number>
): { x: number; width: number } {
  // Get the screen position of this column using gridToScreen with row 0
  // But we only need X, so we can use getCellBounds which is simpler
  const bounds = getCellBounds(col, 0, scrollX, 0, colWidths, new Map());
  const baseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
  return { x: bounds.x, width: getZoomedColWidth(baseWidth) };
}

/**
 * Helper to get row Y position and height in zoomed screen coordinates
 */
function getRowBounds(
  row: number,
  scrollY: number,
  rowHeights: Map<number, number>
): { y: number; height: number } {
  const bounds = getCellBounds(0, row, 0, scrollY, new Map(), rowHeights);
  const baseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
  return { y: bounds.y, height: getZoomedRowHeight(baseHeight) };
}

/**
 * Helper to get column range X position and total width in zoomed screen coordinates
 */
function getColumnRangeBounds(
  startCol: number,
  endCol: number,
  scrollX: number,
  colWidths: Map<number, number>
): { x: number; width: number } {
  const minCol = Math.min(startCol, endCol);
  const maxCol = Math.max(startCol, endCol);
  // Use getRangeBounds with a single row (row 0 to 0)
  const bounds = getRangeBounds(minCol, 0, maxCol, 0, scrollX, 0, colWidths, new Map());
  return { x: bounds.x, width: bounds.width };
}

/**
 * Helper to get row range Y position and total height in zoomed screen coordinates
 */
function getRowRangeBounds(
  startRow: number,
  endRow: number,
  scrollY: number,
  rowHeights: Map<number, number>
): { y: number; height: number } {
  const minRow = Math.min(startRow, endRow);
  const maxRow = Math.max(startRow, endRow);
  // Use getRangeBounds with a single column (col 0 to 0)
  const bounds = getRangeBounds(0, minRow, 0, maxRow, 0, scrollY, new Map(), rowHeights);
  return { y: bounds.y, height: bounds.height };
}

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
  // Use zoom-aware cell bounds calculation
  const bounds = getCellBounds(
    col, row,
    state.scrollX, state.scrollY,
    state.colWidths, state.rowHeights
  );
  const { x: cellX, y: cellY, width: cellW, height: cellH } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if cell is visible
  if (
    cellX + cellW <= zoomedHeaderWidth ||
    cellX >= viewWidth ||
    cellY + cellH <= zoomedHeaderHeight ||
    cellY >= viewHeight
  ) {
    return;
  }

  // Clip to visible area
  const clipX = Math.max(zoomedHeaderWidth, cellX);
  const clipY = Math.max(zoomedHeaderHeight, cellY);
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
  // Use zoom-aware range bounds calculation
  const bounds = getRangeBounds(
    startCol, startRow, endCol, endRow,
    state.scrollX, state.scrollY,
    state.colWidths, state.rowHeights
  );
  const { x: rangeX, y: rangeY, width: rangeW, height: rangeH } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if range is visible
  if (
    rangeX + rangeW <= zoomedHeaderWidth ||
    rangeX >= viewWidth ||
    rangeY + rangeH <= zoomedHeaderHeight ||
    rangeY >= viewHeight
  ) {
    return;
  }

  // Clip to visible area
  const clipX = Math.max(zoomedHeaderWidth, rangeX);
  const clipY = Math.max(zoomedHeaderHeight, rangeY);
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
          const { x: colX, width: colW } = getColumnBounds(highlight.col, state.scrollX, state.colWidths);
          const zoomedHeaderWidth = getZoomedHeaderWidth();
          const zoomedHeaderHeight = getZoomedHeaderHeight();

          if (colX + colW > zoomedHeaderWidth && colX < viewWidth) {
            const clipX = Math.max(zoomedHeaderWidth, colX);
            const clipW = Math.min(colW, colX + colW - clipX);

            ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
            ctx.fillRect(clipX, zoomedHeaderHeight, clipW, viewHeight - zoomedHeaderHeight);

            ctx.strokeStyle = color.border;
            ctx.lineWidth = isHovered ? 3 : 2;
            const inset = isHovered ? 1.5 : 1;
            ctx.strokeRect(
              clipX + inset,
              zoomedHeaderHeight + inset,
              clipW - inset * 2,
              viewHeight - zoomedHeaderHeight - inset * 2
            );
          }
        }
        break;

      case "row":
        // Draw entire row highlight (from visible left to right)
        if (highlight.row !== undefined) {
          const { y: rowY, height: rowH } = getRowBounds(highlight.row, state.scrollY, state.rowHeights);
          const zoomedHeaderWidth = getZoomedHeaderWidth();
          const zoomedHeaderHeight = getZoomedHeaderHeight();

          if (rowY + rowH > zoomedHeaderHeight && rowY < viewHeight) {
            const clipY = Math.max(zoomedHeaderHeight, rowY);
            const clipH = Math.min(rowH, rowY + rowH - clipY);

            ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
            ctx.fillRect(zoomedHeaderWidth, clipY, viewWidth - zoomedHeaderWidth, clipH);

            ctx.strokeStyle = color.border;
            ctx.lineWidth = isHovered ? 3 : 2;
            const inset = isHovered ? 1.5 : 1;
            ctx.strokeRect(
              zoomedHeaderWidth + inset,
              clipY + inset,
              viewWidth - zoomedHeaderWidth - inset * 2,
              clipH - inset * 2
            );
          }
        }
        break;

      case "named":
        // Named ranges render based on their resolved target type
        switch (highlight.namedTargetType) {
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

          case "column": {
            // Single column or column range
            const zoomedHeaderWidth = getZoomedHeaderWidth();
            const zoomedHeaderHeight = getZoomedHeaderHeight();
            if (highlight.col !== undefined) {
              const { x: colX, width: colW } = getColumnBounds(highlight.col, state.scrollX, state.colWidths);
              if (colX + colW > zoomedHeaderWidth && colX < viewWidth) {
                const clipX = Math.max(zoomedHeaderWidth, colX);
                const clipW = Math.min(colW, colX + colW - clipX);
                ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
                ctx.fillRect(clipX, zoomedHeaderHeight, clipW, viewHeight - zoomedHeaderHeight);
                ctx.strokeStyle = color.border;
                ctx.lineWidth = isHovered ? 3 : 2;
                const inset = isHovered ? 1.5 : 1;
                ctx.strokeRect(clipX + inset, zoomedHeaderHeight + inset, clipW - inset * 2, viewHeight - zoomedHeaderHeight - inset * 2);
              }
            } else if (highlight.startCol !== undefined && highlight.endCol !== undefined) {
              // Column range
              const { x: rangeX, width: rangeW } = getColumnRangeBounds(highlight.startCol, highlight.endCol, state.scrollX, state.colWidths);
              if (rangeX + rangeW > zoomedHeaderWidth && rangeX < viewWidth) {
                const clipX = Math.max(zoomedHeaderWidth, rangeX);
                const clipW = Math.min(rangeW, rangeX + rangeW - clipX);
                ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
                ctx.fillRect(clipX, zoomedHeaderHeight, clipW, viewHeight - zoomedHeaderHeight);
                ctx.strokeStyle = color.border;
                ctx.lineWidth = isHovered ? 3 : 2;
                const inset = isHovered ? 1.5 : 1;
                ctx.strokeRect(clipX + inset, zoomedHeaderHeight + inset, clipW - inset * 2, viewHeight - zoomedHeaderHeight - inset * 2);
              }
            }
            break;
          }

          case "row": {
            // Single row or row range
            const zoomedHeaderWidth = getZoomedHeaderWidth();
            const zoomedHeaderHeight = getZoomedHeaderHeight();
            if (highlight.row !== undefined) {
              const { y: rowY, height: rowH } = getRowBounds(highlight.row, state.scrollY, state.rowHeights);
              if (rowY + rowH > zoomedHeaderHeight && rowY < viewHeight) {
                const clipY = Math.max(zoomedHeaderHeight, rowY);
                const clipH = Math.min(rowH, rowY + rowH - clipY);
                ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
                ctx.fillRect(zoomedHeaderWidth, clipY, viewWidth - zoomedHeaderWidth, clipH);
                ctx.strokeStyle = color.border;
                ctx.lineWidth = isHovered ? 3 : 2;
                const inset = isHovered ? 1.5 : 1;
                ctx.strokeRect(zoomedHeaderWidth + inset, clipY + inset, viewWidth - zoomedHeaderWidth - inset * 2, clipH - inset * 2);
              }
            } else if (highlight.startRow !== undefined && highlight.endRow !== undefined) {
              // Row range
              const { y: rangeY, height: rangeH } = getRowRangeBounds(highlight.startRow, highlight.endRow, state.scrollY, state.rowHeights);
              if (rangeY + rangeH > zoomedHeaderHeight && rangeY < viewHeight) {
                const clipY = Math.max(zoomedHeaderHeight, rangeY);
                const clipH = Math.min(rangeH, rangeY + rangeH - clipY);
                ctx.fillStyle = isHovered ? color.bg.replace("0.15", "0.25") : color.bg;
                ctx.fillRect(zoomedHeaderWidth, clipY, viewWidth - zoomedHeaderWidth, clipH);
                ctx.strokeStyle = color.border;
                ctx.lineWidth = isHovered ? 3 : 2;
                const inset = isHovered ? 1.5 : 1;
                ctx.strokeRect(zoomedHeaderWidth + inset, clipY + inset, viewWidth - zoomedHeaderWidth - inset * 2, clipH - inset * 2);
              }
            }
            break;
          }
        }
        break;
    }
  }

  ctx.restore();
}
