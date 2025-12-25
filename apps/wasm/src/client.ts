// Cells WASM Client - Main thread API for communicating with the WASM worker
// Provides a Promise-based API that matches the REST server semantics

import { RTCProxy, type RTCMessagePayload } from "./rtc-proxy";
import type {
  FileFormat,
  CellData,
  ColumnInfo,
  RowInfo,
  SheetInfo,
  PeerPresence,
} from "./types";

// ============================================================================
// Re-exports for index.html inline script
// ============================================================================
export {
  GridRenderer,
  HEADER_HEIGHT,
  HEADER_WIDTH,
  DEFAULT_COL_WIDTH,
  DEFAULT_ROW_HEIGHT,
  COLORS,
} from "./grid-renderer";
export {
  detectFormat,
  getBaseName,
  downloadBlob,
  getMimeType,
} from "./utils";
export { createUIStateMachine, UIState, UIEvent } from "./ui-state";
export { CollabUI } from "./collab-ui";
export {
  clearRoomIdFromUrl,
  getRoomIdFromUrl,
  RoomManager,
} from "./room-url";
export { CppSyncAdapter } from "./cpp-sync-adapter";
export { WasmDataSource } from "./wasm-data-source";
export {
  getColAtX,
  getRowAtY,
  getResizeHandleCol,
  getResizeHandleRow,
  getDropTargetCol,
  getDropTargetRow,
  getColumnId,
  getRowId,
  getCellAt,
  colToLetter,
  getNormalizedRange,
  hasRangeSelection,
  formatCellReference,
  RESIZE_HANDLE_WIDTH,
  DRAG_THRESHOLD,
} from "./grid-utils";
export { App, createApp } from "./app";
export { initApp, type AppContext } from "./init";

// ============================================================================
// Worker Message Types
// ============================================================================

/** Base worker message with correlation ID */
interface WorkerMessageBase {
  id?: number;
  type: string;
}

/** Generic worker response with any additional properties */
type WorkerMessage = WorkerMessageBase & Record<string, unknown>;

/** Pending request entry */
interface PendingRequest {
  resolve: (value: WorkerMessage) => void;
  reject: (error: Error) => void;
}

// ============================================================================
// Response Types
// ============================================================================

/** Load file result */
interface LoadFileResult {
  sheetCount: number;
  sheetNames: string[];
}

/** Get sheets result */
interface GetSheetsResult {
  sheets: Array<{ index: number; name: string; active: boolean }>;
  activeIndex: number;
}

/** Add sheet result */
interface AddSheetResult {
  index: number;
  name: string;
}

/** Delete sheet result */
interface DeleteSheetResult {
  activeIndex: number;
}

/** Move sheet result */
interface MoveSheetResult {
  activeIndex: number;
}

/** Viewport query result */
interface ViewportResult {
  cells: CellData[];
  columns: ColumnInfo[];
  rows: RowInfo[];
}

/** Create cell result */
interface CreateCellResult {
  id: string;
}

/** Get or create cell result */
interface GetOrCreateCellResult {
  id: string;
  existed: boolean;
  value: string;
  formula: string | null;
}

/** Delete cell at position result */
interface DeleteCellAtResult {
  deleted: boolean;
}

/** Resize by position result */
interface ResizeByPosResult {
  id: string;
  success: boolean;
}

/** Rename column by position result */
interface RenameColumnByPosResult {
  success: boolean;
  id: string;
}

/** Export result */
interface ExportResult {
  data: ArrayBuffer;
  filename: string;
}

/** Enable sync result */
interface EnableSyncResult {
  success: boolean;
  peerId: string;
}

/** Sync state result */
interface SyncStateResult {
  state: string;
  peerId: string;
  roomId: string;
  peerCount: number;
  peers: Array<{ id: string; latency: number | null }>;
}

/** Remote presences result */
interface RemotePresencesResult {
  peers: Record<string, PeerPresence>;
}

/** Formula parse result for debug AST visualization */
export interface FormulaParseResult {
  formula: string;
  errors: string[];
  ast: ASTNode | null;
}

/** AST node type - matches C++ AST node types */
export interface ASTNode {
  type: string;
  [key: string]: unknown;
}

// ============================================================================
// CellsClient Class
// ============================================================================

/**
 * CellsClient - Communicates with the WASM worker
 *
 * Usage:
 *   const client = new CellsClient('/path/to/worker.js');
 *   await client.ready;
 *   await client.loadFile(arrayBuffer, 'xlsx');
 *   const viewport = await client.queryViewport(0, 0, 10, 20);
 */
export class CellsClient {
  private _worker: Worker;
  private _requestId: number;
  private _pending: Map<number, PendingRequest>;
  private _isReady: boolean;
  private _readyPromise: Promise<void>;
  private _readyResolve: (() => void) | null;
  private _onDataChanged:
    | ((changeType: "cell" | "structure" | "sheet" | "loaded") => void)
    | null;
  private _rtcProxy: RTCProxy | null;

  constructor(workerPath: string = "./worker.js") {
    this._worker = new Worker(workerPath);
    this._requestId = 0;
    this._pending = new Map();
    this._isReady = false;
    this._readyResolve = null;
    this._onDataChanged = null;
    this._rtcProxy = null;

    // Initialize RTC proxy
    this._initRTCProxy();

    // Create ready promise
    this._readyPromise = new Promise<void>((resolve, reject) => {
      this._readyResolve = resolve;
      // Timeout after 30 seconds
      setTimeout(() => {
        if (!this._isReady) {
          reject(new Error("Worker initialization timeout"));
        }
      }, 30000);
    });

    // Handle messages from worker
    this._worker.onmessage = (e: MessageEvent<WorkerMessage>) =>
      this._handleMessage(e.data);
    this._worker.onerror = (e: ErrorEvent) => this._handleError(e);
  }

  private async _initRTCProxy(): Promise<void> {
    try {
      this._rtcProxy = new RTCProxy();
      this._rtcProxy.setSendCallback(
        (type: string, payload: Record<string, unknown>) => {
          // Send RTC events back to the worker
          // Note: Put type last to avoid payload.type (e.g., SDP type) overwriting message type
          this._worker.postMessage({ ...payload, type });
        }
      );
    } catch (e) {
      console.warn(
        "Failed to load RTCProxy, WebRTC functionality will be disabled:",
        e
      );
    }
  }

  /**
   * Promise that resolves when the worker is ready
   */
  get ready(): Promise<void> {
    return this._readyPromise;
  }

  /**
   * Whether the worker is ready to receive messages
   */
  get isReady(): boolean {
    return this._isReady;
  }

  private _handleMessage(msg: WorkerMessage): void {
    // Handle ready message
    if (msg.type === "ready") {
      this._isReady = true;
      if (this._readyResolve) {
        this._readyResolve();
        this._readyResolve = null;
      }
      return;
    }

    // Handle unsolicited data change notifications from WASM
    if (msg.type === "dataChanged") {
      if (this._onDataChanged) {
        const changeType = msg.changeType as
          | "cell"
          | "structure"
          | "sheet"
          | "loaded";
        this._onDataChanged(changeType);
      }
      return;
    }

    // Handle RTC messages from worker - forward to main thread RTC proxy
    if (msg.type && (msg.type.startsWith("rtc_") || msg.type.startsWith("dc_"))) {
      if (!this._rtcProxy) {
        console.warn("RTCProxy not yet initialized, message dropped:", msg.type);
        return;
      }
      const { type, id, ...payload } = msg;
      const result = this._rtcProxy.handleMessage(
        type,
        payload as RTCMessagePayload
      );

      // If the message had an ID, it expects a response
      if (id !== undefined) {
        this._worker.postMessage({ id, type: "rtc_response", ...result });
      }
      return;
    }

    // Handle response to a request
    const { id } = msg;
    if (id !== undefined && this._pending.has(id as number)) {
      const pending = this._pending.get(id as number)!;
      this._pending.delete(id as number);

      if (msg.type === "error") {
        pending.reject(new Error(msg.error as string));
      } else {
        pending.resolve(msg);
      }
    }
  }

  private _handleError(e: ErrorEvent): void {
    console.error("Worker error:", e);
    // Reject all pending requests
    for (const [, pending] of this._pending) {
      pending.reject(new Error("Worker error: " + e.message));
    }
    this._pending.clear();
  }

  /**
   * Send a message to the worker and wait for response
   */
  private _send(
    type: string,
    params: Record<string, unknown> = {},
    transfer: Transferable[] = []
  ): Promise<WorkerMessage> {
    return new Promise((resolve, reject) => {
      const id = this._requestId++;
      this._pending.set(id, { resolve, reject });
      this._worker.postMessage({ id, type, ...params }, transfer);
    });
  }

  /**
   * Terminate the worker
   */
  terminate(): void {
    // Clean up RTC proxy
    if (this._rtcProxy) {
      this._rtcProxy.destroy();
    }

    this._worker.terminate();
    // Reject all pending requests
    for (const [, pending] of this._pending) {
      pending.reject(new Error("Worker terminated"));
    }
    this._pending.clear();
  }

  // ========================================================================
  // Change Notification API
  // ========================================================================

  /**
   * Set a callback to be called when data changes in the engine
   * The callback receives a change type: 'cell', 'structure', 'sheet', or 'loaded'
   */
  setOnDataChanged(
    callback: (changeType: "cell" | "structure" | "sheet" | "loaded") => void
  ): void {
    this._onDataChanged = callback;
  }

  /**
   * Remove the data change callback
   */
  removeOnDataChanged(): void {
    this._onDataChanged = null;
  }

  // ========================================================================
  // File Loading API
  // ========================================================================

  /**
   * Load a file into the engine
   */
  async loadFile(
    data: ArrayBuffer | string,
    format: FileFormat,
    options: { delimiter?: string; hasHeader?: boolean } = {}
  ): Promise<LoadFileResult> {
    const params: Record<string, unknown> = { format, data, ...options };

    // For CSV, convert delimiter to string if provided
    if (format === "csv" && options.delimiter) {
      params.delimiter = options.delimiter;
    }

    // Transfer ArrayBuffer if possible
    const transfer = data instanceof ArrayBuffer ? [data] : [];
    const response = await this._send("load", params, transfer);

    return {
      sheetCount: response.sheetCount as number,
      sheetNames: response.sheetNames as string[],
    };
  }

  /**
   * Load a .zcd file from string content
   */
  async loadCells(content: string): Promise<LoadFileResult> {
    return this.loadFile(content, "zcd");
  }

  /**
   * Load a CSV file
   */
  async loadCSV(
    data: ArrayBuffer | string,
    options: { delimiter?: string; hasHeader?: boolean } = {}
  ): Promise<LoadFileResult> {
    return this.loadFile(data, "csv", options);
  }

  /**
   * Load an XLSX file
   */
  async loadXLSX(data: ArrayBuffer): Promise<LoadFileResult> {
    return this.loadFile(data, "xlsx");
  }

  /**
   * Create an empty workbook
   */
  async createEmpty(): Promise<{ sheetCount: number }> {
    const response = await this._send("createEmpty");
    return { sheetCount: response.sheetCount as number };
  }

  // ========================================================================
  // Sheet Info API
  // ========================================================================

  /**
   * Get information about the active sheet
   */
  async getSheetInfo(): Promise<SheetInfo> {
    const response = await this._send("getSheetInfo");
    return {
      name: response.name as string,
      rowCount: response.rowCount as number,
      colCount: response.colCount as number,
      defaultColWidth: response.defaultColWidth as number,
      defaultRowHeight: response.defaultRowHeight as number,
    };
  }

  /**
   * Set the active sheet
   */
  async setActiveSheet(index: number): Promise<void> {
    await this._send("setActiveSheet", { index });
  }

  /**
   * Get list of all sheets in the workbook
   */
  async getSheets(): Promise<GetSheetsResult> {
    const response = await this._send("getSheets");
    return {
      sheets: (response.sheets as GetSheetsResult["sheets"]) || [],
      activeIndex: response.activeIndex as number,
    };
  }

  /**
   * Add a new sheet to the workbook
   */
  async addSheet(name: string = ""): Promise<AddSheetResult> {
    const response = await this._send("addSheet", { name });
    return {
      index: response.index as number,
      name: response.name as string,
    };
  }

  /**
   * Delete a sheet by index
   */
  async deleteSheet(index: number): Promise<DeleteSheetResult> {
    const response = await this._send("deleteSheet", { index });
    return {
      activeIndex: response.activeIndex as number,
    };
  }

  /**
   * Rename a sheet
   */
  async renameSheet(index: number, name: string): Promise<{ success: boolean }> {
    await this._send("renameSheet", { index, name });
    return { success: true };
  }

  /**
   * Move a sheet to a new position
   */
  async moveSheet(fromIndex: number, toIndex: number): Promise<MoveSheetResult> {
    const response = await this._send("moveSheet", { fromIndex, toIndex });
    return {
      activeIndex: response.activeIndex as number,
    };
  }

  // ========================================================================
  // Viewport API
  // ========================================================================

  /**
   * Query cells in a viewport range
   */
  async queryViewport(
    x1: number,
    y1: number,
    x2: number,
    y2: number
  ): Promise<ViewportResult> {
    const response = await this._send("queryViewport", { x1, y1, x2, y2 });
    return {
      cells: (response.cells as CellData[]) || [],
      columns: (response.columns as ColumnInfo[]) || [],
      rows: (response.rows as RowInfo[]) || [],
    };
  }

  // ========================================================================
  // Cell Operations API
  // ========================================================================

  /**
   * Update a cell's value
   */
  async updateCell(cellId: string, value: string): Promise<{ success: boolean }> {
    await this._send("updateCell", { cellId, value });
    return { success: true };
  }

  /**
   * Create a new cell at a position
   */
  async createCell(
    col: number,
    row: number,
    value: string = ""
  ): Promise<CreateCellResult> {
    const response = await this._send("createCell", { col, row, value });
    return { id: response.cellId as string };
  }

  /**
   * Get or create a cell at the given position.
   * This is the primary API for editing - single call avoids multiple round trips:
   * - Returns existing cell ID and value if cell already exists
   * - Creates column/row/cell as needed, with operations committed immediately
   * - For new cells, returns empty value
   */
  async getOrCreateCellAt(col: number, row: number): Promise<GetOrCreateCellResult> {
    const response = await this._send("getOrCreateCellAt", { col, row });
    return {
      id: response.cellId as string,
      existed: response.existed as boolean,
      value: response.value as string,
      formula: (response.formula as string) || null,
    };
  }

  /**
   * Delete a cell by ID
   */
  async deleteCell(cellId: string): Promise<{ success: boolean }> {
    await this._send("deleteCell", { cellId });
    return { success: true };
  }

  /**
   * Delete a cell at the given position if it exists.
   * Does nothing if no cell exists at that position.
   */
  async deleteCellAt(col: number, row: number): Promise<DeleteCellAtResult> {
    const response = await this._send("deleteCellAt", { col, row });
    return { deleted: response.deleted as boolean };
  }

  // ========================================================================
  // Column/Row Operations API
  // ========================================================================

  /**
   * Resize a column by ID
   */
  async resizeColumn(colId: string, width: number): Promise<{ success: boolean }> {
    await this._send("resizeColumn", { colId, width });
    return { success: true };
  }

  /**
   * Resize a column by position (creates column if needed)
   */
  async resizeColumnByPos(pos: number, width: number): Promise<ResizeByPosResult> {
    const response = await this._send("resizeColumnByPos", { pos, width });
    return { id: String(response.id), success: true };
  }

  /**
   * Resize a row by ID
   */
  async resizeRow(rowId: string, height: number): Promise<{ success: boolean }> {
    await this._send("resizeRow", { rowId, height });
    return { success: true };
  }

  /**
   * Resize a row by position (creates row if needed)
   */
  async resizeRowByPos(pos: number, height: number): Promise<ResizeByPosResult> {
    const response = await this._send("resizeRowByPos", { pos, height });
    return { id: String(response.id), success: true };
  }

  /**
   * Move a column to a new position
   */
  async moveColumn(colId: string, targetPos: number): Promise<{ success: boolean }> {
    await this._send("moveColumn", { colId, targetPos });
    return { success: true };
  }

  /**
   * Move a row to a new position
   */
  async moveRow(rowId: string, targetPos: number): Promise<{ success: boolean }> {
    await this._send("moveRow", { rowId, targetPos });
    return { success: true };
  }

  /**
   * Shift columns when moving an empty column position
   */
  async shiftColumnsForEmptyMove(
    sourcePos: number,
    targetPos: number
  ): Promise<{ success: boolean }> {
    await this._send("shiftColumnsForEmptyMove", { sourcePos, targetPos });
    return { success: true };
  }

  /**
   * Shift rows when moving an empty row position
   */
  async shiftRowsForEmptyMove(
    sourcePos: number,
    targetPos: number
  ): Promise<{ success: boolean }> {
    await this._send("shiftRowsForEmptyMove", { sourcePos, targetPos });
    return { success: true };
  }

  /**
   * Rename a column
   */
  async renameColumn(colId: string, name: string): Promise<{ success: boolean }> {
    await this._send("renameColumn", { colId, name });
    return { success: true };
  }

  /**
   * Rename a column by position (creates column if it doesn't exist)
   */
  async renameColumnByPos(pos: number, name: string): Promise<RenameColumnByPosResult> {
    const result = await this._send("renameColumnByPos", { pos, name });
    return { success: true, id: String(result.id) };
  }

  // ========================================================================
  // Export API
  // ========================================================================

  /**
   * Export the workbook to a format
   */
  async exportAs(format: FileFormat): Promise<ExportResult> {
    const response = await this._send("export", { format });
    return {
      data: response.data as ArrayBuffer,
      filename: response.filename as string,
    };
  }

  /**
   * Export to .zcd format
   */
  async exportCells(): Promise<ExportResult> {
    return this.exportAs("zcd");
  }

  /**
   * Export to CSV format
   */
  async exportCSV(): Promise<ExportResult> {
    return this.exportAs("csv");
  }

  /**
   * Export to XLSX format
   */
  async exportXLSX(): Promise<ExportResult> {
    return this.exportAs("xlsx");
  }

  // ========================================================================
  // Workbook Management API
  // ========================================================================

  /**
   * Get the workbook name
   */
  async getWorkbookName(): Promise<string> {
    const response = await this._send("getWorkbookName");
    return response.name as string;
  }

  /**
   * Set the workbook name
   */
  async setWorkbookName(name: string): Promise<void> {
    await this._send("setWorkbookName", { name });
  }

  // ========================================================================
  // CRDT Collaboration API
  // ========================================================================

  /**
   * Set the node ID for HLC generation
   */
  async setNodeId(nodeId: string): Promise<string> {
    const response = await this._send("setNodeId", { nodeId });
    return response.result as string;
  }

  /**
   * Get the current node ID
   */
  async getNodeId(): Promise<string> {
    const response = await this._send("getNodeId");
    return response.nodeId as string;
  }

  /**
   * Get the current HLC timestamp
   */
  async getCurrentHLC(): Promise<string> {
    const response = await this._send("getCurrentHLC");
    return response.hlc as string;
  }

  /**
   * Get operations since a given HLC
   */
  async getOperationsSince(sinceHLC: string = ""): Promise<string> {
    const response = await this._send("getOperationsSince", { sinceHLC });
    return response.result as string;
  }

  /**
   * Apply a remote CRDT operation
   */
  async applyRemoteOperation(opJson: string): Promise<string> {
    const response = await this._send("applyRemoteOperation", { opJson });
    return response.result as string;
  }

  /**
   * Apply multiple remote CRDT operations
   */
  async applyRemoteOperations(opsJson: string): Promise<string> {
    const response = await this._send("applyRemoteOperations", { opsJson });
    return response.result as string;
  }

  /**
   * Get the number of operations in the OpLog
   */
  async getOpLogSize(): Promise<number> {
    const response = await this._send("getOpLogSize");
    return response.size as number;
  }

  /**
   * Check if an operation with the given HLC exists
   */
  async hasOperation(hlc: string): Promise<boolean> {
    const response = await this._send("hasOperation", { hlc });
    return response.exists as boolean;
  }

  // ========================================================================
  // SyncManager API
  // ========================================================================

  /**
   * Initialize the SyncManager
   * Must be called after setNodeId
   */
  async initSyncManager(): Promise<string> {
    const response = await this._send("initSyncManager");
    return response.result as string;
  }

  /**
   * Add a peer to the SyncManager
   */
  async addPeer(peerId: string): Promise<string> {
    const response = await this._send("addPeer", { peerId });
    return response.result as string;
  }

  /**
   * Remove a peer from the SyncManager
   */
  async removePeer(peerId: string): Promise<string> {
    const response = await this._send("removePeer", { peerId });
    return response.result as string;
  }

  /**
   * Get all peer IDs
   */
  async getPeerIds(): Promise<string> {
    const response = await this._send("getPeerIds");
    return response.result as string;
  }

  /**
   * Get the number of connected peers
   */
  async getPeerCount(): Promise<number> {
    const response = await this._send("getPeerCount");
    return response.count as number;
  }

  /**
   * Handle a message from a peer
   */
  async handlePeerMessage(peerId: string, messageJson: string): Promise<string> {
    const response = await this._send("handlePeerMessage", { peerId, messageJson });
    return response.result as string;
  }

  /**
   * Get outgoing messages to send to peers
   */
  async getOutgoingMessages(): Promise<string> {
    const response = await this._send("getOutgoingMessages");
    return response.result as string;
  }

  /**
   * Queue local operations for broadcast to all peers
   */
  async queueOperationsBroadcast(): Promise<string> {
    const response = await this._send("queueOperationsBroadcast");
    return response.result as string;
  }

  /**
   * Start collaboration mode
   * Switches to COLLABORATING mode and bootstraps OpLog with current workbook state.
   * Call this when user clicks "Share" or joins a room.
   */
  async startCollaboration(): Promise<string> {
    const response = await this._send("startCollaboration");
    return response.result as string;
  }

  // ========================================================================
  // C++ SyncClient API (P2P WebRTC sync)
  // ========================================================================

  /**
   * Enable sync - connects to signaling server and joins a room via C++ SyncClient.
   * This uses the full C++ implementation for WebRTC P2P sync.
   */
  async enableSync(
    url: string,
    roomId: string,
    peerId: string = ""
  ): Promise<EnableSyncResult> {
    const response = await this._send("enableSync", { url, roomId, peerId });
    return JSON.parse(response.result as string) as EnableSyncResult;
  }

  /**
   * Disable sync - disconnects from peers and signaling server.
   */
  async disableSync(): Promise<void> {
    await this._send("disableSync");
  }

  /**
   * Get current sync state.
   */
  async getSyncState(): Promise<SyncStateResult> {
    const response = await this._send("getSyncState");
    return JSON.parse(response.result as string) as SyncStateResult;
  }

  /**
   * Check if sync is currently enabled/connected.
   */
  async isSyncEnabled(): Promise<boolean> {
    const response = await this._send("isSyncEnabled");
    return response.enabled as boolean;
  }

  /**
   * Process outgoing sync messages - call periodically (e.g., in requestAnimationFrame).
   */
  async processSyncOutgoing(): Promise<void> {
    await this._send("processSyncOutgoing");
  }

  /**
   * Process pending presence updates - call periodically.
   */
  async processSyncPresence(): Promise<void> {
    await this._send("processSyncPresence");
  }

  /**
   * Broadcast local operations to peers - call after local edits.
   */
  async broadcastSyncOperations(): Promise<void> {
    await this._send("broadcastSyncOperations");
  }

  // ========================================================================
  // C++ SyncClient Presence API
  // ========================================================================

  /**
   * Set local user's display name (shown to other peers).
   */
  async setSyncLocalName(name: string): Promise<void> {
    await this._send("setSyncLocalName", { name });
  }

  /**
   * Set current sheet (for multi-sheet presence tracking).
   */
  async setSyncCurrentSheet(sheetId: string): Promise<void> {
    await this._send("setSyncCurrentSheet", { sheetId });
  }

  /**
   * Set cursor position (cell the user is editing).
   */
  async setSyncCursor(col: number, row: number): Promise<void> {
    await this._send("setSyncCursor", { col, row });
  }

  /**
   * Clear cursor position.
   */
  async clearSyncCursor(): Promise<void> {
    await this._send("clearSyncCursor");
  }

  /**
   * Set selection range.
   */
  async setSyncSelection(
    startCol: number,
    startRow: number,
    endCol: number,
    endRow: number
  ): Promise<void> {
    await this._send("setSyncSelection", { startCol, startRow, endCol, endRow });
  }

  /**
   * Clear selection.
   */
  async clearSyncSelection(): Promise<void> {
    await this._send("clearSyncSelection");
  }

  /**
   * Set mouse position for collaboration cursor.
   */
  async setSyncMousePosition(x: number, y: number): Promise<void> {
    await this._send("setSyncMousePosition", { x, y });
  }

  /**
   * Clear mouse position.
   */
  async clearSyncMousePosition(): Promise<void> {
    await this._send("clearSyncMousePosition");
  }

  /**
   * Set editing state (ephemeral, shows what user is typing).
   */
  async setSyncEditing(col: number, row: number, text: string): Promise<void> {
    await this._send("setSyncEditing", { col, row, text });
  }

  /**
   * Clear editing state.
   */
  async clearSyncEditing(): Promise<void> {
    await this._send("clearSyncEditing");
  }

  /**
   * Get remote peers' presence data.
   */
  async getRemotePresences(): Promise<RemotePresencesResult> {
    const response = await this._send("getRemotePresences");
    return JSON.parse(response.result as string) as RemotePresencesResult;
  }

  // ========================================================================
  // Debug/Development API
  // ========================================================================

  /**
   * Parse a formula and return its AST as JSON.
   * This is a debug function for visualizing the parse tree.
   * Does not modify any state or require a workbook to be loaded.
   */
  async debugParseFormula(formulaText: string): Promise<FormulaParseResult> {
    const response = await this._send("debugParseFormula", { formulaText });
    return JSON.parse(response.result as string) as FormulaParseResult;
  }
}
