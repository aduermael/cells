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
   * Result from applying a CRDT operation
   */
  interface ApplyOperationResult {
    result: 'success' | 'already_applied' | 'superseded' | 'invalid_target' | 'invalid_payload' | 'resurrected' | 'error';
    error?: string;
  }

  /**
   * Result from applying multiple CRDT operations
   */
  interface ApplyOperationsResult {
    applied: number;  // Number of operations successfully applied
    total: number;    // Total operations in batch
    error?: string;
  }

  /**
   * CRDT Operation for sync protocol
   */
  interface CRDTOperation {
    hlc: string;      // Hybrid Logical Clock timestamp: "wall.logical.node"
    op: string;       // Operation type: CELL_SET_VALUE, CELL_CLEAR, etc.
    target: string;   // Target entity ID (cell, axis, or sheet)
    payload: object;  // Operation-specific data
  }

  /**
   * Response from getOperationsSince
   */
  interface OperationsResponse {
    operations: CRDTOperation[];
    error?: string;
  }

  /**
   * Result from validateFormula
   */
  interface ValidateFormulaResult {
    formula: string;
    valid: boolean;
    errors: string[];
    rootType: string | null;  // AST root node type, or null if parse failed
  }

  /**
   * Reference type for formula dependencies
   */
  type RefType = 'cell' | 'range' | 'column' | 'row' | 'columnRange' | 'rowRange' | 'named';

  /**
   * Dependency reference from a formula
   */
  interface DependencyRef {
    type: RefType;
    cellId?: string;           // For cell refs
    startCellId?: string;      // For range refs
    endCellId?: string;        // For range refs
    columnId?: string;         // For column refs
    rowId?: string;            // For row refs
    startColumnId?: string;    // For column range refs
    endColumnId?: string;      // For column range refs
    startRowId?: string;       // For row range refs
    endRowId?: string;         // For row range refs
    sourceStart: number;       // Position in formula text
    sourceEnd: number;
  }

  /**
   * Response from getCellDependencies
   */
  interface CellDependenciesResult {
    dependencies: DependencyRef[];
    error?: string;
  }

  /**
   * Response from getCellDependents
   */
  interface CellDependentsResult {
    dependents: string[];  // Array of cell IDs
    error?: string;
  }

  /**
   * Reference info for formula highlighting
   */
  interface ReferenceInfo {
    type: RefType;
    cellId?: string;
    topLeftCellId?: string;
    bottomRightCellId?: string;
    axisId?: string;
    startAxisId?: string;
    endAxisId?: string;
    name?: string;          // For named refs
    sheetId?: string;       // For cross-sheet refs
    sourceStart: number;
    sourceEnd: number;
  }

  /**
   * Response from getFormulaReferences / getReferencesFromPartial
   */
  interface FormulaReferencesResult {
    references: ReferenceInfo[];
    error?: string;
  }

  /**
   * Response from detectCircularRef
   */
  interface CircularRefResult {
    hasCycle: boolean;
    cycle: string[];  // Array of cell IDs forming the cycle
    error?: string;
  }

  /**
   * Response from getVolatileCells
   */
  interface VolatileCellsResult {
    volatileCells: string[];  // Array of cell IDs
    error?: string;
  }

  /**
   * Response from getCellDisplayValue
   */
  interface CellDisplayValueResult {
    value: string;                          // The display value (calculated result)
    type: 'n' | 's' | 'b' | 'e' | 'empty'; // n=number, s=string, b=boolean, e=error, empty
    error?: string;                         // Error message if type is 'e'
  }

  /**
   * Response from recalculate
   */
  interface RecalculateResult {
    recalculated: number;  // Number of cells recalculated
    errors: number;        // Number of cells that evaluated to errors
    error?: string;        // Error message if operation failed
  }

  /**
   * Response from markCellDirty
   */
  interface MarkDirtyResult {
    success: boolean;
    markedDirty: number;  // Number of cells marked dirty
    error?: string;
  }

  /**
   * Response from getDirtyCellIds
   */
  interface DirtyCellsResult {
    dirtyCells: string[];  // Array of cell IDs needing recalculation
    error?: string;
  }

  /**
   * Response from executeScript (Luau scripting)
   */
  interface ScriptResult {
    success: boolean;
    output?: string;       // Script output if success
    error?: string;        // Error message if !success
    instructions: number;  // Number of instructions executed
  }

  /**
   * Token type from Luau lexer
   */
  type LuauTokenType = 'keyword' | 'string' | 'number' | 'comment' | 'name' | 'operator' | 'error';

  /**
   * Token from Luau lexer
   */
  interface LuauToken {
    type: LuauTokenType;
    text: string;
    start: number;  // Byte offset in source
    end: number;    // Byte offset in source (exclusive)
  }

  /**
   * Autocomplete suggestion kind
   */
  type AutocompleteSuggestionKind = 'property' | 'variable' | 'keyword' | 'string' | 'type' | 'module' | 'function' | 'path' | 'text';

  /**
   * Single autocomplete suggestion
   */
  interface AutocompleteSuggestion {
    label: string;         // Display text
    insertText: string;    // Text to insert (may differ from label)
    kind: AutocompleteSuggestionKind;
    detail: string;        // Additional info (e.g., type signature)
    deprecated: boolean;   // Whether this suggestion is deprecated
  }

  /**
   * Autocomplete context type
   */
  type AutocompleteContext = 'statement' | 'expression' | 'property' | 'type' | 'keyword' | 'string' | 'unknown';

  /**
   * Response from getAutocomplete
   */
  interface AutocompleteResult {
    context: AutocompleteContext;
    suggestions: AutocompleteSuggestion[];
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
   * Horizontal text alignment within cell
   */
  type TextAlign = 'left' | 'center' | 'right' | 'justify';

  /**
   * Vertical text alignment within cell
   */
  type VerticalAlign = 'top' | 'middle' | 'bottom';

  /**
   * Cell style properties for formatting
   */
  interface CellStyle {
    bold: boolean;
    italic: boolean;
    underline: boolean;
    bgColor: string;     // Background color (hex, e.g. "#FF0000"), empty for default
    textColor: string;   // Text color (hex, e.g. "#000000"), empty for default
    fontFamily: string;  // Font name (e.g. "Arial"), empty for system default
    fontSize: number;    // Font size in points, 0 for default (11pt)
    hAlign: TextAlign;
    vAlign: VerticalAlign;
  }

  /**
   * Registered style entry
   */
  interface RegisteredStyle {
    id: string;          // Style ID (8-char base62)
    style: CellStyle;
  }

  /**
   * Result from createStyle
   */
  interface CreateStyleResult {
    success?: boolean;
    styleId?: string;
    existing?: boolean;  // true if style already existed
    error?: string;
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
    isError?: boolean;   // True if formula evaluated to an error
    styleId?: string;    // Cell style ID (~ or empty for default style)
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
     * Load a workbook from .zcd format string
     * @param content - The .zcd file content as a string
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
    // Cell style operations
    // ========================================================================

    /**
     * Set a cell's style by cell ID
     * @param cellId - Cell ID (8-char base62)
     * @param styleJson - JSON string with style properties
     * @returns JSON string with OperationResult
     */
    setCellStyle(cellId: string, styleJson: string): string;

    /**
     * Set a cell's style by position, creating cell if needed
     * @param col - Column position (0-based)
     * @param row - Row position (0-based)
     * @param styleJson - JSON string with style properties
     * @returns JSON string with OperationResult
     */
    setCellStyleAt(col: number, row: number, styleJson: string): string;

    /**
     * Get a cell's style by cell ID
     * @param cellId - Cell ID (8-char base62)
     * @returns JSON string with CellStyle properties
     */
    getCellStyle(cellId: string): string;

    /**
     * Get a cell's style by position
     * @param col - Column position (0-based)
     * @param row - Row position (0-based)
     * @returns JSON string with CellStyle properties
     */
    getCellStyleAt(col: number, row: number): string;

    /**
     * Create a style definition and get its ID
     * @param styleJson - JSON string with style properties
     * @returns JSON string with CreateStyleResult
     */
    createStyle(styleJson: string): string;

    /**
     * Get all registered styles
     * @returns JSON string with array of RegisteredStyle
     */
    getAvailableStyles(): string;

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

    /**
     * Resize a row by position, creating if needed
     * @param pos - Row position (0-based)
     * @param height - New height in pixels (10-500)
     * @returns JSON string with OperationResult including row ID
     */
    resizeRowByPos(pos: number, height: number): string;

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

    /**
     * Rename a column by position (creates column if it doesn't exist)
     * @param pos - Column position (0-indexed)
     * @param name - New column name (empty string to clear)
     * @returns JSON string with OperationResult including the column ID
     */
    renameColumnByPos(pos: number, name: string): string;

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

    /**
     * Shift columns when moving an empty column position
     * @param sourcePos - Source position (empty)
     * @param targetPos - Target position (insert before)
     * @returns JSON string with OperationResult
     */
    shiftColumnsForEmptyMove(sourcePos: number, targetPos: number): string;

    /**
     * Shift rows when moving an empty row position
     * @param sourcePos - Source position (empty)
     * @param targetPos - Target position (insert before)
     * @returns JSON string with OperationResult
     */
    shiftRowsForEmptyMove(sourcePos: number, targetPos: number): string;

    // ========================================================================
    // Export methods
    // ========================================================================

    /**
     * Export workbook to .zcd format
     * @returns .zcd file content as string, or empty on error
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

    // ========================================================================
    // CRDT collaboration methods
    // ========================================================================

    /**
     * Set the local node ID for HLC generation
     * Should be called once when initializing collaboration
     * @param nodeId - 8-character base62 ID
     * @returns JSON string with OperationResult
     */
    setNodeId(nodeId: string): string;

    /**
     * Get the local node ID
     * @returns Node ID string, or empty if not set
     */
    getNodeId(): string;

    /**
     * Get the current (highest) HLC timestamp
     * @returns HLC string in format "wall.logical.node", or empty if no workbook
     */
    getCurrentHLC(): string;

    /**
     * Get all operations since a given HLC timestamp (exclusive)
     * @param sinceHLC - HLC string to get operations after, or empty for all
     * @returns JSON string with OperationsResponse
     */
    getOperationsSince(sinceHLC: string): string;

    /**
     * Apply a remote CRDT operation
     * @param opJson - Operation in JSON format
     * @returns JSON string with ApplyOperationResult
     */
    applyRemoteOperation(opJson: string): string;

    /**
     * Apply multiple remote CRDT operations
     * @param opsJson - JSON with {"operations":[...]} array
     * @returns JSON string with ApplyOperationsResult
     */
    applyRemoteOperations(opsJson: string): string;

    /**
     * Get the number of operations in the OpLog
     * @returns Operation count
     */
    getOpLogSize(): number;

    /**
     * Check if an operation with the given HLC exists
     * @param hlc - HLC string to check
     * @returns true if operation exists
     */
    hasOperation(hlc: string): boolean;

    // ========================================================================
    // Formula API (Phase 7)
    // ========================================================================

    /**
     * Validate a formula without side effects
     * @param formulaText - Formula text (e.g., "=A1+B2")
     * @returns JSON string with ValidateFormulaResult
     */
    validateFormula(formulaText: string): string;

    /**
     * Get the A1 display string for a cell's formula
     * @param cellId - Cell ID (8-char base62)
     * @returns Formula in A1 notation, or empty string if no formula
     */
    getFormulaDisplay(cellId: string): string;

    /**
     * Get dependencies for a cell's formula (what cells it reads from)
     * @param cellId - Cell ID (8-char base62)
     * @returns JSON string with CellDependenciesResult
     */
    getCellDependencies(cellId: string): string;

    /**
     * Get cells that depend on the given cell (what reads this cell)
     * @param cellId - Cell ID (8-char base62)
     * @returns JSON string with CellDependentsResult
     */
    getCellDependents(cellId: string): string;

    /**
     * Get references from a formula with source positions (for colored highlighting)
     * @param formulaText - Formula text (e.g., "=A1+B2")
     * @returns JSON string with FormulaReferencesResult
     */
    getFormulaReferences(formulaText: string): string;

    /**
     * Parse incomplete formula and extract valid references
     * @param formulaText - Possibly incomplete formula (e.g., "=SUM(A1+")
     * @returns JSON string with FormulaReferencesResult
     */
    getReferencesFromPartial(formulaText: string): string;

    /**
     * Detect circular reference starting from a cell
     * @param cellId - Cell ID to check (8-char base62)
     * @returns JSON string with CircularRefResult
     */
    detectCircularRef(cellId: string): string;

    /**
     * Get list of volatile cells (containing NOW, RAND, etc.)
     * @returns JSON string with VolatileCellsResult
     */
    getVolatileCells(): string;

    // ========================================================================
    // Formula Evaluation (Phase 8)
    // ========================================================================

    /**
     * Evaluate a cell and return its display value (calculated result)
     * For formula cells, returns the computed result (number, string, boolean, or error)
     * For non-formula cells, returns the cell's raw value
     * @param cellId - Cell ID (8-char base62)
     * @returns JSON string with CellDisplayValueResult
     */
    getCellDisplayValue(cellId: string): string;

    /**
     * Trigger recalculation of all dirty cells
     * Evaluates formulas marked dirty and updates their values
     * Uses dependency graph for correct evaluation order
     * @returns JSON string with RecalculateResult
     */
    recalculate(): string;

    /**
     * Check if any cells need recalculation
     * @returns true if there are dirty formula cells
     */
    hasDirtyCells(): boolean;

    /**
     * Mark a cell as dirty and mark all its dependents as dirty
     * Use when a cell's value changes to trigger dependent recalculation
     * @param cellId - Cell ID (8-char base62)
     * @returns JSON string with MarkDirtyResult
     */
    markCellDirty(cellId: string): string;

    /**
     * Get list of dirty cell IDs (cells needing recalculation)
     * @returns JSON string with DirtyCellsResult
     */
    getDirtyCellIds(): string;

    /**
     * Parse a formula and return its AST as JSON (debug function)
     * @param formulaText - Formula text (e.g., "=A1+B2")
     * @returns JSON string with AST structure
     */
    debugParseFormula(formulaText: string): string;

    // ========================================================================
    // Scripting (Luau)
    // ========================================================================

    /**
     * Execute a Luau script in the sandboxed environment
     * Scripts can use cells API functions like cellGet(), cellSet(), etc.
     * Scripts starting with '/' prefix are meant to be entered in the formula bar
     * @param script - Luau script code to execute
     * @returns JSON string with ScriptResult
     */
    executeScript(script: string): string;

    /**
     * Tokenize a Luau script using the Luau lexer
     * Returns an array of tokens for syntax highlighting
     * @param source - Luau source code to tokenize
     * @returns JSON string with array of LuauToken
     */
    tokenizeLuau(source: string): string;

    /**
     * Get autocomplete suggestions for a Luau script at a given position
     * @param source - Luau source code
     * @param line - 0-indexed line number
     * @param column - 0-indexed column number
     * @returns JSON string with AutocompleteResult
     */
    getAutocomplete(source: string, line: number, column: number): string;

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
   * Log level enum for controlling C++ log output
   */
  enum LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
  }

  /**
   * The initialized WASM module
   */
  interface CellsModule {
    CellsEngine: typeof CellsEngine;

    // Logger enum
    LogLevel: typeof LogLevel;

    // Logger functions - output to browser console
    logDebug(message: string): void;
    logInfo(message: string): void;
    logWarn(message: string): void;
    logError(message: string): void;

    // Logger configuration
    setLogEnabled(enabled: boolean): void;
    isLogEnabled(): boolean;
    setLogLevel(level: LogLevel): void;
    getLogLevel(): LogLevel;
  }

  /**
   * Factory function to create the WASM module
   * @param options - Initialization options
   * @returns Promise that resolves to the module
   */
  function createCellsModule(options?: CellsModuleOptions): Promise<CellsModule>;

  export default createCellsModule;
  export { CellsEngine, CellsModule, CellsModuleOptions, LogLevel };
  export {
    LoadResult,
    OperationResult,
    ApplyOperationResult,
    ApplyOperationsResult,
    CRDTOperation,
    OperationsResponse,
    SheetInfo,
    CellData,
    ColumnInfo,
    RowInfo,
    ViewportResult,
    // Cell Style types
    TextAlign,
    VerticalAlign,
    CellStyle,
    RegisteredStyle,
    CreateStyleResult,
    // Formula API types (Phase 7)
    ValidateFormulaResult,
    RefType,
    DependencyRef,
    CellDependenciesResult,
    CellDependentsResult,
    ReferenceInfo,
    FormulaReferencesResult,
    CircularRefResult,
    VolatileCellsResult,
    // Formula Evaluation types (Phase 8)
    CellDisplayValueResult,
    RecalculateResult,
    MarkDirtyResult,
    DirtyCellsResult,
    // Scripting types (Luau)
    ScriptResult,
    LuauTokenType,
    LuauToken,
  };
}
