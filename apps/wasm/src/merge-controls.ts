// =============================================================================
// Merge Controls
// =============================================================================
//
// Toolbar UI for cell merging (merge cells, unmerge cells).
// Provides dropdown button for merge operations.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Merge All button (merge all selected cells)
// - Merge Horizontally button (merge rows individually)
// - Unmerge button (unmerge cells at current selection)
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { Position } from "./types";

// =============================================================================
// Types
// =============================================================================

/** Merge controls configuration - DOM element references */
export interface MergeControlsConfig {
  mergeDropdown: HTMLElement;
  mergeBtn: HTMLButtonElement;
  mergeAllBtn: HTMLButtonElement;
  mergeHorizontalBtn: HTMLButtonElement;
  unmergeBtn: HTMLButtonElement;
}

/** Callback signatures */
export interface MergeControlsCallbacks {
  /** Get the currently selected cell position */
  getSelectedCell: () => Position | null;
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Request render after merge operation */
  requestRender: () => void;
}

// =============================================================================
// MergeControls Class
// =============================================================================

/**
 * MergeControls manages the merge/unmerge dropdown in the toolbar.
 */
export class MergeControls {
  private mergeDropdown: HTMLElement;
  private mergeBtn: HTMLButtonElement;
  private mergeAllBtn: HTMLButtonElement;
  private mergeHorizontalBtn: HTMLButtonElement;
  private unmergeBtn: HTMLButtonElement;

  private dataSource: WasmDataSource | null = null;

  private getSelectedCell: () => Position | null;
  private getSelectionRange: () => { start: Position | null; end: Position | null };
  private requestRender: () => void;

  constructor(config: MergeControlsConfig, callbacks: MergeControlsCallbacks) {
    this.mergeDropdown = config.mergeDropdown;
    this.mergeBtn = config.mergeBtn;
    this.mergeAllBtn = config.mergeAllBtn;
    this.mergeHorizontalBtn = config.mergeHorizontalBtn;
    this.unmergeBtn = config.unmergeBtn;

    this.getSelectedCell = callbacks.getSelectedCell;
    this.getSelectionRange = callbacks.getSelectionRange;
    this.requestRender = callbacks.requestRender;

    this.setupEventListeners();
  }

  /** Set the data source after WASM initialization */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
  }

  private setupEventListeners(): void {
    // Toggle dropdown
    this.mergeBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleDropdown();
    });

    // Merge All
    this.mergeAllBtn.addEventListener("click", async () => {
      await this.mergeAll();
      this.closeDropdown();
    });

    // Merge Horizontally
    this.mergeHorizontalBtn.addEventListener("click", async () => {
      await this.mergeHorizontally();
      this.closeDropdown();
    });

    // Unmerge
    this.unmergeBtn.addEventListener("click", async () => {
      await this.unmerge();
      this.closeDropdown();
    });

    // Close dropdown on outside click
    document.addEventListener("click", (e) => {
      const target = e.target as Node;
      if (!this.mergeDropdown.contains(target)) {
        this.closeDropdown();
      }
    });
  }

  private toggleDropdown(): void {
    const isOpen = this.mergeDropdown.classList.contains("open");
    if (isOpen) {
      this.closeDropdown();
    } else {
      this.mergeDropdown.classList.add("open");
    }
  }

  private closeDropdown(): void {
    this.mergeDropdown.classList.remove("open");
  }

  /**
   * Merge all cells in the current selection into a single cell.
   */
  private async mergeAll(): Promise<void> {
    if (!this.dataSource) {
      console.error("MergeControls: dataSource is null, cannot merge");
      return;
    }

    const { start, end } = this.getSelectionRange();
    const cell = this.getSelectedCell();

    // If no range selection, we need at least a selection start
    if (!start || !end) {
      if (!cell) return;
      // Single cell selected - can't merge
      console.warn("Cannot merge a single cell");
      return;
    }

    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    // Single cell selection - can't merge
    if (minCol === maxCol && minRow === maxRow) {
      console.warn("Cannot merge a single cell");
      return;
    }

    try {
      await this.dataSource.mergeCells(minCol, minRow, maxCol, maxRow);
      this.requestRender();
    } catch (error) {
      console.error("Failed to merge cells:", error);
    }
  }

  /**
   * Merge cells horizontally (merge each row in the selection separately).
   */
  private async mergeHorizontally(): Promise<void> {
    if (!this.dataSource) return;

    const { start, end } = this.getSelectionRange();
    if (!start || !end) return;

    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    // Need at least 2 columns to merge horizontally
    if (minCol === maxCol) {
      console.warn("Need at least 2 columns to merge horizontally");
      return;
    }

    try {
      // Merge each row separately
      for (let row = minRow; row <= maxRow; row++) {
        await this.dataSource.mergeCells(minCol, row, maxCol, row);
      }
      this.requestRender();
    } catch (error) {
      console.error("Failed to merge cells horizontally:", error);
    }
  }

  /**
   * Unmerge the merged region containing the current selection.
   */
  private async unmerge(): Promise<void> {
    if (!this.dataSource) return;

    const cell = this.getSelectedCell();
    if (!cell) return;

    try {
      await this.dataSource.unmergeCells(cell.col, cell.row);
      this.requestRender();
    } catch (error) {
      console.error("Failed to unmerge cells:", error);
    }
  }
}
