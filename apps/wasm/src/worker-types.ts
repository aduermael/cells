// =============================================================================
// Worker Types
// =============================================================================
//
// Shared type definitions for the worker module and its sub-modules.
// Extracted from worker.ts to enable type sharing across worker-handlers.ts
// and worker-collab.ts.
//
// Key types:
// - CellsModule: WASM module interface (Emscripten-generated)
// - CellsEngine: Spreadsheet engine class interface
// - WorkerRequest/WorkerResponse: Message protocol types
//
// =============================================================================

/** WASM Module type - Emscripten-generated */
export interface CellsModule {
    CellsEngine: new () => CellsEngine;
    _malloc: (size: number) => number;
    _free: (ptr: number) => void;
    HEAPU8: Uint8Array;
}

/** CellsEngine WASM class interface */
export interface CellsEngine {
    // Listener - callback receives (changeType, data?) where data is optional
    setListener(callback: (changeType: string, data?: string) => void): void;

    // File loading
    loadFromCells(content: string): string;
    loadFromCSV(content: string, delimiter: number, hasHeader: boolean): string;
    loadFromXLSXDataPtr(ptr: number, length: number): string;

    // Sheet info
    getSheetInfo(): string;
    getSheetCount(): number;
    getSheetName(index: number): string;
    setActiveSheet(index: number): void;
    getActiveSheetIndex(): number;
    addSheet(name: string): string;
    deleteSheet(index: number): string;
    renameSheet(index: number, name: string): string;
    moveSheet(fromIndex: number, toIndex: number): string;
    setFreezePanes(freezeCol: number, freezeRow: number): void;

    // Viewport
    queryViewport(x1: number, y1: number, x2: number, y2: number): string;

    // Cell operations
    updateCell(cellId: string, value: string): string;
    updateCellWithFormatDetection(cellId: string, value: string): string;
    createCell(col: number, row: number, value: string): string;
    getOrCreateCellAt(col: number, row: number): string;
    deleteCell(cellId: string): string;
    deleteCellAt(col: number, row: number): string;

    // Number format operations
    setCellFormat(cellId: string, formatId: string): string;
    setCellFormatAt(col: number, row: number, formatId: string): string;
    getAvailableFormats(): string;
    createCustomFormat(formatCode: string): string;
    getFormulaFunctions(): string;
    getNamedRanges(): string;
    getCellFormatId(cellId: string): string;
    parseUserInputValue(input: string): string;
    formatCellValue(value: number, formatId: string): string;
    formatWithCode(value: number, formatCode: string): string;
    formatCellById(cellId: string): string;
    getFormatDetails(formatId: string): string;
    makeFormatId(
        category: string,
        decimals: number,
        separator: boolean,
        currency: string,
    ): string;

    // Cell style operations
    setCellStyle(cellId: string, styleJson: string): string;
    setCellStyleAt(col: number, row: number, styleJson: string): string;
    getCellStyle(cellId: string): string;
    getCellStyleAt(col: number, row: number): string;
    getAvailableStyles(): string;
    getCellStylePresets(): string;

    // Range style operations
    setRangeStyle(
        startCol: number,
        startRow: number,
        endCol: number,
        endRow: number,
        styleJson: string
    ): string;
    setRangeStyleOnSheet(
        sheetIndex: number,
        startCol: number,
        startRow: number,
        endCol: number,
        endRow: number,
        styleJson: string
    ): string;
    removeRangeStyle(col: number, row: number): string;

    // Effective style operations (resolves style hierarchy)
    getEffectiveCellStyle(col: number, row: number): string;
    getEffectiveStyleForRange(col1: number, row1: number, col2: number, row2: number): string;

    // Axis style operations (entire column/row styles)
    setColumnStyle(colPosition: number, styleJson: string): string;
    setRowStyle(rowPosition: number, styleJson: string): string;
    getColumnStyle(colPosition: number): string;
    getRowStyle(rowPosition: number): string;

    // Axis format operations (entire column/row formats)
    setColumnFormat(colPosition: number, formatJson: string): string;
    setRowFormat(rowPosition: number, formatJson: string): string;
    clearColumnFormat(colPosition: number): string;
    clearRowFormat(rowPosition: number): string;
    getColumnFormat(colPosition: number): string;
    getRowFormat(rowPosition: number): string;

    // Range format operations
    setRangeFormat(
        startCol: number,
        startRow: number,
        endCol: number,
        endRow: number,
        formatJson: string,
    ): string;
    setRangeFormatOnSheet(
        sheetIndex: number,
        startCol: number,
        startRow: number,
        endCol: number,
        endRow: number,
        formatJson: string,
    ): string;
    removeRangeFormat(col: number, row: number): string;

    // Column/row operations
    resizeColumn(colId: string, width: number): string;
    resizeColumnByPos(pos: number, width: number): string;
    resizeRow(rowId: string, height: number): string;
    resizeRowByPos(pos: number, height: number): string;
    renameColumn(colId: string, name: string): string;
    renameColumnByPos(pos: number, name: string): string;
    moveColumn(colId: string, targetPos: number): string;
    moveRow(rowId: string, targetPos: number): string;
    shiftColumnsForEmptyMove(sourcePos: number, targetPos: number): string;
    shiftRowsForEmptyMove(sourcePos: number, targetPos: number): string;
    insertColumnAt(position: number, insertBefore: boolean): string;
    insertRowAt(position: number, insertBefore: boolean): string;
    deleteColumnById(colId: string): string;
    deleteRowById(rowId: string): string;
    fillRange(
        sourceMinCol: number,
        sourceMinRow: number,
        sourceMaxCol: number,
        sourceMaxRow: number,
        targetMinCol: number,
        targetMinRow: number,
        targetMaxCol: number,
        targetMaxRow: number,
    ): string;

    // Merge operations
    addMergeRange(startCol: number, startRow: number, endCol: number, endRow: number): string;
    removeMergeRange(col: number, row: number): string;

    // Export
    exportToCells(): string;
    exportToCSV(): string;
    exportToXLSXPtr(): string;
    freeExportBuffer(): void;
    hasFormulas(): boolean;

    // Workbook management
    getWorkbookName(): string;
    setWorkbookName(name: string): void;
    createEmptyWorkbook(): void;
    getTheme(): string;
    getBuiltinThemes(): string;
    setTheme(themeJson: string): string;

    // CRDT collaboration
    setNodeId(nodeId: string): string;
    getNodeId(): string;
    getCurrentHLC(): string;
    getOperationsSince(sinceHLC: string): string;
    applyRemoteOperation(opJson: string): string;
    applyRemoteOperations(opsJson: string): string;
    getOpLogSize(): number;
    hasOperation(hlc: string): boolean;

    // SyncManager
    initSyncManager(): string;
    addPeer(peerId: string): string;
    removePeer(peerId: string): string;
    getPeerIds(): string;
    getPeerCount(): number;
    handlePeerMessage(peerId: string, messageJson: string): string;
    getOutgoingMessages(): string;
    queueOperationsBroadcast(): string;
    setDebugNoPrune(noPrune: boolean): void;
    startCollaboration(): string;

    // C++ SyncClient
    enableSync(url: string, roomId: string, peerId: string): string;
    disableSync(): void;
    getSyncState(): string;
    isSyncEnabled(): boolean;
    processSyncOutgoing(): void;
    processSyncPresence(): void;
    broadcastSyncOperations(): void;

    // Presence
    setSyncLocalName(name: string): void;
    setSyncCurrentSheet(sheetId: string): void;
    setSyncCursor(col: number, row: number): void;
    clearSyncCursor(): void;
    setSyncSelection(
        startCol: number,
        startRow: number,
        endCol: number,
        endRow: number,
    ): void;
    clearSyncSelection(): void;
    setSyncMousePosition(x: number, y: number): void;
    clearSyncMousePosition(): void;
    setSyncEditing(col: number, row: number, text: string): void;
    clearSyncEditing(): void;
    getRemotePresences(): string;

    // Debug/Development
    debugParseFormula(formulaText: string): string;

    // Scripting (Luau)
    executeScript(script: string): string;
    tokenizeLuau(source: string): string;
    getAutocomplete(source: string, line: number, column: number): string;


    // Viewport pixel queries (Phase 5)
    getColumnPixelOffset(position: number): number;
    getRowPixelOffset(position: number): number;
    getTotalWidth(): number;
    getTotalHeight(): number;

    // Formula API (Phase 7)
    validateFormula(formulaText: string): string;
    getFormulaDisplay(cellId: string): string;
    getCellDependencies(cellId: string): string;
    getCellDependents(cellId: string): string;
    getFormulaReferences(formulaText: string): string;
    getReferencesFromPartial(formulaText: string): string;
    detectCircularRef(cellId: string): string;
    getVolatileCells(): string;

    // Spill range queries
    getSpillRangeAt(col: number, row: number): string;
}

/** Factory function type for WASM module initialization */
export declare function createCellsModule(): Promise<CellsModule>;

/** Worker message from main thread */
export interface WorkerRequest {
    id: number;
    type: string;
    [key: string]: unknown;
}

/** Worker response to main thread */
export interface WorkerResponse {
    type: string;
    [key: string]: unknown;
}

/** JSON result with possible error */
export interface JsonResult {
    error?: string;
    [key: string]: unknown;
}
