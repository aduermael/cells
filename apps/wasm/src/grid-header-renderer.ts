// Grid Header Renderer Module
// Handles rendering of column and row headers

import type { SheetInfo, Position } from "./types.js";
import {
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
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
  /** Zoom factor (1.0 = 100%, 0.5 = 50%, 2.0 = 200%) */
  zoomFactor: number;
}

/**
 * Get the total width of frozen columns (from col 0 to freezeCol-1).
 * Returns 0 if no columns are frozen.
 * @param freezeCol Number of frozen columns
 * @param colWidths Map of base column widths
 * @param zoomFactor Zoom factor (1.0 = 100%), defaults to 1.0
 */
export function getFrozenColWidth(
  freezeCol: number,
  colWidths: Map<number, number>,
  zoomFactor: number = 1.0
): number {
  if (freezeCol <= 0) return 0;
  let width = 0;
  for (let col = 0; col < freezeCol; col++) {
    const baseWidth = colWidths.get(col) || DEFAULT_COL_WIDTH;
    width += getZoomedColWidth(baseWidth, zoomFactor);
  }
  return width;
}

/**
 * Get the total height of frozen rows (from row 0 to freezeRow-1).
 * Returns 0 if no rows are frozen.
 * @param freezeRow Number of frozen rows
 * @param rowHeights Map of base row heights
 * @param zoomFactor Zoom factor (1.0 = 100%), defaults to 1.0
 */
export function getFrozenRowHeight(
  freezeRow: number,
  rowHeights: Map<number, number>,
  zoomFactor: number = 1.0
): number {
  if (freezeRow <= 0) return 0;
  let height = 0;
  for (let row = 0; row < freezeRow; row++) {
    const baseHeight = rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    height += getZoomedRowHeight(baseHeight, zoomFactor);
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
 * Get the visual X position for a column during drag operations.
 * Uses O(1) lookup via pre-computed pixel offsets when not dragging.
 * All positions are returned in zoomed pixels.
 *
 * For frozen panes:
 * - Frozen columns (col < freezeCol) are not affected by scrollX
 * - Non-frozen columns scroll normally, but start after the frozen area
 */
export function getDragAdjustedColX(
  col: number,
  state: HeaderRendererState
): number {
  const colHasMoved =
    state.isDraggingColumn &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  const freezeCol = state.sheetInfo?.freezeCol || 0;
  const isFrozen = col < freezeCol;
  const zoomFactor = state.zoomFactor;
  const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
  // Convert scroll to zoomed pixels
  const zoomedScrollX = Math.round(state.scrollX * zoomFactor);

  if (!colHasMoved) {
    // Fast path for frozen columns: no scroll offset
    if (isFrozen) {
      // Use cached offset or calculate (cached offsets are unzoomed)
      const cachedOffset = state.colPixelOffsets.get(col);
      if (cachedOffset !== undefined) {
        return zoomedHeaderWidth + Math.round(cachedOffset * zoomFactor);
      }
      let x = zoomedHeaderWidth;
      for (let i = 0; i < col; i++) {
        const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
        x += getZoomedColWidth(baseWidth, zoomFactor);
      }
      return x;
    }

    // Non-frozen columns: apply scroll but stay to the right of frozen area
    const cachedOffset = state.colPixelOffsets.get(col);
    if (cachedOffset !== undefined) {
      return zoomedHeaderWidth + Math.round(cachedOffset * zoomFactor) - zoomedScrollX;
    }
    // Fallback: calculate from scratch (O(n))
    let x = zoomedHeaderWidth - zoomedScrollX;
    for (let i = 0; i < col; i++) {
      const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
      x += getZoomedColWidth(baseWidth, zoomFactor);
    }
    return x;
  }

  // Dragging case - maintain existing logic but add freeze awareness
  const sourceW = getZoomedColWidth(
    state.colWidths.get(state.dragSourceIndex) || DEFAULT_COL_WIDTH,
    zoomFactor
  );
  let x = isFrozen ? zoomedHeaderWidth : zoomedHeaderWidth - zoomedScrollX;

  if (state.dragTargetIndex < state.dragSourceIndex) {
    for (let i = 0; i < col; i++) {
      if (i === state.dragSourceIndex) continue;
      const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
      x += getZoomedColWidth(baseWidth, zoomFactor);
    }
    if (col >= state.dragTargetIndex && col !== state.dragSourceIndex) {
      x += sourceW;
    }
  } else {
    for (let i = 0; i < col; i++) {
      if (i === state.dragSourceIndex) continue;
      const baseWidth = state.colWidths.get(i) || DEFAULT_COL_WIDTH;
      x += getZoomedColWidth(baseWidth, zoomFactor);
      if (i === state.dragTargetIndex - 1) {
        x += sourceW;
      }
    }
  }
  return x;
}

/**
 * Get the visual Y position for a row during drag operations.
 * Uses O(1) lookup via pre-computed pixel offsets when not dragging.
 * All positions are returned in zoomed pixels.
 *
 * For frozen panes:
 * - Frozen rows (row < freezeRow) are not affected by scrollY
 * - Non-frozen rows scroll normally, but start after the frozen area
 */
export function getDragAdjustedRowY(
  row: number,
  state: HeaderRendererState
): number {
  const rowHasMoved =
    state.isDraggingRow &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  const freezeRow = state.sheetInfo?.freezeRow || 0;
  const isFrozen = row < freezeRow;
  const zoomFactor = state.zoomFactor;
  const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);
  // Convert scroll to zoomed pixels
  const zoomedScrollY = Math.round(state.scrollY * zoomFactor);

  if (!rowHasMoved) {
    // Fast path for frozen rows: no scroll offset
    if (isFrozen) {
      // Use cached offset or calculate (cached offsets are unzoomed)
      const cachedOffset = state.rowPixelOffsets.get(row);
      if (cachedOffset !== undefined) {
        return zoomedHeaderHeight + Math.round(cachedOffset * zoomFactor);
      }
      let y = zoomedHeaderHeight;
      for (let i = 0; i < row; i++) {
        const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
        y += getZoomedRowHeight(baseHeight, zoomFactor);
      }
      return y;
    }

    // Non-frozen rows: apply scroll
    const cachedOffset = state.rowPixelOffsets.get(row);
    if (cachedOffset !== undefined) {
      return zoomedHeaderHeight + Math.round(cachedOffset * zoomFactor) - zoomedScrollY;
    }
    // Fallback: calculate from scratch (O(n))
    let y = zoomedHeaderHeight - zoomedScrollY;
    for (let i = 0; i < row; i++) {
      const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      y += getZoomedRowHeight(baseHeight, zoomFactor);
    }
    return y;
  }

  // Dragging case - maintain existing logic but add freeze awareness
  const sourceH = getZoomedRowHeight(
    state.rowHeights.get(state.dragSourceIndex) || DEFAULT_ROW_HEIGHT,
    zoomFactor
  );
  let y = isFrozen ? zoomedHeaderHeight : zoomedHeaderHeight - zoomedScrollY;

  if (state.dragTargetIndex < state.dragSourceIndex) {
    for (let i = 0; i < row; i++) {
      if (i === state.dragSourceIndex) continue;
      const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      y += getZoomedRowHeight(baseHeight, zoomFactor);
    }
    if (row >= state.dragTargetIndex && row !== state.dragSourceIndex) {
      y += sourceH;
    }
  } else {
    for (let i = 0; i < row; i++) {
      if (i === state.dragSourceIndex) continue;
      const baseHeight = state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      y += getZoomedRowHeight(baseHeight, zoomFactor);
      if (i === state.dragTargetIndex - 1) {
        y += sourceH;
      }
    }
  }
  return y;
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
  const zoomFactor = state.zoomFactor;
  const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
  const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);
  const zoomedFontSize = getZoomedFontSize(12, zoomFactor);

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
    const zoomedColW = getZoomedColWidth(baseColW, zoomFactor);
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
  const zoomFactor = state.zoomFactor;
  const zoomedHeaderWidth = getZoomedHeaderWidth(zoomFactor);
  const zoomedHeaderHeight = getZoomedHeaderHeight(zoomFactor);
  const zoomedFontSize = getZoomedFontSize(12, zoomFactor);

  ctx.fillStyle = colors.headerBg;
  ctx.fillRect(0, zoomedHeaderHeight, zoomedHeaderWidth, viewHeight - zoomedHeaderHeight);

  // Use discoveredRows for virtual scrolling
  const rowCount = Math.max(state.sheetInfo.rowCount, state.discoveredRows);

  // Calculate visible row range - only iterate through visible rows
  // Use unzoomed values for logical calculation
  const startRow = Math.max(0, Math.floor(state.scrollY / DEFAULT_ROW_HEIGHT) - 1);
  const endRow = Math.min(
    rowCount,
    startRow + Math.ceil(viewHeight / (DEFAULT_ROW_HEIGHT * zoomFactor)) + 2
  );

  ctx.font = `${zoomedFontSize}px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  for (let row = startRow; row < endRow; row++) {
    if (rowHasMoved && row === state.dragSourceIndex) continue;
    const baseRowH = state.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    const zoomedRowH = getZoomedRowHeight(baseRowH, zoomFactor);
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
