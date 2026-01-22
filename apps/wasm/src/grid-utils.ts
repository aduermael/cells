// =============================================================================
// Grid Utilities
// =============================================================================
//
// Coordinate conversion and cell lookup functions. Maps between screen pixels,
// grid positions (col/row indices), and cell data from the sparse model.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Coordinate conversion: pixel ↔ column/row index
// - Cell lookup: getCellAt() finds cell data at grid position
// - Hit testing: detect resize handles, drop targets
// - Selection helpers: hasRangeSelection(), normalizeRange()
// - ID helpers: getColumnId(), getRowId() from sparse model
//
// Performance notes:
// - getColAtX/getRowAtY use O(n) scan for simplicity
// - getColAtXFast/getRowAtYFast use precomputed pixel offsets for O(1)
// - Use fast versions when pixel offset arrays are available
//
// =============================================================================

import {
  HEADER_WIDTH,
  HEADER_HEIGHT,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  getZoomFactor,
  getZoomedHeaderWidth,
  getZoomedHeaderHeight,
  getZoomedColWidth,
  getZoomedRowHeight,
} from "./grid-constants";
import type { Position, CellData, SheetInfo } from "./types";

// =============================================================================
// Types for Coordinate Conversion
// =============================================================================

/** Screen coordinates (pixels on canvas) */
export interface ScreenCoords {
  x: number;
  y: number;
}

/** Grid coordinates (column/row indices) */
export interface GridCoords {
  col: number;
  row: number;
}

/** Bounding box in screen coordinates */
export interface CellBounds {
  x: number;
  y: number;
  width: number;
  height: number;
}

// =============================================================================
// Centralized Coordinate Conversion Functions
// =============================================================================

/**
 * Convert screen coordinates (pixels) to grid coordinates (col/row).
 * Handles zoom factor and scroll offset automatically.
 *
 * @param screenX Screen X coordinate (pixels)
 * @param screenY Screen Y coordinate (pixels)
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param rowHeights Map of row positions to base heights
 * @param colCount Total number of columns
 * @param rowCount Total number of rows
 * @returns Grid coordinates {col, row}, or {col: -1, row: -1} if in header area
 */
export function screenToGrid(
  screenX: number,
  screenY: number,
  scrollX: number,
  scrollY: number,
  colWidths: Map<number, number>,
  rowHeights: Map<number, number>,
  colCount: number = 1000,
  rowCount: number = 1000
): GridCoords {
  const col = getColAtX(screenX, scrollX, colWidths, colCount);
  const row = getRowAtY(screenY, scrollY, rowHeights, rowCount);
  return { col, row };
}

/**
 * Convert grid coordinates (col/row) to screen coordinates (pixels).
 * Returns the top-left corner of the cell in screen space.
 * Handles zoom factor and scroll offset automatically.
 *
 * @param col Column index
 * @param row Row index
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param rowHeights Map of row positions to base heights
 * @returns Screen coordinates {x, y} of the cell's top-left corner
 */
export function gridToScreen(
  col: number,
  row: number,
  scrollX: number,
  scrollY: number,
  colWidths: Map<number, number>,
  rowHeights: Map<number, number>
): ScreenCoords {
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedScrollX = Math.round(scrollX * zoomFactor);
  const zoomedScrollY = Math.round(scrollY * zoomFactor);

  // Sum unzoomed offsets first, then zoom once to match cell renderer
  // (avoids rounding accumulation errors at non-standard zoom levels like 72%)
  let unzoomedOffsetX = 0;
  for (let c = 0; c < col; c++) {
    unzoomedOffsetX += colWidths.get(c) ?? DEFAULT_COL_WIDTH;
  }
  const x = zoomedHeaderWidth + Math.round(unzoomedOffsetX * zoomFactor) - zoomedScrollX;

  let unzoomedOffsetY = 0;
  for (let r = 0; r < row; r++) {
    unzoomedOffsetY += rowHeights.get(r) ?? DEFAULT_ROW_HEIGHT;
  }
  const y = zoomedHeaderHeight + Math.round(unzoomedOffsetY * zoomFactor) - zoomedScrollY;

  return { x, y };
}

/**
 * Get the bounding box of a cell in screen coordinates.
 * Includes position and zoomed dimensions.
 *
 * @param col Column index
 * @param row Row index
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param rowHeights Map of row positions to base heights
 * @returns Cell bounds {x, y, width, height} in screen coordinates
 */
export function getCellBounds(
  col: number,
  row: number,
  scrollX: number,
  scrollY: number,
  colWidths: Map<number, number>,
  rowHeights: Map<number, number>
): CellBounds {
  const { x, y } = gridToScreen(col, row, scrollX, scrollY, colWidths, rowHeights);
  const baseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
  const baseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;

  return {
    x,
    y,
    width: getZoomedColWidth(baseWidth),
    height: getZoomedRowHeight(baseHeight),
  };
}

/**
 * Get the bounding box for a range of cells in screen coordinates.
 * Returns the combined bounds of all cells in the range.
 *
 * @param startCol Starting column index
 * @param startRow Starting row index
 * @param endCol Ending column index (inclusive)
 * @param endRow Ending row index (inclusive)
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param rowHeights Map of row positions to base heights
 * @returns Combined bounds {x, y, width, height} in screen coordinates
 */
export function getRangeBounds(
  startCol: number,
  startRow: number,
  endCol: number,
  endRow: number,
  scrollX: number,
  scrollY: number,
  colWidths: Map<number, number>,
  rowHeights: Map<number, number>
): CellBounds {
  // Normalize to min/max
  const minCol = Math.min(startCol, endCol);
  const maxCol = Math.max(startCol, endCol);
  const minRow = Math.min(startRow, endRow);
  const maxRow = Math.max(startRow, endRow);

  // Get position of top-left cell
  const { x, y } = gridToScreen(minCol, minRow, scrollX, scrollY, colWidths, rowHeights);

  const zoomFactor = getZoomFactor();

  // Sum unzoomed widths first, then zoom once to match cell renderer
  // (avoids rounding accumulation errors at non-standard zoom levels like 72%)
  let unzoomedWidth = 0;
  for (let c = minCol; c <= maxCol; c++) {
    unzoomedWidth += colWidths.get(c) ?? DEFAULT_COL_WIDTH;
  }
  const width = Math.round(unzoomedWidth * zoomFactor);

  // Sum unzoomed heights first, then zoom once
  let unzoomedHeight = 0;
  for (let r = minRow; r <= maxRow; r++) {
    unzoomedHeight += rowHeights.get(r) ?? DEFAULT_ROW_HEIGHT;
  }
  const height = Math.round(unzoomedHeight * zoomFactor);

  return { x, y, width, height };
}

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
 * Uses global zoom factor from grid-constants
 * @param x Screen X coordinate
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param colCount Total number of columns (from sheetInfo)
 * @returns Column index, or -1 if in header area
 */
export function getColAtX(
  x: number,
  scrollX: number,
  colWidths: Map<number, number>,
  colCount: number = 1000
): number {
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedScrollX = Math.round(scrollX * zoomFactor);
  if (x < zoomedHeaderWidth) return -1;

  // Track cumulative unzoomed offset, zoom once per boundary check
  // (matches gridToScreen approach to avoid rounding accumulation errors)
  let unzoomedOffset = 0;
  let col = 0;
  while (col < colCount) {
    const baseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const nextOffset = unzoomedOffset + baseWidth;
    const nextX = zoomedHeaderWidth + Math.round(nextOffset * zoomFactor) - zoomedScrollX;
    if (nextX > x) return col;
    unzoomedOffset = nextOffset;
    col++;
  }
  return col;
}

/**
 * Get the row index at a screen Y coordinate
 * Uses O(n) linear scan - prefer getRowAtYFast with pixel offsets when available
 * Uses global zoom factor from grid-constants
 * @param y Screen Y coordinate
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param rowHeights Map of row positions to base heights
 * @param rowCount Total number of rows (from sheetInfo)
 * @returns Row index, or -1 if in header area
 */
export function getRowAtY(
  y: number,
  scrollY: number,
  rowHeights: Map<number, number>,
  rowCount: number = 1000
): number {
  const zoomFactor = getZoomFactor();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedScrollY = Math.round(scrollY * zoomFactor);
  if (y < zoomedHeaderHeight) return -1;

  // Track cumulative unzoomed offset, zoom once per boundary check
  // (matches gridToScreen approach to avoid rounding accumulation errors)
  let unzoomedOffset = 0;
  let row = 0;
  while (row < rowCount) {
    const baseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const nextOffset = unzoomedOffset + baseHeight;
    const nextY = zoomedHeaderHeight + Math.round(nextOffset * zoomFactor) - zoomedScrollY;
    if (nextY > y) return row;
    unzoomedOffset = nextOffset;
    row++;
  }
  return row;
}

// =============================================================================
// Fast Coordinate Conversion (using pre-computed pixel offsets with smart caching)
// =============================================================================

/**
 * Get the Y pixel offset for a row position.
 * Uses smart caching:
 * - O(1) if row offset is already cached
 * - O(delta) by starting from nearest cached row below target
 * - Caches computed offsets for future lookups
 */
export function getRowPixelY(
  row: number,
  scrollY: number,
  rowPixelOffsets: Map<number, number>,
  rowHeights: Map<number, number>
): number {
  // Fast path: already cached
  const cachedOffset = rowPixelOffsets.get(row);
  if (cachedOffset !== undefined) {
    return HEADER_HEIGHT + cachedOffset - scrollY;
  }

  // Find nearest cached row below target to start from
  let startRow = 0;
  let startOffset = 0;

  // Check a few candidates (row-1, row-10, row-100, etc.) for a cache hit
  // This avoids scanning the entire map while still finding nearby cached values
  for (const delta of [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000]) {
    const candidate = row - delta;
    if (candidate >= 0) {
      const offset = rowPixelOffsets.get(candidate);
      if (offset !== undefined) {
        startRow = candidate;
        startOffset = offset;
        break;
      }
    }
  }

  // Compute from startRow to target row, caching intermediate results
  let offset = startOffset;
  for (let i = startRow; i < row; i++) {
    offset += rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    // Cache this intermediate result for future lookups
    rowPixelOffsets.set(i + 1, offset);
  }

  return HEADER_HEIGHT + offset - scrollY;
}

/**
 * Get the X pixel offset for a column position.
 * Uses smart caching (same approach as getRowPixelY).
 */
export function getColPixelX(
  col: number,
  scrollX: number,
  colPixelOffsets: Map<number, number>,
  colWidths: Map<number, number>
): number {
  // Fast path: already cached
  const cachedOffset = colPixelOffsets.get(col);
  if (cachedOffset !== undefined) {
    return HEADER_WIDTH + cachedOffset - scrollX;
  }

  // Find nearest cached column below target
  let startCol = 0;
  let startOffset = 0;

  for (const delta of [1, 2, 5, 10, 20, 50, 100]) {
    const candidate = col - delta;
    if (candidate >= 0) {
      const offset = colPixelOffsets.get(candidate);
      if (offset !== undefined) {
        startCol = candidate;
        startOffset = offset;
        break;
      }
    }
  }

  // Compute from startCol to target, caching intermediate results
  let offset = startOffset;
  for (let i = startCol; i < col; i++) {
    offset += colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    colPixelOffsets.set(i + 1, offset);
  }

  return HEADER_WIDTH + offset - scrollX;
}

/**
 * Get the column index whose resize handle is at the given X coordinate
 * Uses global zoom factor from grid-constants
 * @param mouseX Mouse X coordinate
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
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
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedScrollX = Math.round(scrollX * zoomFactor);

  // Track cumulative unzoomed offset, zoom once per boundary check
  let unzoomedOffset = 0;
  for (let col = 0; col < sheetInfo.colCount; col++) {
    const baseW = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    unzoomedOffset += baseW;
    const rightEdge = zoomedHeaderWidth + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollX;
    if (
      mouseX >= rightEdge - RESIZE_HANDLE_WIDTH &&
      mouseX <= rightEdge + RESIZE_HANDLE_WIDTH
    ) {
      return col;
    }
  }
  return -1;
}

/**
 * Get the row index whose resize handle is at the given Y coordinate
 * Uses global zoom factor from grid-constants
 * @param mouseY Mouse Y coordinate
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param rowHeights Map of row positions to base heights
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
  const zoomFactor = getZoomFactor();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedScrollY = Math.round(scrollY * zoomFactor);

  // Track cumulative unzoomed offset, zoom once per boundary check
  let unzoomedOffset = 0;
  for (let row = 0; row < sheetInfo.rowCount; row++) {
    const baseH = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    unzoomedOffset += baseH;
    const bottomEdge = zoomedHeaderHeight + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollY;
    if (
      mouseY >= bottomEdge - RESIZE_HANDLE_WIDTH &&
      mouseY <= bottomEdge + RESIZE_HANDLE_WIDTH
    ) {
      return row;
    }
  }
  return -1;
}

/**
 * Get the column index for a drop target at the given X coordinate
 * Uses global zoom factor from grid-constants
 * @param mouseX Mouse X coordinate
 * @param scrollX Current horizontal scroll offset (in logical units)
 * @param colWidths Map of column positions to base widths
 * @param sheetInfo Current sheet info (for column count)
 * @returns Target column index for dropping
 */
export function getDropTargetCol(
  mouseX: number,
  scrollX: number,
  colWidths: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  const zoomFactor = getZoomFactor();
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedScrollX = Math.round(scrollX * zoomFactor);
  if (mouseX < zoomedHeaderWidth) return 0;

  // Track cumulative unzoomed offset, zoom once per boundary check
  let unzoomedOffset = 0;
  const colCount = sheetInfo?.colCount ?? 1000;
  for (let col = 0; col < colCount; col++) {
    const baseW = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const startX = zoomedHeaderWidth + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollX;
    unzoomedOffset += baseW;
    const endX = zoomedHeaderWidth + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollX;
    const midX = (startX + endX) / 2;
    if (mouseX < midX) return col;
  }
  return sheetInfo?.colCount ?? 0;
}

/**
 * Get the row index for a drop target at the given Y coordinate
 * Uses global zoom factor from grid-constants
 * @param mouseY Mouse Y coordinate
 * @param scrollY Current vertical scroll offset (in logical units)
 * @param rowHeights Map of row positions to base heights
 * @param sheetInfo Current sheet info (for row count)
 * @returns Target row index for dropping
 */
export function getDropTargetRow(
  mouseY: number,
  scrollY: number,
  rowHeights: Map<number, number>,
  sheetInfo: SheetInfo | null
): number {
  const zoomFactor = getZoomFactor();
  const zoomedHeaderHeight = getZoomedHeaderHeight();
  const zoomedScrollY = Math.round(scrollY * zoomFactor);
  if (mouseY < zoomedHeaderHeight) return 0;

  // Track cumulative unzoomed offset, zoom once per boundary check
  let unzoomedOffset = 0;
  const rowCount = sheetInfo?.rowCount ?? 1000;
  for (let row = 0; row < rowCount; row++) {
    const baseH = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const startY = zoomedHeaderHeight + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollY;
    unzoomedOffset += baseH;
    const endY = zoomedHeaderHeight + Math.round(unzoomedOffset * zoomFactor) - zoomedScrollY;
    const midY = (startY + endY) / 2;
    if (mouseY < midY) return row;
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
