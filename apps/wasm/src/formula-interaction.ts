// Formula Interaction Module
// Handles hit testing and interaction logic for formula range manipulation

import {
  type CornerPosition,
  type BorderPosition,
  type FormulaHighlight,
} from "./grid-constants.js";
import { computeFormulaInteractionZones, type FormulaHighlightRendererState } from "./grid-formula-renderer.js";
import { colToLetter } from "./grid-utils.js";

/**
 * Absolute reference markers for A1 notation.
 * Each flag indicates whether $ was present in the original reference.
 */
export interface AbsoluteMarkers {
  startColAbsolute: boolean;  // $A in $A1 or $A$1:B2
  startRowAbsolute: boolean;  // $1 in A$1 or $A$1:B2
  endColAbsolute: boolean;    // $B in A1:$B2 or A1:$B$2
  endRowAbsolute: boolean;    // $2 in A1:B$2 or A1:$B$2
}

/**
 * State for tracking formula range drag operations (move/resize).
 */
export interface FormulaRangeDragState {
  action: "move" | "resize";
  highlightIndex: number;
  corner?: CornerPosition;  // For resize
  border?: BorderPosition;  // For move
  originalRange: {
    startCol: number;
    startRow: number;
    endCol: number;
    endRow: number;
  };
  sourcePosition: { start: number; end: number };  // Position in formula text
  dragStartCell: { col: number; row: number };  // Grid cell where drag started
  highlight: FormulaHighlight;  // Reference to the highlight being manipulated
  originalRefText: string;  // Original reference text (for preserving $ markers)
  absoluteMarkers?: AbsoluteMarkers;  // Parsed absolute reference markers
}

/**
 * Result of hit testing a formula highlight interaction zone.
 * Indicates which highlight and what action should be taken.
 */
export interface FormulaHighlightHitResult {
  highlightIndex: number;
  action: "resize" | "move";
  corner?: CornerPosition;
  border?: BorderPosition;
}

/**
 * Hit test for formula highlight interaction zones.
 * Priority order: corners (resize) > borders (move) > inside (no action for formula refs)
 *
 * @param mouseX Mouse X position in screen coordinates
 * @param mouseY Mouse Y position in screen coordinates
 * @param state The formula highlight renderer state
 * @param viewWidth Canvas view width
 * @param viewHeight Canvas view height
 * @returns Hit result with highlight index and action type, or null if not over any zone
 */
export function hitTestFormulaHighlight(
  mouseX: number,
  mouseY: number,
  state: FormulaHighlightRendererState,
  viewWidth: number,
  viewHeight: number
): FormulaHighlightHitResult | null {
  // Get all interaction zones
  const zones = computeFormulaInteractionZones(state, viewWidth, viewHeight);

  if (zones.length === 0) {
    return null;
  }

  // First pass: check corners (highest priority - resize)
  for (const zone of zones) {
    if (zone.zone === "corner" && zone.corner) {
      const { x, y, width, height } = zone.bounds;
      if (
        mouseX >= x &&
        mouseX <= x + width &&
        mouseY >= y &&
        mouseY <= y + height
      ) {
        return {
          highlightIndex: zone.highlightIndex,
          action: "resize",
          corner: zone.corner,
        };
      }
    }
  }

  // Second pass: check borders (move)
  for (const zone of zones) {
    if (zone.zone === "border" && zone.border) {
      const { x, y, width, height } = zone.bounds;
      if (
        mouseX >= x &&
        mouseX <= x + width &&
        mouseY >= y &&
        mouseY <= y + height
      ) {
        return {
          highlightIndex: zone.highlightIndex,
          action: "move",
          border: zone.border,
        };
      }
    }
  }

  // Inside zones don't trigger any action for formula refs
  return null;
}

/**
 * Get the appropriate cursor style for a corner position (resize).
 *
 * @param corner The corner position
 * @returns CSS cursor style string
 */
export function getCursorForCorner(corner: CornerPosition): string {
  switch (corner) {
    case "nw":
    case "se":
      return "nwse-resize";
    case "ne":
    case "sw":
      return "nesw-resize";
  }
}

/**
 * Get the appropriate cursor style for a formula highlight hit result.
 *
 * @param hitResult The hit test result
 * @param isDragging Whether currently dragging (for grab/grabbing distinction)
 * @returns CSS cursor style string
 */
export function getCursorForHitResult(
  hitResult: FormulaHighlightHitResult | null,
  isDragging: boolean = false
): string {
  if (!hitResult) {
    return "default";
  }

  if (hitResult.action === "resize" && hitResult.corner) {
    return getCursorForCorner(hitResult.corner);
  }

  if (hitResult.action === "move") {
    return isDragging ? "grabbing" : "grab";
  }

  return "default";
}

/**
 * Create a drag state from a hit result and the corresponding highlight.
 *
 * @param hitResult The hit test result
 * @param highlight The formula highlight being manipulated
 * @param dragStartCell Grid cell where drag started
 * @param originalRefText The original reference text from the formula (for preserving $ markers)
 * @returns FormulaRangeDragState or null if highlight type not supported
 */
export function createDragState(
  hitResult: FormulaHighlightHitResult,
  highlight: FormulaHighlight,
  dragStartCell: { col: number; row: number },
  originalRefText: string
): FormulaRangeDragState | null {
  // Extract range from highlight
  let startCol: number, startRow: number, endCol: number, endRow: number;

  if (highlight.type === "cell" && highlight.col !== undefined && highlight.row !== undefined) {
    startCol = highlight.col;
    startRow = highlight.row;
    endCol = highlight.col;
    endRow = highlight.row;
  } else if (
    highlight.type === "range" &&
    highlight.startCol !== undefined &&
    highlight.startRow !== undefined &&
    highlight.endCol !== undefined &&
    highlight.endRow !== undefined
  ) {
    startCol = highlight.startCol;
    startRow = highlight.startRow;
    endCol = highlight.endCol;
    endRow = highlight.endRow;
  } else {
    // Column/row references and named ranges not supported for resize/move
    return null;
  }

  // Parse absolute markers from original reference text
  const absoluteMarkers = parseAbsoluteMarkers(originalRefText);

  return {
    action: hitResult.action,
    highlightIndex: hitResult.highlightIndex,
    corner: hitResult.corner,
    border: hitResult.border,
    originalRange: { startCol, startRow, endCol, endRow },
    sourcePosition: { start: highlight.sourceStart, end: highlight.sourceEnd },
    dragStartCell,
    highlight,
    originalRefText,
    absoluteMarkers,
  };
}

/**
 * Calculate new range bounds based on resize operation.
 * The dragged corner moves, opposite corner stays fixed.
 *
 * @param dragState Current drag state
 * @param currentCell Current mouse position in grid coordinates
 * @returns New range bounds { startCol, startRow, endCol, endRow }
 */
export function calculateResizedRange(
  dragState: FormulaRangeDragState,
  currentCell: { col: number; row: number }
): { startCol: number; startRow: number; endCol: number; endRow: number } {
  const { originalRange, corner } = dragState;
  let { startCol, startRow, endCol, endRow } = originalRange;

  // Normalize so start <= end
  const minCol = Math.min(startCol, endCol);
  const maxCol = Math.max(startCol, endCol);
  const minRow = Math.min(startRow, endRow);
  const maxRow = Math.max(startRow, endRow);

  // Move the appropriate corner based on which one is being dragged
  let newMinCol = minCol;
  let newMaxCol = maxCol;
  let newMinRow = minRow;
  let newMaxRow = maxRow;

  switch (corner) {
    case "nw": // Top-left corner
      newMinCol = currentCell.col;
      newMinRow = currentCell.row;
      break;
    case "ne": // Top-right corner
      newMaxCol = currentCell.col;
      newMinRow = currentCell.row;
      break;
    case "sw": // Bottom-left corner
      newMinCol = currentCell.col;
      newMaxRow = currentCell.row;
      break;
    case "se": // Bottom-right corner
      newMaxCol = currentCell.col;
      newMaxRow = currentCell.row;
      break;
  }

  // Ensure min <= max (swap if needed due to drag crossing)
  if (newMinCol > newMaxCol) {
    [newMinCol, newMaxCol] = [newMaxCol, newMinCol];
  }
  if (newMinRow > newMaxRow) {
    [newMinRow, newMaxRow] = [newMaxRow, newMinRow];
  }

  // Clamp to valid grid coordinates
  newMinCol = Math.max(0, newMinCol);
  newMinRow = Math.max(0, newMinRow);
  newMaxCol = Math.max(0, newMaxCol);
  newMaxRow = Math.max(0, newMaxRow);

  return {
    startCol: newMinCol,
    startRow: newMinRow,
    endCol: newMaxCol,
    endRow: newMaxRow,
  };
}

/**
 * Calculate new range bounds based on move operation.
 * All corners move by the same delta.
 *
 * @param dragState Current drag state
 * @param currentCell Current mouse position in grid coordinates
 * @returns New range bounds { startCol, startRow, endCol, endRow }
 */
export function calculateMovedRange(
  dragState: FormulaRangeDragState,
  currentCell: { col: number; row: number }
): { startCol: number; startRow: number; endCol: number; endRow: number } {
  const { originalRange, dragStartCell } = dragState;

  // Calculate delta from drag start
  const deltaCol = currentCell.col - dragStartCell.col;
  const deltaRow = currentCell.row - dragStartCell.row;

  // Normalize original range
  const minCol = Math.min(originalRange.startCol, originalRange.endCol);
  const maxCol = Math.max(originalRange.startCol, originalRange.endCol);
  const minRow = Math.min(originalRange.startRow, originalRange.endRow);
  const maxRow = Math.max(originalRange.startRow, originalRange.endRow);

  // Apply delta
  let newMinCol = minCol + deltaCol;
  let newMaxCol = maxCol + deltaCol;
  let newMinRow = minRow + deltaRow;
  let newMaxRow = maxRow + deltaRow;

  // Clamp to valid grid coordinates (>= 0)
  // If the range would go negative, shift it back
  if (newMinCol < 0) {
    const shift = -newMinCol;
    newMinCol = 0;
    newMaxCol += shift;
  }
  if (newMinRow < 0) {
    const shift = -newMinRow;
    newMinRow = 0;
    newMaxRow += shift;
  }

  return {
    startCol: newMinCol,
    startRow: newMinRow,
    endCol: newMaxCol,
    endRow: newMaxRow,
  };
}

/**
 * Parse absolute reference markers ($) from a reference string.
 * Handles cell references (A1, $A1, A$1, $A$1) and range references (A1:B2, $A$1:$B$2, etc.)
 *
 * @param refText The original reference text (e.g., "$A$1:B2")
 * @returns AbsoluteMarkers indicating which parts had $ markers
 */
export function parseAbsoluteMarkers(refText: string): AbsoluteMarkers {
  const markers: AbsoluteMarkers = {
    startColAbsolute: false,
    startRowAbsolute: false,
    endColAbsolute: false,
    endRowAbsolute: false,
  };

  // Remove any sheet prefix (e.g., "Sheet1!" or "'Sheet Name'!")
  let ref = refText;
  const sheetPrefixMatch = ref.match(/^(?:'[^']+'|[^!]+)!/);
  if (sheetPrefixMatch) {
    ref = ref.slice(sheetPrefixMatch[0].length);
  }

  // Split by colon to separate start and end of range
  const parts = ref.split(":");

  // Parse first part (start cell or single cell)
  if (parts[0]) {
    const startMatch = parts[0].match(/^(\$?)([A-Za-z]+)(\$?)(\d+)$/);
    if (startMatch) {
      markers.startColAbsolute = startMatch[1] === "$";
      markers.startRowAbsolute = startMatch[3] === "$";
    }
  }

  // Parse second part (end cell) if it exists
  if (parts[1]) {
    const endMatch = parts[1].match(/^(\$?)([A-Za-z]+)(\$?)(\d+)$/);
    if (endMatch) {
      markers.endColAbsolute = endMatch[1] === "$";
      markers.endRowAbsolute = endMatch[3] === "$";
    }
  } else {
    // Single cell reference: copy start markers to end
    markers.endColAbsolute = markers.startColAbsolute;
    markers.endRowAbsolute = markers.startRowAbsolute;
  }

  return markers;
}

/**
 * Convert range coordinates to A1 notation with optional absolute reference preservation.
 * Returns "A1" for single cells, "A1:B5" for ranges.
 * If absoluteMarkers is provided, preserves $ markers from the original reference.
 *
 * @param startCol Start column (0-indexed)
 * @param startRow Start row (0-indexed)
 * @param endCol End column (0-indexed)
 * @param endRow End row (0-indexed)
 * @param absoluteMarkers Optional markers to preserve $ from original reference
 * @returns A1 notation string
 */
export function rangeToA1Notation(
  startCol: number,
  startRow: number,
  endCol: number,
  endRow: number,
  absoluteMarkers?: AbsoluteMarkers
): string {
  // Convert to 1-indexed rows
  const startRowNum = startRow + 1;
  const endRowNum = endRow + 1;

  // Convert columns to letters
  const startColLetter = colToLetter(startCol);
  const endColLetter = colToLetter(endCol);

  // Check if it's a single cell
  if (startCol === endCol && startRow === endRow) {
    if (absoluteMarkers) {
      const colPrefix = absoluteMarkers.startColAbsolute ? "$" : "";
      const rowPrefix = absoluteMarkers.startRowAbsolute ? "$" : "";
      return `${colPrefix}${startColLetter}${rowPrefix}${startRowNum}`;
    }
    return `${startColLetter}${startRowNum}`;
  }

  // Return range notation
  if (absoluteMarkers) {
    const startColPrefix = absoluteMarkers.startColAbsolute ? "$" : "";
    const startRowPrefix = absoluteMarkers.startRowAbsolute ? "$" : "";
    const endColPrefix = absoluteMarkers.endColAbsolute ? "$" : "";
    const endRowPrefix = absoluteMarkers.endRowAbsolute ? "$" : "";
    return `${startColPrefix}${startColLetter}${startRowPrefix}${startRowNum}:${endColPrefix}${endColLetter}${endRowPrefix}${endRowNum}`;
  }
  return `${startColLetter}${startRowNum}:${endColLetter}${endRowNum}`;
}
