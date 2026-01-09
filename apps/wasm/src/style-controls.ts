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
  // Font controls
  fontFamilyDropdown: HTMLElement;
  fontFamilyBtn: HTMLButtonElement;
  fontFamilyLabel: HTMLElement;
  fontFamilyMenu: HTMLElement;
  fontSizeDropdown: HTMLElement;
  fontSizeBtn: HTMLButtonElement;
  fontSizeLabel: HTMLElement;
  fontSizeMenu: HTMLElement;
  // Alignment controls
  hAlignGroup: HTMLElement;
  alignLeftBtn: HTMLButtonElement;
  alignCenterBtn: HTMLButtonElement;
  alignRightBtn: HTMLButtonElement;
  vAlignGroup: HTMLElement;
  valignTopBtn: HTMLButtonElement;
  valignMiddleBtn: HTMLButtonElement;
  valignBottomBtn: HTMLButtonElement;
}

/** Callback signatures */
export interface StyleControlsCallbacks {
  /** Get the currently selected cell position */
  getSelectedCell: () => Position | null;
  /** Get the selected cell data (for current style) */
  getSelectedCellData: () => CellData | null;
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Get cell data at a specific position */
  getCellDataAt: (col: number, row: number) => CellData | null;
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
  private fontFamilyDropdown: HTMLElement;
  private fontFamilyBtn: HTMLButtonElement;
  private fontFamilyLabel: HTMLElement;
  private fontFamilyMenu: HTMLElement;
  private fontSizeDropdown: HTMLElement;
  private fontSizeBtn: HTMLButtonElement;
  private fontSizeLabel: HTMLElement;
  private fontSizeMenu: HTMLElement;
  // Alignment controls
  private alignLeftBtn: HTMLButtonElement;
  private alignCenterBtn: HTMLButtonElement;
  private alignRightBtn: HTMLButtonElement;
  private valignTopBtn: HTMLButtonElement;
  private valignMiddleBtn: HTMLButtonElement;
  private valignBottomBtn: HTMLButtonElement;

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
  private getCellDataAt: (col: number, row: number) => CellData | null;
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
    this.fontFamilyDropdown = config.fontFamilyDropdown;
    this.fontFamilyBtn = config.fontFamilyBtn;
    this.fontFamilyLabel = config.fontFamilyLabel;
    this.fontFamilyMenu = config.fontFamilyMenu;
    this.fontSizeDropdown = config.fontSizeDropdown;
    this.fontSizeBtn = config.fontSizeBtn;
    this.fontSizeLabel = config.fontSizeLabel;
    this.fontSizeMenu = config.fontSizeMenu;
    // Alignment controls (hAlignGroup and vAlignGroup containers are passed but not stored)
    this.alignLeftBtn = config.alignLeftBtn;
    this.alignCenterBtn = config.alignCenterBtn;
    this.alignRightBtn = config.alignRightBtn;
    this.valignTopBtn = config.valignTopBtn;
    this.valignMiddleBtn = config.valignMiddleBtn;
    this.valignBottomBtn = config.valignBottomBtn;

    this.getSelectedCell = callbacks.getSelectedCell;
    this.getSelectedCellData = callbacks.getSelectedCellData;
    this.getSelectionRange = callbacks.getSelectionRange;
    this.getCellDataAt = callbacks.getCellDataAt;
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

    // Check if selection has mixed styles
    const { start, end } = this.getSelectionRange();
    if (start && end && (start.col !== end.col || start.row !== end.row)) {
      const mixedProps = await this.checkMixedStyles(start, end);
      if (mixedProps) {
        this.setDisplayedStyle(mixedProps.style, mixedProps.mixed);
        return;
      }
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

  /**
   * Check if cells in the selection range have different styles.
   * Returns the style of the first cell and which properties are mixed.
   */
  private async checkMixedStyles(start: Position, end: Position): Promise<{
    style: Partial<CellStyle>;
    mixed: Partial<Record<keyof CellStyle, boolean>>;
  } | null> {
    if (!this.dataSource) return null;

    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    // Get the style of the first cell (anchor)
    const firstCell = this.getCellDataAt(minCol, minRow);
    let firstStyle: Partial<CellStyle> = {};
    if (firstCell?.styleId && firstCell.styleId !== "~") {
      const style = await this.dataSource.getCellStyleAt(minCol, minRow);
      if (style) {
        firstStyle = style;
      }
    }

    // Track which properties are mixed
    const mixed: Partial<Record<keyof CellStyle, boolean>> = {};

    // Check all cells in range
    for (let col = minCol; col <= maxCol; col++) {
      for (let row = minRow; row <= maxRow; row++) {
        if (col === minCol && row === minRow) continue; // Skip first cell

        const cell = this.getCellDataAt(col, row);
        let cellStyle: Partial<CellStyle> = {};
        if (cell?.styleId && cell.styleId !== "~") {
          const style = await this.dataSource.getCellStyleAt(col, row);
          if (style) {
            cellStyle = style;
          }
        }

        // Compare each style property
        if (!!cellStyle.bold !== !!firstStyle.bold) mixed.bold = true;
        if (!!cellStyle.italic !== !!firstStyle.italic) mixed.italic = true;
        if (!!cellStyle.underline !== !!firstStyle.underline) mixed.underline = true;
        if ((cellStyle.bgColor || "") !== (firstStyle.bgColor || "")) mixed.bgColor = true;
        if ((cellStyle.textColor || "") !== (firstStyle.textColor || "")) mixed.textColor = true;
        if ((cellStyle.fontFamily || "") !== (firstStyle.fontFamily || "")) mixed.fontFamily = true;
        if ((cellStyle.fontSize || 0) !== (firstStyle.fontSize || 0)) mixed.fontSize = true;
        if ((cellStyle.hAlign || "left") !== (firstStyle.hAlign || "left")) mixed.hAlign = true;
        if ((cellStyle.vAlign || "top") !== (firstStyle.vAlign || "top")) mixed.vAlign = true;
      }
    }

    // If any property is mixed, return the result
    if (Object.keys(mixed).length > 0) {
      return { style: firstStyle, mixed };
    }

    return null;
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
      if (
        !this.fontFamilyDropdown.contains(target) &&
        !this.fontSizeDropdown.contains(target)
      ) {
        this.closeFontDropdowns();
      }
    });

    // Font family dropdown toggle
    this.fontFamilyBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleFontDropdown("family");
    });

    // Font size dropdown toggle
    this.fontSizeBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleFontDropdown("size");
    });

    // Font family selection
    this.fontFamilyMenu.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const fontItem = target.closest("[data-font]") as HTMLElement;
      if (fontItem) {
        const fontFamily = fontItem.dataset.font || "Arial";
        this.applyFontFamily(fontFamily);
        this.closeFontDropdowns();
      }
    });

    // Font size selection
    this.fontSizeMenu.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const sizeItem = target.closest("[data-size]") as HTMLElement;
      if (sizeItem) {
        const fontSize = parseInt(sizeItem.dataset.size || "12", 10);
        this.applyFontSize(fontSize);
        this.closeFontDropdowns();
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

    // Horizontal alignment buttons
    this.alignLeftBtn.addEventListener("click", () => {
      this.applyHAlign("left");
    });
    this.alignCenterBtn.addEventListener("click", () => {
      this.applyHAlign("center");
    });
    this.alignRightBtn.addEventListener("click", () => {
      this.applyHAlign("right");
    });

    // Vertical alignment buttons
    this.valignTopBtn.addEventListener("click", () => {
      this.applyVAlign("top");
    });
    this.valignMiddleBtn.addEventListener("click", () => {
      this.applyVAlign("middle");
    });
    this.valignBottomBtn.addEventListener("click", () => {
      this.applyVAlign("bottom");
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

  private async applyHAlign(hAlign: "left" | "center" | "right"): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { hAlign };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.hAlign = hAlign;
      this.updateHAlignButtons(hAlign);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply horizontal alignment:", error);
    }
  }

  private async applyVAlign(vAlign: "top" | "middle" | "bottom"): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { vAlign };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.vAlign = vAlign;
      this.updateVAlignButtons(vAlign);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply vertical alignment:", error);
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

  private setDisplayedStyle(
    style: Partial<CellStyle>,
    mixed?: Partial<Record<keyof CellStyle, boolean>>
  ): void {
    this.currentStyle = style;

    // Update button states (with mixed/indeterminate support)
    this.updateButtonState("bold", !!style.bold, mixed?.bold);
    this.updateButtonState("italic", !!style.italic, mixed?.italic);
    this.updateButtonState("underline", !!style.underline, mixed?.underline);

    // Update color swatches (show mixed indicator if colors differ)
    this.updateBgColorSwatch(style.bgColor || "", mixed?.bgColor);
    this.updateTextColorSwatch(style.textColor || "", mixed?.textColor);

    // Update font dropdowns
    this.updateFontFamilyDisplay(style.fontFamily || "Arial", mixed?.fontFamily);
    this.updateFontSizeDisplay(style.fontSize || 12, mixed?.fontSize);

    // Update alignment buttons (TextAlign includes "justify", but we only show left/center/right)
    const hAlign = style.hAlign === "justify" ? "left" : (style.hAlign || "left");
    this.updateHAlignButtons(hAlign as "left" | "center" | "right", mixed?.hAlign);
    this.updateVAlignButtons(style.vAlign || "top", mixed?.vAlign);
  }

  private updateButtonState(
    property: "bold" | "italic" | "underline",
    active: boolean,
    isMixed?: boolean
  ): void {
    const btn =
      property === "bold"
        ? this.boldBtn
        : property === "italic"
          ? this.italicBtn
          : this.underlineBtn;

    btn.classList.toggle("active", active && !isMixed);
    btn.classList.toggle("mixed", !!isMixed);
  }

  private updateBgColorSwatch(color: string, isMixed?: boolean): void {
    if (isMixed) {
      // Show mixed indicator (diagonal stripes)
      this.bgColorSwatch.style.background = "repeating-linear-gradient(45deg, #ccc, #ccc 2px, #fff 2px, #fff 4px)";
      this.bgColorSwatch.style.border = "1px solid var(--color-border)";
    } else if (color) {
      this.bgColorSwatch.style.background = color;
      this.bgColorSwatch.style.border = "none";
    } else {
      this.bgColorSwatch.style.background = "transparent";
      this.bgColorSwatch.style.border = "1px solid var(--color-border)";
    }

    // Update palette selection
    this.updatePaletteSelection(this.bgColorPopup, isMixed ? "" : color);
  }

  private updateTextColorSwatch(color: string, isMixed?: boolean): void {
    if (isMixed) {
      // Show mixed indicator (diagonal stripes)
      this.textColorSwatch.style.background = "repeating-linear-gradient(45deg, #333, #333 2px, #666 2px, #666 4px)";
    } else {
      const displayColor = color || "#000000";
      this.textColorSwatch.style.background = displayColor;
    }

    // Update palette selection
    this.updatePaletteSelection(this.textColorPopup, isMixed ? "" : color);
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

  private updateHAlignButtons(hAlign: "left" | "center" | "right", isMixed?: boolean): void {
    // When mixed, don't highlight any button
    this.alignLeftBtn.classList.toggle("active", !isMixed && hAlign === "left");
    this.alignCenterBtn.classList.toggle("active", !isMixed && hAlign === "center");
    this.alignRightBtn.classList.toggle("active", !isMixed && hAlign === "right");
  }

  private updateVAlignButtons(vAlign: "top" | "middle" | "bottom", isMixed?: boolean): void {
    // When mixed, don't highlight any button
    this.valignTopBtn.classList.toggle("active", !isMixed && vAlign === "top");
    this.valignMiddleBtn.classList.toggle("active", !isMixed && vAlign === "middle");
    this.valignBottomBtn.classList.toggle("active", !isMixed && vAlign === "bottom");
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

  // =========================================================================
  // Private Methods - Font Controls
  // =========================================================================

  private toggleFontDropdown(type: "family" | "size"): void {
    const dropdown = type === "family" ? this.fontFamilyDropdown : this.fontSizeDropdown;
    const isOpen = dropdown.classList.contains("open");

    // Close all font dropdowns first
    this.closeFontDropdowns();

    // Toggle the clicked one
    if (!isOpen) {
      dropdown.classList.add("open");
    }
  }

  private closeFontDropdowns(): void {
    this.fontFamilyDropdown.classList.remove("open");
    this.fontSizeDropdown.classList.remove("open");
  }

  private async applyFontFamily(fontFamily: string): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { fontFamily };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.fontFamily = fontFamily;
      this.updateFontFamilyDisplay(fontFamily);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply font family:", error);
    }
  }

  private async applyFontSize(fontSize: number): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = { fontSize };

    try {
      await this.applyStyleToSelection(styleUpdate);

      this.currentStyle.fontSize = fontSize;
      this.updateFontSizeDisplay(fontSize);

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply font size:", error);
    }
  }

  private updateFontFamilyDisplay(fontFamily: string, isMixed?: boolean): void {
    if (isMixed) {
      this.fontFamilyLabel.textContent = "Multiple";
      this.fontFamilyLabel.style.fontFamily = "";
    } else {
      this.fontFamilyLabel.textContent = fontFamily;
      this.fontFamilyLabel.style.fontFamily = fontFamily;
    }

    // Update menu selection
    const items = this.fontFamilyMenu.querySelectorAll("[data-font]");
    items.forEach((item) => {
      const itemFont = (item as HTMLElement).dataset.font || "";
      item.classList.toggle("active", !isMixed && itemFont === fontFamily);
    });
  }

  private updateFontSizeDisplay(fontSize: number, isMixed?: boolean): void {
    if (isMixed) {
      this.fontSizeLabel.textContent = "-";
    } else {
      this.fontSizeLabel.textContent = String(fontSize);
    }

    // Update menu selection
    const items = this.fontSizeMenu.querySelectorAll("[data-size]");
    items.forEach((item) => {
      const itemSize = parseInt((item as HTMLElement).dataset.size || "0", 10);
      item.classList.toggle("active", !isMixed && itemSize === fontSize);
    });
  }
}
