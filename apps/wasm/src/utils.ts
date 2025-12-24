// Utilities Module
// Shared helper functions for the spreadsheet application

import type { FileFormat, Position } from "./types";

/**
 * Detect file format from filename and content
 * @param filename - The filename to check
 * @param data - The file content
 * @returns Format: 'zcd', 'csv', or 'xlsx'
 */
export function detectFormat(filename: string, data: ArrayBuffer): FileFormat {
  const ext = filename.split(".").pop()?.toLowerCase();
  if (ext === "zcd") return "zcd";
  if (ext === "csv" || ext === "tsv") return "csv";
  if (ext === "xlsx") return "xlsx";

  // Fallback: check magic bytes for XLSX (ZIP format)
  const view = new Uint8Array(data.slice(0, 4));
  if (view[0] === 0x50 && view[1] === 0x4b) return "xlsx";

  return "csv";
}

/**
 * Get base filename without extension
 * @param filename - The filename
 * @returns Filename without extension
 */
export function getBaseName(filename: string): string {
  const lastDot = filename.lastIndexOf(".");
  return lastDot > 0 ? filename.substring(0, lastDot) : filename;
}

/**
 * Convert column index to Excel-style letter (A, B, ..., Z, AA, AB, ...)
 * @param col - Zero-based column index
 * @returns Column letter
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
 * Convert Excel-style letter to column index
 * @param letter - Column letter (A, B, ..., AA, etc.)
 * @returns Zero-based column index
 */
export function letterToCol(letter: string): number {
  let col = 0;
  for (let i = 0; i < letter.length; i++) {
    col = col * 26 + (letter.charCodeAt(i) - 64);
  }
  return col - 1;
}

/**
 * Format a cell reference (e.g., "A1", "B5")
 * @param col - Zero-based column index
 * @param row - Zero-based row index
 * @returns Cell reference
 */
export function formatCellRef(col: number, row: number): string {
  return colToLetter(col) + (row + 1);
}

/**
 * Parse a cell reference (e.g., "A1" -> { col: 0, row: 0 })
 * @param ref - Cell reference
 * @returns Parsed cell position or null
 */
export function parseCellRef(ref: string): Position | null {
  const match = ref.match(/^([A-Z]+)(\d+)$/i);
  if (!match?.[1] || !match[2]) return null;
  return {
    col: letterToCol(match[1].toUpperCase()),
    row: parseInt(match[2], 10) - 1,
  };
}

/**
 * Format a range reference (e.g., "A1:B5")
 * @param startCol - Start column (zero-based)
 * @param startRow - Start row (zero-based)
 * @param endCol - End column (zero-based)
 * @param endRow - End row (zero-based)
 * @returns Range reference
 */
export function formatRangeRef(
  startCol: number,
  startRow: number,
  endCol: number,
  endRow: number,
): string {
  if (startCol === endCol && startRow === endRow) {
    return formatCellRef(startCol, startRow);
  }
  return formatCellRef(startCol, startRow) + ":" + formatCellRef(endCol, endRow);
}

/**
 * Create a debounced version of a function
 * @param fn - Function to debounce
 * @param delay - Delay in milliseconds
 * @returns Debounced function
 */
export function debounce<T extends (...args: Parameters<T>) => void>(
  fn: T,
  delay: number,
): (...args: Parameters<T>) => void {
  let timeoutId: ReturnType<typeof setTimeout> | undefined;
  return function (this: ThisParameterType<T>, ...args: Parameters<T>) {
    clearTimeout(timeoutId);
    timeoutId = setTimeout(() => fn.apply(this, args), delay);
  };
}

/**
 * Create a throttled version of a function
 * @param fn - Function to throttle
 * @param limit - Minimum time between calls in milliseconds
 * @returns Throttled function
 */
export function throttle<T extends (...args: Parameters<T>) => void>(
  fn: T,
  limit: number,
): (...args: Parameters<T>) => void {
  let inThrottle = false;
  return function (this: ThisParameterType<T>, ...args: Parameters<T>) {
    if (!inThrottle) {
      fn.apply(this, args);
      inThrottle = true;
      setTimeout(() => (inThrottle = false), limit);
    }
  };
}

/**
 * Download a blob as a file
 * @param blob - The blob to download
 * @param filename - The filename
 */
export function downloadBlob(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

/**
 * Get MIME type for a file format
 * @param format - File format ('xlsx', 'csv', 'zcd')
 * @returns MIME type
 */
export function getMimeType(format: FileFormat): string {
  switch (format) {
    case "xlsx":
      return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    case "csv":
      return "text/csv";
    case "zcd":
    default:
      return "text/plain";
  }
}

/**
 * Clamp a value between min and max
 * @param value - Value to clamp
 * @param min - Minimum value
 * @param max - Maximum value
 * @returns Clamped value
 */
export function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}
