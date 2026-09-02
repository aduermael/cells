// =============================================================================
// WASM Data Source
// =============================================================================
//
// Facade over CellsClient that provides a clean interface for UI components
// to interact with the spreadsheet engine.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Manage workbook metadata (name, file info)
// - Subscribe to and dispatch change notifications
// - Provide simplified async API for common operations
// - Handle file export with proper MIME types
//
// Design:
// - Wraps CellsClient to add UI-specific concerns
// - Tracks workbook name separately from engine state
// - Routes change events to registered listeners
// - Used by init.ts to wire up the application
//
// =============================================================================

import type { CellsClient } from "./client";
import type {
  SheetInfo,
  CellData,
  ColumnInfo,
  RowInfo,
  FormatProperties,
  FormatTemplate,
  ParsedInputResult,
  FormattedValueResult,
  CellFormatResult,
  FunctionInfo,
  NamedRangeInfo,
  CellStyle,
  RegisteredStyle,
  WorkbookTheme,
  CellStylePreset,
} from "./types";
import type { LuauToken, AutocompleteResult } from "./client-types";
import { getMimeType, toSnakeCase } from "./utils";

/** Change notification types */
export type DataChangeType = "cell" | "structure" | "sheet" | "loaded";

/** Change notification callback */
export type OnChangeCallback = (changeType: DataChangeType) => void;

/** Export result */
export interface ExportResult {
  blob: Blob;
  filename: string;
}

/**
 * WasmDataSource - Facade for WASM worker communication
 *
 * Wraps CellsClient to provide:
 * - Workbook metadata management (name)
 * - Change notification subscription
 * - Clean async API for all data operations
 */
export class WasmDataSource {
  private _client: CellsClient;
  private _workbookName: string = "Untitled";

  constructor(client: CellsClient) {
    this._client = client;
  }

  /** Get the underlying client for direct access when needed */
  get client(): CellsClient {
    return this._client;
  }

  /** Get current workbook name */
  get workbookName(): string {
    return this._workbookName;
  }

  /** Set workbook name (for display and export) */
  setWorkbookName(name: string): void {
    this._workbookName = name;
  }

  /**
   * Subscribe to change notifications
   * @param callback Receives change type: 'cell', 'structure', 'sheet', or 'loaded'
   */
  setOnChange(callback: OnChangeCallback): void {
    this._client.setOnDataChanged(callback);
  }

  /** Unsubscribe from change notifications */
  removeOnChange(): void {
    this._client.removeOnDataChanged();
  }

  // ==========================================================================
  // Sheet Information
  // ==========================================================================

  /** Get current sheet information */
  async getSheetInfo(): Promise<SheetInfo> {
    return this._client.getSheetInfo();
  }

  // ==========================================================================
  // Viewport Queries
  // ==========================================================================

  /** Get cells and axes in viewport */
  async getViewport(
    x1: number,
    y1: number,
    x2: number,
    y2: number
  ): Promise<{
    cells: CellData[];
    columns: ColumnInfo[];
    rows: RowInfo[];
    styleRanges?: Array<{
      startCol: number;
      startRow: number;
      endCol: number;
      endRow: number;
      style: { bgColor?: string; textColor?: string };
    }>;
    axisStyles?: Array<{
      type: "column" | "row";
      position: number;
      style: { bgColor?: string; textColor?: string };
    }>;
  }> {
    const result = await this._client.queryViewport(x1, y1, x2, y2);
    // Cast cells since WASM returns string type but runtime values are valid
    return {
      cells: result.cells as CellData[],
      columns: result.columns as ColumnInfo[],
      rows: result.rows as RowInfo[],
      styleRanges: (result as { styleRanges?: unknown }).styleRanges as Array<{
        startCol: number;
        startRow: number;
        endCol: number;
        endRow: number;
        style: { bgColor?: string; textColor?: string };
      }> | undefined,
      axisStyles: (result as { axisStyles?: unknown }).axisStyles as Array<{
        type: "column" | "row";
        position: number;
        style: { bgColor?: string; textColor?: string };
      }> | undefined,
    };
  }

  // ==========================================================================
  // Cell Operations
  // ==========================================================================

  /** Create a new cell at the specified position */
  async createCell(
    col: number,
    row: number,
    value: string
  ): Promise<{ id: string }> {
    return this._client.createCell(col, row, value);
  }

  /** Get or create a cell at the specified position */
  async getOrCreateCellAt(
    col: number,
    row: number
  ): Promise<{ id: string; value: string; editValue: string; formula?: string | null; existed: boolean }> {
    return this._client.getOrCreateCellAt(col, row);
  }

  /** Delete cell at position */
  async deleteCellAt(col: number, row: number): Promise<{ deleted: boolean }> {
    return this._client.deleteCellAt(col, row);
  }

  /** Update cell value by ID */
  async updateCell(cellId: string, value: string): Promise<{ success: true }> {
    await this._client.updateCell(cellId, value);
    return { success: true };
  }

  /** Update cell value with automatic format detection */
  async updateCellWithFormatDetection(
    cellId: string,
    value: string
  ): Promise<{ success: boolean; formatId?: string }> {
    return this._client.updateCellWithFormatDetection(cellId, value);
  }

  /** Delete cell by ID */
  async deleteCell(cellId: string): Promise<{ success: true }> {
    await this._client.deleteCell(cellId);
    return { success: true };
  }

  // ==========================================================================
  // Number Format Operations
  // ==========================================================================

  /** Set cell format by cell ID */
  async setCellFormat(cellId: string, format: FormatProperties): Promise<{ success: boolean }> {
    return this._client.setCellFormat(cellId, format);
  }

  /** Set cell format by position */
  async setCellFormatAt(col: number, row: number, format: FormatProperties): Promise<{ success: boolean }> {
    return this._client.setCellFormatAt(col, row, format);
  }

  /** Get all available format templates */
  async getAvailableFormats(): Promise<FormatTemplate[]> {
    return this._client.getAvailableFormats();
  }

  /** Get all registered formula functions with metadata for autocomplete */
  async getFormulaFunctions(): Promise<FunctionInfo[]> {
    return this._client.getFormulaFunctions();
  }

  /** Get all named ranges in the workbook for dropdown */
  async getNamedRanges(): Promise<NamedRangeInfo[]> {
    return this._client.getNamedRanges();
  }

  /** Get cell format properties */
  async getCellFormat(cellId: string): Promise<CellFormatResult> {
    return this._client.getCellFormat(cellId);
  }

  /** Parse user input and auto-detect format */
  async parseUserInputValue(input: string): Promise<ParsedInputResult> {
    return this._client.parseUserInputValue(input);
  }

  /** Format a numeric value according to format properties */
  async formatCellValue(value: number, format: FormatProperties): Promise<FormattedValueResult> {
    return this._client.formatCellValue(value, format);
  }

  /** Format a cell's value using its assigned format */
  async formatCellById(cellId: string): Promise<FormattedValueResult> {
    return this._client.formatCellById(cellId);
  }

  // ==========================================================================
  // Cell Style Operations
  // ==========================================================================

  /** Set cell style by cell ID */
  async setCellStyle(cellId: string, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    return this._client.setCellStyle(cellId, style);
  }

  /** Set cell style by position */
  async setCellStyleAt(col: number, row: number, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    return this._client.setCellStyleAt(col, row, style);
  }

  /** Get cell style by cell ID */
  async getCellStyle(cellId: string): Promise<CellStyle> {
    return this._client.getCellStyle(cellId);
  }

  /** Get cell style by position */
  async getCellStyleAt(col: number, row: number): Promise<CellStyle> {
    return this._client.getCellStyleAt(col, row);
  }

  /** Get all registered styles */
  async getAvailableStyles(): Promise<RegisteredStyle[]> {
    return this._client.getAvailableStyles();
  }

  // ==========================================================================
  // Range Style Operations
  // ==========================================================================

  /**
   * Apply a style to a range using the Range system.
   * Creates a Range with RANGE_STYLE flag for efficient range-based styling.
   */
  async setRangeStyle(
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number,
    style: Partial<CellStyle>,
  ): Promise<{ success: boolean; rangeId?: string }> {
    return this._client.setRangeStyle(startCol, startRow, endCol, endRow, style);
  }

  /**
   * Apply a style to a range on a specific sheet.
   * Unlike setRangeStyle(), this targets a specific sheet by index.
   */
  async setRangeStyleOnSheet(
    sheetIndex: number,
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number,
    style: Partial<CellStyle>,
  ): Promise<{ success: boolean; rangeId?: string }> {
    return this._client.setRangeStyleOnSheet(
      sheetIndex,
      startCol,
      startRow,
      endCol,
      endRow,
      style,
    );
  }

  /**
   * Remove a style range at the given position.
   */
  async removeRangeStyle(col: number, row: number): Promise<{ success: boolean }> {
    return this._client.removeRangeStyle(col, row);
  }

  // ==========================================================================
  // Effective Style Operations
  // ==========================================================================

  /**
   * Get effective style for a cell, resolving the full style hierarchy.
   * Returns the computed style from cell > range > column > row precedence.
   */
  async getEffectiveCellStyle(col: number, row: number): Promise<CellStyle> {
    return this._client.getEffectiveCellStyle(col, row);
  }

  /**
   * Get effective style for a range with mixed indicators.
   * Returns the anchor cell's style plus which properties differ across the range.
   */
  async getEffectiveStyleForRange(
    col1: number,
    row1: number,
    col2: number,
    row2: number,
  ): Promise<{ style: CellStyle; mixed: Partial<Record<keyof CellStyle, boolean>> }> {
    return this._client.getEffectiveStyleForRange(col1, row1, col2, row2);
  }

  /**
   * Set style for a range of cells.
   * Uses the Range system for efficient styling.
   */
  async setStyleForRange(
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number,
    style: Partial<CellStyle>,
  ): Promise<{ success: boolean; cellsStyled: number }> {
    // Use the Range system for efficient range-based styling
    const result = await this.setRangeStyle(startCol, startRow, endCol, endRow, style);
    if (result.success) {
      // Calculate the number of cells in the range
      const cols = Math.abs(endCol - startCol) + 1;
      const rows = Math.abs(endRow - startRow) + 1;
      return { success: true, cellsStyled: cols * rows };
    }
    return { success: false, cellsStyled: 0 };
  }

  // ==========================================================================
  // Axis Style Operations (entire column/row styles)
  // ==========================================================================

  /**
   * Set a column's default style, creating the column if needed.
   */
  async setColumnStyle(colPosition: number, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    return this._client.setColumnStyle(colPosition, style);
  }

  /**
   * Set a row's default style, creating the row if needed.
   */
  async setRowStyle(rowPosition: number, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    return this._client.setRowStyle(rowPosition, style);
  }

  /**
   * Get a column's default style.
   */
  async getColumnStyle(colPosition: number): Promise<CellStyle> {
    return this._client.getColumnStyle(colPosition);
  }

  /**
   * Get a row's default style.
   */
  async getRowStyle(rowPosition: number): Promise<CellStyle> {
    return this._client.getRowStyle(rowPosition);
  }

  // ==========================================================================
  // Axis Format Operations
  // ==========================================================================

  /** Set a column's default format */
  async setColumnFormat(colPosition: number, format: FormatProperties): Promise<{ success: boolean }> {
    return this._client.setColumnFormat(colPosition, format);
  }

  /** Set a row's default format */
  async setRowFormat(rowPosition: number, format: FormatProperties): Promise<{ success: boolean }> {
    return this._client.setRowFormat(rowPosition, format);
  }

  /** Clear a column's format */
  async clearColumnFormat(colPosition: number): Promise<{ success: boolean }> {
    return this._client.clearColumnFormat(colPosition);
  }

  /** Clear a row's format */
  async clearRowFormat(rowPosition: number): Promise<{ success: boolean }> {
    return this._client.clearRowFormat(rowPosition);
  }

  /** Get a column's default format */
  async getColumnFormat(colPosition: number): Promise<FormatProperties> {
    return this._client.getColumnFormat(colPosition);
  }

  /** Get a row's default format */
  async getRowFormat(rowPosition: number): Promise<FormatProperties> {
    return this._client.getRowFormat(rowPosition);
  }

  // ==========================================================================
  // Range Format Operations
  // ==========================================================================

  /** Set a range's format */
  async setRangeFormat(
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number,
    format: FormatProperties,
  ): Promise<{ success: boolean; range_id?: string }> {
    return this._client.setRangeFormat(startCol, startRow, endCol, endRow, format);
  }

  /** Set a range's format on a specific sheet */
  async setRangeFormatOnSheet(
    sheetIndex: number,
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number,
    format: FormatProperties,
  ): Promise<{ success: boolean; range_id?: string }> {
    return this._client.setRangeFormatOnSheet(sheetIndex, startCol, startRow, endCol, endRow, format);
  }

  /** Remove a format range at a position */
  async removeRangeFormat(col: number, row: number): Promise<{ success: boolean }> {
    return this._client.removeRangeFormat(col, row);
  }

  // ==========================================================================
  // Column Operations
  // ==========================================================================

  /** Resize column by ID */
  async resizeColumn(colId: string, width: number): Promise<{ success: true }> {
    await this._client.resizeColumn(colId, width);
    return { success: true };
  }

  /** Resize column by position */
  async resizeColumnByPos(
    pos: number,
    width: number
  ): Promise<{ success: boolean; id?: string }> {
    return this._client.resizeColumnByPos(pos, width);
  }

  /** Move column by ID */
  async moveColumn(colId: string, targetPos: number): Promise<{ success: true }> {
    await this._client.moveColumn(colId, targetPos);
    return { success: true };
  }

  /** Shift columns for empty column move */
  async shiftColumnsForEmptyMove(
    sourcePos: number,
    targetPos: number
  ): Promise<{ success: true }> {
    await this._client.shiftColumnsForEmptyMove(sourcePos, targetPos);
    return { success: true };
  }

  /** Rename column by ID */
  async renameColumn(colId: string, name: string): Promise<{ success: true }> {
    await this._client.renameColumn(colId, name);
    return { success: true };
  }

  /** Rename column by position */
  async renameColumnByPos(
    pos: number,
    name: string
  ): Promise<{ success: boolean; id?: string }> {
    return await this._client.renameColumnByPos(pos, name);
  }

  // ==========================================================================
  // Row Operations
  // ==========================================================================

  /** Resize row by ID */
  async resizeRow(rowId: string, height: number): Promise<{ success: true }> {
    await this._client.resizeRow(rowId, height);
    return { success: true };
  }

  /** Resize row by position */
  async resizeRowByPos(
    pos: number,
    height: number
  ): Promise<{ success: boolean; id?: string }> {
    return this._client.resizeRowByPos(pos, height);
  }

  /** Move row by ID */
  async moveRow(rowId: string, targetPos: number): Promise<{ success: true }> {
    await this._client.moveRow(rowId, targetPos);
    return { success: true };
  }

  /** Shift rows for empty row move */
  async shiftRowsForEmptyMove(
    sourcePos: number,
    targetPos: number
  ): Promise<{ success: true }> {
    await this._client.shiftRowsForEmptyMove(sourcePos, targetPos);
    return { success: true };
  }

  // ==========================================================================
  // Column/Row Insert/Delete Operations
  // ==========================================================================

  /** Insert a column at the specified position */
  async insertColumnAt(
    position: number,
    insertBefore: boolean
  ): Promise<{ id: string; position: number }> {
    return this._client.insertColumnAt(position, insertBefore);
  }

  /** Insert a row at the specified position */
  async insertRowAt(
    position: number,
    insertBefore: boolean
  ): Promise<{ id: string; position: number }> {
    return this._client.insertRowAt(position, insertBefore);
  }

  /** Delete a column by ID */
  async deleteColumnById(colId: string): Promise<{ success: boolean }> {
    return this._client.deleteColumnById(colId);
  }

  /** Delete a row by ID */
  async deleteRowById(rowId: string): Promise<{ success: boolean }> {
    return this._client.deleteRowById(rowId);
  }

  /** Set freeze panes (frozen columns and rows) */
  async setFreezePanes(freezeCol: number, freezeRow: number): Promise<void> {
    return this._client.setFreezePanes(freezeCol, freezeRow);
  }

  /** Fill a range with extrapolated values from a source range */
  async fillRange(
    sourceMinCol: number, sourceMinRow: number,
    sourceMaxCol: number, sourceMaxRow: number,
    targetMinCol: number, targetMinRow: number,
    targetMaxCol: number, targetMaxRow: number
  ): Promise<{ success: boolean; cellsFilled: number }> {
    return this._client.fillRange(
      sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow,
      targetMinCol, targetMinRow, targetMaxCol, targetMaxRow
    );
  }

  // ==========================================================================
  // Merge Cell Operations
  // ==========================================================================

  /**
   * Merge cells in the specified range.
   * The content of the top-left cell becomes the content of the merged cell.
   */
  async mergeCells(
    startCol: number, startRow: number,
    endCol: number, endRow: number
  ): Promise<{ success: boolean; colSpan: number; rowSpan: number }> {
    return this._client.addMergeRange(startCol, startRow, endCol, endRow);
  }

  /**
   * Unmerge cells at the specified position.
   * If the cell is part of a merged region, the entire region will be unmerged.
   */
  async unmergeCells(col: number, row: number): Promise<{ success: boolean }> {
    return this._client.removeMergeRange(col, row);
  }

  // ==========================================================================
  // Sheet Management
  // ==========================================================================

  /** Get all sheets */
  async getSheets(): Promise<{
    sheets: Array<{ index: number; name: string; active: boolean }>;
    activeIndex: number;
  }> {
    return this._client.getSheets();
  }

  /** Set active sheet by index */
  async setActiveSheet(index: number): Promise<void> {
    await this._client.setActiveSheet(index);
  }

  /** Add a new sheet */
  async addSheet(name: string = ""): Promise<{ index: number; name: string }> {
    return this._client.addSheet(name);
  }

  /** Delete a sheet by index */
  async deleteSheet(index: number): Promise<{ activeIndex: number }> {
    return this._client.deleteSheet(index);
  }

  /** Rename a sheet */
  async renameSheet(index: number, name: string): Promise<{ success: boolean }> {
    return this._client.renameSheet(index, name);
  }

  /** Move a sheet from one position to another */
  async moveSheet(
    fromIndex: number,
    toIndex: number
  ): Promise<{ activeIndex: number }> {
    return this._client.moveSheet(fromIndex, toIndex);
  }

  // ==========================================================================
  // Viewport Pixel Queries (Phase 5)
  // ==========================================================================

  /** Get pixel offset of column at given position */
  async getColumnPixelOffset(position: number): Promise<number> {
    return this._client.getColumnPixelOffset(position);
  }

  /** Get pixel offset of row at given position */
  async getRowPixelOffset(position: number): Promise<number> {
    return this._client.getRowPixelOffset(position);
  }

  /** Get total width of all columns in pixels */
  async getTotalWidth(): Promise<number> {
    return this._client.getTotalWidth();
  }

  /** Get total height of all rows in pixels */
  async getTotalHeight(): Promise<number> {
    return this._client.getTotalHeight();
  }

  // ==========================================================================
  // Theme
  // ==========================================================================

  /** Get the workbook's theme (color palette + font scheme), or null if none */
  async getTheme(): Promise<WorkbookTheme | null> {
    return this._client.getTheme();
  }

  /** Get all built-in theme palettes */
  async getBuiltinThemes(): Promise<WorkbookTheme[]> {
    return this._client.getBuiltinThemes();
  }

  /** Apply a theme to the workbook */
  async setTheme(theme: WorkbookTheme): Promise<void> {
    return this._client.setTheme(theme);
  }

  /** Get built-in cell style presets with resolved preview colors */
  async getCellStylePresets(): Promise<CellStylePreset[]> {
    return this._client.getCellStylePresets();
  }

  // ==========================================================================
  // Export
  // ==========================================================================

  /** Check if the workbook contains any formula cells */
  async hasFormulas(): Promise<boolean> {
    return this._client.hasFormulas();
  }

  /** Export workbook to specified format */
  async exportAs(format: "csv" | "xlsx" | "zcd"): Promise<ExportResult> {
    const result = await this._client.exportAs(format);
    const blob = new Blob([result.data], { type: getMimeType(format) });
    // Always use snake_case for the filename (ignore worker's filename)
    const snakeCaseName = toSnakeCase(this._workbookName) || "untitled";
    return {
      blob,
      filename: `${snakeCaseName}.${format}`,
    };
  }

  // ==========================================================================
  // Scripting (Luau)
  // ==========================================================================

  /**
   * Execute a Luau script in the sandboxed environment
   * Scripts can use cells API functions like cellGet(), cellSet(), etc.
   */
  async executeScript(script: string, language?: string): Promise<{
    success: boolean;
    output?: string;
    error?: string;
    instructions: number;
  }> {
    return this._client.executeScript(script, language);
  }

  /**
   * Tokenize Luau source code for syntax highlighting
   */
  async tokenizeLuau(source: string): Promise<LuauToken[]> {
    return this._client.tokenizeLuau(source);
  }

  /**
   * Get autocomplete suggestions for Luau source code
   */
  async getAutocomplete(source: string, line: number, column: number): Promise<AutocompleteResult> {
    return this._client.getAutocomplete(source, line, column);
  }

  // ==========================================================================
  // Spill Range Queries
  // ==========================================================================

  /**
   * Get spill range information for a cell at the given position.
   * Returns empty object if the cell is not part of a spill range.
   */
  async getSpillRangeAt(col: number, row: number): Promise<{
    masterId?: string;
    masterCol?: number;
    masterRow?: number;
    minCol?: number;
    minRow?: number;
    maxCol?: number;
    maxRow?: number;
    spillCount?: number;
  }> {
    return this._client.getSpillRangeAt(col, row);
  }
}
