// Cells WASM Worker - Runs the spreadsheet engine in a Web Worker
// This provides the same API semantics as the REST server but via postMessage

// Import the WASM module (Emscripten-generated)
// The path will be resolved relative to the worker's location
let Module = null;
let engine = null;
let isReady = false;
let pendingMessages = [];

// Load the WASM module
async function initModule() {
    try {
        // Import the Emscripten-generated module
        // Using importScripts for classic workers, or dynamic import for module workers
        if (typeof importScripts === 'function') {
            // Classic worker - use importScripts
            importScripts('./cells_wasm_bin.js');
            Module = await createCellsModule();
        } else {
            // Module worker - use dynamic import
            const module = await import('./cells_wasm_bin.js');
            Module = await module.default();
        }

        // Create the engine instance
        engine = new Module.CellsEngine();

        // Register listener for change notifications from WASM
        // This sends unsolicited messages to the main thread when data changes
        engine.setListener((changeType) => {
            self.postMessage({
                type: 'dataChanged',
                changeType: changeType  // 'cell', 'structure', 'sheet', or 'loaded'
            });
        });

        isReady = true;

        // Notify main thread that we're ready
        self.postMessage({ type: 'ready' });

        // Process any messages that arrived before we were ready
        for (const msg of pendingMessages) {
            handleMessage(msg);
        }
        pendingMessages = [];
    } catch (err) {
        self.postMessage({
            type: 'error',
            error: 'Failed to initialize WASM module: ' + err.message
        });
    }
}

// Handle messages from the main thread
function handleMessage(msg) {
    const { id, type, ...params } = msg;

    // Wrap response with request ID for correlation
    function respond(response) {
        self.postMessage({ id, ...response });
    }

    try {
        switch (type) {
            case 'load': {
                const { format, data } = params;
                let result;

                if (format === 'cells') {
                    // data is a string for .cells format
                    const content = typeof data === 'string' ? data : new TextDecoder().decode(data);
                    result = JSON.parse(engine.loadFromCells(content));
                } else if (format === 'csv') {
                    // data is ArrayBuffer, delimiter defaults to comma
                    const content = new TextDecoder().decode(data);
                    const delimiter = params.delimiter || ',';
                    const hasHeader = params.hasHeader !== false;
                    result = JSON.parse(engine.loadFromCSV(content, delimiter.charCodeAt(0), hasHeader));
                } else if (format === 'xlsx') {
                    // data is ArrayBuffer, convert to binary string for C++
                    const bytes = new Uint8Array(data);
                    let binaryStr = '';
                    for (let i = 0; i < bytes.length; i++) {
                        binaryStr += String.fromCharCode(bytes[i]);
                    }
                    result = JSON.parse(engine.loadFromXLSXData(binaryStr));
                } else {
                    respond({ type: 'error', error: 'Unknown format: ' + format });
                    return;
                }

                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    // Get sheet names
                    const sheetNames = [];
                    for (let i = 0; i < result.sheetCount; i++) {
                        sheetNames.push(engine.getSheetName(i));
                    }
                    respond({
                        type: 'loaded',
                        sheetCount: result.sheetCount,
                        sheetNames
                    });
                }
                break;
            }

            case 'getSheetInfo': {
                const result = JSON.parse(engine.getSheetInfo());
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'sheetInfo', ...result });
                }
                break;
            }

            case 'queryViewport': {
                const { x1, y1, x2, y2 } = params;
                const result = JSON.parse(engine.queryViewport(x1, y1, x2, y2));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'viewport', ...result });
                }
                break;
            }

            case 'setActiveSheet': {
                const { index } = params;
                engine.setActiveSheet(index);
                respond({ type: 'sheetChanged', index });
                break;
            }

            case 'getSheets': {
                const count = engine.getSheetCount();
                const activeIndex = engine.getActiveSheetIndex();
                const sheets = [];
                for (let i = 0; i < count; i++) {
                    sheets.push({
                        index: i,
                        name: engine.getSheetName(i),
                        active: i === activeIndex
                    });
                }
                respond({ type: 'sheets', sheets, activeIndex });
                break;
            }

            case 'addSheet': {
                const { name } = params;
                const result = JSON.parse(engine.addSheet(name || ''));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'sheetAdded', index: result.index, name: result.name });
                }
                break;
            }

            case 'deleteSheet': {
                const { index } = params;
                const result = JSON.parse(engine.deleteSheet(index));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'sheetDeleted', activeIndex: result.activeIndex });
                }
                break;
            }

            case 'renameSheet': {
                const { index, name } = params;
                const result = JSON.parse(engine.renameSheet(index, name));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'sheetRenamed', success: true });
                }
                break;
            }

            case 'moveSheet': {
                const { fromIndex, toIndex } = params;
                const result = JSON.parse(engine.moveSheet(fromIndex, toIndex));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'sheetMoved', activeIndex: result.activeIndex });
                }
                break;
            }

            case 'updateCell': {
                const { cellId, value } = params;
                const result = JSON.parse(engine.updateCell(cellId, value));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'cellUpdated', success: true });
                }
                break;
            }

            case 'createCell': {
                const { col, row, value } = params;
                const result = JSON.parse(engine.createCell(col, row, value || ''));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'cellCreated', cellId: result.id });
                }
                break;
            }

            case 'createCellIfNeeded': {
                const { col, row } = params;
                const result = JSON.parse(engine.createCellIfNeeded(col, row));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'cellCreated', cellId: result.id, existed: result.existed });
                }
                break;
            }

            case 'deleteCell': {
                const { cellId } = params;
                const result = JSON.parse(engine.deleteCell(cellId));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'cellDeleted', success: true });
                }
                break;
            }

            case 'resizeColumn': {
                const { colId, width } = params;
                const result = JSON.parse(engine.resizeColumn(colId, width));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnResized', success: true });
                }
                break;
            }

            case 'resizeColumnByPos': {
                const { pos, width } = params;
                const result = JSON.parse(engine.resizeColumnByPos(pos, width));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnResized', id: result.id, success: true });
                }
                break;
            }

            case 'resizeRow': {
                const { rowId, height } = params;
                const result = JSON.parse(engine.resizeRow(rowId, height));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'rowResized', success: true });
                }
                break;
            }

            case 'resizeRowByPos': {
                const { pos, height } = params;
                const result = JSON.parse(engine.resizeRowByPos(pos, height));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'rowResized', id: result.id, success: true });
                }
                break;
            }

            case 'renameColumn': {
                const { colId, name } = params;
                const result = JSON.parse(engine.renameColumn(colId, name));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnRenamed', success: true });
                }
                break;
            }

            case 'renameColumnByPos': {
                const { pos, name } = params;
                const result = JSON.parse(engine.renameColumnByPos(pos, name));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnRenamed', id: result.id, success: true });
                }
                break;
            }

            case 'moveColumn': {
                const { colId, targetPos } = params;
                const result = JSON.parse(engine.moveColumn(colId, targetPos));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnMoved', success: true });
                }
                break;
            }

            case 'moveRow': {
                const { rowId, targetPos } = params;
                const result = JSON.parse(engine.moveRow(rowId, targetPos));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'rowMoved', success: true });
                }
                break;
            }

            case 'shiftColumnsForEmptyMove': {
                const { sourcePos, targetPos } = params;
                const result = JSON.parse(engine.shiftColumnsForEmptyMove(sourcePos, targetPos));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'columnsShifted', success: true });
                }
                break;
            }

            case 'shiftRowsForEmptyMove': {
                const { sourcePos, targetPos } = params;
                const result = JSON.parse(engine.shiftRowsForEmptyMove(sourcePos, targetPos));
                if (result.error) {
                    respond({ type: 'error', error: result.error });
                } else {
                    respond({ type: 'rowsShifted', success: true });
                }
                break;
            }

            case 'export': {
                const { format } = params;
                let data, filename;
                const workbookName = engine.getWorkbookName() || 'spreadsheet';

                if (format === 'cells') {
                    const content = engine.exportToCells();
                    if (!content) {
                        respond({ type: 'error', error: 'Export failed' });
                        return;
                    }
                    // Convert string to ArrayBuffer for transfer
                    data = new TextEncoder().encode(content).buffer;
                    filename = workbookName + '.cells';
                } else if (format === 'csv') {
                    const content = engine.exportToCSV();
                    if (!content) {
                        respond({ type: 'error', error: 'Export failed' });
                        return;
                    }
                    data = new TextEncoder().encode(content).buffer;
                    filename = workbookName + '.csv';
                } else if (format === 'xlsx') {
                    const binaryStr = engine.exportToXLSX();
                    if (!binaryStr) {
                        respond({ type: 'error', error: 'XLSX export not available or failed' });
                        return;
                    }
                    // Convert binary string to ArrayBuffer
                    const bytes = new Uint8Array(binaryStr.length);
                    for (let i = 0; i < binaryStr.length; i++) {
                        bytes[i] = binaryStr.charCodeAt(i);
                    }
                    data = bytes.buffer;
                    filename = workbookName + '.xlsx';
                } else {
                    respond({ type: 'error', error: 'Unknown export format: ' + format });
                    return;
                }

                // Transfer the ArrayBuffer for efficiency
                respond({ type: 'exported', format, data, filename }, [data]);
                break;
            }

            case 'createEmpty': {
                engine.createEmptyWorkbook();
                respond({ type: 'created', sheetCount: 1 });
                break;
            }

            case 'setWorkbookName': {
                const { name } = params;
                engine.setWorkbookName(name);
                respond({ type: 'nameSet', success: true });
                break;
            }

            case 'getWorkbookName': {
                const name = engine.getWorkbookName();
                respond({ type: 'workbookName', name });
                break;
            }

            // ================================================================
            // CRDT Collaboration methods
            // ================================================================

            case 'setNodeId': {
                const { nodeId } = params;
                const result = engine.setNodeId(nodeId);
                respond({ type: 'nodeIdSet', result });
                break;
            }

            case 'getNodeId': {
                const nodeId = engine.getNodeId();
                respond({ type: 'nodeId', nodeId });
                break;
            }

            case 'getCurrentHLC': {
                const hlc = engine.getCurrentHLC();
                respond({ type: 'currentHLC', hlc });
                break;
            }

            case 'getOperationsSince': {
                const { sinceHLC } = params;
                const result = engine.getOperationsSince(sinceHLC || '');
                respond({ type: 'operationsSince', result });
                break;
            }

            case 'applyRemoteOperation': {
                const { opJson } = params;
                const result = engine.applyRemoteOperation(opJson);
                respond({ type: 'operationApplied', result });
                break;
            }

            case 'applyRemoteOperations': {
                const { opsJson } = params;
                const result = engine.applyRemoteOperations(opsJson);
                respond({ type: 'operationsApplied', result });
                break;
            }

            case 'getOpLogSize': {
                const size = engine.getOpLogSize();
                respond({ type: 'opLogSize', size });
                break;
            }

            case 'hasOperation': {
                const { hlc } = params;
                const exists = engine.hasOperation(hlc);
                respond({ type: 'hasOperation', exists });
                break;
            }

            // ================================================================
            // SyncManager methods
            // ================================================================

            case 'initSyncManager': {
                const result = engine.initSyncManager();
                respond({ type: 'syncManagerInitialized', result });
                break;
            }

            case 'addPeer': {
                const { peerId } = params;
                const result = engine.addPeer(peerId);
                respond({ type: 'peerAdded', result });
                break;
            }

            case 'removePeer': {
                const { peerId } = params;
                const result = engine.removePeer(peerId);
                respond({ type: 'peerRemoved', result });
                break;
            }

            case 'getPeerIds': {
                const result = engine.getPeerIds();
                respond({ type: 'peerIds', result });
                break;
            }

            case 'getPeerCount': {
                const count = engine.getPeerCount();
                respond({ type: 'peerCount', count });
                break;
            }

            case 'handlePeerMessage': {
                const { peerId, messageJson } = params;
                const result = engine.handlePeerMessage(peerId, messageJson);
                respond({ type: 'peerMessageHandled', result });
                break;
            }

            case 'getOutgoingMessages': {
                const result = engine.getOutgoingMessages();
                respond({ type: 'outgoingMessages', result });
                break;
            }

            case 'queueOperationsBroadcast': {
                const result = engine.queueOperationsBroadcast();
                respond({ type: 'operationsQueued', result });
                break;
            }

            default:
                respond({ type: 'error', error: 'Unknown message type: ' + type });
        }
    } catch (err) {
        respond({ type: 'error', error: err.message || String(err) });
    }
}

// Listen for messages from main thread
self.onmessage = function(e) {
    if (!isReady) {
        pendingMessages.push(e.data);
        return;
    }
    handleMessage(e.data);
};

// Start initialization
initModule();
