// Grid Utilities - Coordinate conversion and cell lookup functions
// These utilities handle mapping between screen coordinates, grid positions,
// and cell data for the spreadsheet grid.

import {
  HEADER_WIDTH,
  HEADER_HEIGHT,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
} from "./grid-renderer";
import type { Position, CellData, SheetInfo } from "./types";

// =============================================================================
// Constants
// =============================================================================

/** Width of resize handle hot zone in pixels */
export const RESIZE_HANDLE_WIDTH = 6;

/** Minimum pixels to move before starting drag */
export const DRAG_THRESHOLD = 5;

// =============================================================================
// Coordinate Conversion
// =============================================================================

/**
 * Get the column index at a screen X coordinate
 * Uses O(n) linear scan - prefer getColAtXFast with pixel offsets when available
 * @param x Screen X coordinate
 * @param scrollX Current horizontal scroll offset
 * @param colWidths Map of column positions to widths
 * @param colCount Total number of columns (from sheetInfo)
 * @returns Column index, or -1 if in header area
 */
export function getColAtX(
  x: number,
  scrollX: number,
  colWidths: Map<number, number>,
  colCount: number = 1000
): number {
  if (x < HEADER_WIDTH) return -1;
  let accX = HEADER_WIDTH - scrollX;
  let col = 0;
  while (accX < x && col < colCount) {
    accX += colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    if (accX > x) return col;
    col++;
  }
  return col;
}

/**
 * Get the row index at a screen Y coordinate
 * Uses O(n) linear scan - prefer getRowAtYFast with pixel offsets when available
 * @param y Screen Y coordinate
 * @param scrollY Current vertical scroll offset
 * @param rowHeights Map of row positions to heights
 * @param rowCount Total number of rows (from sheetInfo)
 * @returns Row index, or -1 if in header area
 */
export function getRowAtY(
  y: number,
  scrollY: number,
  rowHeights: Map<number, number>,
  rowCount: number = 1000
): number {
  if (y < HEADER_HEIGHT) return -1;
  let accY = HEADER_HEIGHT - scrollY;
  let row = 0;
  while (accY < y && row < rowCount) {
    accY += rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    if (accY > y) return row;
    row++;
  }
  return row;
}

// =============================================================================
// Fast Coordinate Conversion (using pre-computed pixel offsets)
// =============================================================================

/**
 * Get the X pixel offset for a column position.
 * O(1) lookup using pre-computed pixel offsets from WASM ViewportIndex.
 * Falls back to O(n) loop if offset not cached.
 */
export function getColPixelX(
  col: number,
  scrollX: number,
  colPixelOffsets: Map<number, number>,
  colWidths: Map<number, number>
): number {
  const cachedOffset = colPixelOffsets.get(col);
  if (cachedOffset !== undefined) {
    return HEADER_WIDTH + cachedOffset - scrollX;
  }
  // Fallback to O(n) calculation if not in cache
  let x = HEADER_WIDTH - scrollX;
  for (let i = 0; i < col; i++) {
    x += colWidths.get(i) ?? DEFAULT_COL_WIDTH;
  }
  return x;
}

/**
 * Get the Y pixel offset for a row position.
 * O(1) lookup using pre-computed pixel offsets from WASM ViewportIndex.
 * Falls back to O(n) loop if offset not cached.
 */
export function getRowPixelY(
  row: number,
  scrollY: number,
  rowPixelOffsets: Map<number, number>,
  rowHeights: Map<number, number>
): number {
  const cachedOffset = rowPixelOffsets.get(row);
  if (cachedOffset !== undefined) {
    return HEADER_HEIGHT + cachedOffset - scrollY;
  }
  // Fallback to O(n) calculation if not in cache
  let y = HEADER_HEIGHT - scrollY;
  for (let i = 0; i < row; i++) {
    y += rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
  }
  return y;
}

/**
 * Get the column index whose resize handle is at the given X coordinate
 * @param mouseX Mouse X coordinate
 * @param scrollX Current horizontal scroll offset
 * @param colWidths Map of column positions to widths
 * @param sheetInfo Current sheet info (for column count)
 * @returns Column index for resize, or -1 if not over a resize handle
 */
export function getResizeHandleCol(
  mouseX: number,
  scrollX: number,
  colWidths: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  if (!sheetInfo) return -1;
  let x = HEADER_WIDTH - scrollX;
  for (let col = 0; col < sheetInfo.colCount; col++) {
    const colW = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const rightEdge = x + colW;
    if (
      mouseX >= rightEdge - RESIZE_HANDLE_WIDTH &&
      mouseX <= rightEdge + RESIZE_HANDLE_WIDTH
    ) {
      return col;
    }
    x = rightEdge;
  }
  return -1;
}

/**
 * Get the row index whose resize handle is at the given Y coordinate
 * @param mouseY Mouse Y coordinate
 * @param scrollY Current vertical scroll offset
 * @param rowHeights Map of row positions to heights
 * @param sheetInfo Current sheet info (for row count)
 * @returns Row index for resize, or -1 if not over a resize handle
 */
export function getResizeHandleRow(
  mouseY: number,
  scrollY: number,
  rowHeights: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  if (!sheetInfo) return -1;
  let y = HEADER_HEIGHT - scrollY;
  for (let row = 0; row < sheetInfo.rowCount; row++) {
    const rowH = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const bottomEdge = y + rowH;
    if (
      mouseY >= bottomEdge - RESIZE_HANDLE_WIDTH &&
      mouseY <= bottomEdge + RESIZE_HANDLE_WIDTH
    ) {
      return row;
    }
    y = bottomEdge;
  }
  return -1;
}

/**
 * Get the column index for a drop target at the given X coordinate
 * @param mouseX Mouse X coordinate
 * @param scrollX Current horizontal scroll offset
 * @param colWidths Map of column positions to widths
 * @param sheetInfo Current sheet info (for column count)
 * @returns Target column index for dropping
 */
export function getDropTargetCol(
  mouseX: number,
  scrollX: number,
  colWidths: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  if (mouseX < HEADER_WIDTH) return 0;
  let x = HEADER_WIDTH - scrollX;
  const colCount = sheetInfo?.colCount ?? 1000;
  for (let col = 0; col < colCount; col++) {
    const colW = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const midX = x + colW / 2;
    if (mouseX < midX) return col;
    x += colW;
  }
  return sheetInfo?.colCount ?? 0;
}

/**
 * Get the row index for a drop target at the given Y coordinate
 * @param mouseY Mouse Y coordinate
 * @param scrollY Current vertical scroll offset
 * @param rowHeights Map of row positions to heights
 * @param sheetInfo Current sheet info (for row count)
 * @returns Target row index for dropping
 */
export function getDropTargetRow(
  mouseY: number,
  scrollY: number,
  rowHeights: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  if (mouseY < HEADER_HEIGHT) return 0;
  let y = HEADER_HEIGHT - scrollY;
  const rowCount = sheetInfo?.rowCount ?? 1000;
  for (let row = 0; row < rowCount; row++) {
    const rowH = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const midY = y + rowH / 2;
    if (mouseY < midY) return row;
    y += rowH;
  }
  return sheetInfo?.rowCount ?? 0;
}

// =============================================================================
// ID Lookup
// =============================================================================

/**
 * Get column ID by position from columns array
 * @param colPos Column position to look up
 * @param columns Array of column info
 * @returns Column ID or null if not found
 */
export function getColumnId(
  colPos: number,
  columns: Array<{ id: string; pos: number }>
): string | null {
  for (const col of columns) {
    if (col.pos === colPos) return col.id;
  }
  return null;
}

/**
 * Get row ID by position from rows array
 * @param rowPos Row position to look up
 * @param rows Array of row info
 * @returns Row ID or null if not found
 */
export function getRowId(
  rowPos: number,
  rows: Array<{ id: string; pos: number }>
): string | null {
  for (const row of rows) {
    if (row.pos === rowPos) return row.id;
  }
  return null;
}

// =============================================================================
// Cell Lookup
// =============================================================================

/**
 * Find a cell at the given position
 * @param col Column position
 * @param row Row position
 * @param cells Array of cell data
 * @returns Cell data or undefined if not found
 */
export function getCellAt(
  col: number,
  row: number,
  cells: CellData[]
): CellData | undefined {
  return cells.find((c) => c.col === col && c.row === row);
}

// =============================================================================
// Column/Row Name Utilities
// =============================================================================

/**
 * Convert column index to Excel-style letter (A, B, ..., Z, AA, AB, ...)
 * @param col 0-based column index
 * @returns Excel-style column letter
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

// =============================================================================
// Selection Utilities
// =============================================================================

/** Normalized selection range with min/max coordinates */
export interface NormalizedRange {
  minCol: number;
  maxCol: number;
  minRow: number;
  maxRow: number;
}

/**
 * Get normalized range (min/max coordinates) from selection
 * @param selectionStart Selection anchor cell
 * @param selectionEnd Selection current end
 * @returns Normalized range or null if no selection
 */
export function getNormalizedRange(
  selectionStart: Position | null,
  selectionEnd: Position | null
): NormalizedRange | null {
  if (!selectionStart || !selectionEnd) return null;
  return {
    minCol: Math.min(selectionStart.col, selectionEnd.col),
    maxCol: Math.max(selectionStart.col, selectionEnd.col),
    minRow: Math.min(selectionStart.row, selectionEnd.row),
    maxRow: Math.max(selectionStart.row, selectionEnd.row),
  };
}

/**
 * Check if we have a multi-cell range selection
 * @param selectionStart Selection anchor cell
 * @param selectionEnd Selection current end
 * @returns True if selection spans multiple cells
 */
export function hasRangeSelection(
  selectionStart: Position | null,
  selectionEnd: Position | null
): boolean {
  if (!selectionStart || !selectionEnd) return false;
  return (
    selectionStart.col !== selectionEnd.col ||
    selectionStart.row !== selectionEnd.row
  );
}

/**
 * Format cell reference for display (e.g., "A1" or "A1:B3")
 * @param selectionStart Selection anchor cell
 * @param selectionEnd Selection current end (optional, for range)
 * @param colNames Optional map of custom column names
 * @returns Formatted cell reference string
 */
export function formatCellReference(
  selectionStart: Position | null,
  selectionEnd: Position | null = null,
  colNames: Map<number, string> = new Map()
): string {
  if (!selectionStart) return "";

  const startCol = colNames.get(selectionStart.col) || colToLetter(selectionStart.col);
  const startRef = `${startCol}${selectionStart.row + 1}`;

  if (!selectionEnd || !hasRangeSelection(selectionStart, selectionEnd)) {
    return startRef;
  }

  const endCol = colNames.get(selectionEnd.col) || colToLetter(selectionEnd.col);
  const endRef = `${endCol}${selectionEnd.row + 1}`;
  return `${startRef}:${endRef}`;
}
