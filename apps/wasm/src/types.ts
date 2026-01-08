// Shared Type Definitions for Cells WASM Application
// These types are used across all modules in the application

// ============================================================================
// WASM Types (from C++ bindings - also in cells.d.ts)
// Duplicated here since cells.d.ts is a module declaration file
// ============================================================================

/** Response from file loading operations */
export interface LoadResult {
  success?: boolean;
  error?: string;
  sheetCount?: number;
}

/** Response from operations that return success/error */
export interface OperationResult {
  success?: boolean;
  error?: string;
  id?: string; // Cell/column/row ID for create operations
}

/** Result from applying a CRDT operation */
export interface ApplyOperationResult {
  result:
    | "success"
    | "already_applied"
    | "superseded"
    | "invalid_target"
    | "invalid_payload"
    | "resurrected"
    | "error";
  error?: string;
}

/** Result from applying multiple CRDT operations */
export interface ApplyOperationsResult {
  applied: number; // Number of operations successfully applied
  total: number; // Total operations in batch
  error?: string;
}

/** CRDT Operation for sync protocol */
export interface CRDTOperation {
  hlc: string; // Hybrid Logical Clock timestamp: "wall.logical.node"
  op: string; // Operation type: CELL_SET_VALUE, CELL_CLEAR, etc.
  target: string; // Target entity ID (cell, axis, or sheet)
  payload: Record<string, unknown>; // Operation-specific data
}

/** Response from getOperationsSince */
export interface OperationsResponse {
  operations: CRDTOperation[];
  error?: string;
}

/** Sheet information */
export interface SheetInfo {
  name: string;
  rowCount: number;
  colCount: number;
  defaultColWidth: number;
  defaultRowHeight: number;
}

/** Cell data from viewport query */
export interface CellData {
  id: string;
  col: number;
  row: number;
  type: "n" | "s" | "f" | "b" | "e" | "d" | "t"; // number, string, formula, boolean, error, date, datetime
  value?: string; // For non-formula cells
  formula?: string; // For formula cells (A1 notation)
  display?: string; // For formula cells (computed value)
  formatId?: string; // Number format ID (~ or empty for GENERAL)
}

// ============================================================================
// Number Format Types
// ============================================================================

/** Number format category (matches C++ NumberFormatCategory) */
export type NumberFormatCategory =
  | "GENERAL"
  | "NUMBER"
  | "CURRENCY"
  | "ACCOUNTING"
  | "PERCENTAGE"
  | "DATE"
  | "TIME"
  | "DATE_TIME"
  | "SCIENTIFIC"
  | "FRACTION"
  | "TEXT"
  | "CUSTOM";

/** Number format definition */
export interface NumberFormat {
  id: string; // Format ID (8-char base62)
  category: NumberFormatCategory;
  formatCode: string; // Excel-style format code (e.g., "#,##0.00")
  decimalPlaces: number;
  useThousandsSeparator: boolean;
  currencySymbol: string;
  isAccounting: boolean;
}

/** Result from parseUserInputValue */
export interface ParsedInputResult {
  success: boolean;
  type?: "number" | "string";
  numericValue?: number;
  stringValue?: string;
  formatId?: string; // ~ for GENERAL
  category?: NumberFormatCategory;
  error?: string;
}

/** Result from formatCellValue */
export interface FormattedValueResult {
  text?: string;
  error?: string;
}

/** Result from getCellFormatId */
export interface CellFormatIdResult {
  formatId?: string; // ~ for GENERAL
  error?: string;
}

/** Details about a format ID from getFormatDetails */
export interface FormatDetails {
  category: string; // "number", "currency", "percentage", "general", etc.
  decimals: number; // 0-15
  separator: boolean; // whether thousands separator is used
  currency: string | null; // currency code if applicable, null otherwise
  error?: string; // set if format ID is not recognized
}

// ============================================================================
// Formula Function Types
// ============================================================================

/** Formula function information for autocomplete */
export interface FunctionInfo {
  name: string; // Function name, e.g., "SUM"
  signature: string; // Arguments, e.g., "(number1, [number2], ...)"
  description: string; // Brief description
  category: string; // Category, e.g., "Math", "Logic", "Text"
}

/** Column information */
export interface ColumnInfo {
  id: string;
  pos: number;
  width: number;
  pixelOffset: number; // Pre-computed X pixel offset (O(log n) from ViewportIndex)
  name: string;
}

/** Row information */
export interface RowInfo {
  id: string;
  pos: number;
  height: number;
  pixelOffset: number; // Pre-computed Y pixel offset (O(log n) from ViewportIndex)
  name: string;
}

/** Viewport query result */
export interface ViewportResult {
  cells: CellData[];
  columns: ColumnInfo[];
  rows: RowInfo[];
}

// ============================================================================
// Geometry Types
// ============================================================================

/** 2D coordinate position */
export interface Position {
  col: number;
  row: number;
}

/** 2D point with x/y coordinates (for canvas/mouse positions) */
export interface Point {
  x: number;
  y: number;
}

/** Selection range with start and end positions */
export interface SelectionRange {
  start: Position;
  end: Position;
}

/** Rectangle bounds */
export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

// ============================================================================
// File Format Types
// ============================================================================

/** Supported file formats */
export type FileFormat = "zcd" | "csv" | "xlsx";

// ============================================================================
// UI State Types
// ============================================================================

/** Modifier key state */
export interface ModifierKeys {
  meta: boolean;
  shift: boolean;
  ctrl: boolean;
  alt: boolean;
}

/** UI states - mutually exclusive interaction modes */
export type UIStateType =
  | "IDLE"
  | "SELECTING"
  | "CELL_EDITING"
  | "FORMULA_BAR_EDITING"
  | "COLUMN_RESIZING"
  | "ROW_RESIZING"
  | "COLUMN_DRAGGING"
  | "ROW_DRAGGING"
  | "COLUMN_HEADER_EDITING"
  | "SHEET_TAB_EDITING"
  | "SHEET_TAB_DRAGGING";

/** UI events that trigger state transitions */
export type UIEventType =
  | "START_SELECTING"
  | "STOP_SELECTING"
  | "START_CELL_EDIT"
  | "COMMIT_CELL_EDIT"
  | "CANCEL_CELL_EDIT"
  | "START_FORMULA_EDIT"
  | "COMMIT_FORMULA_EDIT"
  | "CANCEL_FORMULA_EDIT"
  | "START_COLUMN_RESIZE"
  | "END_COLUMN_RESIZE"
  | "CANCEL_COLUMN_RESIZE"
  | "START_ROW_RESIZE"
  | "END_ROW_RESIZE"
  | "CANCEL_ROW_RESIZE"
  | "START_COLUMN_DRAG"
  | "END_COLUMN_DRAG"
  | "CANCEL_COLUMN_DRAG"
  | "START_ROW_DRAG"
  | "END_ROW_DRAG"
  | "CANCEL_ROW_DRAG"
  | "START_COLUMN_HEADER_EDIT"
  | "COMMIT_COLUMN_HEADER_EDIT"
  | "CANCEL_COLUMN_HEADER_EDIT"
  | "START_SHEET_TAB_EDIT"
  | "COMMIT_SHEET_TAB_EDIT"
  | "CANCEL_SHEET_TAB_EDIT"
  | "START_SHEET_TAB_DRAG"
  | "END_SHEET_TAB_DRAG"
  | "CANCEL_SHEET_TAB_DRAG"
  | "ESCAPE"
  | "CLICK_AWAY";

/** Sync states */
export type SyncStateType =
  | "offline"
  | "connecting"
  | "syncing"
  | "online"
  | "error";

/** Sync context for tracking collaboration state */
export interface SyncContext {
  enabled: boolean;
  state: SyncStateType;
  peerCount: number;
}

/** Full UI context including state, modifiers, selection, and sync */
export interface UIContext {
  state: UIStateType;
  stateContext: Record<string, unknown>;
  modifiers: ModifierKeys;
  selectionRange: SelectionRange;
  activeSheet: number;
  sync: SyncContext;
}

/** State transition event */
export interface StateTransitionEvent {
  previousState: UIStateType;
  currentState: UIStateType;
  event: UIEventType;
  context: UIContext;
}

// ============================================================================
// Presence Types
// ============================================================================

/** User colors for collaboration cursors */
export type UserColor = string; // Hex color like '#E53935'

/** Remote peer presence data */
export interface PeerPresence {
  peer_id: string;
  name: string;
  color: UserColor;
  sheet_id?: string;
  cursor?: Position | null;
  selection?: SelectionRange | null;
  mouse?: Point | null;
  editing?: EditingState | null;
  timestamp: number;
}

/** Editing state for ephemeral presence updates */
export interface EditingState {
  col: number;
  row: number;
  text: string;
}

/** C++ presence format (from getRemotePresences) */
export interface CppPresence {
  name?: string;
  color?: string;
  sheetId?: string;
  cursor?: Position;
  selection?: {
    startCol: number;
    startRow: number;
    endCol: number;
    endRow: number;
  };
  mouse?: Point;
  editing?: EditingState;
}

// ============================================================================
// Sync Adapter Types
// ============================================================================

/** Sync statistics */
export interface SyncStats {
  operationsSent: number;
  operationsReceived: number;
  operationsApplied: number;
  operationsDuplicate: number;
  oplogSize: number;
}

/** C++ sync state response */
export interface CppSyncState {
  state: string;
  peers?: CppPeerInfo[];
  oplogSize?: number;
}

/** C++ peer info from sync state */
export interface CppPeerInfo {
  id: string;
  latency: number | null;
}

/** Enable sync result */
export interface EnableSyncResult {
  success: boolean;
  error?: string;
  peerId?: string;
}

// ============================================================================
// Room/Collaboration Types
// ============================================================================

/** Room ID (8-char base62) */
export type RoomId = string;

/** Peer ID (8-char base62) */
export type PeerId = string;

/** Room manager callbacks */
export interface RoomManagerCallbacks {
  onJoining?: (roomId: RoomId) => void;
  onJoined?: (roomId: RoomId) => void;
  onError?: (error: Error, roomId: RoomId) => void;
}

// ============================================================================
// Grid Renderer Types
// ============================================================================

/** Grid cell for rendering */
export interface GridCell {
  id: string;
  col: number;
  row: number;
  x: number;
  y: number;
  width: number;
  height: number;
  value?: string;
  display?: string;
  formula?: string;
  type: string;
}

/** Rendered column data */
export interface RenderedColumn {
  id: string;
  pos: number;
  x: number;
  width: number;
  name: string;
}

/** Rendered row data */
export interface RenderedRow {
  id: string;
  pos: number;
  y: number;
  height: number;
  name: string;
}

/** Scroll position */
export interface ScrollPosition {
  x: number;
  y: number;
}

/** Grid viewport bounds */
export interface ViewportBounds {
  startCol: number;
  startRow: number;
  endCol: number;
  endRow: number;
}

// ============================================================================
// Event Handler Types
// ============================================================================

/** Hit test result types */
export type HitTestType =
  | "cell"
  | "column-header"
  | "row-header"
  | "column-resize"
  | "row-resize"
  | "select-all"
  | "outside";

/** Hit test result */
export interface HitTestResult {
  type: HitTestType;
  col?: number;
  row?: number;
  colId?: string;
  rowId?: string;
}

// ============================================================================
// Collab UI Types
// ============================================================================

/** Collab UI status */
export interface CollabStatus {
  state: SyncStateType;
  peerCount: number;
  roomId: RoomId | null;
  latency: number | null;
}

// ============================================================================
// Client Types
// ============================================================================

/** Client configuration options */
export interface ClientOptions {
  canvas: HTMLCanvasElement;
  onReady?: () => void;
  onError?: (error: Error) => void;
}

/** Worker message types */
export type WorkerMessageType =
  | "init"
  | "load"
  | "query"
  | "update"
  | "create"
  | "resize"
  | "rename"
  | "move"
  | "export"
  | "sync";

/** Base worker message */
export interface WorkerMessage {
  id: number;
  type: WorkerMessageType;
}

/** Worker response */
export interface WorkerResponse {
  id: number;
  success: boolean;
  result?: unknown;
  error?: string;
}
