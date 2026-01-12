// Client Types Module
// Type definitions for CellsClient worker communication

import type { CellData, ColumnInfo, RowInfo, PeerPresence } from "./types";

// ============================================================================
// Worker Message Types
// ============================================================================

/** Base worker message with correlation ID */
export interface WorkerMessageBase {
  id?: number;
  type: string;
}

/** Generic worker response with any additional properties */
export type WorkerMessage = WorkerMessageBase & Record<string, unknown>;

/** Pending request entry */
export interface PendingRequest {
  resolve: (value: WorkerMessage) => void;
  reject: (error: Error) => void;
}

// ============================================================================
// Response Types
// ============================================================================

/** Load file result */
export interface LoadFileResult {
  sheetCount: number;
  sheetNames: string[];
}

/** Get sheets result */
export interface GetSheetsResult {
  sheets: Array<{ index: number; name: string; active: boolean }>;
  activeIndex: number;
}

/** Add sheet result */
export interface AddSheetResult {
  index: number;
  name: string;
}

/** Delete sheet result */
export interface DeleteSheetResult {
  activeIndex: number;
}

/** Move sheet result */
export interface MoveSheetResult {
  activeIndex: number;
}

/** Viewport query result */
export interface ViewportResult {
  cells: CellData[];
  columns: ColumnInfo[];
  rows: RowInfo[];
}

/** Create cell result */
export interface CreateCellResult {
  id: string;
}

/** Get or create cell result */
export interface GetOrCreateCellResult {
  id: string;
  existed: boolean;
  value: string;
  editValue: string; // Human-readable value for editing (dates, percentages, etc.)
  formula: string | null;
}

/** Delete cell at position result */
export interface DeleteCellAtResult {
  deleted: boolean;
}

/** Resize by position result */
export interface ResizeByPosResult {
  id: string;
  success: boolean;
}

/** Rename column by position result */
export interface RenameColumnByPosResult {
  success: boolean;
  id: string;
}

/** Export result */
export interface ExportResult {
  data: ArrayBuffer;
  filename: string;
}

/** Enable sync result */
export interface EnableSyncResult {
  success: boolean;
  peerId: string;
}

/** Sync state result */
export interface SyncStateResult {
  state: string;
  peerId: string;
  roomId: string;
  peerCount: number;
  peers: Array<{ id: string; latency: number | null }>;
}

/** Remote presences result */
export interface RemotePresencesResult {
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
// Formula API Types (Phase 7d)
// ============================================================================

/** Reference type for formula dependencies */
export type RefType =
  | "cell"
  | "range"
  | "column"
  | "row"
  | "columnRange"
  | "rowRange"
  | "named";

/** Reference info for formula highlighting - with positions for display */
export interface ReferenceInfo {
  type: RefType;
  cellId?: string;
  topLeftCellId?: string;
  bottomRightCellId?: string;
  axisId?: string;
  startAxisId?: string;
  endAxisId?: string;
  name?: string; // For named refs
  targetType?: "cell" | "range" | "column" | "row"; // Resolved target type for named refs
  sheetId?: string; // For cross-sheet refs
  sourceStart: number; // Start position in formula text
  sourceEnd: number; // End position in formula text
  // Resolved positions for rendering (added by UI after resolution)
  col?: number;
  row?: number;
  startCol?: number;
  startRow?: number;
  endCol?: number;
  endRow?: number;
}

/** Response from getFormulaReferences / getReferencesFromPartial */
export interface FormulaReferencesResult {
  references: ReferenceInfo[];
  error?: string;
}

/** Response from validateFormula */
export interface ValidateFormulaResult {
  formula: string;
  valid: boolean;
  errors: string[];
  rootType: string | null;
}

/** Response from detectCircularRef */
export interface CircularRefResult {
  hasCycle: boolean;
  cycle: string[]; // Array of cell IDs forming the cycle
  error?: string;
}

/** Response from getVolatileCells */
export interface VolatileCellsResult {
  volatileCells: string[]; // Array of cell IDs
  error?: string;
}

/** Response from getCellDependencies */
export interface CellDependenciesResult {
  dependencies: ReferenceInfo[];
  error?: string;
}

/** Response from getCellDependents */
export interface CellDependentsResult {
  dependents: string[]; // Array of cell IDs
  error?: string;
}

// ============================================================================
// Scripting Types (Luau)
// ============================================================================

/** Response from executeScript */
export interface ScriptResult {
  success: boolean;
  output?: string; // Script output if success
  error?: string; // Error message if !success
  instructions: number; // Number of instructions executed
}

/** Token type from Luau lexer */
export type LuauTokenType =
  | "keyword"
  | "string"
  | "number"
  | "comment"
  | "name"
  | "operator"
  | "error";

/** Token from Luau lexer */
export interface LuauToken {
  type: LuauTokenType;
  text: string;
  start: number; // Byte offset in source
  end: number; // Byte offset in source (exclusive)
}

/** Autocomplete suggestion kind */
export type AutocompleteSuggestionKind =
  | "property"
  | "variable"
  | "keyword"
  | "string"
  | "type"
  | "module"
  | "function"
  | "class"
  | "path"
  | "text";

/** Type correctness for autocomplete suggestions */
export type TypeCorrectKind = "correct" | "correctFunctionResult" | "";

/** Single autocomplete suggestion */
export interface AutocompleteSuggestion {
  label: string; // Display text
  insertText: string; // Text to insert (may differ from label)
  kind: AutocompleteSuggestionKind;
  detail: string; // Additional info (e.g., type signature)
  deprecated: boolean; // Whether this suggestion is deprecated
  typeCorrect: TypeCorrectKind; // Whether this matches expected type at position
}

/** Autocomplete context type */
export type AutocompleteContext =
  | "statement"
  | "expression"
  | "property"
  | "type"
  | "keyword"
  | "string"
  | "unknown";

/** Response from getAutocomplete */
export interface AutocompleteResult {
  context: AutocompleteContext;
  suggestions: AutocompleteSuggestion[];
}

// ============================================================================
// AI Agent Types
// ============================================================================

/** Agent event type */
export type AgentEventType =
  | "text"
  | "tool_use"
  | "tool_result_needed"
  | "done"
  | "error";

/** Tool use event data */
export interface AgentToolUseData {
  id: string;
  name: string;
  input: { code: string };
}

/** Done event data */
export interface AgentDoneData {
  stop_reason: string;
  conversation_id: string;
}

/** Agent event callback */
export type AgentEventCallback = (
  eventType: AgentEventType,
  data: string
) => void;
