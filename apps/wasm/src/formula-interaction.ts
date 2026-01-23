// Formula Interaction Module
// Handles hit testing and interaction logic for formula range manipulation

import {
  type FormulaHighlight,
  type FormulaHighlightInteraction,
  type CornerPosition,
  type BorderPosition,
} from "./grid-constants.js";
import { computeFormulaInteractionZones, type FormulaHighlightRendererState } from "./grid-formula-renderer.js";

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
