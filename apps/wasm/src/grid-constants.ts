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

// Grid constants (unzoomed, base values)
export const HEADER_HEIGHT = 24;
export const HEADER_WIDTH = 50;
export const DEFAULT_COL_WIDTH = 100;
export const DEFAULT_ROW_HEIGHT = 24;
export const CELL_PADDING = 4;

// =============================================================================
// Global Zoom State
// =============================================================================

/** Current global zoom factor (1.0 = 100%) */
let currentZoomFactor = 1.0;

/**
 * Get the current global zoom factor
 * @returns Zoom factor (1.0 = 100%)
 */
export function getZoomFactor(): number {
  return currentZoomFactor;
}

/**
 * Set the global zoom factor
 * @param zoomFactor Zoom factor (1.0 = 100%)
 */
export function setZoomFactor(zoomFactor: number): void {
  currentZoomFactor = zoomFactor;
}

// =============================================================================
// Zoom-aware dimension helpers
// =============================================================================

/**
 * Get zoomed header height
 * Uses global zoom factor
 */
export function getZoomedHeaderHeight(): number {
  return Math.round(HEADER_HEIGHT * currentZoomFactor);
}

/**
 * Get zoomed header width
 * Uses global zoom factor
 */
export function getZoomedHeaderWidth(): number {
  return Math.round(HEADER_WIDTH * currentZoomFactor);
}

/**
 * Get zoomed column width
 * @param baseWidth Base column width in pixels
 * Uses global zoom factor
 */
export function getZoomedColWidth(baseWidth: number): number {
  return Math.round(baseWidth * currentZoomFactor);
}

/**
 * Get zoomed row height
 * @param baseHeight Base row height in pixels
 * Uses global zoom factor
 */
export function getZoomedRowHeight(baseHeight: number): number {
  return Math.round(baseHeight * currentZoomFactor);
}

/**
 * Get zoomed cell padding
 * Uses global zoom factor
 */
export function getZoomedCellPadding(): number {
  return Math.round(CELL_PADDING * currentZoomFactor);
}

/**
 * Get zoomed font size
 * @param baseFontSize Base font size in pixels
 * Uses global zoom factor
 */
export function getZoomedFontSize(baseFontSize: number): number {
  // Don't round font sizes to allow smooth scaling
  return baseFontSize * currentZoomFactor;
}

// Color palette
// Primary brand colors (should match CSS variables)
export const PRIMARY_COLOR = "#058601";
export const SECONDARY_COLOR = "#50AA4D";

// Static color constants (used as fallback)
export const COLORS = {
  gridLine: "#f0f0f0", // Subtle grid lines
  headerBg: "#f8f9fa",
  headerBorder: "#dee2e6",
  headerSeparator: "rgba(0, 0, 0, 0.06)", // Very subtle separators between header cells
  headerText: "#495057",
  cellText: "#212529",
  cellBg: "#ffffff",
  selectionBorder: PRIMARY_COLOR,
  selectionBg: "rgba(5, 134, 1, 0.1)",
  cornerBg: "#e9ecef",
} as const;

/** Grid colors that can change with theme */
export interface GridColors {
  gridLine: string;
  headerBg: string;
  headerBorder: string;
  headerSeparator: string;
  headerText: string;
  cellText: string;
  cellBg: string;
  selectionBorder: string;
  selectionBg: string;
  cornerBg: string;
}

/** Cache for computed grid colors */
let cachedColors: GridColors | null = null;
let lastTheme: string | null = null;

/**
 * Get grid colors from CSS variables.
 * Colors are cached and only recomputed when theme changes.
 */
export function getGridColors(): GridColors {
  const currentTheme = document.documentElement.getAttribute("data-theme") || "light";

  if (cachedColors && lastTheme === currentTheme) {
    return cachedColors;
  }

  const styles = getComputedStyle(document.documentElement);

  cachedColors = {
    gridLine: styles.getPropertyValue("--grid-line").trim() || COLORS.gridLine,
    headerBg: styles.getPropertyValue("--grid-header-bg").trim() || COLORS.headerBg,
    headerBorder: styles.getPropertyValue("--grid-header-border").trim() || COLORS.headerBorder,
    headerSeparator: styles.getPropertyValue("--grid-header-separator").trim() || COLORS.headerSeparator,
    headerText: styles.getPropertyValue("--grid-header-text").trim() || COLORS.headerText,
    cellText: styles.getPropertyValue("--grid-cell-text").trim() || COLORS.cellText,
    cellBg: styles.getPropertyValue("--grid-cell-bg").trim() || COLORS.cellBg,
    selectionBorder: styles.getPropertyValue("--color-primary").trim() || PRIMARY_COLOR,
    selectionBg: "rgba(5, 134, 1, 0.1)", // Keep this as-is, green selection works in both modes
    cornerBg: styles.getPropertyValue("--grid-corner-bg").trim() || COLORS.cornerBg,
  };

  lastTheme = currentTheme;
  return cachedColors;
}

/**
 * Clear the cached colors (call when theme changes)
 */
export function clearGridColorsCache(): void {
  cachedColors = null;
  lastTheme = null;
}

// Formula reference highlight colors (like Numbers/Excel)
// Each reference in a formula gets a unique color for visual identification
export const FORMULA_REF_COLORS = [
  { border: "#4285f4", bg: "rgba(66, 133, 244, 0.15)" }, // Blue
  { border: "#ea4335", bg: "rgba(234, 67, 53, 0.15)" }, // Red
  { border: "#fbbc04", bg: "rgba(251, 188, 4, 0.15)" }, // Yellow
  { border: "#34a853", bg: "rgba(52, 168, 83, 0.15)" }, // Green
  { border: "#ff6d00", bg: "rgba(255, 109, 0, 0.15)" }, // Orange
  { border: "#ab47bc", bg: "rgba(171, 71, 188, 0.15)" }, // Purple
  { border: "#00acc1", bg: "rgba(0, 172, 193, 0.15)" }, // Cyan
  { border: "#8d6e63", bg: "rgba(141, 110, 99, 0.15)" }, // Brown
] as const;

// Error highlight color for invalid references
export const FORMULA_ERROR_COLOR = {
  border: "#d32f2f",
  bg: "rgba(211, 47, 47, 0.15)",
} as const;

// Spill range highlight color (blue like Excel)
export const SPILL_RANGE_COLOR = {
  border: "#4285f4",
  bg: "rgba(66, 133, 244, 0.08)",
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

/** Formula reference highlight for rendering */
export interface FormulaHighlight {
  type: "cell" | "range" | "column" | "row" | "named";
  colorIndex: number; // Index into FORMULA_REF_COLORS array
  isError?: boolean; // Use error color instead
  // For cell references
  col?: number;
  row?: number;
  // For range references
  startCol?: number;
  startRow?: number;
  endCol?: number;
  endRow?: number;
  // Source position in formula text (for text highlighting)
  sourceStart: number;
  sourceEnd: number;
  // For named references - the resolved target type and name
  namedRangeName?: string;
  namedTargetType?: "cell" | "range" | "column" | "row";
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

/** Spill range highlight information */
export interface SpillRangeHighlight {
  minCol: number;
  maxCol: number;
  minRow: number;
  maxRow: number;
  masterCol: number;
  masterRow: number;
}

// =============================================================================
// Formula Highlight Interaction Types
// =============================================================================

/** Corner positions for resize handles */
export type CornerPosition = "nw" | "ne" | "sw" | "se";

/** Border positions for move handles */
export type BorderPosition = "n" | "s" | "e" | "w";

/** Type of interaction zone */
export type InteractionZoneType = "corner" | "border" | "inside";

/** Interaction zone for formula highlight hit testing */
export interface FormulaHighlightInteraction {
  highlightIndex: number;
  zone: InteractionZoneType;
  corner?: CornerPosition;
  border?: BorderPosition;
  bounds: { x: number; y: number; width: number; height: number };
}

/** Constants for formula highlight interaction */
export const FORMULA_HANDLE_SIZE = 6;      // 6x6 pixel corner handles
export const FORMULA_HANDLE_PADDING = 3;   // Extra hit area padding
export const FORMULA_BORDER_HIT_WIDTH = 4; // Border hit area width for move

/** State references that can be set from the main application */
export interface GridRendererState {
  sheetInfo?: SheetInfo | null;
  cells?: CellData[];
  columns?: Array<{ id: string; pos: number; width: number; name: string }>;
  rows?: Array<{ id: string; pos: number; height: number; name: string }>;
  colWidths?: Map<number, number>;
  rowHeights?: Map<number, number>;
  colNames?: Map<number, string>;
  colPixelOffsets?: Map<number, number>;
  rowPixelOffsets?: Map<number, number>;
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
  formulaHighlights?: FormulaHighlight[];
  /** Index of hovered formula reference (-1 = none) */
  hoveredFormulaRefIndex?: number;
  /** Whether formula editing is active (controls handle visibility) */
  isFormulaEditing?: boolean;
  /** Virtual scrolling: discovered row count (expands as user scrolls down) */
  discoveredRows?: number;
  /** Whether currently dragging the fill handle */
  isFillDragging?: boolean;
  /** Fill preview range (shown with dashed border during fill drag) */
  fillPreviewRange?: { minCol: number; maxCol: number; minRow: number; maxRow: number } | null;
  /** Spill range highlight (shown when selected cell is part of a spill range) */
  spillRangeHighlight?: SpillRangeHighlight | null;
  /** Style ranges for rendering backgrounds on empty cells */
  styleRanges?: Array<{
    startCol: number;
    startRow: number;
    endCol: number;
    endRow: number;
    style: { bgColor?: string; textColor?: string };
  }>;
}
