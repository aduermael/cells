// =============================================================================
// Dropdown Positioning Utilities
// =============================================================================
//
// Shared utility functions for positioning dropdown menus and popups.
// Ensures dropdowns stay within screen bounds with appropriate padding.
//
// Based on the positioning logic from context-menu.ts, extracted for reuse
// across all dropdown components.
//
// =============================================================================

/**
 * Options for positioning a dropdown
 */
export interface PositionDropdownOptions {
  /** Padding from screen edges in pixels (default: 8) */
  padding?: number;
  /** Whether to prefer positioning below the anchor (default: true) */
  preferBelow?: boolean;
  /** Whether to prefer positioning to the right of the anchor (default: true) */
  preferRight?: boolean;
}

/**
 * Position a dropdown element relative to an anchor element, adjusting for screen edges.
 *
 * Uses fixed positioning to allow the dropdown to escape any parent overflow constraints.
 * The dropdown will be positioned:
 * - Below the anchor if preferBelow is true (default), otherwise above
 * - Aligned to the left edge of the anchor if preferRight is true (default)
 *
 * If the dropdown would overflow the viewport, it will be repositioned:
 * - Flipped to the opposite side (above/below or left/right)
 * - Clamped to stay within the viewport with padding
 *
 * @param dropdown - The dropdown element to position
 * @param anchorRect - The bounding rect of the anchor element
 * @param options - Positioning options
 */
export function positionDropdown(
  dropdown: HTMLElement,
  anchorRect: DOMRect,
  options: PositionDropdownOptions = {}
): void {
  const { padding = 8, preferBelow = true, preferRight = true } = options;

  // Reset any existing position properties that might conflict
  dropdown.style.right = "auto";
  dropdown.style.bottom = "auto";
  dropdown.style.position = "fixed";

  // Temporarily position off-screen to measure dimensions
  dropdown.style.left = "-9999px";
  dropdown.style.top = "-9999px";

  // Get dropdown dimensions
  const dropdownRect = dropdown.getBoundingClientRect();
  const dropdownWidth = dropdownRect.width;
  const dropdownHeight = dropdownRect.height;

  // Get viewport dimensions
  const viewportWidth = window.innerWidth;
  const viewportHeight = window.innerHeight;

  // Calculate initial position
  let finalX: number;
  let finalY: number;

  // Horizontal positioning
  if (preferRight) {
    // Align left edge of dropdown with left edge of anchor
    finalX = anchorRect.left;
    if (finalX + dropdownWidth + padding > viewportWidth) {
      // Would overflow right, try aligning right edges
      finalX = anchorRect.right - dropdownWidth;
      if (finalX < padding) {
        // Still overflows, clamp to left edge
        finalX = padding;
      }
    }
  } else {
    // Align right edge of dropdown with right edge of anchor
    finalX = anchorRect.right - dropdownWidth;
    if (finalX < padding) {
      // Would overflow left, try aligning left edges
      finalX = anchorRect.left;
      if (finalX + dropdownWidth + padding > viewportWidth) {
        // Still overflows, clamp to right edge
        finalX = viewportWidth - dropdownWidth - padding;
      }
    }
  }

  // Vertical positioning
  if (preferBelow) {
    // Position below anchor
    finalY = anchorRect.bottom;
    if (finalY + dropdownHeight + padding > viewportHeight) {
      // Would overflow bottom, try positioning above
      finalY = anchorRect.top - dropdownHeight;
      if (finalY < padding) {
        // Still overflows, clamp to top edge
        finalY = padding;
      }
    }
  } else {
    // Position above anchor
    finalY = anchorRect.top - dropdownHeight;
    if (finalY < padding) {
      // Would overflow top, try positioning below
      finalY = anchorRect.bottom;
      if (finalY + dropdownHeight + padding > viewportHeight) {
        // Still overflows, clamp to bottom edge
        finalY = viewportHeight - dropdownHeight - padding;
      }
    }
  }

  // Apply final position
  dropdown.style.left = `${finalX}px`;
  dropdown.style.top = `${finalY}px`;
}

/**
 * Position a dropdown element relative to an anchor element using absolute positioning.
 *
 * Similar to positionDropdown, but uses absolute positioning relative to a container
 * instead of fixed positioning relative to the viewport. Useful for dropdowns that
 * are children of a positioned container.
 *
 * @param dropdown - The dropdown element to position
 * @param anchorElement - The anchor element to position relative to
 * @param container - The positioned container element
 * @param options - Positioning options
 */
export function positionDropdownAbsolute(
  dropdown: HTMLElement,
  anchorElement: HTMLElement,
  container: HTMLElement,
  options: PositionDropdownOptions = {}
): void {
  const { padding = 8, preferBelow = true, preferRight = true } = options;

  const anchorRect = anchorElement.getBoundingClientRect();
  const containerRect = container.getBoundingClientRect();

  // Reset any existing position properties that might conflict
  dropdown.style.right = "auto";
  dropdown.style.bottom = "auto";
  dropdown.style.position = "absolute";

  // Temporarily position off-screen to measure dimensions
  dropdown.style.left = "-9999px";
  dropdown.style.top = "-9999px";

  // Get dropdown dimensions
  const dropdownRect = dropdown.getBoundingClientRect();
  const dropdownWidth = dropdownRect.width;
  const dropdownHeight = dropdownRect.height;

  // Get viewport dimensions
  const viewportWidth = window.innerWidth;
  const viewportHeight = window.innerHeight;

  // Calculate initial position in viewport coordinates
  let viewportX: number;
  let viewportY: number;

  // Horizontal positioning
  if (preferRight) {
    viewportX = anchorRect.left;
    if (viewportX + dropdownWidth + padding > viewportWidth) {
      viewportX = anchorRect.right - dropdownWidth;
      if (viewportX < padding) {
        viewportX = padding;
      }
    }
  } else {
    viewportX = anchorRect.right - dropdownWidth;
    if (viewportX < padding) {
      viewportX = anchorRect.left;
      if (viewportX + dropdownWidth + padding > viewportWidth) {
        viewportX = viewportWidth - dropdownWidth - padding;
      }
    }
  }

  // Vertical positioning
  if (preferBelow) {
    viewportY = anchorRect.bottom;
    if (viewportY + dropdownHeight + padding > viewportHeight) {
      viewportY = anchorRect.top - dropdownHeight;
      if (viewportY < padding) {
        viewportY = padding;
      }
    }
  } else {
    viewportY = anchorRect.top - dropdownHeight;
    if (viewportY < padding) {
      viewportY = anchorRect.bottom;
      if (viewportY + dropdownHeight + padding > viewportHeight) {
        viewportY = viewportHeight - dropdownHeight - padding;
      }
    }
  }

  // Convert to container-relative coordinates
  const finalX = viewportX - containerRect.left;
  const finalY = viewportY - containerRect.top;

  // Apply final position
  dropdown.style.left = `${finalX}px`;
  dropdown.style.top = `${finalY}px`;
}
