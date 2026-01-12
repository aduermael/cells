// Formula Colorizer - Color-coded formula reference text
// Applies colored spans to formula text matching grid highlight colors

import { FORMULA_REF_COLORS, FORMULA_ERROR_COLOR } from "./grid-constants.js";
import type { FormulaHighlight } from "./grid-constants.js";

/**
 * Segment of formula text with optional color
 */
export interface FormulaSegment {
  text: string;
  colorIndex?: number; // Index into FORMULA_REF_COLORS, undefined for uncolored text
  isError?: boolean;
}

/**
 * Parse formula text into colored segments based on highlights.
 * Segments are non-overlapping and cover the entire formula.
 */
export function getFormulaSegments(
  formula: string,
  highlights: FormulaHighlight[]
): FormulaSegment[] {
  if (!formula || highlights.length === 0) {
    return [{ text: formula }];
  }

  // Sort highlights by source position
  const sortedHighlights = [...highlights].sort(
    (a, b) => a.sourceStart - b.sourceStart
  );

  const segments: FormulaSegment[] = [];
  let pos = 0;

  for (const highlight of sortedHighlights) {
    const { sourceStart, sourceEnd, colorIndex, isError } = highlight;

    // Skip invalid ranges
    if (sourceStart < 0 || sourceEnd <= sourceStart || sourceEnd > formula.length) {
      continue;
    }

    // Add uncolored text before this highlight
    if (pos < sourceStart) {
      segments.push({ text: formula.slice(pos, sourceStart) });
    }

    // Add colored highlight text
    segments.push({
      text: formula.slice(sourceStart, sourceEnd),
      colorIndex,
      isError,
    });

    pos = sourceEnd;
  }

  // Add remaining uncolored text
  if (pos < formula.length) {
    segments.push({ text: formula.slice(pos) });
  }

  return segments;
}

/**
 * Get CSS color for a highlight based on color index
 */
export function getHighlightColor(colorIndex: number, isError?: boolean): string {
  if (isError) {
    return FORMULA_ERROR_COLOR.border;
  }
  const index = colorIndex % FORMULA_REF_COLORS.length;
  const colors = FORMULA_REF_COLORS[index];
  return colors ? colors.border : FORMULA_REF_COLORS[0].border;
}

/**
 * Escape HTML special characters
 */
function escapeHtml(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

/**
 * Generate HTML string with colored spans for formula text.
 * Each reference segment gets a colored span matching grid highlights.
 * The data-ref-index attribute allows hover interaction with grid highlights.
 * @param hoveredRefIndex - Index of reference to highlight with emphasis (-1 for none)
 */
export function colorizeFormula(
  formula: string,
  highlights: FormulaHighlight[],
  hoveredRefIndex: number = -1
): string {
  const segments = getFormulaSegments(formula, highlights);

  // Sort highlights by source position to match segment order
  const sortedHighlights = [...highlights].sort(
    (a, b) => a.sourceStart - b.sourceStart
  );

  // Track which highlight index each colored segment corresponds to
  let highlightIndex = 0;
  let segmentHighlightIndex = 0;

  return segments
    .map((segment) => {
      const escapedText = escapeHtml(segment.text);
      if (segment.colorIndex !== undefined) {
        const color = getHighlightColor(segment.colorIndex, segment.isError);
        const currentHighlight = sortedHighlights[segmentHighlightIndex];
        const refIndex = highlightIndex++;
        segmentHighlightIndex++;
        const isHovered = refIndex === hoveredRefIndex;
        const hoverClass = isHovered ? " formula-ref-hovered" : "";

        // Add named range class and data attribute if this is a named reference
        let namedRangeAttr = "";
        let namedRangeClass = "";
        if (currentHighlight?.type === "named" && currentHighlight.namedRangeName) {
          namedRangeClass = " formula-ref-named";
          namedRangeAttr = ` data-named-range="${escapeHtml(currentHighlight.namedRangeName)}"`;
        }

        return `<span class="formula-ref${hoverClass}${namedRangeClass}" data-ref-index="${refIndex}"${namedRangeAttr} style="color: ${color}; font-weight: 600;">${escapedText}</span>`;
      }
      return escapedText;
    })
    .join("");
}

/**
 * Apply colored formula to a contenteditable element.
 * Preserves cursor position after update.
 */
export function applyColorizedFormula(
  element: HTMLElement,
  formula: string,
  highlights: FormulaHighlight[]
): void {
  // Get current cursor position
  const selection = window.getSelection();
  let cursorOffset = 0;

  if (selection && selection.rangeCount > 0) {
    const range = selection.getRangeAt(0);
    // Calculate offset from start of contenteditable
    cursorOffset = getTextOffset(element, range.startContainer, range.startOffset);
  }

  // Update HTML content
  element.innerHTML = colorizeFormula(formula, highlights);

  // Restore cursor position
  restoreCursor(element, cursorOffset);
}

/**
 * Get text offset from start of element to a position in a child node.
 */
function getTextOffset(
  root: HTMLElement,
  node: Node,
  offset: number
): number {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
  let totalOffset = 0;

  let current = walker.nextNode();
  while (current) {
    if (current === node) {
      return totalOffset + offset;
    }
    totalOffset += current.textContent?.length ?? 0;
    current = walker.nextNode();
  }

  // If node not found, return offset from the end
  return totalOffset + offset;
}

/**
 * Find the node and local offset for a given text offset in an element.
 */
function findNodeAtOffset(element: HTMLElement, targetOffset: number): { node: Node; offset: number } | null {
  const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT, null);
  let totalOffset = 0;
  let current = walker.nextNode();
  while (current) {
    const nodeLength = current.textContent?.length ?? 0;
    if (totalOffset + nodeLength >= targetOffset) {
      return { node: current, offset: targetOffset - totalOffset };
    }
    totalOffset += nodeLength;
    current = walker.nextNode();
  }
  return null;
}

/**
 * Restore cursor/selection to text offset position(s) in element.
 * @param element The contenteditable element
 * @param startOffset The start position of the cursor/selection
 * @param endOffset Optional end position for selection range (defaults to startOffset)
 */
function restoreCursor(element: HTMLElement, startOffset: number, endOffset?: number): void {
  const selection = window.getSelection();
  if (!selection) return;

  const effectiveEnd = endOffset ?? startOffset;
  const range = document.createRange();

  // Find start position
  const startPos = findNodeAtOffset(element, startOffset);
  if (startPos) {
    range.setStart(startPos.node, startPos.offset);
  } else {
    // Start offset beyond content, place at end
    range.selectNodeContents(element);
    range.collapse(false);
    selection.removeAllRanges();
    selection.addRange(range);
    return;
  }

  // Find end position (for selection range)
  if (effectiveEnd !== startOffset) {
    const endPos = findNodeAtOffset(element, effectiveEnd);
    if (endPos) {
      range.setEnd(endPos.node, endPos.offset);
    } else {
      // End offset beyond content, extend to end
      range.setEndAfter(element.lastChild || element);
    }
  } else {
    range.collapse(true);
  }

  selection.removeAllRanges();
  selection.addRange(range);
}

/**
 * Get plain text content from a contenteditable element.
 */
export function getPlainText(element: HTMLElement): string {
  return element.textContent ?? "";
}

/**
 * Get cursor position in plain text (0-indexed offset from start).
 * Returns { start, end } for selection range.
 */
export function getCursorPosition(
  element: HTMLElement
): { start: number; end: number } {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    const len = (element.textContent ?? "").length;
    return { start: len, end: len };
  }

  const range = selection.getRangeAt(0);
  const start = getTextOffset(element, range.startContainer, range.startOffset);
  const end = getTextOffset(element, range.endContainer, range.endOffset);

  return { start, end };
}

/**
 * Set cursor/selection position in a contenteditable element.
 * @param element The contenteditable element
 * @param start The start position of the cursor/selection
 * @param end Optional end position for selection range (defaults to start)
 */
export function setCursorPosition(
  element: HTMLElement,
  start: number,
  end?: number
): void {
  restoreCursor(element, start, end);
}
