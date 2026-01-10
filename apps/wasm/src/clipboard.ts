// =============================================================================
// Clipboard Manager
// =============================================================================
//
// Copy, cut, and paste operations for spreadsheet cells. Supports both
// internal JSON format (preserving formulas) and external TSV format.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Copy selection to system clipboard (JSON + TSV formats)
// - Cut selection (copy + mark for deletion on paste)
// - Paste from clipboard (adjust formula references)
// - Handle external paste (TSV from Excel/Sheets)
// - Track cut state for visual feedback (dashed border)
//
// Clipboard formats:
// - Internal: JSON with cell values, formulas, types, and format IDs
// - External: TSV (tab-separated values) for cross-app compatibility
//
// Formula adjustment:
// - Relative references (A1) are adjusted based on paste offset
// - Absolute references ($A$1) remain unchanged
// - Uses TypeScript implementation for browser clipboard API
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { CellData, Position } from "./types";
import { getNormalizedRange, getCellAt } from "./grid-utils";

// =============================================================================
// Types
// =============================================================================

/** Internal clipboard data format */
export interface ClipboardData {
  /** Number of rows in the copied region */
  rows: number;
  /** Number of columns in the copied region */
  cols: number;
  /** Cell data relative to top-left of selection */
  cells: ClipboardCell[];
  /** Source top-left column (0-indexed, for formula reference adjustment) */
  sourceCol?: number;
  /** Source top-left row (0-indexed, for formula reference adjustment) */
  sourceRow?: number;
}

/** Cell entry in clipboard data */
export interface ClipboardCell {
  /** Relative row within selection (0-indexed from top-left) */
  row: number;
  /** Relative column within selection (0-indexed from top-left) */
  col: number;
  /** Raw cell value (for internal paste operations) */
  value: string;
  /** Formatted display value (for TSV export to external apps) */
  display?: string;
  /** Formula if this is a formula cell (without leading =) */
  formula?: string;
  /** Cell type */
  type: CellData["type"];
  /** Number format ID (e.g., "FMT_P002" for percentage with 2 decimals) */
  formatId?: string;
  /** Style ID for cell styling */
  styleId?: string;
  /** Style properties (if available from viewport data) */
  style?: CellData["style"];
}


// =============================================================================
// ClipboardManager Class
// =============================================================================

/**
 * ClipboardManager handles copy, cut, and paste operations for cells.
 *
 * Features:
 * - Copy selection to clipboard (JSON + TSV formats)
 * - Cut selection (copy + delete)
 * - Paste from clipboard (internal JSON or external TSV)
 * - Multi-cell rectangular region support
 */
export class ClipboardManager {
  private dataSource: WasmDataSource | null = null;

  // State accessors
  private getSelectionStart: () => Position | null;
  private getSelectionEnd: () => Position | null;
  private getSelectedCell: () => Position | null;
  private getCells: () => CellData[];

  // Callbacks
  private onFetchViewport: () => void | Promise<void>;
  private onRender: () => void;

  constructor(config: {
    getSelectionStart: () => Position | null;
    getSelectionEnd: () => Position | null;
    getSelectedCell: () => Position | null;
    getCells: () => CellData[];
    onFetchViewport: () => void | Promise<void>;
    onRender: () => void;
  }) {
    this.getSelectionStart = config.getSelectionStart;
    this.getSelectionEnd = config.getSelectionEnd;
    this.getSelectedCell = config.getSelectedCell;
    this.getCells = config.getCells;
    this.onFetchViewport = config.onFetchViewport;
    this.onRender = config.onRender;
  }

  /** Set the data source for WASM operations */
  setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
  }

  // =========================================================================
  // Copy
  // =========================================================================

  // Marker prefix for internal clipboard data (JSON embedded in text/plain)
  private static readonly CLIPBOARD_MARKER = "CELLS_CLIPBOARD::";

  /**
   * Copy the current selection to clipboard
   * @returns true if copy succeeded
   */
  async copy(): Promise<boolean> {
    const range = this.getSelectionRange();
    if (!range) return false;

    const clipboardData = this.serializeSelection(range);
    if (!clipboardData) return false;

    const tsvText = this.toTSV(clipboardData);
    const jsonText = JSON.stringify(clipboardData);

    // Embed JSON in text/plain with a marker for reliable internal paste detection.
    // Format: CELLS_CLIPBOARD::<json>\n\n<tsv>
    // This ensures format preservation works even when custom MIME types fail.
    const combinedText = `${ClipboardManager.CLIPBOARD_MARKER}${jsonText}\n\n${tsvText}`;

    try {
      await navigator.clipboard.writeText(combinedText);
      return true;
    } catch (err) {
      console.error("Failed to copy to clipboard:", err);
      return false;
    }
  }

  // =========================================================================
  // Cut
  // =========================================================================

  /**
   * Cut the current selection (copy + delete)
   * @returns true if cut succeeded
   */
  async cut(): Promise<boolean> {
    if (!this.dataSource) return false;

    // First copy
    const copySuccess = await this.copy();
    if (!copySuccess) return false;

    // Then delete the cells
    const range = this.getSelectionRange();
    if (!range) return false;

    try {
      for (let col = range.minCol; col <= range.maxCol; col++) {
        for (let row = range.minRow; row <= range.maxRow; row++) {
          await this.dataSource.deleteCellAt(col, row);
        }
      }
      return true;
    } catch (err) {
      console.error("Failed to delete cells after cut:", err);
      return false;
    }
  }

  // =========================================================================
  // Paste
  // =========================================================================

  /**
   * Paste from clipboard at the current selection
   * @returns true if paste succeeded
   */
  async paste(): Promise<boolean> {
    if (!this.dataSource) return false;

    const targetPos = this.getSelectedCell();
    if (!targetPos) return false;

    try {
      const text = await navigator.clipboard.readText();
      if (!text) return false;

      let clipboardData: ClipboardData | null = null;

      // Check for our internal format marker
      if (text.startsWith(ClipboardManager.CLIPBOARD_MARKER)) {
        // Internal paste: extract JSON from our format
        // Format: CELLS_CLIPBOARD::<json>\n\n<tsv>
        const jsonStart = ClipboardManager.CLIPBOARD_MARKER.length;
        const jsonEnd = text.indexOf("\n\n");
        if (jsonEnd > jsonStart) {
          try {
            const jsonText = text.substring(jsonStart, jsonEnd);
            clipboardData = JSON.parse(jsonText) as ClipboardData;
          } catch {
            // JSON parse failed, fall back to TSV
          }
        }
      }

      // Fall back to TSV parsing (external paste or parse error)
      if (!clipboardData) {
        // If our marker was present but JSON failed, extract TSV portion
        let tsvText = text;
        if (text.startsWith(ClipboardManager.CLIPBOARD_MARKER)) {
          const tsvStart = text.indexOf("\n\n");
          if (tsvStart > 0) {
            tsvText = text.substring(tsvStart + 2);
          }
        }
        clipboardData = this.parseTSV(tsvText);
      }

      if (!clipboardData) return false;

      // Paste the cells at target position
      await this.pasteClipboardData(clipboardData, targetPos);
      return true;
    } catch (err) {
      console.error("Failed to paste from clipboard:", err);
      return false;
    }
  }

  // =========================================================================
  // Private Helpers
  // =========================================================================

  /**
   * Get the normalized selection range
   */
  private getSelectionRange(): ReturnType<typeof getNormalizedRange> {
    const selStart = this.getSelectionStart();
    const selEnd = this.getSelectionEnd();

    // If no range selection, use selected cell as single-cell range
    if (!selStart || !selEnd) {
      const cell = this.getSelectedCell();
      if (!cell) return null;
      return {
        minCol: cell.col,
        maxCol: cell.col,
        minRow: cell.row,
        maxRow: cell.row,
      };
    }

    return getNormalizedRange(selStart, selEnd);
  }

  /**
   * Serialize the current selection to clipboard data format
   */
  private serializeSelection(range: NonNullable<ReturnType<typeof getNormalizedRange>>): ClipboardData {
    const cells = this.getCells();
    const clipboardCells: ClipboardCell[] = [];

    const rows = range.maxRow - range.minRow + 1;
    const cols = range.maxCol - range.minCol + 1;

    // Find cells in the range
    for (let row = range.minRow; row <= range.maxRow; row++) {
      for (let col = range.minCol; col <= range.maxCol; col++) {
        const cell = getCellAt(col, row, cells);
        if (cell) {
          const clipCell: ClipboardCell = {
            row: row - range.minRow,
            col: col - range.minCol,
            // Store raw value for internal paste operations
            value: cell.value || "",
            type: cell.type,
          };

          // Store formatted display value for TSV export (if different from raw)
          if (cell.display && cell.display !== cell.value) {
            clipCell.display = cell.display;
          }

          // Include formula if present
          if (cell.formula) {
            clipCell.formula = cell.formula;
          }

          // Include format ID if present (non-GENERAL format)
          // Format ID "~" means GENERAL, so we skip it
          if (cell.formatId && cell.formatId !== "~") {
            clipCell.formatId = cell.formatId;
          }

          // Include style ID and style properties if present
          if (cell.styleId && cell.styleId !== "~") {
            clipCell.styleId = cell.styleId;
          }
          if (cell.style) {
            clipCell.style = cell.style;
          }

          clipboardCells.push(clipCell);
        }
      }
    }

    // Store source position for formula reference adjustment on paste
    return {
      rows,
      cols,
      cells: clipboardCells,
      sourceCol: range.minCol,
      sourceRow: range.minRow,
    };
  }

  /**
   * Convert clipboard data to TSV format for external apps
   */
  private toTSV(data: ClipboardData): string {
    // Create a 2D array for the TSV
    const grid: string[][] = [];
    for (let r = 0; r < data.rows; r++) {
      const row: string[] = [];
      for (let c = 0; c < data.cols; c++) {
        row[c] = "";
      }
      grid[r] = row;
    }

    // Fill in the values
    for (const cell of data.cells) {
      // Use formula if present, otherwise use display value (formatted) for external apps
      // Formula is stored with "=" prefix from WASM, so use as-is
      const row = grid[cell.row];
      if (row) {
        row[cell.col] = cell.formula || cell.display || cell.value;
      }
    }

    // Convert to TSV
    return grid.map((row) => row.map(escapeForTSV).join("\t")).join("\n");
  }

  /**
   * Parse TSV text into clipboard data
   */
  private parseTSV(text: string): ClipboardData | null {
    if (!text.trim()) return null;

    const lines = text.split(/\r?\n/);
    const cells: ClipboardCell[] = [];

    let maxCols = 0;
    const rows = lines.length;

    for (let rowIdx = 0; rowIdx < lines.length; rowIdx++) {
      const line = lines[rowIdx];
      if (line === undefined) continue;
      // Handle quoted fields and tabs
      const values = parseTSVLine(line);
      maxCols = Math.max(maxCols, values.length);

      for (let colIdx = 0; colIdx < values.length; colIdx++) {
        const value = values[colIdx];
        if (value) {
          cells.push({
            row: rowIdx,
            col: colIdx,
            value,
            type: inferCellType(value),
          });
        }
      }
    }

    return { rows, cols: maxCols, cells };
  }

  /**
   * Paste clipboard data at the target position
   */
  private async pasteClipboardData(
    data: ClipboardData,
    targetPos: Position
  ): Promise<void> {
    if (!this.dataSource) return;

    // Calculate offset for formula reference adjustment
    // If we have source position info, use it to compute the offset
    const colOffset =
      data.sourceCol !== undefined ? targetPos.col - data.sourceCol : 0;
    const rowOffset =
      data.sourceRow !== undefined ? targetPos.row - data.sourceRow : 0;

    for (const cell of data.cells) {
      const targetCol = targetPos.col + cell.col;
      const targetRow = targetPos.row + cell.row;

      // Determine what value to set
      let valueToSet: string;

      if (cell.formula) {
        // For formula cells, adjust references based on paste offset
        // Only adjust if there's actually an offset
        if (colOffset !== 0 || rowOffset !== 0) {
          valueToSet = adjustFormulaReferences(
            cell.formula,
            colOffset,
            rowOffset
          );
        } else {
          valueToSet = cell.formula;
        }
      } else {
        // For non-formula cells, use the value as-is
        valueToSet = cell.value;
      }

      if (valueToSet) {
        try {
          await this.dataSource.createCell(targetCol, targetRow, valueToSet);
        } catch (err) {
          // Cell might already exist, try updating it
          try {
            const existing = await this.dataSource.getOrCreateCellAt(
              targetCol,
              targetRow
            );
            await this.dataSource.updateCell(existing.id, valueToSet);
          } catch (updateErr) {
            console.error(
              `Failed to paste cell at (${targetCol}, ${targetRow}):`,
              updateErr
            );
          }
        }

        // Apply format if present (only for internal paste, not TSV from external apps)
        // We detect internal paste by checking if sourceCol/sourceRow are set
        if (cell.formatId && data.sourceCol !== undefined) {
          try {
            await this.dataSource.setCellFormatAt(
              targetCol,
              targetRow,
              cell.formatId
            );
          } catch (formatErr) {
            console.error(
              `Failed to set format at (${targetCol}, ${targetRow}):`,
              formatErr
            );
          }
        }

        // Apply style if present (only for internal paste)
        if (cell.style && data.sourceCol !== undefined) {
          try {
            await this.dataSource.setCellStyleAt(
              targetCol,
              targetRow,
              cell.style
            );
          } catch (styleErr) {
            console.error(
              `Failed to set style at (${targetCol}, ${targetRow}):`,
              styleErr
            );
          }
        }
      }
    }

    // Refresh the viewport
    await this.onFetchViewport();
    this.onRender();
  }
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Escape a value for TSV format
 */
function escapeForTSV(value: string): string {
  // If value contains tab, newline, or quotes, quote it
  if (/[\t\n\r"]/.test(value)) {
    return '"' + value.replace(/"/g, '""') + '"';
  }
  return value;
}

/**
 * Parse a single TSV line, handling quoted fields
 */
function parseTSVLine(line: string): string[] {
  const result: string[] = [];
  let current = "";
  let inQuotes = false;
  let i = 0;

  while (i < line.length) {
    const char = line[i];

    if (inQuotes) {
      if (char === '"') {
        // Check for escaped quote
        if (i + 1 < line.length && line[i + 1] === '"') {
          current += '"';
          i += 2;
        } else {
          // End of quoted field
          inQuotes = false;
          i++;
        }
      } else {
        current += char;
        i++;
      }
    } else {
      if (char === '"') {
        // Start of quoted field
        inQuotes = true;
        i++;
      } else if (char === "\t") {
        // Field separator
        result.push(current);
        current = "";
        i++;
      } else {
        current += char;
        i++;
      }
    }
  }

  // Don't forget the last field
  result.push(current);

  return result;
}

/**
 * Infer cell type from value
 */
function inferCellType(value: string): CellData["type"] {
  // Check for formula
  if (value.startsWith("=")) {
    return "f";
  }

  // Check for number
  const num = parseFloat(value);
  if (!isNaN(num) && isFinite(num) && value.trim() !== "") {
    return "n";
  }

  // Check for boolean
  if (value.toLowerCase() === "true" || value.toLowerCase() === "false") {
    return "b";
  }

  // Default to string
  return "s";
}

// =============================================================================
// Formula Reference Adjustment
// =============================================================================

/**
 * Convert column index (0-based) to Excel letter (A, B, ..., Z, AA, AB, ...)
 */
function columnIndexToLetter(index: number): string {
  let result = "";
  let n = index + 1; // Convert to 1-based (A=1, not A=0)
  while (n > 0) {
    n--; // Adjust back for 0-based letter calculation
    result = String.fromCharCode("A".charCodeAt(0) + (n % 26)) + result;
    n = Math.floor(n / 26);
  }
  return result;
}

/**
 * Convert Excel column letter to index (0-based)
 * Returns -1 if invalid
 */
function columnLetterToIndex(letter: string): number {
  if (!letter) return -1;

  let result = 0;
  for (const c of letter.toUpperCase()) {
    if (c >= "A" && c <= "Z") {
      result = result * 26 + (c.charCodeAt(0) - "A".charCodeAt(0) + 1);
    } else {
      return -1; // Invalid character
    }
  }
  return result - 1; // Convert to 0-based
}

/** Parsed cell reference */
interface CellRef {
  colIndex: number;
  rowIndex: number;
  colAbsolute: boolean;
  rowAbsolute: boolean;
  valid: boolean;
}

/**
 * Format a CellRef back to A1 notation
 */
function formatA1Ref(ref: CellRef): string {
  if (!ref.valid) return "";

  let result = "";

  // Add absolute column marker if needed
  if (ref.colAbsolute) {
    result += "$";
  }

  // Add column letter
  result += columnIndexToLetter(ref.colIndex);

  // Add absolute row marker if needed
  if (ref.rowAbsolute) {
    result += "$";
  }

  // Add row number (1-based)
  result += (ref.rowIndex + 1).toString();

  return result;
}

/**
 * Adjust a single cell reference by the given offsets
 * Returns the adjusted A1 notation, or "#REF!" if the adjustment would be invalid
 */
function adjustSingleRef(
  ref: CellRef,
  colOffset: number,
  rowOffset: number
): string {
  // Start with the original ref
  const adjusted = { ...ref };

  // Adjust column if relative
  if (!ref.colAbsolute) {
    const newCol = ref.colIndex + colOffset;
    if (newCol < 0) {
      return "#REF!";
    }
    adjusted.colIndex = newCol;
  }

  // Adjust row if relative
  if (!ref.rowAbsolute) {
    const newRow = ref.rowIndex + rowOffset;
    if (newRow < 0) {
      return "#REF!";
    }
    adjusted.rowIndex = newRow;
  }

  return formatA1Ref(adjusted);
}

/**
 * Check if character is a column letter
 */
function isColumnChar(c: string): boolean {
  return (c >= "A" && c <= "Z") || (c >= "a" && c <= "z");
}

/**
 * Check if we're at the start of an A1 ref pattern
 */
function isA1RefStart(formula: string, pos: number): boolean {
  if (pos >= formula.length) return false;
  const c = formula[pos];
  return c === "$" || isColumnChar(c!);
}

/**
 * Extract A1 ref at position, returns [length consumed, CellRef]
 */
function extractA1Ref(formula: string, pos: number): [number, CellRef] {
  const start = pos;
  const ref: CellRef = {
    colIndex: 0,
    rowIndex: 0,
    colAbsolute: false,
    rowAbsolute: false,
    valid: false,
  };

  // Check for absolute column marker
  if (pos < formula.length && formula[pos] === "$") {
    ref.colAbsolute = true;
    pos++;
  }

  // Parse column letters
  let colLetters = "";
  while (pos < formula.length && isColumnChar(formula[pos]!)) {
    colLetters += formula[pos]!.toUpperCase();
    pos++;
  }

  if (!colLetters) return [0, ref]; // No column letters found

  // Check for absolute row marker
  if (pos < formula.length && formula[pos] === "$") {
    ref.rowAbsolute = true;
    pos++;
  }

  // Parse row number
  let rowDigits = "";
  while (pos < formula.length && /[0-9]/.test(formula[pos]!)) {
    rowDigits += formula[pos];
    pos++;
  }

  if (!rowDigits) return [0, ref]; // No row number found - not a valid cell ref

  // Convert to indices
  const colIdx = columnLetterToIndex(colLetters);
  if (colIdx < 0) return [0, ref];

  const rowNum = parseInt(rowDigits, 10);
  if (rowNum < 1) return [0, ref];

  ref.colIndex = colIdx;
  ref.rowIndex = rowNum - 1;
  ref.valid = true;

  return [pos - start, ref];
}

/**
 * Adjust cell references in a formula by the given row and column offsets.
 * Only relative references are adjusted; absolute references ($A$1) are preserved.
 * The formula should be in A1 notation (e.g., "=A1+B2", "=$A$1+B2").
 *
 * @param formula - The formula string in A1 notation (with leading '=')
 * @param colOffset - Number of columns to shift relative column references
 * @param rowOffset - Number of rows to shift relative row references
 * @returns The adjusted formula string
 *
 * Examples:
 *   adjustFormulaReferences("=A1+B2", 1, 1)     -> "=B2+C3"
 *   adjustFormulaReferences("=$A$1+B2", 1, 1)   -> "=$A$1+C3"
 *   adjustFormulaReferences("=$A1+B$2", 1, 1)   -> "=$A2+C$2"
 *   adjustFormulaReferences("=A1", -1, 0)       -> "=#REF!" (column would be negative)
 */
export function adjustFormulaReferences(
  formula: string,
  colOffset: number,
  rowOffset: number
): string {
  if (!formula) return formula;

  let result = "";
  let i = 0;

  while (i < formula.length) {
    // Skip if we're in a string literal
    if (formula[i] === '"') {
      result += formula[i++];
      while (i < formula.length && formula[i] !== '"') {
        if (formula[i] === "\\" && i + 1 < formula.length) {
          result += formula[i++];
        }
        result += formula[i++];
      }
      if (i < formula.length) {
        result += formula[i++]; // Closing quote
      }
      continue;
    }

    // Check for A1 reference
    // An A1 ref should not be preceded by an alphanumeric character
    const canStartRef = i === 0 || !/[a-zA-Z0-9]/.test(formula[i - 1]!);

    if (canStartRef && isA1RefStart(formula, i)) {
      const [len, ref] = extractA1Ref(formula, i);

      if (len > 0 && ref.valid) {
        // Check if we're parsing a range (next char is ':')
        if (i + len < formula.length && formula[i + len] === ":") {
          // Range reference - adjust both parts
          const startRefStr = adjustSingleRef(ref, colOffset, rowOffset);

          // Parse the end of the range
          const [endLen, endRef] = extractA1Ref(formula, i + len + 1);

          if (endLen > 0 && endRef.valid) {
            const endRefStr = adjustSingleRef(endRef, colOffset, rowOffset);
            result += startRefStr;
            result += ":";
            result += endRefStr;
            i += len + 1 + endLen;
            continue;
          }
        }

        // Single cell reference - adjust it
        const adjustedRef = adjustSingleRef(ref, colOffset, rowOffset);
        result += adjustedRef;
        i += len;
        continue;
      }
    }

    // Not a reference, copy character as-is
    result += formula[i];
    i++;
  }

  return result;
}
