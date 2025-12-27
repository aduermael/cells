// Grid Header Renderer Module
// Handles rendering of column and row headers

import type { SheetInfo, Position } from "./types.js";
import {
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  COLORS,
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
 */
export function getDragAdjustedColX(
  col: number,
  state: HeaderRendererState
): number {
  const colHasMoved =
    state.isDraggingColumn &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  if (!colHasMoved) {
    let x = HEADER_WIDTH - state.scrollX;
    for (let i = 0; i < col; i++) {
      x += state.colWidths.get(i) || DEFAULT_COL_WIDTH;
    }
    return x;
  }

  const sourceW = state.colWidths.get(state.dragSourceIndex) || DEFAULT_COL_WIDTH;
  let x = HEADER_WIDTH - state.scrollX;

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
 */
export function getDragAdjustedRowY(
  row: number,
  state: HeaderRendererState
): number {
  const rowHasMoved =
    state.isDraggingRow &&
    state.dragTargetIndex !== state.dragSourceIndex &&
    state.dragTargetIndex !== state.dragSourceIndex + 1;

  if (!rowHasMoved) {
    let y = HEADER_HEIGHT - state.scrollY;
    for (let i = 0; i < row; i++) {
      y += state.rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
    }
    return y;
  }

  const sourceH = state.rowHeights.get(state.dragSourceIndex) || DEFAULT_ROW_HEIGHT;
  let y = HEADER_HEIGHT - state.scrollY;

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

  ctx.fillStyle = COLORS.headerBg;
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
      ctx.fillStyle = COLORS.selectionBorder;
      ctx.fillRect(
        Math.max(HEADER_WIDTH, headerX),
        0,
        Math.min(colW, headerX + colW - HEADER_WIDTH),
        HEADER_HEIGHT
      );
      ctx.fillStyle = "#fff";
    } else {
      ctx.fillStyle = COLORS.headerText;
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
  ctx.strokeStyle = COLORS.headerSeparator;
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

  ctx.fillStyle = COLORS.headerBg;
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
      ctx.fillStyle = COLORS.selectionBorder;
      ctx.fillRect(
        0,
        Math.max(HEADER_HEIGHT, headerY),
        HEADER_WIDTH,
        Math.min(rowH, headerY + rowH - HEADER_HEIGHT)
      );
      ctx.fillStyle = "#fff";
    } else {
      ctx.fillStyle = COLORS.headerText;
    }
    ctx.fillText(String(row + 1), HEADER_WIDTH / 2, headerY + rowH / 2);
  }

  // Row header separators (horizontal lines between 1, 2, 3...)
  ctx.strokeStyle = COLORS.headerSeparator;
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
