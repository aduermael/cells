// Cells WASM Worker - Runs the spreadsheet engine in a Web Worker
// This provides the same API semantics as the REST server but via postMessage

// ============================================================================
// Type Definitions
// ============================================================================

// Use the global self from DedicatedWorkerGlobalScope
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const workerSelf = self as unknown as DedicatedWorkerGlobalScope;

/** WASM Module type - Emscripten-generated */
interface CellsModule {
  CellsEngine: new () => CellsEngine;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
}

/** CellsEngine WASM class interface */
interface CellsEngine {
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
  getCellFormatId(cellId: string): string;
  parseUserInputValue(input: string): string;
  formatCellValue(value: number, formatId: string): string;
  formatWithCode(value: number, formatCode: string): string;
  formatCellById(cellId: string): string;

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
    sourceMinCol: number, sourceMinRow: number,
    sourceMaxCol: number, sourceMaxRow: number,
    targetMinCol: number, targetMinRow: number,
    targetMaxCol: number, targetMaxRow: number
  ): string;

  // Export
  exportToCells(): string;
  exportToCSV(): string;
  exportToXLSX(): string;
  hasFormulas(): boolean;

  // Workbook management
  getWorkbookName(): string;
  setWorkbookName(name: string): void;
  createEmptyWorkbook(): void;

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
    endRow: number
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

  // AI Agent
  setAgentListener(callback: (type: string, data: string) => void): void;
  removeAgentListener(): void;
  initAgent(serverUrl: string): void;
  isAgentInitialized(): boolean;
  sendAgentMessage(prompt: string, conversationId: string): void;
  getAgentConversationId(): string;
  clearAgentConversation(): void;
  cancelAgent(): void;
  isAgentProcessing(): boolean;
  // JS-based streaming
  getAgentServerUrl(): string;
  feedAgentStreamData(data: string): void;
  endAgentStream(): void;
  errorAgentStream(error: string): void;
  isAgentStreaming(): boolean;
  setAgentStreaming(streaming: boolean): void;

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
}

/** Factory function type for WASM module initialization */
declare function createCellsModule(): Promise<CellsModule>;

/** Worker message from main thread */
interface WorkerRequest {
  id: number;
  type: string;
  [key: string]: unknown;
}

/** Worker response to main thread */
interface WorkerResponse {
  type: string;
  [key: string]: unknown;
}

/** JSON result with possible error */
interface JsonResult {
  error?: string;
  [key: string]: unknown;
}

// ============================================================================
// Module State
// ============================================================================

let Module: CellsModule | null = null;
let engine: CellsEngine | null = null;
let isReady = false;
const pendingMessages: WorkerRequest[] = [];

// ============================================================================
// Module Initialization
// ============================================================================

async function initModule(): Promise<void> {
  try {
    // Import the Emscripten-generated module
    // Using importScripts for classic workers, or dynamic import for module workers
    if (typeof importScripts === "function") {
      // Classic worker - use importScripts
      importScripts("./cells_wasm_bin.js");
      Module = await createCellsModule();
    } else {
      // Module worker - use dynamic import
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const module = (await import("./cells_wasm_bin.js" as any)) as {
        default: () => Promise<CellsModule>;
      };
      Module = await module.default();
    }

    // Create the engine instance (Module is guaranteed non-null here)
    engine = new (Module as CellsModule).CellsEngine();

    // Register listener for change notifications from WASM
    // This sends unsolicited messages to the main thread when data changes
    // The callback receives (changeType, data?) where data is optional extra info
    engine.setListener((changeType: string, data?: string) => {
      if (changeType === "load_progress" && data) {
        // Parse progress data: "cellsLoaded:totalEstimate"
        const [loaded, total] = data.split(":").map(Number);
        workerSelf.postMessage({
          type: "loadProgress",
          cellsLoaded: loaded,
          totalEstimate: total,
        });
      } else {
        workerSelf.postMessage({
          type: "dataChanged",
          changeType: changeType, // 'cell', 'structure', 'sheet', or 'loaded'
        });
      }
    });

    // Register agent listener for AI events
    // The agent callback receives (eventType, data) where data is JSON or text
    // Note: engine is guaranteed non-null here since we're inside the init callback
    const eng = engine!;
    eng.setAgentListener((eventType: string, data: string) => {
      // Handle special event to send tool results
      if (eventType === "send_tool_result") {
        const toolResult = JSON.parse(data);
        const serverUrl = eng.getAgentServerUrl();
        if (serverUrl != null) {
          streamAgentMessage(serverUrl + "/api/agent/tool-result", {
            conversation_id: eng.getAgentConversationId(),
            tool_use_id: toolResult.tool_use_id,
            result: toolResult.result,
            is_error: toolResult.is_error,
          });
        }
        return;
      }

      workerSelf.postMessage({
        type: "agentEvent",
        eventType: eventType, // 'text', 'tool_use', 'tool_result_needed', 'done', 'error'
        data: data,
      });
    });

    isReady = true;

    // Notify main thread that we're ready
    workerSelf.postMessage({ type: "ready" });

    // Process any messages that arrived before we were ready
    for (const msg of pendingMessages) {
      handleMessage(msg);
    }
    pendingMessages.length = 0;
  } catch (err) {
    const error = err instanceof Error ? err.message : String(err);
    workerSelf.postMessage({
      type: "error",
      error: "Failed to initialize WASM module: " + error,
    });
  }
}

// ============================================================================
// Agent Streaming Fetch
// ============================================================================

// Perform a streaming fetch to the agent server and feed chunks to C++
async function streamAgentMessage(url: string, body: Record<string, unknown>): Promise<void> {
  const isToolResult = url.includes("/tool-result");

  try {
    const response = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });

    if (!response.ok) {
      // Provide more context for tool result failures
      if (isToolResult) {
        const convId = body.conversation_id || "unknown";
        const toolId = body.tool_use_id || "unknown";
        console.error(`[Agent] Tool result failed: HTTP ${response.status} for conversation=${convId}, tool=${toolId}`);

        // Try to read error body for more details
        let errorDetail = response.statusText;
        try {
          const errorText = await response.text();
          if (errorText) errorDetail = errorText;
        } catch {
          // Ignore error reading body
        }

        throw new Error(`Failed to send tool result (${response.status}): ${errorDetail}. The AI session may have been lost - please try your request again.`);
      }
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const reader = response.body?.getReader();
    if (!reader) {
      throw new Error("ReadableStream not supported");
    }

    const decoder = new TextDecoder();

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;

      // Feed raw SSE data to C++ for parsing
      const chunk = decoder.decode(value, { stream: true });
      engine?.feedAgentStreamData(chunk);
    }

    // Signal stream end
    engine?.endAgentStream();
  } catch (err) {
    const error = err instanceof Error ? err.message : String(err);
    console.error(`[Agent] Stream error (isToolResult=${isToolResult}):`, error);
    engine?.errorAgentStream(error);
  }
}

// ============================================================================
// Message Handler
// ============================================================================

function handleMessage(msg: WorkerRequest): void {
  const { id, type, ...params } = msg;

  // Wrap response with request ID for correlation
  function respond(
    response: WorkerResponse,
    transfer?: Transferable[]
  ): void {
    workerSelf.postMessage(
      { id, ...response },
      transfer ? { transfer } : undefined
    );
  }

  // Ensure engine is available
  if (!engine || !Module) {
    respond({ type: "error", error: "Engine not initialized" });
    return;
  }

  try {
    switch (type) {
      case "load": {
        const { format, data } = params;
        let result: JsonResult;

        if (format === "zcd") {
          // data is a string for .zcd format
          const content =
            typeof data === "string"
              ? data
              : new TextDecoder().decode(data as ArrayBuffer);
          result = JSON.parse(engine.loadFromCells(content)) as JsonResult;
        } else if (format === "csv") {
          // data is ArrayBuffer, delimiter defaults to comma
          const content = new TextDecoder().decode(data as ArrayBuffer);
          const delimiter = (params.delimiter as string) || ",";
          const hasHeader = params.hasHeader !== false;
          result = JSON.parse(
            engine.loadFromCSV(content, delimiter.charCodeAt(0), hasHeader)
          ) as JsonResult;
        } else if (format === "xlsx") {
          // data is ArrayBuffer - copy directly to WASM heap to avoid UTF-8 encoding issues
          const bytes = new Uint8Array(data as ArrayBuffer);
          const ptr = Module._malloc(bytes.length);
          Module.HEAPU8.set(bytes, ptr);
          result = JSON.parse(
            engine.loadFromXLSXDataPtr(ptr, bytes.length)
          ) as JsonResult;
          Module._free(ptr);
        } else {
          respond({ type: "error", error: "Unknown format: " + format });
          return;
        }

        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          // Get sheet names
          const sheetCount = result.sheetCount as number;
          const sheetNames: string[] = [];
          for (let i = 0; i < sheetCount; i++) {
            sheetNames.push(engine.getSheetName(i));
          }
          respond({
            type: "loaded",
            sheetCount,
            sheetNames,
          });
        }
        break;
      }

      case "getSheetInfo": {
        const result = JSON.parse(engine.getSheetInfo()) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "sheetInfo", ...result });
        }
        break;
      }

      case "queryViewport": {
        const { x1, y1, x2, y2 } = params as {
          x1: number;
          y1: number;
          x2: number;
          y2: number;
        };
        const result = JSON.parse(
          engine.queryViewport(x1, y1, x2, y2)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "viewport", ...result });
        }
        break;
      }

      case "setActiveSheet": {
        const { index } = params as { index: number };
        engine.setActiveSheet(index);
        respond({ type: "sheetChanged", index });
        break;
      }

      case "getSheets": {
        const count = engine.getSheetCount();
        const activeIndex = engine.getActiveSheetIndex();
        const sheets: Array<{ index: number; name: string; active: boolean }> =
          [];
        for (let i = 0; i < count; i++) {
          sheets.push({
            index: i,
            name: engine.getSheetName(i),
            active: i === activeIndex,
          });
        }
        respond({ type: "sheets", sheets, activeIndex });
        break;
      }

      case "addSheet": {
        const { name } = params as { name?: string };
        const result = JSON.parse(engine.addSheet(name || "")) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({
            type: "sheetAdded",
            index: result.index,
            name: result.name,
          });
        }
        break;
      }

      case "deleteSheet": {
        const { index } = params as { index: number };
        const result = JSON.parse(engine.deleteSheet(index)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "sheetDeleted", activeIndex: result.activeIndex });
        }
        break;
      }

      case "renameSheet": {
        const { index, name } = params as { index: number; name: string };
        const result = JSON.parse(engine.renameSheet(index, name)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "sheetRenamed", success: true });
        }
        break;
      }

      case "moveSheet": {
        const { fromIndex, toIndex } = params as {
          fromIndex: number;
          toIndex: number;
        };
        const result = JSON.parse(
          engine.moveSheet(fromIndex, toIndex)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "sheetMoved", activeIndex: result.activeIndex });
        }
        break;
      }

      case "updateCell": {
        const { cellId, value } = params as { cellId: string; value: string };
        const result = JSON.parse(engine.updateCell(cellId, value)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "cellUpdated", success: true });
        }
        break;
      }

      case "updateCellWithFormatDetection": {
        const { cellId, value } = params as { cellId: string; value: string };
        const result = JSON.parse(engine.updateCellWithFormatDetection(cellId, value)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "cellUpdated", success: true, formatId: result.formatId });
        }
        break;
      }

      case "createCell": {
        const { col, row, value } = params as {
          col: number;
          row: number;
          value?: string;
        };
        const result = JSON.parse(
          engine.createCell(col, row, value || "")
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "cellCreated", cellId: result.id });
        }
        break;
      }

      case "getOrCreateCellAt": {
        const { col, row } = params as { col: number; row: number };
        const result = JSON.parse(
          engine.getOrCreateCellAt(col, row)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({
            type: "cellInfo",
            cellId: result.id,
            existed: result.existed,
            value: result.value,
            formula: result.formula || null,
          });
        }
        break;
      }

      case "deleteCell": {
        const { cellId } = params as { cellId: string };
        const result = JSON.parse(engine.deleteCell(cellId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "cellDeleted", success: true });
        }
        break;
      }

      case "deleteCellAt": {
        const { col, row } = params as { col: number; row: number };
        const result = JSON.parse(engine.deleteCellAt(col, row)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "cellDeleted", deleted: result.deleted });
        }
        break;
      }

      // Number format operations
      case "setCellFormat": {
        const { cellId, formatId } = params as { cellId: string; formatId: string };
        const result = JSON.parse(engine.setCellFormat(cellId, formatId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formatSet", success: true });
        }
        break;
      }

      case "setCellFormatAt": {
        const { col, row, formatId } = params as { col: number; row: number; formatId: string };
        const result = JSON.parse(engine.setCellFormatAt(col, row, formatId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formatSet", success: true });
        }
        break;
      }

      case "getAvailableFormats": {
        const formats = JSON.parse(engine.getAvailableFormats());
        respond({ type: "formats", formats });
        break;
      }

      case "createCustomFormat": {
        const { formatCode } = params as { formatCode: string };
        const result = JSON.parse(engine.createCustomFormat(formatCode)) as { success?: boolean; formatId?: string; error?: string };
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formatCreated", formatId: result.formatId });
        }
        break;
      }

      case "getFormulaFunctions": {
        const functions = JSON.parse(engine.getFormulaFunctions());
        respond({ type: "functions", functions });
        break;
      }

      case "getCellFormatId": {
        const { cellId } = params as { cellId: string };
        const result = JSON.parse(engine.getCellFormatId(cellId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formatId", formatId: result.formatId });
        }
        break;
      }

      case "parseUserInputValue": {
        const { input } = params as { input: string };
        const result = JSON.parse(engine.parseUserInputValue(input));
        respond({ type: "parsedInput", ...result });
        break;
      }

      case "formatCellValue": {
        const { value, formatId } = params as { value: number; formatId: string };
        const result = JSON.parse(engine.formatCellValue(value, formatId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formattedValue", text: result.text });
        }
        break;
      }

      case "formatWithCode": {
        const { value, formatCode } = params as { value: number; formatCode: string };
        const result = JSON.parse(engine.formatWithCode(value, formatCode)) as JsonResult;
        if (result.error) {
          respond({ type: "formatWithCode", error: result.error });
        } else {
          respond({ type: "formatWithCode", text: result.text });
        }
        break;
      }

      case "formatCellById": {
        const { cellId } = params as { cellId: string };
        const result = JSON.parse(engine.formatCellById(cellId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "formattedValue", text: result.text });
        }
        break;
      }

      case "resizeColumn": {
        const { colId, width } = params as { colId: string; width: number };
        const result = JSON.parse(engine.resizeColumn(colId, width)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnResized", success: true });
        }
        break;
      }

      case "resizeColumnByPos": {
        const { pos, width } = params as { pos: number; width: number };
        const result = JSON.parse(
          engine.resizeColumnByPos(pos, width)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnResized", id: result.id, success: true });
        }
        break;
      }

      case "resizeRow": {
        const { rowId, height } = params as { rowId: string; height: number };
        const result = JSON.parse(engine.resizeRow(rowId, height)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rowResized", success: true });
        }
        break;
      }

      case "resizeRowByPos": {
        const { pos, height } = params as { pos: number; height: number };
        const result = JSON.parse(
          engine.resizeRowByPos(pos, height)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rowResized", id: result.id, success: true });
        }
        break;
      }

      case "renameColumn": {
        const { colId, name } = params as { colId: string; name: string };
        const result = JSON.parse(engine.renameColumn(colId, name)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnRenamed", success: true });
        }
        break;
      }

      case "renameColumnByPos": {
        const { pos, name } = params as { pos: number; name: string };
        const result = JSON.parse(
          engine.renameColumnByPos(pos, name)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnRenamed", id: result.id, success: true });
        }
        break;
      }

      case "moveColumn": {
        const { colId, targetPos } = params as {
          colId: string;
          targetPos: number;
        };
        const result = JSON.parse(
          engine.moveColumn(colId, targetPos)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnMoved", success: true });
        }
        break;
      }

      case "moveRow": {
        const { rowId, targetPos } = params as {
          rowId: string;
          targetPos: number;
        };
        const result = JSON.parse(
          engine.moveRow(rowId, targetPos)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rowMoved", success: true });
        }
        break;
      }

      case "shiftColumnsForEmptyMove": {
        const { sourcePos, targetPos } = params as {
          sourcePos: number;
          targetPos: number;
        };
        const result = JSON.parse(
          engine.shiftColumnsForEmptyMove(sourcePos, targetPos)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnsShifted", success: true });
        }
        break;
      }

      case "shiftRowsForEmptyMove": {
        const { sourcePos, targetPos } = params as {
          sourcePos: number;
          targetPos: number;
        };
        const result = JSON.parse(
          engine.shiftRowsForEmptyMove(sourcePos, targetPos)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rowsShifted", success: true });
        }
        break;
      }

      case "insertColumnAt": {
        const { position, insertBefore } = params as {
          position: number;
          insertBefore: boolean;
        };
        const result = JSON.parse(
          engine.insertColumnAt(position, insertBefore)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({
            type: "columnInserted",
            id: result.id,
            position: result.position,
            success: true,
          });
        }
        break;
      }

      case "insertRowAt": {
        const { position, insertBefore } = params as {
          position: number;
          insertBefore: boolean;
        };
        const result = JSON.parse(
          engine.insertRowAt(position, insertBefore)
        ) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({
            type: "rowInserted",
            id: result.id,
            position: result.position,
            success: true,
          });
        }
        break;
      }

      case "deleteColumnById": {
        const { colId } = params as { colId: string };
        const result = JSON.parse(engine.deleteColumnById(colId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "columnDeleted", success: true });
        }
        break;
      }

      case "deleteRowById": {
        const { rowId } = params as { rowId: string };
        const result = JSON.parse(engine.deleteRowById(rowId)) as JsonResult;
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rowDeleted", success: true });
        }
        break;
      }

      case "fillRange": {
        const { sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow,
                targetMinCol, targetMinRow, targetMaxCol, targetMaxRow } = params as {
          sourceMinCol: number; sourceMinRow: number;
          sourceMaxCol: number; sourceMaxRow: number;
          targetMinCol: number; targetMinRow: number;
          targetMaxCol: number; targetMaxRow: number;
        };
        const result = JSON.parse(engine.fillRange(
          sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow,
          targetMinCol, targetMinRow, targetMaxCol, targetMaxRow
        )) as JsonResult & { cellsFilled?: number };
        if (result.error) {
          respond({ type: "error", error: result.error });
        } else {
          respond({ type: "rangeFilled", success: true, cellsFilled: result.cellsFilled ?? 0 });
        }
        break;
      }

      case "hasFormulas": {
        const hasFormulas = engine.hasFormulas();
        respond({ type: "hasFormulas", hasFormulas });
        break;
      }

      case "export": {
        const { format } = params as { format: string };
        let data: ArrayBuffer;
        let filename: string;
        const workbookName = engine.getWorkbookName() || "spreadsheet";

        if (format === "zcd") {
          const content = engine.exportToCells();
          if (!content) {
            respond({ type: "error", error: "Export failed" });
            return;
          }
          // Convert string to ArrayBuffer for transfer
          data = new TextEncoder().encode(content).buffer;
          filename = workbookName + ".zcd";
        } else if (format === "csv") {
          const content = engine.exportToCSV();
          if (!content) {
            respond({ type: "error", error: "Export failed" });
            return;
          }
          data = new TextEncoder().encode(content).buffer;
          filename = workbookName + ".csv";
        } else if (format === "xlsx") {
          const binaryStr = engine.exportToXLSX();
          if (!binaryStr) {
            respond({
              type: "error",
              error: "XLSX export not available or failed",
            });
            return;
          }
          // Convert binary string to ArrayBuffer
          const bytes = new Uint8Array(binaryStr.length);
          for (let i = 0; i < binaryStr.length; i++) {
            bytes[i] = binaryStr.charCodeAt(i);
          }
          data = bytes.buffer;
          filename = workbookName + ".xlsx";
        } else {
          respond({ type: "error", error: "Unknown export format: " + format });
          return;
        }

        // Transfer the ArrayBuffer for efficiency
        respond({ type: "exported", format, data, filename }, [data]);
        break;
      }

      case "createEmpty": {
        engine.createEmptyWorkbook();
        respond({ type: "created", sheetCount: 1 });
        break;
      }

      case "setWorkbookName": {
        const { name } = params as { name: string };
        engine.setWorkbookName(name);
        respond({ type: "nameSet", success: true });
        break;
      }

      case "getWorkbookName": {
        const name = engine.getWorkbookName();
        respond({ type: "workbookName", name });
        break;
      }

      // ================================================================
      // CRDT Collaboration methods
      // ================================================================

      case "setNodeId": {
        const { nodeId } = params as { nodeId: string };
        const result = engine.setNodeId(nodeId);
        respond({ type: "nodeIdSet", result });
        break;
      }

      case "getNodeId": {
        const nodeId = engine.getNodeId();
        respond({ type: "nodeId", nodeId });
        break;
      }

      case "getCurrentHLC": {
        const hlc = engine.getCurrentHLC();
        respond({ type: "currentHLC", hlc });
        break;
      }

      case "getOperationsSince": {
        const { sinceHLC } = params as { sinceHLC?: string };
        const result = engine.getOperationsSince(sinceHLC || "");
        respond({ type: "operationsSince", result });
        break;
      }

      case "applyRemoteOperation": {
        const { opJson } = params as { opJson: string };
        const result = engine.applyRemoteOperation(opJson);
        respond({ type: "operationApplied", result });
        break;
      }

      case "applyRemoteOperations": {
        const { opsJson } = params as { opsJson: string };
        const result = engine.applyRemoteOperations(opsJson);
        respond({ type: "operationsApplied", result });
        break;
      }

      case "getOpLogSize": {
        const size = engine.getOpLogSize();
        respond({ type: "opLogSize", size });
        break;
      }

      case "hasOperation": {
        const { hlc } = params as { hlc: string };
        const exists = engine.hasOperation(hlc);
        respond({ type: "hasOperation", exists });
        break;
      }

      // ================================================================
      // SyncManager methods
      // ================================================================

      case "initSyncManager": {
        const result = engine.initSyncManager();
        respond({ type: "syncManagerInitialized", result });
        break;
      }

      case "addPeer": {
        const { peerId } = params as { peerId: string };
        const result = engine.addPeer(peerId);
        respond({ type: "peerAdded", result });
        break;
      }

      case "removePeer": {
        const { peerId } = params as { peerId: string };
        const result = engine.removePeer(peerId);
        respond({ type: "peerRemoved", result });
        break;
      }

      case "getPeerIds": {
        const result = engine.getPeerIds();
        respond({ type: "peerIds", result });
        break;
      }

      case "getPeerCount": {
        const count = engine.getPeerCount();
        respond({ type: "peerCount", count });
        break;
      }

      case "handlePeerMessage": {
        const { peerId, messageJson } = params as {
          peerId: string;
          messageJson: string;
        };
        const result = engine.handlePeerMessage(peerId, messageJson);
        respond({ type: "peerMessageHandled", result });
        break;
      }

      case "getOutgoingMessages": {
        const result = engine.getOutgoingMessages();
        respond({ type: "outgoingMessages", result });
        break;
      }

      case "queueOperationsBroadcast": {
        const result = engine.queueOperationsBroadcast();
        respond({ type: "operationsQueued", result });
        break;
      }

      case "startCollaboration": {
        const result = engine.startCollaboration();
        respond({ type: "collaborationStarted", result });
        break;
      }

      // ================================================================
      // C++ SyncClient methods (P2P WebRTC sync)
      // ================================================================

      case "enableSync": {
        const { url, roomId, peerId } = params as {
          url: string;
          roomId: string;
          peerId?: string;
        };
        const result = engine.enableSync(url, roomId, peerId || "");
        respond({ type: "syncEnabled", result });
        break;
      }

      case "disableSync": {
        engine.disableSync();
        respond({ type: "syncDisabled", success: true });
        break;
      }

      case "getSyncState": {
        const result = engine.getSyncState();
        respond({ type: "syncState", result });
        break;
      }

      case "isSyncEnabled": {
        const enabled = engine.isSyncEnabled();
        respond({ type: "syncEnabled", enabled });
        break;
      }

      case "processSyncOutgoing": {
        engine.processSyncOutgoing();
        respond({ type: "syncOutgoingProcessed", success: true });
        break;
      }

      case "processSyncPresence": {
        engine.processSyncPresence();
        respond({ type: "syncPresenceProcessed", success: true });
        break;
      }

      case "broadcastSyncOperations": {
        engine.broadcastSyncOperations();
        respond({ type: "syncOperationsBroadcast", success: true });
        break;
      }

      // ================================================================
      // C++ SyncClient presence methods
      // ================================================================

      case "setSyncLocalName": {
        const { name } = params as { name: string };
        engine.setSyncLocalName(name);
        respond({ type: "syncLocalNameSet", success: true });
        break;
      }

      case "setSyncCurrentSheet": {
        const { sheetId } = params as { sheetId: string };
        engine.setSyncCurrentSheet(sheetId);
        respond({ type: "syncCurrentSheetSet", success: true });
        break;
      }

      case "setSyncCursor": {
        const { col, row } = params as { col: number; row: number };
        engine.setSyncCursor(col, row);
        respond({ type: "syncCursorSet", success: true });
        break;
      }

      case "clearSyncCursor": {
        engine.clearSyncCursor();
        respond({ type: "syncCursorCleared", success: true });
        break;
      }

      case "setSyncSelection": {
        const { startCol, startRow, endCol, endRow } = params as {
          startCol: number;
          startRow: number;
          endCol: number;
          endRow: number;
        };
        engine.setSyncSelection(startCol, startRow, endCol, endRow);
        respond({ type: "syncSelectionSet", success: true });
        break;
      }

      case "clearSyncSelection": {
        engine.clearSyncSelection();
        respond({ type: "syncSelectionCleared", success: true });
        break;
      }

      case "setSyncMousePosition": {
        const { x, y } = params as { x: number; y: number };
        engine.setSyncMousePosition(x, y);
        respond({ type: "syncMousePositionSet", success: true });
        break;
      }

      case "clearSyncMousePosition": {
        engine.clearSyncMousePosition();
        respond({ type: "syncMousePositionCleared", success: true });
        break;
      }

      case "setSyncEditing": {
        const { col, row, text } = params as {
          col: number;
          row: number;
          text: string;
        };
        engine.setSyncEditing(col, row, text);
        respond({ type: "syncEditingSet", success: true });
        break;
      }

      case "clearSyncEditing": {
        engine.clearSyncEditing();
        respond({ type: "syncEditingCleared", success: true });
        break;
      }

      case "getRemotePresences": {
        const result = engine.getRemotePresences();
        respond({ type: "remotePresences", result });
        break;
      }

      // ================================================================
      // Debug/Development methods
      // ================================================================

      case "debugParseFormula": {
        const { formulaText } = params as { formulaText: string };
        const result = engine.debugParseFormula(formulaText);
        respond({ type: "formulaParsed", result });
        break;
      }

      // ================================================================
      // Viewport pixel queries (Phase 5)
      // ================================================================

      case "getColumnPixelOffset": {
        const { position } = params as { position: number };
        const offset = engine.getColumnPixelOffset(position);
        respond({ type: "pixelOffset", offset });
        break;
      }

      case "getRowPixelOffset": {
        const { position } = params as { position: number };
        const offset = engine.getRowPixelOffset(position);
        respond({ type: "pixelOffset", offset });
        break;
      }

      case "getTotalWidth": {
        const width = engine.getTotalWidth();
        respond({ type: "totalSize", size: width });
        break;
      }

      case "getTotalHeight": {
        const height = engine.getTotalHeight();
        respond({ type: "totalSize", size: height });
        break;
      }

      // ================================================================
      // Formula API (Phase 7)
      // ================================================================

      case "validateFormula": {
        const { formulaText } = params as { formulaText: string };
        const result = engine.validateFormula(formulaText);
        respond({ type: "formulaValidated", result });
        break;
      }

      case "getFormulaDisplay": {
        const { cellId } = params as { cellId: string };
        const result = engine.getFormulaDisplay(cellId);
        respond({ type: "formulaDisplay", result });
        break;
      }

      case "getCellDependencies": {
        const { cellId } = params as { cellId: string };
        const result = engine.getCellDependencies(cellId);
        respond({ type: "cellDependencies", result });
        break;
      }

      case "getCellDependents": {
        const { cellId } = params as { cellId: string };
        const result = engine.getCellDependents(cellId);
        respond({ type: "cellDependents", result });
        break;
      }

      case "getFormulaReferences": {
        const { formulaText } = params as { formulaText: string };
        const result = engine.getFormulaReferences(formulaText);
        respond({ type: "formulaReferences", result });
        break;
      }

      case "getReferencesFromPartial": {
        const { formulaText } = params as { formulaText: string };
        const result = engine.getReferencesFromPartial(formulaText);
        respond({ type: "formulaReferences", result });
        break;
      }

      case "detectCircularRef": {
        const { cellId } = params as { cellId: string };
        const result = engine.detectCircularRef(cellId);
        respond({ type: "circularRef", result });
        break;
      }

      case "getVolatileCells": {
        const result = engine.getVolatileCells();
        respond({ type: "volatileCells", result });
        break;
      }

      // ================================================================
      // Scripting (Luau)
      // ================================================================

      case "executeScript": {
        const { script } = params as { script: string };
        const result = engine.executeScript(script);
        respond({ type: "scriptExecuted", result });
        break;
      }

      case "tokenizeLuau": {
        const { source } = params as { source: string };
        const result = engine.tokenizeLuau(source);
        respond({ type: "tokenized", result });
        break;
      }

      case "getAutocomplete": {
        const { source, line, column } = params as { source: string; line: number; column: number };
        const result = engine.getAutocomplete(source, line, column);
        respond({ type: "autocomplete", result });
        break;
      }

      // ================================================================
      // AI Agent methods
      // ================================================================

      case "initAgent": {
        const { serverUrl } = params as { serverUrl: string };
        engine.initAgent(serverUrl);
        respond({ type: "agentInitialized", success: true });
        break;
      }

      case "isAgentInitialized": {
        const initialized = engine.isAgentInitialized();
        respond({ type: "agentStatus", initialized });
        break;
      }

      case "sendAgentMessage": {
        const { prompt, conversationId } = params as { prompt: string; conversationId: string };
        // Use JavaScript streaming fetch for reliable SSE
        const serverUrl = engine.getAgentServerUrl();
        if (serverUrl == null) {
          respond({ type: "error", error: "Agent server URL not set" });
          break;
        }

        engine.setAgentStreaming(true);
        respond({ type: "agentMessageSent", success: true });

        // Perform streaming fetch
        streamAgentMessage(serverUrl + "/api/agent/message", {
          prompt,
          conversation_id: conversationId || undefined,
        });
        break;
      }

      case "sendAgentToolResult": {
        const { conversationId: convId, toolUseId, result, isError } = params as {
          conversationId: string;
          toolUseId: string;
          result: string;
          isError: boolean;
        };
        const serverUrl = engine.getAgentServerUrl();
        if (serverUrl == null) {
          respond({ type: "error", error: "Agent server URL not set" });
          break;
        }

        engine.setAgentStreaming(true);
        respond({ type: "agentToolResultSent", success: true });

        // Perform streaming fetch for tool result
        streamAgentMessage(serverUrl + "/api/agent/tool-result", {
          conversation_id: convId,
          tool_use_id: toolUseId,
          result,
          is_error: isError,
        });
        break;
      }

      case "getAgentConversationId": {
        const conversationId = engine.getAgentConversationId();
        respond({ type: "agentConversationId", conversationId });
        break;
      }

      case "clearAgentConversation": {
        engine.clearAgentConversation();
        respond({ type: "agentConversationCleared", success: true });
        break;
      }

      case "cancelAgent": {
        engine.cancelAgent();
        respond({ type: "agentCancelled", success: true });
        break;
      }

      case "isAgentProcessing": {
        const processing = engine.isAgentProcessing();
        respond({ type: "agentProcessing", processing });
        break;
      }

      default:
        respond({ type: "error", error: "Unknown message type: " + type });
    }
  } catch (err) {
    const error = err instanceof Error ? err.message : String(err);
    respond({ type: "error", error });
  }
}

// ============================================================================
// Event Listeners
// ============================================================================

// Listen for messages from main thread
workerSelf.onmessage = function (e: MessageEvent<WorkerRequest>): void {
  if (!isReady) {
    pendingMessages.push(e.data);
    return;
  }
  handleMessage(e.data);
};

// Start initialization
initModule();
