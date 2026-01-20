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
import { getMenuStateManager } from "./menu-state";

// =============================================================================
// Types
// =============================================================================

/** Style controls configuration - DOM element references */
export interface StyleControlsConfig {
  styleControls: HTMLElement;
  boldBtn: HTMLButtonElement;
  italicBtn: HTMLButtonElement;
  underlineBtn: HTMLButtonElement;
  wrapTextBtn: HTMLButtonElement;
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
  private wrapTextBtn: HTMLButtonElement;
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
    this.wrapTextBtn = config.wrapTextBtn;
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
    // Note: getSelectedCellData from callbacks is no longer used - we now use
    // getEffectiveCellStyle which resolves the full style hierarchy
    this.getSelectionRange = callbacks.getSelectionRange;
    this.requestRender = callbacks.requestRender;
    this.updateFormulaBar = callbacks.updateFormulaBar;

    this.setupEventListeners();

    // Register menus with MenuStateManager for mutual exclusivity
    const menuState = getMenuStateManager();
    menuState.registerMenu("bgColor", () => this.closeColorPopups());
    menuState.registerMenu("textColor", () => this.closeColorPopups());
    menuState.registerMenu("fontFamily", () => this.closeFontDropdowns());
    menuState.registerMenu("fontSize", () => this.closeFontDropdowns());
  }

  // =========================================================================
  // Public Methods
  // =========================================================================

  /** Set the data source after WASM initialization */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
  }

  /**
   * Update the displayed style for the current cell selection.
   *
   * Uses the effective style API which resolves the full style hierarchy:
   * 1. Cell's own style (highest priority)
   * 2. Range styles (merged from overlapping RANGE_STYLE ranges)
   * 3. Column's default style
   * 4. Row's default style
   *
   * This ensures the toolbar shows the actual rendered style, including
   * styles inherited from ranges.
   */
  async updateForCurrentCell(): Promise<void> {
    const position = this.getSelectedCell();

    if (!position || !this.dataSource) {
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

    // Check if selection spans multiple cells
    const { start, end } = this.getSelectionRange();
    if (start && end && (start.col !== end.col || start.row !== end.row)) {
      // Multi-cell selection - use the efficient range query
      const result = await this.dataSource.getEffectiveStyleForRange(
        start.col,
        start.row,
        end.col,
        end.row,
      );
      this.setDisplayedStyle(result.style, result.mixed);
      return;
    }

    // Single cell - get effective style (resolves cell > range > column > row hierarchy)
    const effectiveStyle = await this.dataSource.getEffectiveCellStyle(
      position.col,
      position.row,
    );
    this.setDisplayedStyle(effectiveStyle);
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

    // Wrap text button
    this.wrapTextBtn.addEventListener("click", () => {
      this.toggleStyle("wrapText");
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

  private async toggleStyle(property: "bold" | "italic" | "underline" | "wrapText"): Promise<void> {
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
   *
   * Uses Range-based styling for multi-cell selections, which creates a single
   * Range object with RANGE_STYLE flag instead of creating empty cell entries.
   * Single-cell selections still use cell-level styling.
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

    // For multi-cell ranges, use the Range system for efficient styling
    // This creates a single Range with RANGE_STYLE flag instead of
    // individual cell style entries
    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    await this.dataSource.setStyleForRange(minCol, minRow, maxCol, maxRow, styleUpdate);
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
    this.updateButtonState("wrapText", !!style.wrapText, mixed?.wrapText);

    // Update color swatches (show mixed indicator if colors differ)
    this.updateBgColorSwatch(style.bgColor || "", mixed?.bgColor);
    this.updateTextColorSwatch(style.textColor || "", mixed?.textColor);

    // Update font dropdowns
    this.updateFontFamilyDisplay(style.fontFamily || "Arial", mixed?.fontFamily);
    this.updateFontSizeDisplay(style.fontSize || 12, mixed?.fontSize);

    // Update alignment buttons (TextAlign includes "justify", but we only show left/center/right)
    const hAlign = style.hAlign === "justify" ? "left" : (style.hAlign || "left");
    this.updateHAlignButtons(hAlign as "left" | "center" | "right", mixed?.hAlign);
    this.updateVAlignButtons(style.vAlign || "bottom", mixed?.vAlign);
  }

  private updateButtonState(
    property: "bold" | "italic" | "underline" | "wrapText",
    active: boolean,
    isMixed?: boolean
  ): void {
    let btn: HTMLButtonElement;
    switch (property) {
      case "bold":
        btn = this.boldBtn;
        break;
      case "italic":
        btn = this.italicBtn;
        break;
      case "underline":
        btn = this.underlineBtn;
        break;
      case "wrapText":
        btn = this.wrapTextBtn;
        break;
    }

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
    const menuId = type === "bg" ? "bgColor" : "textColor";
    const menuState = getMenuStateManager();
    const isOpen = wrapper.classList.contains("open");

    // Close all popups first (MenuStateManager will handle closing other menus)
    this.closeColorPopups();

    // Toggle the clicked one
    if (!isOpen) {
      wrapper.classList.add("open");
      menuState.openMenu(menuId);
    }
  }

  private closeColorPopups(): void {
    const menuState = getMenuStateManager();
    if (this.bgColorWrapper.classList.contains("open")) {
      this.bgColorWrapper.classList.remove("open");
      menuState.closeMenu("bgColor");
    }
    if (this.textColorWrapper.classList.contains("open")) {
      this.textColorWrapper.classList.remove("open");
      menuState.closeMenu("textColor");
    }
  }

  private isValidHexColor(color: string): boolean {
    return /^#[0-9A-Fa-f]{6}$/.test(color) || /^#[0-9A-Fa-f]{3}$/.test(color);
  }

  // =========================================================================
  // Private Methods - Font Controls
  // =========================================================================

  private toggleFontDropdown(type: "family" | "size"): void {
    const dropdown = type === "family" ? this.fontFamilyDropdown : this.fontSizeDropdown;
    const menuId = type === "family" ? "fontFamily" : "fontSize";
    const menuState = getMenuStateManager();
    const isOpen = dropdown.classList.contains("open");

    // Close all font dropdowns first (MenuStateManager will handle closing other menus)
    this.closeFontDropdowns();

    // Toggle the clicked one
    if (!isOpen) {
      dropdown.classList.add("open");
      menuState.openMenu(menuId);
    }
  }

  private closeFontDropdowns(): void {
    const menuState = getMenuStateManager();
    if (this.fontFamilyDropdown.classList.contains("open")) {
      this.fontFamilyDropdown.classList.remove("open");
      menuState.closeMenu("fontFamily");
    }
    if (this.fontSizeDropdown.classList.contains("open")) {
      this.fontSizeDropdown.classList.remove("open");
      menuState.closeMenu("fontSize");
    }
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
    } else {
      this.fontFamilyLabel.textContent = fontFamily;
      // Don't apply font-family to label - different fonts have different metrics
      // which causes layout shifts. Font preview is shown in the dropdown menu.
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
