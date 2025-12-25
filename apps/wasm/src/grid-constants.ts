// Grid Constants Module
// Contains all grid rendering constants, colors, and shared types

import type {
  SheetInfo,
  CellData,
  Position,
  SelectionRange,
  Point,
  EditingState,
} from "./types.js";

// Grid constants
export const HEADER_HEIGHT = 24;
export const HEADER_WIDTH = 50;
export const DEFAULT_COL_WIDTH = 100;
export const DEFAULT_ROW_HEIGHT = 24;
export const CELL_PADDING = 4;

// Color palette
// Primary brand colors (should match CSS variables)
export const PRIMARY_COLOR = "#058601";
export const SECONDARY_COLOR = "#50AA4D";

export const COLORS = {
  gridLine: "#f0f0f0", // Subtle grid lines
  headerBg: "#f8f9fa",
  headerBorder: "#dee2e6",
  headerSeparator: "rgba(0, 0, 0, 0.06)", // Very subtle separators between header cells
  headerText: "#495057",
  cellText: "#212529",
  selectionBorder: PRIMARY_COLOR,
  selectionBg: "rgba(5, 134, 1, 0.1)",
  cornerBg: "#e9ecef",
} as const;

// Remote presence label styling
export const PRESENCE_LABEL_FONT =
  '10px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
export const PRESENCE_LABEL_PADDING = 4;
export const PRESENCE_LABEL_HEIGHT = 16;

/** Normalized selection range with min/max coordinates */
export interface NormalizedRange {
  minCol: number;
  maxCol: number;
  minRow: number;
  maxRow: number;
}

/** Remote presence data for rendering */
export interface RemotePresenceRender {
  peerId: string;
  name: string;
  color: string;
  cursor?: Position | null;
  selection?: SelectionRange | null;
  mouse?: Point | null;
  editing?: EditingState | null;
  mouseOpacity?: number;
}

/** State references that can be set from the main application */
export interface GridRendererState {
  sheetInfo?: SheetInfo | null;
  cells?: CellData[];
  columns?: Array<{ id: string; pos: number; width: number; name: string }>;
  rows?: Array<{ id: string; pos: number; height: number; name: string }>;
  colWidths?: Map<number, number>;
  rowHeights?: Map<number, number>;
  colNames?: Map<number, string>;
  scrollX?: number;
  scrollY?: number;
  selectedCell?: Position | null;
  selectedColumn?: number | null;
  selectedRow?: number | null;
  selectionStart?: Position | null;
  selectionEnd?: Position | null;
  isDraggingColumn?: boolean;
  isDraggingRow?: boolean;
  dragSourceIndex?: number;
  dragTargetIndex?: number;
  dragMouseX?: number;
  dragMouseY?: number;
  isResizing?: boolean;
  resizePreviewX?: number;
  isResizingRow?: boolean;
  resizePreviewY?: number;
  editingColumnIndex?: number;
  remotePresence?: RemotePresenceRender[];
}
