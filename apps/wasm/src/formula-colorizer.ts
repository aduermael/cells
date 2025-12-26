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
 */
export function colorizeFormula(
  formula: string,
  highlights: FormulaHighlight[]
): string {
  const segments = getFormulaSegments(formula, highlights);

  return segments
    .map((segment) => {
      const escapedText = escapeHtml(segment.text);
      if (segment.colorIndex !== undefined) {
        const color = getHighlightColor(segment.colorIndex, segment.isError);
        return `<span class="formula-ref" style="color: ${color}; font-weight: 600;">${escapedText}</span>`;
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
 * Restore cursor to a text offset position in element.
 */
function restoreCursor(element: HTMLElement, offset: number): void {
  const selection = window.getSelection();
  if (!selection) return;

  const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT, null);
  let totalOffset = 0;

  let current = walker.nextNode();
  while (current) {
    const nodeLength = current.textContent?.length ?? 0;
    if (totalOffset + nodeLength >= offset) {
      // Found the node containing our offset
      const range = document.createRange();
      range.setStart(current, offset - totalOffset);
      range.collapse(true);
      selection.removeAllRanges();
      selection.addRange(range);
      return;
    }
    totalOffset += nodeLength;
    current = walker.nextNode();
  }

  // Offset is beyond content, place cursor at end
  const range = document.createRange();
  range.selectNodeContents(element);
  range.collapse(false);
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
 * Set cursor position in a contenteditable element.
 */
export function setCursorPosition(
  element: HTMLElement,
  start: number,
  _end?: number
): void {
  restoreCursor(element, start);
  // TODO: Handle selection range (start !== end) if needed
}
