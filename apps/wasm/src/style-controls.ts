// =============================================================================
// Style Controls
// =============================================================================
//
// Toolbar UI for cell styling (bold, italic, underline, colors).
// Provides buttons for text formatting and color pickers.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Bold/Italic/Underline toggle buttons
// - Background color picker
// - Text color picker
// - Update button states based on current cell's style
// - Apply styles to selected cells
//
// Style flow:
// - User clicks style button → StyleControls → WasmDataSource.setCellStyleAt()
// - C++ applies CRDT op → notifies change → GridRenderer re-renders cell
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { CellStyle, Position, CellData } from "./types";

// =============================================================================
// Types
// =============================================================================

/** Style controls configuration - DOM element references */
export interface StyleControlsConfig {
  styleControls: HTMLElement;
  boldBtn: HTMLButtonElement;
  italicBtn: HTMLButtonElement;
  underlineBtn: HTMLButtonElement;
  bgColorWrapper: HTMLElement;
  bgColorBtn: HTMLButtonElement;
  bgColorSwatch: HTMLElement;
  bgColorPopup: HTMLElement;
  bgColorHexInput: HTMLInputElement;
  textColorWrapper: HTMLElement;
  textColorBtn: HTMLButtonElement;
  textColorSwatch: HTMLElement;
  textColorPopup: HTMLElement;
  textColorHexInput: HTMLInputElement;
}

/** Callback signatures */
export interface StyleControlsCallbacks {
  /** Get the currently selected cell position */
  getSelectedCell: () => Position | null;
  /** Get the selected cell data (for current style) */
  getSelectedCellData: () => CellData | null;
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Request render after style change */
  requestRender: () => void;
  /** Update the formula bar display */
  updateFormulaBar: () => void;
}

// =============================================================================
// StyleControls Class
// =============================================================================

/**
 * StyleControls manages the style buttons in the formula bar.
 *
 * Responsibilities:
 * - Display current cell style in buttons (active state)
 * - Handle B/I/U toggle clicks
 * - Handle color picker interactions
 * - Apply styles to selected cells
 */
export class StyleControls {
  // =========================================================================
  // Elements
  // =========================================================================

  private boldBtn: HTMLButtonElement;
  private italicBtn: HTMLButtonElement;
  private underlineBtn: HTMLButtonElement;
  private bgColorWrapper: HTMLElement;
  private bgColorBtn: HTMLButtonElement;
  private bgColorSwatch: HTMLElement;
  private bgColorPopup: HTMLElement;
  private bgColorHexInput: HTMLInputElement;
  private textColorWrapper: HTMLElement;
  private textColorBtn: HTMLButtonElement;
  private textColorSwatch: HTMLElement;
  private textColorPopup: HTMLElement;
  private textColorHexInput: HTMLInputElement;

  // =========================================================================
  // Dependencies
  // =========================================================================

  private dataSource: WasmDataSource | null = null;

  // =========================================================================
  // Callbacks
  // =========================================================================

  private getSelectedCell: () => Position | null;
  private getSelectedCellData: () => CellData | null;
  private getSelectionRange: () => { start: Position | null; end: Position | null };
  private requestRender: () => void;
  private updateFormulaBar: () => void;

  // =========================================================================
  // State
  // =========================================================================

  private currentStyle: Partial<CellStyle> = {};

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(
    config: StyleControlsConfig,
    callbacks: StyleControlsCallbacks
  ) {
    // config.styleControls is available but unused - stored in DOM reference only
    this.boldBtn = config.boldBtn;
    this.italicBtn = config.italicBtn;
    this.underlineBtn = config.underlineBtn;
    this.bgColorWrapper = config.bgColorWrapper;
    this.bgColorBtn = config.bgColorBtn;
    this.bgColorSwatch = config.bgColorSwatch;
    this.bgColorPopup = config.bgColorPopup;
    this.bgColorHexInput = config.bgColorHexInput;
    this.textColorWrapper = config.textColorWrapper;
    this.textColorBtn = config.textColorBtn;
    this.textColorSwatch = config.textColorSwatch;
    this.textColorPopup = config.textColorPopup;
    this.textColorHexInput = config.textColorHexInput;

    this.getSelectedCell = callbacks.getSelectedCell;
    this.getSelectedCellData = callbacks.getSelectedCellData;
    this.getSelectionRange = callbacks.getSelectionRange;
    this.requestRender = callbacks.requestRender;
    this.updateFormulaBar = callbacks.updateFormulaBar;

    this.setupEventListeners();
  }

  // =========================================================================
  // Public Methods
  // =========================================================================

  /** Set the data source after WASM initialization */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
  }

  /** Update the displayed style for the current cell selection */
  async updateForCurrentCell(): Promise<void> {
    const cellData = this.getSelectedCellData();

    if (!cellData || !this.dataSource) {
      // No cell selected or no data source - reset to defaults
      this.setDisplayedStyle({
        bold: false,
        italic: false,
        underline: false,
        bgColor: "",
        textColor: "",
      });
      return;
    }

    // Get style from cell data via WASM
    if (cellData.styleId && cellData.styleId !== "~") {
      const styleJson = await this.dataSource.getCellStyleAt(
        cellData.col,
        cellData.row
      );
      if (styleJson) {
        this.setDisplayedStyle(styleJson);
        return;
      }
    }

    // No style - show defaults
    this.setDisplayedStyle({
      bold: false,
      italic: false,
      underline: false,
      bgColor: "",
      textColor: "",
    });
  }

  // =========================================================================
  // Private Methods - Setup
  // =========================================================================

  private setupEventListeners(): void {
    // Bold button
    this.boldBtn.addEventListener("click", () => {
      this.toggleStyle("bold");
    });

    // Italic button
    this.italicBtn.addEventListener("click", () => {
      this.toggleStyle("italic");
    });

    // Underline button
    this.underlineBtn.addEventListener("click", () => {
      this.toggleStyle("underline");
    });

    // Background color button - toggle popup
    this.bgColorBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleColorPopup("bg");
    });

    // Text color button - toggle popup
    this.textColorBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleColorPopup("text");
    });

    // Background color palette clicks
    this.bgColorPopup.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const colorOption = target.closest(".color-option") as HTMLElement;
      if (colorOption) {
        const color = colorOption.dataset.color || "";
        this.applyBgColor(color);
        this.closeColorPopups();
      }
    });

    // Text color palette clicks
    this.textColorPopup.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const colorOption = target.closest(".color-option") as HTMLElement;
      if (colorOption) {
        const color = colorOption.dataset.color || "";
        this.applyTextColor(color);
        this.closeColorPopups();
      }
    });

    // Background hex input
    this.bgColorHexInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        const color = this.bgColorHexInput.value.trim();
        if (this.isValidHexColor(color)) {
          this.applyBgColor(color);
          this.closeColorPopups();
        }
      } else if (e.key === "Escape") {
        this.closeColorPopups();
      }
    });

    // Text hex input
    this.textColorHexInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        const color = this.textColorHexInput.value.trim();
        if (this.isValidHexColor(color)) {
          this.applyTextColor(color);
          this.closeColorPopups();
        }
      } else if (e.key === "Escape") {
        this.closeColorPopups();
      }
    });

    // Close popups on outside click
    document.addEventListener("click", (e) => {
      const target = e.target as Node;
      if (
        !this.bgColorWrapper.contains(target) &&
        !this.textColorWrapper.contains(target)
      ) {
        this.closeColorPopups();
      }
    });

    // Keyboard shortcuts for style toggles
    document.addEventListener("keydown", (e) => {
      // Only handle if no input/textarea is focused
      const active = document.activeElement;
      if (
        active &&
        (active.tagName === "INPUT" ||
          active.tagName === "TEXTAREA" ||
          (active as HTMLElement).contentEditable === "true")
      ) {
        return;
      }

      // Cmd/Ctrl + B/I/U for bold/italic/underline
      if (e.metaKey || e.ctrlKey) {
        switch (e.key.toLowerCase()) {
          case "b":
            e.preventDefault();
            this.toggleStyle("bold");
            break;
          case "i":
            e.preventDefault();
            this.toggleStyle("italic");
            break;
          case "u":
            e.preventDefault();
            this.toggleStyle("underline");
            break;
        }
      }
    });
  }

  // =========================================================================
  // Private Methods - Style Operations
  // =========================================================================

  private async toggleStyle(property: "bold" | "italic" | "underline"): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    // Toggle the current value
    const newValue = !this.currentStyle[property];

    // Build partial style update
    const styleUpdate: Partial<CellStyle> = {
      [property]: newValue,
    };

    try {
      // Apply to all cells in selection range
      await this.applyStyleToSelection(styleUpdate);

      // Update button state
      this.currentStyle[property] = newValue;
      this.updateButtonState(property, newValue);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error(`Failed to toggle ${property}:`, error);
    }
  }

  private async applyBgColor(color: string): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { bgColor: color };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.bgColor = color;
      this.updateBgColorSwatch(color);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply background color:", error);
    }
  }

  private async applyTextColor(color: string): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { textColor: color };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.textColor = color;
      this.updateTextColorSwatch(color);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply text color:", error);
    }
  }

  /**
   * Apply a style update to all cells in the current selection range.
   */
  private async applyStyleToSelection(styleUpdate: Partial<CellStyle>): Promise<void> {
    if (!this.dataSource) return;

    const { start, end } = this.getSelectionRange();
    const cell = this.getSelectedCell();

    // If no range selection, just apply to selected cell
    if (!start || !end || (start.col === end.col && start.row === end.row)) {
      if (cell) {
        await this.dataSource.setCellStyleAt(cell.col, cell.row, styleUpdate);
      }
      return;
    }

    // Apply to all cells in range
    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    for (let col = minCol; col <= maxCol; col++) {
      for (let row = minRow; row <= maxRow; row++) {
        await this.dataSource.setCellStyleAt(col, row, styleUpdate);
      }
    }
  }

  // =========================================================================
  // Private Methods - UI Updates
  // =========================================================================

  private setDisplayedStyle(style: Partial<CellStyle>): void {
    this.currentStyle = style;

    // Update button states
    this.updateButtonState("bold", !!style.bold);
    this.updateButtonState("italic", !!style.italic);
    this.updateButtonState("underline", !!style.underline);

    // Update color swatches
    this.updateBgColorSwatch(style.bgColor || "");
    this.updateTextColorSwatch(style.textColor || "");
  }

  private updateButtonState(
    property: "bold" | "italic" | "underline",
    active: boolean
  ): void {
    const btn =
      property === "bold"
        ? this.boldBtn
        : property === "italic"
          ? this.italicBtn
          : this.underlineBtn;

    btn.classList.toggle("active", active);
  }

  private updateBgColorSwatch(color: string): void {
    if (color) {
      this.bgColorSwatch.style.background = color;
      this.bgColorSwatch.style.border = "none";
    } else {
      this.bgColorSwatch.style.background = "transparent";
      this.bgColorSwatch.style.border = "1px solid var(--color-border)";
    }

    // Update palette selection
    this.updatePaletteSelection(this.bgColorPopup, color);
  }

  private updateTextColorSwatch(color: string): void {
    const displayColor = color || "#000000";
    this.textColorSwatch.style.background = displayColor;

    // Update palette selection
    this.updatePaletteSelection(this.textColorPopup, color);
  }

  private updatePaletteSelection(popup: HTMLElement, color: string): void {
    const options = popup.querySelectorAll(".color-option");
    options.forEach((option) => {
      const optionColor = (option as HTMLElement).dataset.color || "";
      option.classList.toggle(
        "selected",
        optionColor.toUpperCase() === color.toUpperCase()
      );
    });
  }

  // =========================================================================
  // Private Methods - Color Picker
  // =========================================================================

  private toggleColorPopup(type: "bg" | "text"): void {
    const wrapper = type === "bg" ? this.bgColorWrapper : this.textColorWrapper;
    const isOpen = wrapper.classList.contains("open");

    // Close all popups first
    this.closeColorPopups();

    // Toggle the clicked one
    if (!isOpen) {
      wrapper.classList.add("open");
    }
  }

  private closeColorPopups(): void {
    this.bgColorWrapper.classList.remove("open");
    this.textColorWrapper.classList.remove("open");
  }

  private isValidHexColor(color: string): boolean {
    return /^#[0-9A-Fa-f]{6}$/.test(color) || /^#[0-9A-Fa-f]{3}$/.test(color);
  }
}
