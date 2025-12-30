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
