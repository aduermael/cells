// Pure decisions for Open vs in-document import (unit-testable, no DOM).

/** Subtitle under "Open as new document" — only when collaborating. */
export function newDocumentDropHint(isCollaborating: boolean): string | null {
  return isCollaborating ? "Leaves collaboration room" : null;
}

export type SheetImportMode = "into_current" | "replace" | "new_sheet";

/**
 * Placement for a single-sheet CSV/XLSX drop onto the document (not new-doc zone).
 * Empty active sheet fills in place; otherwise the UI must prompt for replace/new.
 */
export function resolveInDocumentImportMode(
  activeSheetEmpty: boolean,
  userChoice: "replace" | "new_sheet" | "cancel" | null,
): SheetImportMode | "cancel" | "prompt" {
  if (activeSheetEmpty) {
    return "into_current";
  }
  if (userChoice === null) {
    return "prompt";
  }
  return userChoice;
}

/**
 * Whether this format can use in-document CRDT import.
 * Multi-sheet XLSX and ZCD always open as a new document.
 */
export function canImportIntoDocument(
  format: "csv" | "xlsx" | "zcd",
  sourceSheetCount: number = 1,
): boolean {
  if (format === "zcd") {
    return false;
  }
  if (format === "xlsx" && sourceSheetCount > 1) {
    return false;
  }
  return format === "csv" || format === "xlsx";
}
