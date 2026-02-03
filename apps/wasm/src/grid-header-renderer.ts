// Grid Header Renderer Module
// Handles rendering of column and row headers

import type { SheetInfo, Position } from "./types.js";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
  getZoomFactor,
  getZoomedHeaderHeight,
  getZoomedHeaderWidth,
  getZoomedColWidth,
  getZoomedRowHeight,
  getZoomedFontSize,
  type NormalizedRange,
} from "./grid-constants.js";

/** State needed for header rendering */
export interface HeaderRendererState {
  sheetInfo: SheetInfo | null;
  scrollX: number;
  scrollY: number;
  colWidths: Map<number, number>;
  rowHeights: Map<number, number>;
  colNames: Map<number, string>;
  /** Pre-computed column pixel offsets for O(1) lookups */
  colPixelOffsets: Map<number, number>;
  /** Pre-computed row pixel offsets for O(1) lookups */
  rowPixelOffsets: Map<number, number>;
  selectedColumn: number | null;
  selectedRow: number | null;
  selectedCell: Position | null;
  isDraggingColumn: boolean;
  isDraggingRow: boolean;
  dragSourceIndex: number;
  dragTargetIndex: number;
  editingColumnIndex: number;
  /** Virtual scrolling: discovered row count */
  discoveredRows: number;
}

/**
 * Get the total width of frozen columns (from col 0 to freezeCol-1).
 * Returns 0 if no columns are frozen.
 * Uses global zoom factor from grid-constants
 * @param freezeCol Number of frozen columns
 * @param colWidths Map of base column widths
 */
export function getFrozenColWidth(
  freezeCol: number,
  colWidths: Map<number, number>
): number {
  if (freezeCol <= 0) return 0;
  let width = 0;
  for (let col = 0; col < freezeCol; col++) {
    const baseWidth = colWidths.get(col) || DEFAULT_COL_WIDTH;
    width += getZoomedColWidth(baseWidth);
  }
  return width;
}

/**
 * Get the total height of frozen rows (from row 0 to freezeRow-1).
 * Returns 0 if no rows are frozen.
 * Uses global zoom factor from grid-constants
 * @param freezeRow Number of frozen rows
 * @param rowHeights Map of base row heights
 */
export function getFrozenRowHeight(
  freezeRow: number,
  rowHeights: Map<number, number>
): number {
  if (freezeRow <= 0) return 0;
  let height = 0;
  for (let row = 0; row < freezeRow; row++) {
    const baseHeight = rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    height += getZoomedRowHeight(baseHeight);
  }
  return height;
}

/**
 * Convert column index to Excel-style letter (A, B, ..., Z, AA, AB, ...)
 */
export function colToLetter(col: number): string {
  let s = "";
  let n = col + 1;
  while (n > 0) {
    n--;
    s = String.fromCharCode(65 + (n % 26)) + s;
    n = Math.floor(n / 26);
  }
  return s;
}

/**
 * Get display text for a column header (custom name or default letter)
 */
export function getColumnHeaderText(
  col: number,
  colNames: Map<number, string>
): string {
  const customName = colNames.get(col);
  return customName || colToLetter(col);
}

/**
 * Compute column pixel offset efficiently in O(customCols) instead of O(col).
 * Uses formula: offset = col * DEFAULT_COL_WIDTH + sum of width adjustments for custom cols < col
 */
function computeColOffsetFast(col: number, colWidths: Map<number, number>): number {
  // Start with default width assumption for all columns
  let offset = col * DEFAULT_COL_WIDTH;

  // Add adjustments for custom column widths (only iterate custom cols, not all cols)
  for (const [customCol, width] of colWidths) {
    if (customCol < col) {
      offset += width - DEFAULT_COL_WIDTH;
    }
  }

  return offset;
}

/**
 * Get the visual X position for a column during drag operations.
 * Uses O(1) lookup via pre-computed pixel offsets when not dragging.
 * Falls back to O(customCols) calculation (NOT O(col)) when cache misses.
 * All positions are returned in zoomed pixels.
 */
export function getDragAdjustedColX(
  col: number,
  state: HeaderRendererState
): number {
  const colHasMoved =
    state.isDraggingColumn &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  // Convert scroll to zoomed pixels
  const zoomedScrollX = Math.round(state.scrollX * zoomFactor);

  if (!colHasMoved) {
    // Use cached offset or calculate (cached offsets are unzoomed)
    const cachedOffset = state.colPixelOffsets.get(col);
    if (cachedOffset !== undefined) {
      return zoomedHeaderWidth + Math.round(cachedOffset * zoomFactor) - zoomedScrollX;
    }
    // Fallback: use O(customCols) calculation instead of O(col)
    const offset = computeColOffsetFast(col, state.colWidths);
    return zoomedHeaderWidth + Math.round(offset * zoomFactor) - zoomedScrollX;
  }

  // Dragging case - use O(customCols) calculation
  const sourceBaseW = state.colWidths.get(state.dragSourceIndex) || DEFAULT_COL_WIDTH;

  // Start with fast calculation excluding source column
  let offset = computeColOffsetFast(col, state.colWidths);

  // Adjust for the source column being moved
  if (col > state.dragSourceIndex) {
    // Source column is to our left, so we've overcounted by sourceBaseW
    offset -= sourceBaseW;
  }

  // Add source column width at new position if applicable
  if (state.dragTargetIndex < state.dragSourceIndex) {
    // Moving left: insert source before target
    if (col >= state.dragTargetIndex && col !== state.dragSourceIndex) {
      offset += sourceBaseW;
    }
  } else {
    // Moving right: insert source after target-1
    if (col > state.dragSourceIndex && col <= state.dragTargetIndex - 1) {
      // These columns shift left, no change needed
    } else if (col >= state.dragTargetIndex && col !== state.dragSourceIndex) {
      offset += sourceBaseW;
    }
  }

  const zoomedOffset = Math.round(offset * zoomFactor);
  return zoomedHeaderWidth + zoomedOffset - zoomedScrollX;
}

/**
 * Compute row pixel offset efficiently in O(customRows) instead of O(row).
 * Uses formula: offset = row * DEFAULT_ROW_HEIGHT + sum of height adjustments for custom rows < row
 */
function computeRowOffsetFast(row: number, rowHeights: Map<number, number>): number {
  // Start with default height assumption for all rows
  let offset = row * DEFAULT_ROW_HEIGHT;

  // Add adjustments for custom row heights (only iterate custom rows, not all rows)
  for (const [customRow, height] of rowHeights) {
    if (customRow < row) {
      offset += height - DEFAULT_ROW_HEIGHT;
    }
  }

  return offset;
}

/**
 * Get the visual Y position for a row during drag operations.
 * Uses O(1) lookup via pre-computed pixel offsets when not dragging.
 * Falls back to O(customRows) calculation (NOT O(row)) when cache misses.
 * All positions are returned in zoomed pixels.
 */
export function getDragAdjustedRowY(
  row: number,
  state: HeaderRendererState
): number {
  const rowHasMoved =
    state.isDraggingRow &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  const zoomFactor = getZoomFactor();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  // Convert scroll to zoomed pixels
  const zoomedScrollY = Math.round(state.scrollY * zoomFactor);

  if (!rowHasMoved) {
    // Use cached offset or calculate (cached offsets are unzoomed)
    const cachedOffset = state.rowPixelOffsets.get(row);
    if (cachedOffset !== undefined) {
      return zoomedHeaderHeight + Math.round(cachedOffset * zoomFactor) - zoomedScrollY;
    }
    // Fallback: use O(customRows) calculation instead of O(row)
    const offset = computeRowOffsetFast(row, state.rowHeights);
    return zoomedHeaderHeight + Math.round(offset * zoomFactor) - zoomedScrollY;
  }

  // Dragging case - use O(customRows) calculation
  const sourceBaseH = state.rowHeights.get(state.dragSourceIndex) || DEFAULT_ROW_HEIGHT;

  // Start with fast calculation excluding source row
  let offset = computeRowOffsetFast(row, state.rowHeights);

  // Adjust for the source row being moved
  if (row > state.dragSourceIndex) {
    // Source row is above us, so we've overcounted by sourceBaseH
    offset -= sourceBaseH;
  }

  // Add source row width at new position if applicable
  if (state.dragTargetIndex < state.dragSourceIndex) {
    // Moving up: insert source before target
    if (row >= state.dragTargetIndex && row !== state.dragSourceIndex) {
      offset += sourceBaseH;
    }
  } else {
    // Moving down: insert source after target-1
    if (row > state.dragSourceIndex && row <= state.dragTargetIndex - 1) {
      // These rows shift up, no change needed
    } else if (row >= state.dragTargetIndex && row !== state.dragSourceIndex) {
      offset += sourceBaseH;
    }
  }

  const zoomedOffset = Math.round(offset * zoomFactor);
  return zoomedHeaderHeight + zoomedOffset - zoomedScrollY;
}

/**
 * Draw column headers
 * All dimensions are zoom-aware via state.zoomFactor
 */
export function drawColumnHeaders(
  ctx: CanvasRenderingContext2D,
  state: HeaderRendererState,
  viewWidth: number,
  colHasMoved: boolean,
  range: NormalizedRange | null
): void {
  if (!state.sheetInfo) return;

  const colors = getGridColors();
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedFontSize = getZoomedFontSize(12);

  ctx.fillStyle = colors.headerBg;
  ctx.fillRect(zoomedHeaderWidth, 0, viewWidth - zoomedHeaderWidth, zoomedHeaderHeight);

  // Calculate visible column range - only iterate through visible columns
  // Use unzoomed values for logical calculation
  const startCol = Math.max(0, Math.floor(state.scrollX / DEFAULT_COL_WIDTH) - 1);
  const endCol = Math.min(
    state.sheetInfo.colCount,
    startCol + Math.ceil(viewWidth / (DEFAULT_COL_WIDTH * zoomFactor)) + 2
  );

  ctx.font = `${zoomedFontSize}px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  for (let col = startCol; col < endCol; col++) {
    if (colHasMoved && col === state.dragSourceIndex) continue;
    const baseColW = state.colWidths.get(col) || DEFAULT_COL_WIDTH;
    const zoomedColW = getZoomedColWidth(baseColW);
    const headerX = getDragAdjustedColX(col, state);
    if (headerX >= viewWidth || headerX + zoomedColW < zoomedHeaderWidth) continue;

    // Check if column is in selection range or is selected column
    let isSelected = state.selectedColumn === col;
    if (!isSelected && state.selectedCell && state.selectedCell.col === col) {
      isSelected = true;
    }
    if (!isSelected && range && col >= range.minCol && col <= range.maxCol) {
      isSelected = true;
    }

    if (isSelected && !state.isDraggingColumn) {
      ctx.fillStyle = colors.selectionBorder;
      ctx.fillRect(
        Math.max(zoomedHeaderWidth, headerX),
        0,
        Math.min(zoomedColW, headerX + zoomedColW - zoomedHeaderWidth),
        zoomedHeaderHeight
      );
      ctx.fillStyle = colors.cellBg;
    } else {
      ctx.fillStyle = colors.headerText;
    }
    // Skip drawing header text if this column is being edited (editor covers it)
    if (col !== state.editingColumnIndex) {
      ctx.fillText(
        getColumnHeaderText(col, state.colNames),
        headerX + zoomedColW / 2,
        zoomedHeaderHeight / 2
      );
    }
  }

  // Column header separators (vertical lines between A, B, C...)
  ctx.strokeStyle = colors.headerSeparator;
  ctx.lineWidth = 1;
  for (let col = startCol; col < endCol; col++) {
    if (colHasMoved && col === state.dragSourceIndex) continue;
    const lineX = getDragAdjustedColX(col, state) + 0.5;
    if (lineX > zoomedHeaderWidth && lineX < viewWidth) {
      ctx.beginPath();
      ctx.moveTo(lineX, 0);
      ctx.lineTo(lineX, zoomedHeaderHeight);
      ctx.stroke();
    }
  }
}

/**
 * Draw row headers
 * All dimensions are zoom-aware via state.zoomFactor
 */
export function drawRowHeaders(
  ctx: CanvasRenderingContext2D,
  state: HeaderRendererState,
  viewHeight: number,
  rowHasMoved: boolean,
  range: NormalizedRange | null
): void {
  if (!state.sheetInfo) return;

  const colors = getGridColors();
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedFontSize = getZoomedFontSize(12);

  ctx.fillStyle = colors.headerBg;
  ctx.fillRect(0, zoomedHeaderHeight, zoomedHeaderWidth, viewHeight - zoomedHeaderHeight);

  // Use discoveredRows for virtual scrolling
  const rowCount = Math.max(state.sheetInfo.rowCount, state.discoveredRows);

  // Calculate visible row range - only iterate through visible rows
  // Use unzoomed values for logical calculation
  // Use larger buffer (-5) to handle custom row heights and rounding at different zoom levels
  const startRow = Math.max(0, Math.floor(state.scrollY / DEFAULT_ROW_HEIGHT) - 5);
  const endRow = Math.min(
    rowCount,
    startRow + Math.ceil(viewHeight / (DEFAULT_ROW_HEIGHT * zoomFactor)) + 10
  );

  ctx.font = `${zoomedFontSize}px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  for (let row = startRow; row < endRow; row++) {
    if (rowHasMoved && row === state.dragSourceIndex) continue;
    const baseRowH = state.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    const zoomedRowH = getZoomedRowHeight(baseRowH);
    const headerY = getDragAdjustedRowY(row, state);
    if (headerY >= viewHeight || headerY + zoomedRowH < zoomedHeaderHeight) continue;

    // Check if row is in selection range or is selected row
    let isSelected = state.selectedRow === row;
    if (!isSelected && state.selectedCell && state.selectedCell.row === row) {
      isSelected = true;
    }
    if (!isSelected && range && row >= range.minRow && row <= range.maxRow) {
      isSelected = true;
    }

    if (isSelected && !state.isDraggingRow) {
      ctx.fillStyle = colors.selectionBorder;
      ctx.fillRect(
        0,
        Math.max(zoomedHeaderHeight, headerY),
        zoomedHeaderWidth,
        Math.min(zoomedRowH, headerY + zoomedRowH - zoomedHeaderHeight)
      );
      ctx.fillStyle = colors.cellBg;
    } else {
      ctx.fillStyle = colors.headerText;
    }
    ctx.fillText(String(row + 1), zoomedHeaderWidth / 2, headerY + zoomedRowH / 2);
  }

  // Row header separators (horizontal lines between 1, 2, 3...)
  ctx.strokeStyle = colors.headerSeparator;
  ctx.lineWidth = 1;
  for (let row = startRow; row < endRow; row++) {
    if (rowHasMoved && row === state.dragSourceIndex) continue;
    const lineY = getDragAdjustedRowY(row, state) + 0.5;
    if (lineY > zoomedHeaderHeight && lineY < viewHeight) {
      ctx.beginPath();
      ctx.moveTo(0, lineY);
      ctx.lineTo(zoomedHeaderWidth, lineY);
      ctx.stroke();
    }
  }
}
