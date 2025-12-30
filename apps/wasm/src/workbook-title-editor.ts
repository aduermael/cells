// Workbook Title Editor - Editable workbook title in the header
// Handles editing the workbook title with Enter to commit, Escape to cancel

import type { WasmDataSource } from "./wasm-data-source";

/** Configuration for WorkbookTitleEditor */
export interface WorkbookTitleEditorConfig {
  titleElement: HTMLElement;
  onTitleChanged?: (newTitle: string) => void;
}

/**
 * WorkbookTitleEditor - Manages editing of the workbook title
 *
 * The title is rendered as a contenteditable span. When the user:
 * - Clicks on the title: starts editing
 * - Presses Enter: commits the change
 * - Presses Escape: cancels editing, restores original value
 * - Clicks outside: commits the change
 */
export class WorkbookTitleEditor {
  private titleElement: HTMLElement;
  private dataSource: WasmDataSource | null = null;
  private originalValue: string = "";
  private onTitleChanged?: (newTitle: string) => void;

  constructor(config: WorkbookTitleEditorConfig) {
    this.titleElement = config.titleElement;
    this.onTitleChanged = config.onTitleChanged;
    this.setupEventListeners();
  }

  /** Set the data source for saving title changes */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
  }

  /** Get the current title text */
  getTitle(): string {
    return this.titleElement.textContent || "";
  }

  /** Set the title text (used when loading a file) */
  setTitle(title: string): void {
    this.titleElement.textContent = title;
    this.originalValue = title;
  }

  /** Set up event listeners for the title element */
  private setupEventListeners(): void {
    // Focus event - save original value and place cursor at end
    this.titleElement.addEventListener("focus", () => {
      this.originalValue = this.getTitle();
      // Single click places cursor at end
      this.placeCursorAtEnd();
    });

    // Double-click selects all text
    this.titleElement.addEventListener("dblclick", () => {
      this.selectAll();
    });

    // Blur event - commit changes
    this.titleElement.addEventListener("blur", () => {
      this.commitEdit();
    });

    // Keydown event - Enter to commit, Escape to cancel
    this.titleElement.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        this.titleElement.blur();
      } else if (e.key === "Escape") {
        e.preventDefault();
        this.cancelEdit();
      }
    });

    // Prevent line breaks via paste
    this.titleElement.addEventListener("paste", (e) => {
      e.preventDefault();
      const text = e.clipboardData?.getData("text/plain") || "";
      // Remove newlines and insert plain text
      const cleanText = text.replace(/[\r\n]/g, " ").trim();
      document.execCommand("insertText", false, cleanText);
    });

    // Prevent line breaks via Enter in contenteditable
    this.titleElement.addEventListener("keypress", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
      }
    });
  }

  /** Select all text in the title element */
  private selectAll(): void {
    const selection = window.getSelection();
    if (!selection) return;
    const range = document.createRange();
    range.selectNodeContents(this.titleElement);
    selection.removeAllRanges();
    selection.addRange(range);
  }

  /** Place cursor at the end of the title element */
  private placeCursorAtEnd(): void {
    const selection = window.getSelection();
    if (!selection) return;
    const range = document.createRange();
    range.selectNodeContents(this.titleElement);
    range.collapse(false); // false = collapse to end
    selection.removeAllRanges();
    selection.addRange(range);
  }

  /** Commit the current edit */
  private async commitEdit(): Promise<void> {
    let newTitle = this.getTitle().trim();

    // If empty, restore to "Untitled"
    if (!newTitle) {
      newTitle = "Untitled";
      this.titleElement.textContent = newTitle;
    }

    // Only save if changed
    if (newTitle !== this.originalValue) {
      // Update local state
      if (this.dataSource) {
        this.dataSource.setWorkbookName(newTitle);
        // Save to WASM
        await this.dataSource.client.setWorkbookName(newTitle);
      }

      // Notify callback
      if (this.onTitleChanged) {
        this.onTitleChanged(newTitle);
      }
    }

    this.originalValue = newTitle;
  }

  /** Cancel the edit and restore original value */
  private cancelEdit(): void {
    this.titleElement.textContent = this.originalValue;
    this.titleElement.blur();
  }
}
