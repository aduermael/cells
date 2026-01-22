// =============================================================================
// Rendering and Formula Highlighting
// =============================================================================
//
// Rendering helpers and formula highlighting utilities for the spreadsheet UI.
// Manages grid rendering, formula colorization, and cursor restoration.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Convert formula references to visual highlights
// - Update colored formula displays (formula bar and cell editor)
// - Restore cursor position after innerHTML changes
// - Set up hover handlers for formula reference spans
//
// =============================================================================

import type { App, DOMElements } from "./app";
import type { ReferenceInfo } from "./client-types";
import type { FormulaHighlight } from "./grid-constants";
import { colorizeFormula } from "./formula-colorizer.js";
import { editingSession } from "./editing-session";

// =============================================================================
// Formula Reference Conversion
// =============================================================================

/**
 * Convert ReferenceInfo from WASM to FormulaHighlight for rendering.
 * C++ provides resolved positions directly, eliminating viewport lookup race conditions.
 */
export function referenceToHighlight(
  ref: ReferenceInfo,
  colorIndex: number
): FormulaHighlight | null {
  switch (ref.type) {
    case "cell":
      if (ref.col !== undefined && ref.row !== undefined) {
        return {
          type: "cell",
          colorIndex,
          col: ref.col,
          row: ref.row,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "range":
      if (
        ref.startCol !== undefined &&
        ref.startRow !== undefined &&
        ref.endCol !== undefined &&
        ref.endRow !== undefined
      ) {
        return {
          type: "range",
          colorIndex,
          startCol: ref.startCol,
          startRow: ref.startRow,
          endCol: ref.endCol,
          endRow: ref.endRow,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "column":
      if (ref.col !== undefined) {
        return {
          type: "column",
          colorIndex,
          col: ref.col,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "row":
      if (ref.row !== undefined) {
        return {
          type: "row",
          colorIndex,
          row: ref.row,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "columnRange":
      if (ref.startCol !== undefined && ref.endCol !== undefined) {
        return {
          type: "column",
          colorIndex,
          startCol: ref.startCol,
          endCol: ref.endCol,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "rowRange":
      if (ref.startRow !== undefined && ref.endRow !== undefined) {
        return {
          type: "row",
          colorIndex,
          startRow: ref.startRow,
          endRow: ref.endRow,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
        };
      }
      break;

    case "named":
      // Named ranges are rendered based on their resolved target type
      if (ref.targetType && ref.name) {
        const baseHighlight = {
          type: "named" as const,
          colorIndex,
          sourceStart: ref.sourceStart,
          sourceEnd: ref.sourceEnd,
          namedRangeName: ref.name,
          namedTargetType: ref.targetType,
        };

        switch (ref.targetType) {
          case "cell":
            if (ref.col !== undefined && ref.row !== undefined) {
              return { ...baseHighlight, col: ref.col, row: ref.row };
            }
            break;
          case "range":
            if (
              ref.startCol !== undefined &&
              ref.startRow !== undefined &&
              ref.endCol !== undefined &&
              ref.endRow !== undefined
            ) {
              return {
                ...baseHighlight,
                startCol: ref.startCol,
                startRow: ref.startRow,
                endCol: ref.endCol,
                endRow: ref.endRow,
              };
            }
            break;
          case "column":
            if (ref.col !== undefined) {
              return { ...baseHighlight, col: ref.col };
            } else if (ref.startCol !== undefined && ref.endCol !== undefined) {
              return {
                ...baseHighlight,
                startCol: ref.startCol,
                endCol: ref.endCol,
              };
            }
            break;
          case "row":
            if (ref.row !== undefined) {
              return { ...baseHighlight, row: ref.row };
            } else if (ref.startRow !== undefined && ref.endRow !== undefined) {
              return {
                ...baseHighlight,
                startRow: ref.startRow,
                endRow: ref.endRow,
              };
            }
            break;
        }
      }
      break;
  }
  return null;
}

// =============================================================================
// Cursor Restoration
// =============================================================================

/**
 * Find the node and local offset for a given text offset in an element.
 */
export function findNodeAtOffset(
  element: HTMLElement,
  targetOffset: number
): { node: Node; offset: number } | null {
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
 */
export function restoreCursorInElement(
  element: HTMLElement,
  startOffset: number,
  endOffset?: number
): void {
  const selection = window.getSelection();
  if (!selection) return;

  const effectiveEnd = endOffset ?? startOffset;
  const range = document.createRange();

  const startPos = findNodeAtOffset(element, startOffset);
  if (startPos) {
    range.setStart(startPos.node, startPos.offset);
  } else {
    range.selectNodeContents(element);
    range.collapse(false);
    selection.removeAllRanges();
    selection.addRange(range);
    return;
  }

  if (effectiveEnd !== startOffset) {
    const endPos = findNodeAtOffset(element, effectiveEnd);
    if (endPos) {
      range.setEnd(endPos.node, endPos.offset);
    } else {
      range.setEndAfter(element.lastChild || element);
    }
  } else {
    range.collapse(true);
  }

  selection.removeAllRanges();
  selection.addRange(range);
}

// =============================================================================
// Colored Display Updates
// =============================================================================

/**
 * Update the colored formula displays with current highlights.
 */
export function updateColoredDisplays(
  value: string,
  cursorPos: number | undefined,
  elements: Pick<DOMElements, "formulaDisplay" | "cellDisplay">,
  app: Pick<App, "formulaHighlights" | "hoveredGridRefIndex">
): void {
  const isFormula = value.startsWith("=");
  const needsUpdate = isFormula || cursorPos !== undefined;

  if (!needsUpdate) {
    const activeEditor = editingSession.getActiveEditor();
    if (activeEditor === "formula") {
      elements.cellDisplay.textContent = value;
    } else {
      elements.formulaDisplay.textContent = value;
    }
    return;
  }

  const coloredHtml = colorizeFormula(
    value,
    app.formulaHighlights,
    app.hoveredGridRefIndex
  );

  const sessionCursor = editingSession.getSelection();
  const targetStart = cursorPos ?? sessionCursor.start;
  const targetEnd = cursorPos ?? sessionCursor.end;

  const activeEditor = editingSession.getActiveEditor();

  editingSession.withSuppressedSelectionChange(() => {
    elements.formulaDisplay.innerHTML = coloredHtml;
    elements.cellDisplay.innerHTML = coloredHtml;

    if (editingSession.isActive()) {
      const targetElement =
        activeEditor === "formula"
          ? elements.formulaDisplay
          : elements.cellDisplay;

      if (cursorPos !== undefined) {
        requestAnimationFrame(() => {
          targetElement.focus();
          restoreCursorInElement(targetElement, targetStart, targetEnd);
        });
      } else if (document.activeElement === targetElement) {
        restoreCursorInElement(targetElement, targetStart, targetEnd);
      }
    }
  });
}

// =============================================================================
// Formula Reference Hover
// =============================================================================

/**
 * Set up hover handlers for formula reference spans in a container element.
 * Uses event delegation since spans are created dynamically.
 */
export function setupFormulaRefHover(
  container: HTMLElement,
  app: Pick<App, "hoveredFormulaRefIndex">,
  render: () => void
): void {
  container.addEventListener("mouseover", (e) => {
    const target = e.target as HTMLElement;
    if (target.classList.contains("formula-ref")) {
      const refIndex = parseInt(target.dataset.refIndex ?? "-1", 10);
      if (refIndex >= 0 && app.hoveredFormulaRefIndex !== refIndex) {
        app.hoveredFormulaRefIndex = refIndex;
        render();
      }
    }
  });

  container.addEventListener("mouseout", (e) => {
    const target = e.target as HTMLElement;
    if (target.classList.contains("formula-ref")) {
      const related = e.relatedTarget as HTMLElement;
      if (!related || !related.classList?.contains("formula-ref")) {
        if (app.hoveredFormulaRefIndex !== -1) {
          app.hoveredFormulaRefIndex = -1;
          render();
        }
      }
    }
  });
}
