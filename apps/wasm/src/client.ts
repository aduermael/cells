// =============================================================================
// Cells Client
// =============================================================================
//
// Main thread API for communicating with the WASM worker. Provides a
// Promise-based interface that matches the REST server semantics.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Manage Web Worker communication via postMessage/onmessage
// - Provide async/await API for all spreadsheet operations
// - Handle request/response correlation with unique IDs
// - Manage real-time collaboration via RTCProxy
// - Bridge agent events from C++ to TypeScript callbacks
//
// Architecture:
// - Main thread creates CellsClient, which spawns a Web Worker
// - Worker loads WASM module and handles all spreadsheet logic
// - Client sends JSON messages, worker executes and replies
// - Collaboration uses WebRTC signaling via RTCProxy
//
// =============================================================================

import { RTCProxy, type RTCMessagePayload } from "./rtc-proxy";
import type {
  FileFormat,
  SheetInfo,
  NumberFormat,
  ParsedInputResult,
  FormattedValueResult,
  CellFormatIdResult,
  FunctionInfo,
  NamedRangeInfo,
  FormatDetails,
  CellStyle,
  RegisteredStyle,
  CreateStyleResult,
} from "./types";
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
  AgentEventType,
  AgentEventCallback,
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
  private _onAgentEvent: AgentEventCallback | null;

  constructor(workerPath: string = "./worker.js") {
    this._worker = new Worker(workerPath);
    this._requestId = 0;
    this._pending = new Map();
    this._isReady = false;
    this._readyResolve = null;
    this._onDataChanged = null;
    this._onLoadProgress = null;
    this._rtcProxy = null;
    this._onAgentEvent = null;

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

    if (msg.type === "agentEvent") {
      if (this._onAgentEvent) {
        this._onAgentEvent(msg.eventType as AgentEventType, msg.data as string);
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

  /**
   * Update cell value with automatic format detection.
   * Parses input like "15%", "$1,234.56", "1/15/2024" and sets both value and format.
   * @param cellId Cell ID to update
   * @param value Raw input string
   * @returns Result with success status and detected format ID
   */
  async updateCellWithFormatDetection(
    cellId: string,
    value: string
  ): Promise<{ success: boolean; formatId?: string }> {
    const response = await this._send("updateCellWithFormatDetection", { cellId, value });
    return { success: true, formatId: response.formatId as string | undefined };
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
      editValue: (response.editValue as string) ?? (response.value as string),
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

  // ========== Number Format Operations API ==========

  /**
   * Set the number format for a cell by cell ID.
   * @param cellId Cell ID
   * @param formatId Format ID (use "~" for default/GENERAL format)
   */
  async setCellFormat(cellId: string, formatId: string): Promise<{ success: boolean }> {
    await this._send("setCellFormat", { cellId, formatId });
    return { success: true };
  }

  /**
   * Set the number format for a cell by position.
   * @param col Column position (0-indexed)
   * @param row Row position (0-indexed)
   * @param formatId Format ID (use "~" for default/GENERAL format)
   */
  async setCellFormatAt(col: number, row: number, formatId: string): Promise<{ success: boolean }> {
    await this._send("setCellFormatAt", { col, row, formatId });
    return { success: true };
  }

  /**
   * Get all available number formats.
   * Returns an array of NumberFormat objects.
   */
  async getAvailableFormats(): Promise<NumberFormat[]> {
    const response = await this._send("getAvailableFormats", {});
    return (response as unknown as { formats: NumberFormat[] }).formats;
  }

  /**
   * Create a custom number format from an Excel-style format code.
   * @param formatCode Excel-style format code (e.g., "#,##0.00", "0.00%")
   * @returns Object with formatId on success, error on failure
   */
  async createCustomFormat(formatCode: string): Promise<{ formatId?: string; error?: string }> {
    const response = await this._send("createCustomFormat", { formatCode });
    return response as { formatId?: string; error?: string };
  }

  /**
   * Get all registered formula functions with metadata.
   * Returns an array of FunctionInfo objects for autocomplete.
   */
  async getFormulaFunctions(): Promise<FunctionInfo[]> {
    const response = await this._send("getFormulaFunctions", {});
    return (response as unknown as { functions: FunctionInfo[] }).functions;
  }

  /**
   * Get all named ranges in the workbook.
   * Returns an array of NamedRangeInfo objects for the dropdown.
   */
  async getNamedRanges(): Promise<NamedRangeInfo[]> {
    const response = await this._send("getNamedRanges", {});
    return (response as unknown as { namedRanges: NamedRangeInfo[] }).namedRanges;
  }

  /**
   * Get the format ID for a cell by cell ID.
   * @param cellId Cell ID
   * @returns Format ID (~ for GENERAL)
   */
  async getCellFormatId(cellId: string): Promise<CellFormatIdResult> {
    const response = await this._send("getCellFormatId", { cellId });
    return response as CellFormatIdResult;
  }

  /**
   * Parse user input and auto-detect format.
   * @param input Raw user input string (e.g., "15%", "$1,234.56", "1/15/2024")
   * @returns Parsed input result with detected value and suggested format
   */
  async parseUserInputValue(input: string): Promise<ParsedInputResult> {
    const response = await this._send("parseUserInputValue", { input });
    return response as unknown as ParsedInputResult;
  }

  /**
   * Format a numeric value according to a format ID.
   * @param value The numeric value to format
   * @param formatId Format ID (use "~" or empty for GENERAL)
   * @returns Formatted value result
   */
  async formatCellValue(value: number, formatId: string): Promise<FormattedValueResult> {
    const response = await this._send("formatCellValue", { value, formatId });
    return response as FormattedValueResult;
  }

  /**
   * Format a cell's value using its assigned format.
   * @param cellId Cell ID
   * @returns Formatted value result
   */
  async formatCellById(cellId: string): Promise<FormattedValueResult> {
    const response = await this._send("formatCellById", { cellId });
    return response as FormattedValueResult;
  }

  /**
   * Format a numeric value directly with a format code string.
   * Used for live preview in custom format UI.
   * @param value The numeric value to format
   * @param formatCode Excel-style format code (e.g., "#,##0.00")
   * @returns Formatted value result with text or error
   */
  async formatWithCode(value: number, formatCode: string): Promise<FormattedValueResult> {
    const response = await this._send("formatWithCode", { value, formatCode });
    return response as FormattedValueResult;
  }

  /**
   * Get detailed information about a format ID.
   * @param formatId Format ID to get details for
   * @returns Format details including category, decimals, separator, currency
   */
  async getFormatDetails(formatId: string): Promise<FormatDetails> {
    const response = await this._send("getFormatDetails", { formatId });
    return response as unknown as FormatDetails;
  }

  /**
   * Generate a format ID for given parameters.
   * @param category Format category: "number", "currency", "percentage"
   * @param decimals Decimal places (0-15)
   * @param separator Whether to use thousands separator (only for number)
   * @param currency Currency code for currency category (e.g., "USD")
   * @returns Object with formatId on success, error on failure
   */
  async makeFormatId(
    category: string,
    decimals: number,
    separator: boolean,
    currency: string
  ): Promise<{ formatId?: string; error?: string }> {
    const response = await this._send("makeFormatId", {
      category,
      decimals,
      separator,
      currency,
    });
    return response as { formatId?: string; error?: string };
  }

  // ========== Cell Style Operations API ==========

  /**
   * Set the style for a cell by cell ID.
   * @param cellId Cell ID
   * @param style Style properties to apply
   */
  async setCellStyle(cellId: string, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    await this._send("setCellStyle", { cellId, styleJson: JSON.stringify(style) });
    return { success: true };
  }

  /**
   * Set the style for a cell by position.
   * @param col Column position (0-indexed)
   * @param row Row position (0-indexed)
   * @param style Style properties to apply
   */
  async setCellStyleAt(col: number, row: number, style: Partial<CellStyle>): Promise<{ success: boolean }> {
    await this._send("setCellStyleAt", { col, row, styleJson: JSON.stringify(style) });
    return { success: true };
  }

  /**
   * Get the style for a cell by cell ID.
   * @param cellId Cell ID
   * @returns Cell style properties
   */
  async getCellStyle(cellId: string): Promise<CellStyle> {
    const response = await this._send("getCellStyle", { cellId });
    return response as unknown as CellStyle;
  }

  /**
   * Get the style for a cell by position.
   * @param col Column position (0-indexed)
   * @param row Row position (0-indexed)
   * @returns Cell style properties
   */
  async getCellStyleAt(col: number, row: number): Promise<CellStyle> {
    const response = await this._send("getCellStyleAt", { col, row });
    return response as unknown as CellStyle;
  }

  /**
   * Create a style definition and get its ID.
   * @param style Style properties
   * @returns Result with styleId on success
   */
  async createStyle(style: Partial<CellStyle>): Promise<CreateStyleResult> {
    const response = await this._send("createStyle", { styleJson: JSON.stringify(style) });
    return response as unknown as CreateStyleResult;
  }

  /**
   * Get all registered styles.
   * @returns Array of registered style entries
   */
  async getAvailableStyles(): Promise<RegisteredStyle[]> {
    const response = await this._send("getAvailableStyles");
    return (response as unknown as { styles: RegisteredStyle[] }).styles || [];
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

  // ========== Spill Range API ==========

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
    const response = await this._send("getSpillRangeAt", { col, row });
    return JSON.parse(response.result as string);
  }

  // ========== AI Agent API ==========

  /** Set callback for agent events (text, tool_use, done, error) */
  setOnAgentEvent(callback: AgentEventCallback): void {
    this._onAgentEvent = callback;
  }

  /** Remove agent event callback */
  removeOnAgentEvent(): void {
    this._onAgentEvent = null;
  }

  /** Initialize the agent with a server URL */
  async initAgent(serverUrl: string): Promise<void> {
    await this._send("initAgent", { serverUrl });
  }

  /** Check if agent is initialized */
  async isAgentInitialized(): Promise<boolean> {
    const response = await this._send("isAgentInitialized");
    return response.initialized as boolean;
  }

  /** Send a message to the agent */
  async sendAgentMessage(prompt: string, conversationId: string = ""): Promise<void> {
    await this._send("sendAgentMessage", { prompt, conversationId });
  }

  /** Get current conversation ID */
  async getAgentConversationId(): Promise<string> {
    const response = await this._send("getAgentConversationId");
    return response.conversationId as string;
  }

  /** Clear the current conversation */
  async clearAgentConversation(): Promise<void> {
    await this._send("clearAgentConversation");
  }

  /** Cancel any in-progress agent request */
  async cancelAgent(): Promise<void> {
    await this._send("cancelAgent");
  }

  /** Check if agent is currently processing a request */
  async isAgentProcessing(): Promise<boolean> {
    const response = await this._send("isAgentProcessing");
    return response.processing as boolean;
  }
}
