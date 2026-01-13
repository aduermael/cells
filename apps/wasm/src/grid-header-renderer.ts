// Grid Header Renderer Module
// Handles rendering of column and row headers

import type { SheetInfo, Position } from "./types.js";
import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getGridColors,
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
 */
export function getFrozenColWidth(
  freezeCol: number,
  colWidths: Map<number, number>
): number {
  if (freezeCol <= 0) return 0;
  let width = 0;
  for (let col = 0; col < freezeCol; col++) {
    width += colWidths.get(col) || DEFAULT_COL_WIDTH;
  }
  return width;
}

/**
 * Get the total height of frozen rows (from row 0 to freezeRow-1).
 * Returns 0 if no rows are frozen.
 */
export function getFrozenRowHeight(
  freezeRow: number,
  rowHeights: Map<number, number>
): number {
  if (freezeRow <= 0) return 0;
  let height = 0;
  for (let row = 0; row < freezeRow; row++) {
    height += rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
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

  if (!colHasMoved) {
    // Fast path for frozen columns: no scroll offset
    if (isFrozen) {
      // Use cached offset or calculate
      const cachedOffset = state.colPixelOffsets.get(col);
      if (cachedOffset !== undefined) {
        return HEADER_WIDTH + cachedOffset;
      }
      let x = HEADER_WIDTH;
      for (let i = 0; i < col; i++) {
        x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
      }
      return x;
    }

    // Non-frozen columns: apply scroll but stay to the right of frozen area
    const cachedOffset = state.colPixelOffsets.get(col);
    if (cachedOffset !== undefined) {
      return HEADER_WIDTH + cachedOffset - state.scrollX;
    }
    // Fallback: calculate from scratch (O(n))
    let x = HEADER_WIDTH - state.scrollX;
    for (let i = 0; i < col; i++) {
      x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    }
    return x;
  }

  // Dragging case - maintain existing logic but add freeze awareness
  const sourceW = state.colWidths.get(state.dragSourceIndex) || DEFAULT_COL_WIDTH;
  let x = isFrozen ? HEADER_WIDTH : HEADER_WIDTH - state.scrollX;

  if (state.dragTargetIndex < state.dragSourceIndex) {
    for (let i = 0; i < col; i++) {
      if (i === state.dragSourceIndex) continue;
      x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    }
    if (col >= state.dragTargetIndex && col !== state.dragSourceIndex) {
      x += sourceW;
    }
  } else {
    for (let i = 0; i < col; i++) {
      if (i === state.dragSourceIndex) continue;
      x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
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

  if (!rowHasMoved) {
    // Fast path for frozen rows: no scroll offset
    if (isFrozen) {
      // Use cached offset or calculate
      const cachedOffset = state.rowPixelOffsets.get(row);
      if (cachedOffset !== undefined) {
        return HEADER_HEIGHT + cachedOffset;
      }
      let y = HEADER_HEIGHT;
      for (let i = 0; i < row; i++) {
        y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      }
      return y;
    }

    // Non-frozen rows: apply scroll
    const cachedOffset = state.rowPixelOffsets.get(row);
    if (cachedOffset !== undefined) {
      return HEADER_HEIGHT + cachedOffset - state.scrollY;
    }
    // Fallback: calculate from scratch (O(n))
    let y = HEADER_HEIGHT - state.scrollY;
    for (let i = 0; i < row; i++) {
      y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    }
    return y;
  }

  // Dragging case - maintain existing logic but add freeze awareness
  const sourceH = state.rowHeights.get(state.dragSourceIndex) || DEFAULT_ROW_HEIGHT;
  let y = isFrozen ? HEADER_HEIGHT : HEADER_HEIGHT - state.scrollY;

  if (state.dragTargetIndex < state.dragSourceIndex) {
    for (let i = 0; i < row; i++) {
      if (i === state.dragSourceIndex) continue;
      y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    }
    if (row >= state.dragTargetIndex && row !== state.dragSourceIndex) {
      y += sourceH;
    }
  } else {
    for (let i = 0; i < row; i++) {
      if (i === state.dragSourceIndex) continue;
      y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
      if (i === state.dragTargetIndex - 1) {
        y += sourceH;
      }
    }
  }
  return y;
}

/**
 * Draw column headers
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
  ctx.fillStyle = colors.headerBg;
  ctx.fillRect(HEADER_WIDTH, 0, viewWidth - HEADER_WIDTH, HEADER_HEIGHT);

  // Calculate visible column range - only iterate through visible columns
  const startCol = Math.max(0, Math.floor(state.scrollX / DEFAULT_COL_WIDTH) - 1);
  const endCol = Math.min(
    state.sheetInfo.colCount,
    startCol + Math.ceil(viewWidth / DEFAULT_COL_WIDTH) + 2
  );

  ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  for (let col = startCol; col < endCol; col++) {
    if (colHasMoved && col === state.dragSourceIndex) continue;
    const colW = state.colWidths.get(col) || DEFAULT_COL_WIDTH;
    const headerX = getDragAdjustedColX(col, state);
    if (headerX >= viewWidth || headerX + colW < HEADER_WIDTH) continue;

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
        Math.max(HEADER_WIDTH, headerX),
        0,
        Math.min(colW, headerX + colW - HEADER_WIDTH),
        HEADER_HEIGHT
      );
      ctx.fillStyle = colors.cellBg;
    } else {
      ctx.fillStyle = colors.headerText;
    }
    // Skip drawing header text if this column is being edited (editor covers it)
    if (col !== state.editingColumnIndex) {
      ctx.fillText(
        getColumnHeaderText(col, state.colNames),
        headerX + colW / 2,
        HEADER_HEIGHT / 2
      );
    }
  }

  // Column header separators (vertical lines between A, B, C...)
  ctx.strokeStyle = colors.headerSeparator;
  ctx.lineWidth = 1;
  for (let col = startCol; col < endCol; col++) {
    if (colHasMoved && col === state.dragSourceIndex) continue;
    const lineX = getDragAdjustedColX(col, state) + 0.5;
    if (lineX > HEADER_WIDTH && lineX < viewWidth) {
      ctx.beginPath();
      ctx.moveTo(lineX, 0);
      ctx.lineTo(lineX, HEADER_HEIGHT);
      ctx.stroke();
    }
  }
}

/**
 * Draw row headers
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
  ctx.fillStyle = colors.headerBg;
  ctx.fillRect(0, HEADER_HEIGHT, HEADER_WIDTH, viewHeight - HEADER_HEIGHT);

  // Use discoveredRows for virtual scrolling
  const rowCount = Math.max(state.sheetInfo.rowCount, state.discoveredRows);

  // Calculate visible row range - only iterate through visible rows
  const startRow = Math.max(0, Math.floor(state.scrollY / DEFAULT_ROW_HEIGHT) - 1);
  const endRow = Math.min(
    rowCount,
    startRow + Math.ceil(viewHeight / DEFAULT_ROW_HEIGHT) + 2
  );

  ctx.font = '12px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";

  for (let row = startRow; row < endRow; row++) {
    if (rowHasMoved && row === state.dragSourceIndex) continue;
    const rowH = state.rowHeights.get(row) || DEFAULT_ROW_HEIGHT;
    const headerY = getDragAdjustedRowY(row, state);
    if (headerY >= viewHeight || headerY + rowH < HEADER_HEIGHT) continue;

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
        Math.max(HEADER_HEIGHT, headerY),
        HEADER_WIDTH,
        Math.min(rowH, headerY + rowH - HEADER_HEIGHT)
      );
      ctx.fillStyle = colors.cellBg;
    } else {
      ctx.fillStyle = colors.headerText;
    }
    ctx.fillText(String(row + 1), HEADER_WIDTH / 2, headerY + rowH / 2);
  }

  // Row header separators (horizontal lines between 1, 2, 3...)
  ctx.strokeStyle = colors.headerSeparator;
  ctx.lineWidth = 1;
  for (let row = startRow; row < endRow; row++) {
    if (rowHasMoved && row === state.dragSourceIndex) continue;
    const lineY = getDragAdjustedRowY(row, state) + 0.5;
    if (lineY > HEADER_HEIGHT && lineY < viewHeight) {
      ctx.beginPath();
      ctx.moveTo(0, lineY);
      ctx.lineTo(HEADER_WIDTH, lineY);
      ctx.stroke();
    }
  }
}
