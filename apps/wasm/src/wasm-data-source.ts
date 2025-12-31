// WASM Data Source - Wrapper around CellsClient for data operations
// This class provides a clean interface for interacting with the WASM worker
// through the CellsClient, handling workbook metadata and change notifications.

import type { CellsClient } from "./client";
import type { SheetInfo, CellData, ColumnInfo, RowInfo } from "./types";
import type { LuauToken } from "./client-types";
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
  }> {
    const result = await this._client.queryViewport(x1, y1, x2, y2);
    // Cast cells since WASM returns string type but runtime values are valid
    return {
      cells: result.cells as CellData[],
      columns: result.columns as ColumnInfo[],
      rows: result.rows as RowInfo[],
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
  ): Promise<{ id: string; value: string; formula?: string | null; existed: boolean }> {
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

  /** Delete cell by ID */
  async deleteCell(cellId: string): Promise<{ success: true }> {
    await this._client.deleteCell(cellId);
    return { success: true };
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
  async executeScript(script: string): Promise<{
    success: boolean;
    output?: string;
    error?: string;
    instructions: number;
  }> {
    return this._client.executeScript(script);
  }

  /**
   * Tokenize Luau source code for syntax highlighting
   */
  async tokenizeLuau(source: string): Promise<LuauToken[]> {
    return this._client.tokenizeLuau(source);
  }
}
