// Cells WASM Client - Main thread API for communicating with the WASM worker
// Provides a Promise-based API that matches the REST server semantics

import { RTCProxy, type RTCMessagePayload } from "./rtc-proxy";
import type { FileFormat, SheetInfo } from "./types";
import type {
  WorkerMessage,
  PendingRequest,
  LoadFileResult,
  GetSheetsResult,
  AddSheetResult,
  DeleteSheetResult,
  MoveSheetResult,
  ViewportResult,
  CreateCellResult,
  GetOrCreateCellResult,
  DeleteCellAtResult,
  ResizeByPosResult,
  RenameColumnByPosResult,
  ExportResult,
  EnableSyncResult,
  SyncStateResult,
  RemotePresencesResult,
  FormulaParseResult,
  FormulaReferencesResult,
  ValidateFormulaResult,
  CircularRefResult,
  VolatileCellsResult,
  CellDependenciesResult,
  CellDependentsResult,
  ScriptResult,
  LuauToken,
  AutocompleteResult,
} from "./client-types";

// Re-export types for external consumers
export type {
  FormulaParseResult,
  ASTNode,
  FormulaReferencesResult,
  ValidateFormulaResult,
  CircularRefResult,
  VolatileCellsResult,
  CellDependenciesResult,
  CellDependentsResult,
  ReferenceInfo,
  RefType,
  ScriptResult,
  LuauToken,
  LuauTokenType,
  AutocompleteResult,
  AutocompleteSuggestion,
  AutocompleteSuggestionKind,
  AutocompleteContext,
} from "./client-types";

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
  private _onDataChanged: ((changeType: "cell" | "structure" | "sheet" | "loaded") => void) | null;
  private _onLoadProgress: ((cellsLoaded: number, totalEstimate: number) => void) | null;
  private _rtcProxy: RTCProxy | null;

  constructor(workerPath: string = "./worker.js") {
    this._worker = new Worker(workerPath);
    this._requestId = 0;
    this._pending = new Map();
    this._isReady = false;
    this._readyResolve = null;
    this._onDataChanged = null;
    this._onLoadProgress = null;
    this._rtcProxy = null;

    this._initRTCProxy();

    this._readyPromise = new Promise<void>((resolve, reject) => {
      this._readyResolve = resolve;
      setTimeout(() => {
        if (!this._isReady) reject(new Error("Worker initialization timeout"));
      }, 30000);
    });

    this._worker.onmessage = (e: MessageEvent<WorkerMessage>) => this._handleMessage(e.data);
    this._worker.onerror = (e: ErrorEvent) => this._handleError(e);
  }

  private async _initRTCProxy(): Promise<void> {
    try {
      this._rtcProxy = new RTCProxy();
      this._rtcProxy.setSendCallback((type: string, payload: Record<string, unknown>) => {
        this._worker.postMessage({ ...payload, type });
      });
    } catch (e) {
      console.warn("Failed to load RTCProxy, WebRTC functionality will be disabled:", e);
    }
  }

  /** Promise that resolves when the worker is ready */
  get ready(): Promise<void> {
    return this._readyPromise;
  }

  /** Whether the worker is ready to receive messages */
  get isReady(): boolean {
    return this._isReady;
  }

  private _handleMessage(msg: WorkerMessage): void {
    if (msg.type === "ready") {
      this._isReady = true;
      if (this._readyResolve) {
        this._readyResolve();
        this._readyResolve = null;
      }
      return;
    }

    if (msg.type === "dataChanged") {
      if (this._onDataChanged) {
        this._onDataChanged(msg.changeType as "cell" | "structure" | "sheet" | "loaded");
      }
      return;
    }

    if (msg.type === "loadProgress") {
      if (this._onLoadProgress) {
        this._onLoadProgress(msg.cellsLoaded as number, msg.totalEstimate as number);
      }
      return;
    }

    if (msg.type && (msg.type.startsWith("rtc_") || msg.type.startsWith("dc_"))) {
      if (!this._rtcProxy) {
        console.warn("RTCProxy not yet initialized, message dropped:", msg.type);
        return;
      }
      const { type, id, ...payload } = msg;
      const result = this._rtcProxy.handleMessage(type, payload as RTCMessagePayload);
      if (id !== undefined) {
        this._worker.postMessage({ id, type: "rtc_response", ...result });
      }
      return;
    }

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
    for (const [, pending] of this._pending) {
      pending.reject(new Error("Worker error: " + e.message));
    }
    this._pending.clear();
  }

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

  /** Terminate the worker */
  terminate(): void {
    if (this._rtcProxy) this._rtcProxy.destroy();
    this._worker.terminate();
    for (const [, pending] of this._pending) {
      pending.reject(new Error("Worker terminated"));
    }
    this._pending.clear();
  }

  // ========== Change Notification API ==========

  setOnDataChanged(callback: (changeType: "cell" | "structure" | "sheet" | "loaded") => void): void {
    this._onDataChanged = callback;
  }

  removeOnDataChanged(): void {
    this._onDataChanged = null;
  }

  setOnLoadProgress(callback: (cellsLoaded: number, totalEstimate: number) => void): void {
    this._onLoadProgress = callback;
  }

  removeOnLoadProgress(): void {
    this._onLoadProgress = null;
  }

  // ========== File Loading API ==========

  async loadFile(
    data: ArrayBuffer | string,
    format: FileFormat,
    options: { delimiter?: string; hasHeader?: boolean } = {}
  ): Promise<LoadFileResult> {
    const params: Record<string, unknown> = { format, data, ...options };
    if (format === "csv" && options.delimiter) params.delimiter = options.delimiter;
    const transfer = data instanceof ArrayBuffer ? [data] : [];
    const response = await this._send("load", params, transfer);
    return { sheetCount: response.sheetCount as number, sheetNames: response.sheetNames as string[] };
  }

  async loadCells(content: string): Promise<LoadFileResult> {
    return this.loadFile(content, "zcd");
  }

  async loadCSV(data: ArrayBuffer | string, options: { delimiter?: string; hasHeader?: boolean } = {}): Promise<LoadFileResult> {
    return this.loadFile(data, "csv", options);
  }

  async loadXLSX(data: ArrayBuffer): Promise<LoadFileResult> {
    return this.loadFile(data, "xlsx");
  }

  async createEmpty(): Promise<{ sheetCount: number }> {
    const response = await this._send("createEmpty");
    return { sheetCount: response.sheetCount as number };
  }

  // ========== Sheet Info API ==========

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

  async setActiveSheet(index: number): Promise<void> {
    await this._send("setActiveSheet", { index });
  }

  async getSheets(): Promise<GetSheetsResult> {
    const response = await this._send("getSheets");
    return { sheets: (response.sheets as GetSheetsResult["sheets"]) || [], activeIndex: response.activeIndex as number };
  }

  async addSheet(name: string = ""): Promise<AddSheetResult> {
    const response = await this._send("addSheet", { name });
    return { index: response.index as number, name: response.name as string };
  }

  async deleteSheet(index: number): Promise<DeleteSheetResult> {
    const response = await this._send("deleteSheet", { index });
    return { activeIndex: response.activeIndex as number };
  }

  async renameSheet(index: number, name: string): Promise<{ success: boolean }> {
    await this._send("renameSheet", { index, name });
    return { success: true };
  }

  async moveSheet(fromIndex: number, toIndex: number): Promise<MoveSheetResult> {
    const response = await this._send("moveSheet", { fromIndex, toIndex });
    return { activeIndex: response.activeIndex as number };
  }

  // ========== Viewport API ==========

  async queryViewport(x1: number, y1: number, x2: number, y2: number): Promise<ViewportResult> {
    const response = await this._send("queryViewport", { x1, y1, x2, y2 });
    return {
      cells: (response.cells as ViewportResult["cells"]) || [],
      columns: (response.columns as ViewportResult["columns"]) || [],
      rows: (response.rows as ViewportResult["rows"]) || [],
    };
  }

  // ========== Cell Operations API ==========

  async updateCell(cellId: string, value: string): Promise<{ success: boolean }> {
    await this._send("updateCell", { cellId, value });
    return { success: true };
  }

  async createCell(col: number, row: number, value: string = ""): Promise<CreateCellResult> {
    const response = await this._send("createCell", { col, row, value });
    return { id: response.cellId as string };
  }

  async getOrCreateCellAt(col: number, row: number): Promise<GetOrCreateCellResult> {
    const response = await this._send("getOrCreateCellAt", { col, row });
    return {
      id: response.cellId as string,
      existed: response.existed as boolean,
      value: response.value as string,
      formula: (response.formula as string) || null,
    };
  }

  async deleteCell(cellId: string): Promise<{ success: boolean }> {
    await this._send("deleteCell", { cellId });
    return { success: true };
  }

  async deleteCellAt(col: number, row: number): Promise<DeleteCellAtResult> {
    const response = await this._send("deleteCellAt", { col, row });
    return { deleted: response.deleted as boolean };
  }

  // ========== Column/Row Operations API ==========

  async resizeColumn(colId: string, width: number): Promise<{ success: boolean }> {
    await this._send("resizeColumn", { colId, width });
    return { success: true };
  }

  async resizeColumnByPos(pos: number, width: number): Promise<ResizeByPosResult> {
    const response = await this._send("resizeColumnByPos", { pos, width });
    return { id: String(response.id), success: true };
  }

  async resizeRow(rowId: string, height: number): Promise<{ success: boolean }> {
    await this._send("resizeRow", { rowId, height });
    return { success: true };
  }

  async resizeRowByPos(pos: number, height: number): Promise<ResizeByPosResult> {
    const response = await this._send("resizeRowByPos", { pos, height });
    return { id: String(response.id), success: true };
  }

  async moveColumn(colId: string, targetPos: number): Promise<{ success: boolean }> {
    await this._send("moveColumn", { colId, targetPos });
    return { success: true };
  }

  async moveRow(rowId: string, targetPos: number): Promise<{ success: boolean }> {
    await this._send("moveRow", { rowId, targetPos });
    return { success: true };
  }

  async shiftColumnsForEmptyMove(sourcePos: number, targetPos: number): Promise<{ success: boolean }> {
    await this._send("shiftColumnsForEmptyMove", { sourcePos, targetPos });
    return { success: true };
  }

  async shiftRowsForEmptyMove(sourcePos: number, targetPos: number): Promise<{ success: boolean }> {
    await this._send("shiftRowsForEmptyMove", { sourcePos, targetPos });
    return { success: true };
  }

  async insertColumnAt(position: number, insertBefore: boolean): Promise<{ id: string; position: number }> {
    const response = await this._send("insertColumnAt", { position, insertBefore });
    return { id: response.id as unknown as string, position: response.position as number };
  }

  async insertRowAt(position: number, insertBefore: boolean): Promise<{ id: string; position: number }> {
    const response = await this._send("insertRowAt", { position, insertBefore });
    return { id: response.id as unknown as string, position: response.position as number };
  }

  async deleteColumnById(colId: string): Promise<{ success: boolean }> {
    await this._send("deleteColumnById", { colId });
    return { success: true };
  }

  async deleteRowById(rowId: string): Promise<{ success: boolean }> {
    await this._send("deleteRowById", { rowId });
    return { success: true };
  }

  async fillRange(
    sourceMinCol: number, sourceMinRow: number,
    sourceMaxCol: number, sourceMaxRow: number,
    targetMinCol: number, targetMinRow: number,
    targetMaxCol: number, targetMaxRow: number
  ): Promise<{ success: boolean; cellsFilled: number }> {
    const result = await this._send("fillRange", {
      sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow,
      targetMinCol, targetMinRow, targetMaxCol, targetMaxRow
    });
    return { success: true, cellsFilled: result.cellsFilled as number };
  }

  async renameColumn(colId: string, name: string): Promise<{ success: boolean }> {
    await this._send("renameColumn", { colId, name });
    return { success: true };
  }

  async renameColumnByPos(pos: number, name: string): Promise<RenameColumnByPosResult> {
    const result = await this._send("renameColumnByPos", { pos, name });
    return { success: true, id: String(result.id) };
  }

  // ========== Export API ==========

  async hasFormulas(): Promise<boolean> {
    const response = await this._send("hasFormulas");
    return response.hasFormulas as boolean;
  }

  async exportAs(format: FileFormat): Promise<ExportResult> {
    const response = await this._send("export", { format });
    return { data: response.data as ArrayBuffer, filename: response.filename as string };
  }

  async exportCells(): Promise<ExportResult> {
    return this.exportAs("zcd");
  }

  async exportCSV(): Promise<ExportResult> {
    return this.exportAs("csv");
  }

  async exportXLSX(): Promise<ExportResult> {
    return this.exportAs("xlsx");
  }

  // ========== Workbook Management API ==========

  async getWorkbookName(): Promise<string> {
    const response = await this._send("getWorkbookName");
    return response.name as string;
  }

  async setWorkbookName(name: string): Promise<void> {
    await this._send("setWorkbookName", { name });
  }

  // ========== CRDT Collaboration API ==========

  async setNodeId(nodeId: string): Promise<string> {
    const response = await this._send("setNodeId", { nodeId });
    return response.result as string;
  }

  async getNodeId(): Promise<string> {
    const response = await this._send("getNodeId");
    return response.nodeId as string;
  }

  async getCurrentHLC(): Promise<string> {
    const response = await this._send("getCurrentHLC");
    return response.hlc as string;
  }

  async getOperationsSince(sinceHLC: string = ""): Promise<string> {
    const response = await this._send("getOperationsSince", { sinceHLC });
    return response.result as string;
  }

  async applyRemoteOperation(opJson: string): Promise<string> {
    const response = await this._send("applyRemoteOperation", { opJson });
    return response.result as string;
  }

  async applyRemoteOperations(opsJson: string): Promise<string> {
    const response = await this._send("applyRemoteOperations", { opsJson });
    return response.result as string;
  }

  async getOpLogSize(): Promise<number> {
    const response = await this._send("getOpLogSize");
    return response.size as number;
  }

  async hasOperation(hlc: string): Promise<boolean> {
    const response = await this._send("hasOperation", { hlc });
    return response.exists as boolean;
  }

  // ========== SyncManager API ==========

  async initSyncManager(): Promise<string> {
    const response = await this._send("initSyncManager");
    return response.result as string;
  }

  async addPeer(peerId: string): Promise<string> {
    const response = await this._send("addPeer", { peerId });
    return response.result as string;
  }

  async removePeer(peerId: string): Promise<string> {
    const response = await this._send("removePeer", { peerId });
    return response.result as string;
  }

  async getPeerIds(): Promise<string> {
    const response = await this._send("getPeerIds");
    return response.result as string;
  }

  async getPeerCount(): Promise<number> {
    const response = await this._send("getPeerCount");
    return response.count as number;
  }

  async handlePeerMessage(peerId: string, messageJson: string): Promise<string> {
    const response = await this._send("handlePeerMessage", { peerId, messageJson });
    return response.result as string;
  }

  async getOutgoingMessages(): Promise<string> {
    const response = await this._send("getOutgoingMessages");
    return response.result as string;
  }

  async queueOperationsBroadcast(): Promise<string> {
    const response = await this._send("queueOperationsBroadcast");
    return response.result as string;
  }

  async startCollaboration(): Promise<string> {
    const response = await this._send("startCollaboration");
    return response.result as string;
  }

  // ========== C++ SyncClient API (P2P WebRTC sync) ==========

  async enableSync(url: string, roomId: string, peerId: string = ""): Promise<EnableSyncResult> {
    const response = await this._send("enableSync", { url, roomId, peerId });
    return JSON.parse(response.result as string) as EnableSyncResult;
  }

  async disableSync(): Promise<void> {
    await this._send("disableSync");
  }

  async getSyncState(): Promise<SyncStateResult> {
    const response = await this._send("getSyncState");
    return JSON.parse(response.result as string) as SyncStateResult;
  }

  async isSyncEnabled(): Promise<boolean> {
    const response = await this._send("isSyncEnabled");
    return response.enabled as boolean;
  }

  async processSyncOutgoing(): Promise<void> {
    await this._send("processSyncOutgoing");
  }

  async processSyncPresence(): Promise<void> {
    await this._send("processSyncPresence");
  }

  async broadcastSyncOperations(): Promise<void> {
    await this._send("broadcastSyncOperations");
  }

  // ========== C++ SyncClient Presence API ==========

  async setSyncLocalName(name: string): Promise<void> {
    await this._send("setSyncLocalName", { name });
  }

  async setSyncCurrentSheet(sheetId: string): Promise<void> {
    await this._send("setSyncCurrentSheet", { sheetId });
  }

  async setSyncCursor(col: number, row: number): Promise<void> {
    await this._send("setSyncCursor", { col, row });
  }

  async clearSyncCursor(): Promise<void> {
    await this._send("clearSyncCursor");
  }

  async setSyncSelection(startCol: number, startRow: number, endCol: number, endRow: number): Promise<void> {
    await this._send("setSyncSelection", { startCol, startRow, endCol, endRow });
  }

  async clearSyncSelection(): Promise<void> {
    await this._send("clearSyncSelection");
  }

  async setSyncMousePosition(x: number, y: number): Promise<void> {
    await this._send("setSyncMousePosition", { x, y });
  }

  async clearSyncMousePosition(): Promise<void> {
    await this._send("clearSyncMousePosition");
  }

  async setSyncEditing(col: number, row: number, text: string): Promise<void> {
    await this._send("setSyncEditing", { col, row, text });
  }

  async clearSyncEditing(): Promise<void> {
    await this._send("clearSyncEditing");
  }

  async getRemotePresences(): Promise<RemotePresencesResult> {
    const response = await this._send("getRemotePresences");
    return JSON.parse(response.result as string) as RemotePresencesResult;
  }

  // ========== Debug/Development API ==========

  async debugParseFormula(formulaText: string): Promise<FormulaParseResult> {
    const response = await this._send("debugParseFormula", { formulaText });
    return JSON.parse(response.result as string) as FormulaParseResult;
  }

  // ========== Viewport Pixel Queries (Phase 5) ==========

  async getColumnPixelOffset(position: number): Promise<number> {
    const response = await this._send("getColumnPixelOffset", { position });
    return response.offset as number;
  }

  async getRowPixelOffset(position: number): Promise<number> {
    const response = await this._send("getRowPixelOffset", { position });
    return response.offset as number;
  }

  async getTotalWidth(): Promise<number> {
    const response = await this._send("getTotalWidth");
    return response.size as number;
  }

  async getTotalHeight(): Promise<number> {
    const response = await this._send("getTotalHeight");
    return response.size as number;
  }

  // ========== Formula API (Phase 7) ==========

  async validateFormula(formulaText: string): Promise<ValidateFormulaResult> {
    const response = await this._send("validateFormula", { formulaText });
    return JSON.parse(response.result as string) as ValidateFormulaResult;
  }

  async getFormulaDisplay(cellId: string): Promise<string> {
    const response = await this._send("getFormulaDisplay", { cellId });
    return response.result as string;
  }

  async getCellDependencies(cellId: string): Promise<CellDependenciesResult> {
    const response = await this._send("getCellDependencies", { cellId });
    return JSON.parse(response.result as string) as CellDependenciesResult;
  }

  async getCellDependents(cellId: string): Promise<CellDependentsResult> {
    const response = await this._send("getCellDependents", { cellId });
    return JSON.parse(response.result as string) as CellDependentsResult;
  }

  async getFormulaReferences(formulaText: string): Promise<FormulaReferencesResult> {
    const response = await this._send("getFormulaReferences", { formulaText });
    return JSON.parse(response.result as string) as FormulaReferencesResult;
  }

  async getReferencesFromPartial(formulaText: string): Promise<FormulaReferencesResult> {
    const response = await this._send("getReferencesFromPartial", { formulaText });
    return JSON.parse(response.result as string) as FormulaReferencesResult;
  }

  async detectCircularRef(cellId: string): Promise<CircularRefResult> {
    const response = await this._send("detectCircularRef", { cellId });
    return JSON.parse(response.result as string) as CircularRefResult;
  }

  async getVolatileCells(): Promise<VolatileCellsResult> {
    const response = await this._send("getVolatileCells");
    return JSON.parse(response.result as string) as VolatileCellsResult;
  }

  // ========== Scripting API (Luau) ==========

  async executeScript(script: string): Promise<ScriptResult> {
    const response = await this._send("executeScript", { script });
    return JSON.parse(response.result as string) as ScriptResult;
  }

  async tokenizeLuau(source: string): Promise<LuauToken[]> {
    const response = await this._send("tokenizeLuau", { source });
    return JSON.parse(response.result as string) as LuauToken[];
  }

  async getAutocomplete(source: string, line: number, column: number): Promise<AutocompleteResult> {
    const response = await this._send("getAutocomplete", { source, line, column });
    return JSON.parse(response.result as string) as AutocompleteResult;
  }
}
