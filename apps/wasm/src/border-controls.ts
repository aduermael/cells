// =============================================================================
// Border Controls
// =============================================================================
//
// Toolbar UI for cell borders (all borders, outline, top, bottom, left, right, none).
// Provides dropdown button for border operations.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { Position, BorderStyle, CellBorder } from "./types";
import { getMenuStateManager } from "./menu-state";
import { positionDropdown } from "./dropdown-utils";

// =============================================================================
// Types
// =============================================================================

/** Border controls configuration - DOM element references */
export interface BorderControlsConfig {
  borderDropdown: HTMLElement;
  borderDropdownMenu: HTMLElement;
  borderBtn: HTMLButtonElement;
  borderStyleIndicator: SVGElement;
  borderAllBtn: HTMLButtonElement;
  borderOuterBtn: HTMLButtonElement;
  borderTopBtn: HTMLButtonElement;
  borderBottomBtn: HTMLButtonElement;
  borderLeftBtn: HTMLButtonElement;
  borderRightBtn: HTMLButtonElement;
  borderNoneBtn: HTMLButtonElement;
  borderStyleGrid: HTMLElement;
  borderColorPalette: HTMLElement;
  borderColorHexInput: HTMLInputElement;
}

/** Callback signatures */
export interface BorderControlsCallbacks {
  /** Get the currently selected cell position */
  getSelectedCell: () => Position | null;
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Request render after border operation */
  requestRender: () => void;
}

/** Border type for dropdown actions */
export type BorderType = "all" | "outer" | "top" | "bottom" | "left" | "right" | "none";

// =============================================================================
// BorderControls Class
// =============================================================================

/**
 * BorderControls manages the border dropdown in the toolbar.
 */
export class BorderControls {
  private borderDropdown: HTMLElement;
  private borderDropdownMenu: HTMLElement;
  private borderBtn: HTMLButtonElement;
  private borderStyleIndicator: SVGElement;
  private borderAllBtn: HTMLButtonElement;
  private borderOuterBtn: HTMLButtonElement;
  private borderTopBtn: HTMLButtonElement;
  private borderBottomBtn: HTMLButtonElement;
  private borderLeftBtn: HTMLButtonElement;
  private borderRightBtn: HTMLButtonElement;
  private borderNoneBtn: HTMLButtonElement;
  private borderStyleGrid: HTMLElement;
  private borderColorPalette: HTMLElement;
  private borderColorHexInput: HTMLInputElement;

  private dataSource: WasmDataSource | null = null;
  private selectedBorderStyle: BorderStyle = "thin";
  private selectedBorderColor: string = "#000000";

  private getSelectedCell: () => Position | null;
  private getSelectionRange: () => { start: Position | null; end: Position | null };
  private requestRender: () => void;

  constructor(config: BorderControlsConfig, callbacks: BorderControlsCallbacks) {
    this.borderDropdown = config.borderDropdown;
    this.borderDropdownMenu = config.borderDropdownMenu;
    this.borderBtn = config.borderBtn;
    this.borderStyleIndicator = config.borderStyleIndicator;
    this.borderAllBtn = config.borderAllBtn;
    this.borderOuterBtn = config.borderOuterBtn;
    this.borderTopBtn = config.borderTopBtn;
    this.borderBottomBtn = config.borderBottomBtn;
    this.borderLeftBtn = config.borderLeftBtn;
    this.borderRightBtn = config.borderRightBtn;
    this.borderNoneBtn = config.borderNoneBtn;
    this.borderStyleGrid = config.borderStyleGrid;
    this.borderColorPalette = config.borderColorPalette;
    this.borderColorHexInput = config.borderColorHexInput;

    this.getSelectedCell = callbacks.getSelectedCell;
    this.getSelectionRange = callbacks.getSelectionRange;
    this.requestRender = callbacks.requestRender;

    // Register with menu state manager for mutual exclusivity with other menus
    const menuState = getMenuStateManager();
    menuState.registerMenu("border", () => this.closeDropdown());

    this.setupEventListeners();
  }

  /** Set the data source after WASM initialization */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
  }

  private setupEventListeners(): void {
    // Toggle dropdown
    this.borderBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleDropdown();
    });

    // Border options
    this.borderAllBtn.addEventListener("click", async () => {
      await this.applyBorder("all");
      this.closeDropdown();
    });

    this.borderOuterBtn.addEventListener("click", async () => {
      await this.applyBorder("outer");
      this.closeDropdown();
    });

    this.borderTopBtn.addEventListener("click", async () => {
      await this.applyBorder("top");
      this.closeDropdown();
    });

    this.borderBottomBtn.addEventListener("click", async () => {
      await this.applyBorder("bottom");
      this.closeDropdown();
    });

    this.borderLeftBtn.addEventListener("click", async () => {
      await this.applyBorder("left");
      this.closeDropdown();
    });

    this.borderRightBtn.addEventListener("click", async () => {
      await this.applyBorder("right");
      this.closeDropdown();
    });

    this.borderNoneBtn.addEventListener("click", async () => {
      await this.applyBorder("none");
      this.closeDropdown();
    });

    // Border style selection
    this.borderStyleGrid.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const styleBtn = target.closest(".border-style-btn") as HTMLElement | null;
      if (styleBtn) {
        const style = styleBtn.dataset.style as BorderStyle;
        if (style) {
          this.selectBorderStyle(style);
        }
      }
    });

    // Border color palette clicks
    this.borderColorPalette.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const colorOption = target.closest(".border-color-option") as HTMLElement | null;
      if (colorOption) {
        const color = colorOption.dataset.color || "#000000";
        this.selectBorderColor(color);
      }
    });

    // Border color hex input
    this.borderColorHexInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        const color = this.borderColorHexInput.value.trim();
        if (this.isValidHexColor(color)) {
          this.selectBorderColor(color);
          this.borderColorHexInput.value = "";
        }
      }
    });

    // Close dropdown on outside click
    document.addEventListener("click", (e) => {
      const target = e.target as Node;
      if (!this.borderDropdown.contains(target)) {
        this.closeDropdown();
      }
    });

    // Close dropdown on Escape key
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && this.borderDropdown.classList.contains("open")) {
        e.preventDefault();
        this.closeDropdown();
      }
    });
  }

  /**
   * Check if a string is a valid hex color.
   */
  private isValidHexColor(color: string): boolean {
    return /^#[0-9A-Fa-f]{6}$/.test(color) || /^#[0-9A-Fa-f]{3}$/.test(color);
  }

  /**
   * Select a border color and update the UI.
   */
  private selectBorderColor(color: string): void {
    this.selectedBorderColor = color;

    // Update selected state in color palette
    const options = this.borderColorPalette.querySelectorAll(".border-color-option");
    options.forEach((opt) => {
      const optColor = (opt as HTMLElement).dataset.color || "";
      opt.classList.toggle("selected", optColor.toUpperCase() === color.toUpperCase());
    });

    // Update the style indicator color
    this.updateStyleIndicatorColor(color);
  }

  /**
   * Select a border style and update the UI.
   */
  private selectBorderStyle(style: BorderStyle): void {
    this.selectedBorderStyle = style;

    // Update selected state in dropdown grid
    const buttons = this.borderStyleGrid.querySelectorAll(".border-style-btn");
    buttons.forEach((btn) => {
      const btnStyle = (btn as HTMLElement).dataset.style;
      btn.classList.toggle("selected", btnStyle === style);
    });

    // Update the style indicator on the main button
    this.updateStyleIndicator(style);
  }

  /**
   * Update the style indicator SVG to show the selected border style.
   */
  private updateStyleIndicator(style: BorderStyle): void {
    const color = this.selectedBorderColor;

    // For double style, show two lines
    if (style === "double") {
      this.borderStyleIndicator.innerHTML =
        `<line x1="0" y1="1" x2="14" y2="1" stroke="${color}" stroke-width="1"/>` +
        `<line x1="0" y1="3" x2="14" y2="3" stroke="${color}" stroke-width="1"/>`;
      return;
    }

    // For single-line styles, ensure we have one line element
    let line = this.borderStyleIndicator.querySelector("line");
    if (!line || this.borderStyleIndicator.querySelectorAll("line").length !== 1) {
      this.borderStyleIndicator.innerHTML =
        `<line x1="0" y1="2" x2="14" y2="2" stroke="${color}" stroke-width="1"/>`;
      line = this.borderStyleIndicator.querySelector("line")!;
    }

    // Reset attributes
    line.removeAttribute("stroke-dasharray");
    line.setAttribute("stroke-width", "1");
    line.setAttribute("stroke", color);

    // Apply style-specific attributes
    switch (style) {
      case "thin":
        // Default stroke-width is already 1
        break;
      case "medium":
        line.setAttribute("stroke-width", "2");
        break;
      case "thick":
        line.setAttribute("stroke-width", "3");
        break;
      case "dashed":
        line.setAttribute("stroke-dasharray", "4 2");
        break;
      case "dotted":
        line.setAttribute("stroke-dasharray", "1 2");
        break;
    }
  }

  /**
   * Update the style indicator color without changing the style.
   */
  private updateStyleIndicatorColor(color: string): void {
    const lines = this.borderStyleIndicator.querySelectorAll("line");
    lines.forEach((line) => {
      line.setAttribute("stroke", color);
    });
  }

  private toggleDropdown(): void {
    const isOpen = this.borderDropdown.classList.contains("open");
    if (isOpen) {
      this.closeDropdown();
    } else {
      this.openDropdown();
    }
  }

  private openDropdown(): void {
    // Notify MenuStateManager - this will close other menus
    const menuState = getMenuStateManager();
    menuState.openMenu("border");

    this.borderDropdown.classList.add("open");
    // Position the dropdown menu to stay within viewport bounds
    const buttonRect = this.borderBtn.getBoundingClientRect();
    positionDropdown(this.borderDropdownMenu, buttonRect);
  }

  private closeDropdown(): void {
    this.borderDropdown.classList.remove("open");
    // Notify MenuStateManager that this menu is now closed
    const menuState = getMenuStateManager();
    menuState.closeMenu("border");
  }

  /**
   * Create a border edge object with the selected style and color.
   */
  private createBorderEdge(): { style: BorderStyle; color: string } {
    return { style: this.selectedBorderStyle, color: this.selectedBorderColor };
  }

  /**
   * Create a border object with no borders (all edges set to none).
   */
  private createNoBorderEdge(): { style: BorderStyle; color: string } {
    return { style: "none" as BorderStyle, color: "" };
  }

  /**
   * Apply borders to the current selection.
   * Uses Range system for multi-cell selections (except outline which needs per-cell logic).
   * Uses cell-level styling for single-cell selections.
   */
  private async applyBorder(borderType: BorderType): Promise<void> {
    if (!this.dataSource) {
      console.error("BorderControls: dataSource is null, cannot apply border");
      return;
    }

    const { start, end } = this.getSelectionRange();
    const cell = this.getSelectedCell();

    // Check if this is a single cell or range selection
    const isSingleCell =
      !start || !end || (start.col === end.col && start.row === end.row);

    if (isSingleCell) {
      // Single cell: use cell-level styling
      if (cell) {
        await this.applySingleCellBorder(cell.col, cell.row, borderType);
      }
    } else {
      // Range selection: use Range system where possible
      const minCol = Math.min(start!.col, end!.col);
      const maxCol = Math.max(start!.col, end!.col);
      const minRow = Math.min(start!.row, end!.row);
      const maxRow = Math.max(start!.row, end!.row);

      await this.applyRangeBorder(minCol, minRow, maxCol, maxRow, borderType);
    }

    this.requestRender();
  }

  /**
   * Apply border to a single cell.
   */
  private async applySingleCellBorder(
    col: number,
    row: number,
    borderType: BorderType,
  ): Promise<void> {
    const edge = this.createBorderEdge();
    const noEdge = this.createNoBorderEdge();

    let border: CellBorder;

    switch (borderType) {
      case "all":
      case "outer":
        // For single cell, all and outer are the same
        border = { top: edge, right: edge, bottom: edge, left: edge };
        break;
      case "none":
        border = { top: noEdge, right: noEdge, bottom: noEdge, left: noEdge };
        break;
      case "top":
        border = { top: edge, right: noEdge, bottom: noEdge, left: noEdge };
        break;
      case "bottom":
        border = { top: noEdge, right: noEdge, bottom: edge, left: noEdge };
        break;
      case "left":
        border = { top: noEdge, right: noEdge, bottom: noEdge, left: edge };
        break;
      case "right":
        border = { top: noEdge, right: edge, bottom: noEdge, left: noEdge };
        break;
    }

    await this.dataSource!.setCellStyleAt(col, row, { border });
  }

  /**
   * Apply border to a range using the Range system.
   * For uniform borders (all, none, single edges), uses setStyleForRange.
   * For outline, falls back to per-cell logic since edge cells need different borders.
   */
  private async applyRangeBorder(
    minCol: number,
    minRow: number,
    maxCol: number,
    maxRow: number,
    borderType: BorderType,
  ): Promise<void> {
    const edge = this.createBorderEdge();
    const noEdge = this.createNoBorderEdge();

    if (borderType === "outer") {
      // Outline requires different borders on different cells - use per-cell logic
      await this.applyOutlineBorder(minCol, minRow, maxCol, maxRow);
      return;
    }

    // For uniform styles, use Range system
    let border: CellBorder;

    switch (borderType) {
      case "all":
        border = { top: edge, right: edge, bottom: edge, left: edge };
        break;
      case "none":
        border = { top: noEdge, right: noEdge, bottom: noEdge, left: noEdge };
        break;
      case "top":
        border = { top: edge, right: noEdge, bottom: noEdge, left: noEdge };
        break;
      case "bottom":
        border = { top: noEdge, right: noEdge, bottom: edge, left: noEdge };
        break;
      case "left":
        border = { top: noEdge, right: noEdge, bottom: noEdge, left: edge };
        break;
      case "right":
        border = { top: noEdge, right: edge, bottom: noEdge, left: noEdge };
        break;
    }

    // Use Range system for efficient range-based styling
    await this.dataSource!.setStyleForRange(minCol, minRow, maxCol, maxRow, { border });
  }

  /**
   * Apply outline border to a range.
   * This requires per-cell logic since edge cells get different borders than interior cells.
   * Only edge cells of the range get borders on their outer edges.
   */
  private async applyOutlineBorder(
    minCol: number,
    minRow: number,
    maxCol: number,
    maxRow: number,
  ): Promise<void> {
    const edge = this.createBorderEdge();
    const noEdge = this.createNoBorderEdge();

    for (let col = minCol; col <= maxCol; col++) {
      for (let row = minRow; row <= maxRow; row++) {
        const border: CellBorder = {
          top: row === minRow ? edge : noEdge,
          bottom: row === maxRow ? edge : noEdge,
          left: col === minCol ? edge : noEdge,
          right: col === maxCol ? edge : noEdge,
        };
        await this.dataSource!.setCellStyleAt(col, row, { border });
      }
    }
  }
}
