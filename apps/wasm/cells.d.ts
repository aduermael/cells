// TypeScript type definitions for cells WASM module
// Generated for Embind bindings in bindings.cc

declare module 'cells-wasm' {
  /**
   * Response from file loading operations
   */
  interface LoadResult {
    success?: boolean;
    error?: string;
    sheetCount?: number;
  }

  /**
   * Response from operations that return success/error
   */
  interface OperationResult {
    success?: boolean;
    error?: string;
    id?: string;  // Cell/column/row ID for create operations
  }

  /**
   * Sheet information
   */
  interface SheetInfo {
    name: string;
    rowCount: number;
    colCount: number;
    defaultColWidth: number;
    defaultRowHeight: number;
  }

  /**
   * Cell data from viewport query
   */
  interface CellData {
    id: string;
    col: number;
    row: number;
    type: 'n' | 's' | 'f' | 'b' | 'e' | 'd' | 't';  // number, string, formula, boolean, error, date, datetime
    value?: string;      // For non-formula cells
    formula?: string;    // For formula cells (A1 notation)
    display?: string;    // For formula cells (computed value)
  }

  /**
   * Column information
   */
  interface ColumnInfo {
    id: string;
    pos: number;
    width: number;
    name: string;
  }

  /**
   * Row information
   */
  interface RowInfo {
    id: string;
    pos: number;
    height: number;
    name: string;
  }

  /**
   * Viewport query result
   */
  interface ViewportResult {
    cells: CellData[];
    columns: ColumnInfo[];
    rows: RowInfo[];
  }

  /**
   * Main spreadsheet engine class
   * Wrapper around the C++ CellsEngine exposed via Embind
   */
  class CellsEngine {
    constructor();

    // ========================================================================
    // File loading methods
    // ========================================================================

    /**
     * Load a workbook from .cells format string
     * @param content - The .cells file content as a string
     * @returns JSON string with LoadResult
     */
    loadFromCells(content: string): string;

    /**
     * Load a workbook from CSV string
     * @param content - The CSV content as a string
     * @param delimiter - Field delimiter (default: ',')
     * @param hasHeader - Whether first row is header (default: true)
     * @returns JSON string with LoadResult
     */
    loadFromCSV(content: string, delimiter: number, hasHeader: boolean): string;

    /**
     * Load a workbook from XLSX binary data
     * Note: Only available in cells_wasm_full build
     * @param data - The XLSX file as a binary string
     * @returns JSON string with LoadResult
     */
    loadFromXLSXData(data: string): string;

    // ========================================================================
    // Sheet info methods
    // ========================================================================

    /**
     * Get information about the active sheet
     * @returns JSON string with SheetInfo or error
     */
    getSheetInfo(): string;

    /**
     * Get the number of sheets in the workbook
     */
    getSheetCount(): number;

    /**
     * Get the name of a sheet by index
     * @param index - 0-based sheet index
     * @returns Sheet name or empty string if invalid
     */
    getSheetName(index: number): string;

    /**
     * Set the active sheet
     * @param index - 0-based sheet index
     */
    setActiveSheet(index: number): void;

    // ========================================================================
    // Viewport query
    // ========================================================================

    /**
     * Query cells in the visible viewport area
     * @param x1 - Left column position (inclusive)
     * @param y1 - Top row position (inclusive)
     * @param x2 - Right column position (exclusive)
     * @param y2 - Bottom row position (exclusive)
     * @returns JSON string with ViewportResult
     */
    queryViewport(x1: number, y1: number, x2: number, y2: number): string;

    // ========================================================================
    // Cell operations
    // ========================================================================

    /**
     * Update an existing cell's value
     * @param cellId - The cell's unique ID (8-char base62)
     * @param value - New value (string, number, formula starting with '=', etc.)
     * @returns JSON string with OperationResult
     */
    updateCell(cellId: string, value: string): string;

    /**
     * Create a new cell at a position
     * @param col - Column position (0-based)
     * @param row - Row position (0-based)
     * @param value - Initial value
     * @returns JSON string with OperationResult including new cell ID
     */
    createCell(col: number, row: number, value: string): string;

    // ========================================================================
    // Column/row resize
    // ========================================================================

    /**
     * Resize a column by ID
     * @param colId - Column ID (8-char base62)
     * @param width - New width in pixels (20-1000)
     * @returns JSON string with OperationResult
     */
    resizeColumn(colId: string, width: number): string;

    /**
     * Resize a column by position, creating if needed
     * @param pos - Column position (0-based)
     * @param width - New width in pixels (20-1000)
     * @returns JSON string with OperationResult including column ID
     */
    resizeColumnByPos(pos: number, width: number): string;

    /**
     * Resize a row by ID
     * @param rowId - Row ID (8-char base62)
     * @param height - New height in pixels (10-500)
     * @returns JSON string with OperationResult
     */
    resizeRow(rowId: string, height: number): string;

    // ========================================================================
    // Column/row rename
    // ========================================================================

    /**
     * Rename a column
     * @param colId - Column ID (8-char base62)
     * @param name - New column name (empty string to clear)
     * @returns JSON string with OperationResult
     */
    renameColumn(colId: string, name: string): string;

    // ========================================================================
    // Column/row move
    // ========================================================================

    /**
     * Move a column to a new position
     * @param colId - Column ID to move (8-char base62)
     * @param targetPos - Target position (insert before)
     * @returns JSON string with OperationResult
     */
    moveColumn(colId: string, targetPos: number): string;

    /**
     * Move a row to a new position
     * @param rowId - Row ID to move (8-char base62)
     * @param targetPos - Target position (insert before)
     * @returns JSON string with OperationResult
     */
    moveRow(rowId: string, targetPos: number): string;

    // ========================================================================
    // Export methods
    // ========================================================================

    /**
     * Export workbook to .cells format
     * @returns .cells file content as string, or empty on error
     */
    exportToCells(): string;

    /**
     * Export workbook to CSV format
     * @returns CSV content as string, or empty on error
     */
    exportToCSV(): string;

    /**
     * Export workbook to XLSX format
     * Note: Only available in cells_wasm_full build
     * @returns XLSX binary data as string, or empty on error
     */
    exportToXLSX(): string;

    // ========================================================================
    // Workbook management
    // ========================================================================

    /**
     * Get the workbook name
     */
    getWorkbookName(): string;

    /**
     * Set the workbook name
     * @param name - New workbook name
     */
    setWorkbookName(name: string): void;

    /**
     * Create a new empty workbook with one sheet
     */
    createEmptyWorkbook(): void;

    /**
     * Delete the CellsEngine instance and free memory
     */
    delete(): void;
  }

  /**
   * Module initialization options
   */
  interface CellsModuleOptions {
    /** Path to .wasm file if not in same directory */
    locateFile?: (path: string, prefix: string) => string;
    /** Called when module is ready */
    onRuntimeInitialized?: () => void;
  }

  /**
   * The initialized WASM module
   */
  interface CellsModule {
    CellsEngine: typeof CellsEngine;
  }

  /**
   * Factory function to create the WASM module
   * @param options - Initialization options
   * @returns Promise that resolves to the module
   */
  function createCellsModule(options?: CellsModuleOptions): Promise<CellsModule>;

  export default createCellsModule;
  export { CellsEngine, CellsModule, CellsModuleOptions };
  export {
    LoadResult,
    OperationResult,
    SheetInfo,
    CellData,
    ColumnInfo,
    RowInfo,
    ViewportResult,
  };
}
