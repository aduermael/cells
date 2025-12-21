// Cells WASM Client - Main thread API for communicating with the WASM worker
// Provides a Promise-based API that matches the REST server semantics

/**
 * CellsClient - Communicates with the WASM worker
 *
 * Usage:
 *   const client = new CellsClient('/path/to/worker.js');
 *   await client.ready;
 *   await client.loadFile(arrayBuffer, 'xlsx');
 *   const viewport = await client.queryViewport(0, 0, 10, 20);
 */
class CellsClient {
    constructor(workerPath = './worker.js') {
        this._worker = new Worker(workerPath);
        this._requestId = 0;
        this._pending = new Map(); // id -> { resolve, reject }
        this._isReady = false;
        this._readyPromise = null;
        this._readyResolve = null;
        this._onDataChanged = null;  // Optional callback for data change notifications

        // Create ready promise
        this._readyPromise = new Promise((resolve, reject) => {
            this._readyResolve = resolve;
            // Timeout after 30 seconds
            setTimeout(() => {
                if (!this._isReady) {
                    reject(new Error('Worker initialization timeout'));
                }
            }, 30000);
        });

        // Handle messages from worker
        this._worker.onmessage = (e) => this._handleMessage(e.data);
        this._worker.onerror = (e) => this._handleError(e);
    }

    /**
     * Promise that resolves when the worker is ready
     */
    get ready() {
        return this._readyPromise;
    }

    /**
     * Whether the worker is ready to receive messages
     */
    get isReady() {
        return this._isReady;
    }

    _handleMessage(msg) {
        // Handle ready message
        if (msg.type === 'ready') {
            this._isReady = true;
            if (this._readyResolve) {
                this._readyResolve();
                this._readyResolve = null;
            }
            return;
        }

        // Handle unsolicited data change notifications from WASM
        if (msg.type === 'dataChanged') {
            if (this._onDataChanged) {
                this._onDataChanged(msg.changeType);
            }
            return;
        }

        // Handle response to a request
        const { id } = msg;
        if (id !== undefined && this._pending.has(id)) {
            const { resolve, reject } = this._pending.get(id);
            this._pending.delete(id);

            if (msg.type === 'error') {
                reject(new Error(msg.error));
            } else {
                resolve(msg);
            }
        }
    }

    _handleError(e) {
        console.error('Worker error:', e);
        // Reject all pending requests
        for (const [id, { reject }] of this._pending) {
            reject(new Error('Worker error: ' + e.message));
        }
        this._pending.clear();
    }

    /**
     * Send a message to the worker and wait for response
     * @param {string} type - Message type
     * @param {object} params - Message parameters
     * @param {Array} transfer - Transferable objects
     * @returns {Promise} Response from worker
     */
    _send(type, params = {}, transfer = []) {
        return new Promise((resolve, reject) => {
            const id = this._requestId++;
            this._pending.set(id, { resolve, reject });
            this._worker.postMessage({ id, type, ...params }, transfer);
        });
    }

    /**
     * Terminate the worker
     */
    terminate() {
        this._worker.terminate();
        // Reject all pending requests
        for (const [id, { reject }] of this._pending) {
            reject(new Error('Worker terminated'));
        }
        this._pending.clear();
    }

    // ========================================================================
    // Change Notification API
    // ========================================================================

    /**
     * Set a callback to be called when data changes in the engine
     * The callback receives a change type: 'cell', 'structure', 'sheet', or 'loaded'
     * @param {function(string): void} callback - The callback function
     */
    setOnDataChanged(callback) {
        this._onDataChanged = callback;
    }

    /**
     * Remove the data change callback
     */
    removeOnDataChanged() {
        this._onDataChanged = null;
    }

    // ========================================================================
    // File Loading API
    // ========================================================================

    /**
     * Load a file into the engine
     * @param {ArrayBuffer|string} data - File data
     * @param {string} format - 'cells', 'csv', or 'xlsx'
     * @param {object} options - Format-specific options
     * @returns {Promise<{sheetCount: number, sheetNames: string[]}>}
     */
    async loadFile(data, format, options = {}) {
        const params = { format, data, ...options };

        // For CSV, convert delimiter to string if provided
        if (format === 'csv' && options.delimiter) {
            params.delimiter = options.delimiter;
        }

        // Transfer ArrayBuffer if possible
        const transfer = data instanceof ArrayBuffer ? [data] : [];
        const response = await this._send('load', params, transfer);

        return {
            sheetCount: response.sheetCount,
            sheetNames: response.sheetNames
        };
    }

    /**
     * Load a .cells file from string content
     * @param {string} content - .cells file content
     * @returns {Promise<{sheetCount: number, sheetNames: string[]}>}
     */
    async loadCells(content) {
        return this.loadFile(content, 'cells');
    }

    /**
     * Load a CSV file
     * @param {ArrayBuffer|string} data - CSV data
     * @param {object} options - { delimiter: string, hasHeader: boolean }
     * @returns {Promise<{sheetCount: number, sheetNames: string[]}>}
     */
    async loadCSV(data, options = {}) {
        return this.loadFile(data, 'csv', options);
    }

    /**
     * Load an XLSX file
     * @param {ArrayBuffer} data - XLSX data
     * @returns {Promise<{sheetCount: number, sheetNames: string[]}>}
     */
    async loadXLSX(data) {
        return this.loadFile(data, 'xlsx');
    }

    /**
     * Create an empty workbook
     * @returns {Promise<{sheetCount: number}>}
     */
    async createEmpty() {
        const response = await this._send('createEmpty');
        return { sheetCount: response.sheetCount };
    }

    // ========================================================================
    // Sheet Info API
    // ========================================================================

    /**
     * Get information about the active sheet
     * @returns {Promise<{name: string, rowCount: number, colCount: number, defaultColWidth: number, defaultRowHeight: number}>}
     */
    async getSheetInfo() {
        const response = await this._send('getSheetInfo');
        return {
            name: response.name,
            rowCount: response.rowCount,
            colCount: response.colCount,
            defaultColWidth: response.defaultColWidth,
            defaultRowHeight: response.defaultRowHeight
        };
    }

    /**
     * Set the active sheet
     * @param {number} index - Sheet index (0-based)
     * @returns {Promise<void>}
     */
    async setActiveSheet(index) {
        await this._send('setActiveSheet', { index });
    }

    /**
     * Get list of all sheets in the workbook
     * @returns {Promise<{sheets: Array<{index: number, name: string, active: boolean}>, activeIndex: number}>}
     */
    async getSheets() {
        const response = await this._send('getSheets');
        return {
            sheets: response.sheets || [],
            activeIndex: response.activeIndex
        };
    }

    /**
     * Add a new sheet to the workbook
     * @param {string} name - Sheet name (optional, will be auto-generated if empty/duplicate)
     * @returns {Promise<{index: number, name: string}>}
     */
    async addSheet(name = '') {
        const response = await this._send('addSheet', { name });
        return {
            index: response.index,
            name: response.name
        };
    }

    /**
     * Delete a sheet by index
     * @param {number} index - Sheet index (0-based)
     * @returns {Promise<{activeIndex: number}>}
     */
    async deleteSheet(index) {
        const response = await this._send('deleteSheet', { index });
        return {
            activeIndex: response.activeIndex
        };
    }

    /**
     * Rename a sheet
     * @param {number} index - Sheet index (0-based)
     * @param {string} name - New name
     * @returns {Promise<{success: boolean}>}
     */
    async renameSheet(index, name) {
        await this._send('renameSheet', { index, name });
        return { success: true };
    }

    /**
     * Move a sheet to a new position
     * @param {number} fromIndex - Current sheet index
     * @param {number} toIndex - Target position (insert before)
     * @returns {Promise<{activeIndex: number}>}
     */
    async moveSheet(fromIndex, toIndex) {
        const response = await this._send('moveSheet', { fromIndex, toIndex });
        return {
            activeIndex: response.activeIndex
        };
    }

    // ========================================================================
    // Viewport API
    // ========================================================================

    /**
     * Query cells in a viewport range
     * @param {number} x1 - Left column (inclusive)
     * @param {number} y1 - Top row (inclusive)
     * @param {number} x2 - Right column (exclusive)
     * @param {number} y2 - Bottom row (exclusive)
     * @returns {Promise<{cells: Array, columns: Array, rows: Array}>}
     */
    async queryViewport(x1, y1, x2, y2) {
        const response = await this._send('queryViewport', { x1, y1, x2, y2 });
        return {
            cells: response.cells || [],
            columns: response.columns || [],
            rows: response.rows || []
        };
    }

    // ========================================================================
    // Cell Operations API
    // ========================================================================

    /**
     * Update a cell's value
     * @param {string} cellId - Cell ID (8-char base62)
     * @param {string} value - New value
     * @returns {Promise<{success: boolean}>}
     */
    async updateCell(cellId, value) {
        const response = await this._send('updateCell', { cellId, value });
        return { success: true };
    }

    /**
     * Create a new cell at a position
     * @param {number} col - Column position (0-based)
     * @param {number} row - Row position (0-based)
     * @param {string} value - Initial value
     * @returns {Promise<{id: string}>}
     */
    async createCell(col, row, value = '') {
        const response = await this._send('createCell', { col, row, value });
        return { id: response.cellId };
    }

    /**
     * Delete a cell by ID
     * @param {string} cellId - Cell ID (8-char base62)
     * @returns {Promise<{success: boolean}>}
     */
    async deleteCell(cellId) {
        await this._send('deleteCell', { cellId });
        return { success: true };
    }

    // ========================================================================
    // Column/Row Operations API
    // ========================================================================

    /**
     * Resize a column by ID
     * @param {string} colId - Column ID
     * @param {number} width - New width in pixels
     * @returns {Promise<{success: boolean}>}
     */
    async resizeColumn(colId, width) {
        await this._send('resizeColumn', { colId, width });
        return { success: true };
    }

    /**
     * Resize a column by position (creates column if needed)
     * @param {number} pos - Column position (0-based)
     * @param {number} width - New width in pixels
     * @returns {Promise<{id: string, success: boolean}>}
     */
    async resizeColumnByPos(pos, width) {
        const response = await this._send('resizeColumnByPos', { pos, width });
        return { id: response.id, success: true };
    }

    /**
     * Resize a row by ID
     * @param {string} rowId - Row ID
     * @param {number} height - New height in pixels
     * @returns {Promise<{success: boolean}>}
     */
    async resizeRow(rowId, height) {
        await this._send('resizeRow', { rowId, height });
        return { success: true };
    }

    /**
     * Resize a row by position (creates row if needed)
     * @param {number} pos - Row position (0-based)
     * @param {number} height - New height in pixels
     * @returns {Promise<{id: string, success: boolean}>}
     */
    async resizeRowByPos(pos, height) {
        const response = await this._send('resizeRowByPos', { pos, height });
        return { id: response.id, success: true };
    }

    /**
     * Move a column to a new position
     * @param {string} colId - Column ID to move
     * @param {number} targetPos - Target position (insert before)
     * @returns {Promise<{success: boolean}>}
     */
    async moveColumn(colId, targetPos) {
        await this._send('moveColumn', { colId, targetPos });
        return { success: true };
    }

    /**
     * Move a row to a new position
     * @param {string} rowId - Row ID to move
     * @param {number} targetPos - Target position (insert before)
     * @returns {Promise<{success: boolean}>}
     */
    async moveRow(rowId, targetPos) {
        await this._send('moveRow', { rowId, targetPos });
        return { success: true };
    }

    /**
     * Shift columns when moving an empty column position
     * @param {number} sourcePos - Source position (empty)
     * @param {number} targetPos - Target position (insert before)
     * @returns {Promise<{success: boolean}>}
     */
    async shiftColumnsForEmptyMove(sourcePos, targetPos) {
        await this._send('shiftColumnsForEmptyMove', { sourcePos, targetPos });
        return { success: true };
    }

    /**
     * Shift rows when moving an empty row position
     * @param {number} sourcePos - Source position (empty)
     * @param {number} targetPos - Target position (insert before)
     * @returns {Promise<{success: boolean}>}
     */
    async shiftRowsForEmptyMove(sourcePos, targetPos) {
        await this._send('shiftRowsForEmptyMove', { sourcePos, targetPos });
        return { success: true };
    }

    /**
     * Rename a column
     * @param {string} colId - Column ID
     * @param {string} name - New column name (empty string to clear)
     * @returns {Promise<{success: boolean}>}
     */
    async renameColumn(colId, name) {
        await this._send('renameColumn', { colId, name });
        return { success: true };
    }

    /**
     * Rename a column by position (creates column if it doesn't exist)
     * @param {number} pos - Column position (0-indexed)
     * @param {string} name - New column name (empty string to clear)
     * @returns {Promise<{success: boolean, id: string}>}
     */
    async renameColumnByPos(pos, name) {
        const result = await this._send('renameColumnByPos', { pos, name });
        return { success: true, id: result.id };
    }

    // ========================================================================
    // Export API
    // ========================================================================

    /**
     * Export the workbook to a format
     * @param {string} format - 'cells', 'csv', or 'xlsx'
     * @returns {Promise<{data: ArrayBuffer, filename: string}>}
     */
    async exportAs(format) {
        const response = await this._send('export', { format });
        return {
            data: response.data,
            filename: response.filename
        };
    }

    /**
     * Export to .cells format
     * @returns {Promise<{data: ArrayBuffer, filename: string}>}
     */
    async exportCells() {
        return this.exportAs('cells');
    }

    /**
     * Export to CSV format
     * @returns {Promise<{data: ArrayBuffer, filename: string}>}
     */
    async exportCSV() {
        return this.exportAs('csv');
    }

    /**
     * Export to XLSX format
     * @returns {Promise<{data: ArrayBuffer, filename: string}>}
     */
    async exportXLSX() {
        return this.exportAs('xlsx');
    }

    // ========================================================================
    // Workbook Management API
    // ========================================================================

    /**
     * Get the workbook name
     * @returns {Promise<string>}
     */
    async getWorkbookName() {
        const response = await this._send('getWorkbookName');
        return response.name;
    }

    /**
     * Set the workbook name
     * @param {string} name - New workbook name
     * @returns {Promise<void>}
     */
    async setWorkbookName(name) {
        await this._send('setWorkbookName', { name });
    }

    // ========================================================================
    // CRDT Collaboration API
    // ========================================================================

    /**
     * Set the node ID for HLC generation
     * @param {string} nodeId - 8-char base62 ID
     * @returns {Promise<string>} JSON result
     */
    async setNodeId(nodeId) {
        const response = await this._send('setNodeId', { nodeId });
        return response.result;
    }

    /**
     * Get the current node ID
     * @returns {Promise<string>}
     */
    async getNodeId() {
        const response = await this._send('getNodeId');
        return response.nodeId;
    }

    /**
     * Get the current HLC timestamp
     * @returns {Promise<string>}
     */
    async getCurrentHLC() {
        const response = await this._send('getCurrentHLC');
        return response.hlc;
    }

    /**
     * Get operations since a given HLC
     * @param {string} sinceHLC - HLC to get operations after
     * @returns {Promise<string>} JSON result with operations
     */
    async getOperationsSince(sinceHLC = '') {
        const response = await this._send('getOperationsSince', { sinceHLC });
        return response.result;
    }

    /**
     * Apply a remote CRDT operation
     * @param {string} opJson - Operation JSON
     * @returns {Promise<string>} JSON result
     */
    async applyRemoteOperation(opJson) {
        const response = await this._send('applyRemoteOperation', { opJson });
        return response.result;
    }

    /**
     * Apply multiple remote CRDT operations
     * @param {string} opsJson - JSON with operations array
     * @returns {Promise<string>} JSON result
     */
    async applyRemoteOperations(opsJson) {
        const response = await this._send('applyRemoteOperations', { opsJson });
        return response.result;
    }

    /**
     * Get the number of operations in the OpLog
     * @returns {Promise<number>}
     */
    async getOpLogSize() {
        const response = await this._send('getOpLogSize');
        return response.size;
    }

    /**
     * Check if an operation with the given HLC exists
     * @param {string} hlc - HLC to check
     * @returns {Promise<boolean>}
     */
    async hasOperation(hlc) {
        const response = await this._send('hasOperation', { hlc });
        return response.exists;
    }
}

// Export for both browser and module environments
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { CellsClient };
} else if (typeof window !== 'undefined') {
    window.CellsClient = CellsClient;
}
