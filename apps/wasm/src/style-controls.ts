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
import type { CellStyle, Position, WorkbookTheme } from "./types";
import { getMenuStateManager } from "./menu-state";
import { positionDropdown } from "./dropdown-utils";

// =============================================================================
// Theme Color Helpers
// =============================================================================

/** Theme color slot names (OOXML order: lt1, dk1, lt2, dk2, accent1-6, hlink, folHlink) */
const THEME_COLOR_NAMES = [
  "Background 1", "Text 1", "Background 2", "Text 2",
  "Accent 1", "Accent 2", "Accent 3", "Accent 4",
  "Accent 5", "Accent 6",
];

/** Tint values for the 5-row theme color matrix (lightest to darkest) */
const THEME_TINTS = [0.8, 0.6, 0.4, -0.25, -0.5];

/** Build a human-readable label for a theme color slot, e.g. "Accent 1, +40%" */
function themeColorLabel(themeIndex: number, tint: number): string {
  const name = THEME_COLOR_NAMES[themeIndex] || `Theme ${themeIndex}`;
  if (tint === 0) return name;
  const pct = Math.round(tint * 100);
  const tintStr = pct > 0 ? `+${pct}%` : `${pct}%`;
  return `${name}, ${tintStr}`;
}

/** Apply tint to a hex color (TS port of C++ applyTint from theme.h) */
function applyTint(hexColor: string, tint: number): string {
  if (!hexColor || hexColor.length !== 7 || tint === 0) return hexColor;

  const r = parseInt(hexColor.substring(1, 3), 16);
  const g = parseInt(hexColor.substring(3, 5), 16);
  const b = parseInt(hexColor.substring(5, 7), 16);

  // RGB to HSL
  const rd = r / 255, gd = g / 255, bd = b / 255;
  const max = Math.max(rd, gd, bd), min = Math.min(rd, gd, bd);
  let h = 0, s = 0;
  const l = (max + min) / 2;

  if (max !== min) {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (max === rd) h = (gd - bd) / d + (gd < bd ? 6 : 0);
    else if (max === gd) h = (bd - rd) / d + 2;
    else h = (rd - gd) / d + 4;
    h /= 6;
  }

  // Apply tint per ECMA-376 spec
  let newL: number;
  if (tint < 0) {
    newL = l * (1.0 + tint);
  } else {
    newL = l * (1.0 - tint) + tint;
  }
  newL = Math.max(0, Math.min(1, newL));

  // HSL to RGB
  const hue2rgb = (p: number, q: number, t: number) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  };

  let nr: number, ng: number, nb: number;
  if (s === 0) {
    nr = ng = nb = newL;
  } else {
    const q = newL < 0.5 ? newL * (1 + s) : newL + s - newL * s;
    const p = 2 * newL - q;
    nr = hue2rgb(p, q, h + 1 / 3);
    ng = hue2rgb(p, q, h);
    nb = hue2rgb(p, q, h - 1 / 3);
  }

  const toHex = (v: number) => Math.round(v * 255).toString(16).padStart(2, "0").toUpperCase();
  return `#${toHex(nr)}${toHex(ng)}${toHex(nb)}`;
}

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
  bgThemePalette: HTMLElement;
  textColorWrapper: HTMLElement;
  textColorBtn: HTMLButtonElement;
  textColorSwatch: HTMLElement;
  textColorPopup: HTMLElement;
  textColorHexInput: HTMLInputElement;
  textThemePalette: HTMLElement;
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
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Get the selected axis (column or row header click) */
  getSelectedAxis: () => { type: "column" | "row"; index: number } | null;
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
  private bgThemePalette: HTMLElement;
  private textColorWrapper: HTMLElement;
  private textColorBtn: HTMLButtonElement;
  private textColorSwatch: HTMLElement;
  private textColorPopup: HTMLElement;
  private textColorHexInput: HTMLInputElement;
  private textThemePalette: HTMLElement;
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
  private getSelectedAxis: () => { type: "column" | "row"; index: number } | null;
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
    this.bgThemePalette = config.bgThemePalette;
    this.textColorWrapper = config.textColorWrapper;
    this.textColorBtn = config.textColorBtn;
    this.textColorSwatch = config.textColorSwatch;
    this.textColorPopup = config.textColorPopup;
    this.textColorHexInput = config.textColorHexInput;
    this.textThemePalette = config.textThemePalette;
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
    this.getSelectionRange = callbacks.getSelectionRange;
    this.getSelectedAxis = callbacks.getSelectedAxis;
    this.requestRender = callbacks.requestRender;
    this.updateFormulaBar = callbacks.updateFormulaBar;

    this.setupEventListeners();

    // Register menus with MenuStateManager for mutual exclusivity
    // Each menu must have its own close callback to avoid reentrancy bugs
    const menuState = getMenuStateManager();
    menuState.registerMenu("bgColor", () => this.closeBgColorPopup());
    menuState.registerMenu("textColor", () => this.closeTextColorPopup());
    menuState.registerMenu("fontFamily", () => this.closeFontFamilyDropdown());
    menuState.registerMenu("fontSize", () => this.closeFontSizeDropdown());
  }

  // =========================================================================
  // Public Methods
  // =========================================================================

  /** Set the data source after WASM initialization */
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
    this.loadThemePalette();
  }

  /** Reload theme palette (call after loading a new workbook) */
  async loadThemePalette(): Promise<void> {
    if (!this.dataSource) return;
    const theme = await this.dataSource.getTheme();
    this.buildThemePalette(this.bgThemePalette, theme, "bg");
    this.buildThemePalette(this.textThemePalette, theme, "text");
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
    if (!this.dataSource) {
      // No data source - reset to defaults
      this.setDisplayedStyle({
        bold: false,
        italic: false,
        underline: false,
        bgColor: "",
        textColor: "",
      });
      return;
    }

    // Check if a column or row is selected (via header click)
    const selectedAxis = this.getSelectedAxis();
    if (selectedAxis) {
      // Axis selection - fetch the column or row style
      const axisStyle =
        selectedAxis.type === "column"
          ? await this.dataSource.getColumnStyle(selectedAxis.index)
          : await this.dataSource.getRowStyle(selectedAxis.index);
      this.setDisplayedStyle(axisStyle);
      return;
    }

    const position = this.getSelectedCell();
    if (!position) {
      // No cell selected - reset to defaults
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
      // Escape key closes all style dropdowns
      if (e.key === "Escape") {
        const hasOpenDropdown =
          this.bgColorWrapper.classList.contains("open") ||
          this.textColorWrapper.classList.contains("open") ||
          this.fontFamilyDropdown.classList.contains("open") ||
          this.fontSizeDropdown.classList.contains("open");

        if (hasOpenDropdown) {
          e.preventDefault();
          // Close all dropdowns and notify MenuStateManager
          const menuState = getMenuStateManager();
          if (this.bgColorWrapper.classList.contains("open")) {
            this.bgColorWrapper.classList.remove("open");
            menuState.closeMenu("bgColor");
          }
          if (this.textColorWrapper.classList.contains("open")) {
            this.textColorWrapper.classList.remove("open");
            menuState.closeMenu("textColor");
          }
          if (this.fontFamilyDropdown.classList.contains("open")) {
            this.fontFamilyDropdown.classList.remove("open");
            menuState.closeMenu("fontFamily");
          }
          if (this.fontSizeDropdown.classList.contains("open")) {
            this.fontSizeDropdown.classList.remove("open");
            menuState.closeMenu("fontSize");
          }
          return;
        }
      }

      // Only handle other shortcuts if no input/textarea is focused
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
    if (!this.hasValidSelection() || !this.dataSource) return;

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

  /** Check if we have a valid selection (cell, range, or axis) */
  private hasValidSelection(): boolean {
    return !!(this.getSelectedCell() || this.getSelectedAxis());
  }

  private async applyBgColor(color: string): Promise<void> {
    if (!this.hasValidSelection() || !this.dataSource) return;

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
    if (!this.hasValidSelection() || !this.dataSource) return;

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
    if (!this.hasValidSelection() || !this.dataSource) return;

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
    if (!this.hasValidSelection() || !this.dataSource) return;

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
   * Priority for style application:
   * 1. Column selection (header click) → setColumnStyle (axis-level style)
   * 2. Row selection (header click) → setRowStyle (axis-level style)
   * 3. Multi-cell range → setStyleForRange (range-level style)
   * 4. Single cell → setCellStyleAt (cell-level style)
   */
  private async applyStyleToSelection(styleUpdate: Partial<CellStyle>): Promise<void> {
    if (!this.dataSource) return;

    // Check if a full column or row is selected (header click)
    const selectedAxis = this.getSelectedAxis();
    if (selectedAxis) {
      if (selectedAxis.type === "column") {
        await this.dataSource.setColumnStyle(selectedAxis.index, styleUpdate);
      } else {
        await this.dataSource.setRowStyle(selectedAxis.index, styleUpdate);
      }
      return;
    }

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

    // Show theme mapping in hex inputs when theme colors are active
    this.updateColorHexDisplay("bg", style);
    this.updateColorHexDisplay("text", style);

    // Update font dropdowns
    this.updateFontFamilyDisplay(style.fontFamily || "Arial", mixed?.fontFamily);
    this.updateFontSizeDisplay(style.fontSize || 12, mixed?.fontSize);

    // Update alignment buttons (TextAlign includes "justify", but we only show left/center/right)
    // When no explicit alignment is set (undefined), no button should be active (Excel behavior)
    // The renderer handles GENERAL alignment based on content type (right for numbers, left for text)
    const hAlign = style.hAlign === "justify" ? "left" : style.hAlign;
    this.updateHAlignButtons(hAlign, mixed?.hAlign);
    this.updateVAlignButtons(style.vAlign, mixed?.vAlign);
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

  /**
   * Update the hex input to show theme color label or hex value.
   * When a theme color is active, shows e.g. "Accent 1, +40%" as a read-only label.
   * When a direct color is active, shows the editable hex input as before.
   */
  private updateColorHexDisplay(type: "bg" | "text", style: Partial<CellStyle>): void {
    const hexInput = type === "bg" ? this.bgColorHexInput : this.textColorHexInput;
    const themeIndex = type === "bg" ? style.bgThemeIndex : style.textThemeIndex;
    const themeTint = type === "bg" ? (style.bgThemeTint ?? 0) : (style.textThemeTint ?? 0);

    if (themeIndex !== undefined && themeIndex >= 0) {
      // Theme color — show label
      const label = themeColorLabel(themeIndex, themeTint);
      hexInput.value = label;
      hexInput.readOnly = true;
      hexInput.classList.add("theme-label");
    } else {
      // Direct color — show hex
      const color = type === "bg" ? (style.bgColor || "") : (style.textColor || "");
      hexInput.value = color ? color.toUpperCase() : "";
      hexInput.readOnly = false;
      hexInput.classList.remove("theme-label");
    }
  }

  private updateHAlignButtons(hAlign: "left" | "center" | "right" | undefined, isMixed?: boolean): void {
    // When mixed or undefined (GENERAL alignment), don't highlight any button
    // This matches Excel behavior where buttons only show active when explicitly set
    const noActive = isMixed || !hAlign;
    this.alignLeftBtn.classList.toggle("active", !noActive && hAlign === "left");
    this.alignCenterBtn.classList.toggle("active", !noActive && hAlign === "center");
    this.alignRightBtn.classList.toggle("active", !noActive && hAlign === "right");
  }

  private updateVAlignButtons(vAlign: "top" | "middle" | "bottom" | undefined, isMixed?: boolean): void {
    // When mixed or undefined (GENERAL alignment), don't highlight any button
    // This matches Excel behavior where buttons only show active when explicitly set
    const noActive = isMixed || !vAlign;
    this.valignTopBtn.classList.toggle("active", !noActive && vAlign === "top");
    this.valignMiddleBtn.classList.toggle("active", !noActive && vAlign === "middle");
    this.valignBottomBtn.classList.toggle("active", !noActive && vAlign === "bottom");
  }

  // =========================================================================
  // Private Methods - Color Picker
  // =========================================================================

  private toggleColorPopup(type: "bg" | "text"): void {
    const wrapper = type === "bg" ? this.bgColorWrapper : this.textColorWrapper;
    const btn = type === "bg" ? this.bgColorBtn : this.textColorBtn;
    const popup = type === "bg" ? this.bgColorPopup : this.textColorPopup;
    const menuId = type === "bg" ? "bgColor" : "textColor";
    const menuState = getMenuStateManager();
    const isOpen = wrapper.classList.contains("open");

    // Close all color popups first
    this.closeColorPopups();

    // Toggle the clicked one
    if (!isOpen) {
      wrapper.classList.add("open");
      // Position the popup to stay within viewport bounds
      const buttonRect = btn.getBoundingClientRect();
      positionDropdown(popup, buttonRect);
      // Notify MenuStateManager - this will close other menus via their callbacks
      menuState.openMenu(menuId);
    } else {
      // Notify MenuStateManager that this menu is now closed
      menuState.closeMenu(menuId);
    }
  }

  private closeColorPopups(): void {
    this.closeBgColorPopup();
    this.closeTextColorPopup();
  }

  /** Close only the background color popup (for MenuStateManager callback) */
  private closeBgColorPopup(): void {
    if (this.bgColorWrapper.classList.contains("open")) {
      this.bgColorWrapper.classList.remove("open");
    }
  }

  /** Close only the text color popup (for MenuStateManager callback) */
  private closeTextColorPopup(): void {
    if (this.textColorWrapper.classList.contains("open")) {
      this.textColorWrapper.classList.remove("open");
    }
  }

  private isValidHexColor(color: string): boolean {
    return /^#[0-9A-Fa-f]{6}$/.test(color) || /^#[0-9A-Fa-f]{3}$/.test(color);
  }

  /**
   * Build the theme color palette grid for a popup.
   * Generates a 10-column × 6-row grid: first row is the 10 base theme colors
   * (indices 0-9), then 5 rows of tint variations per color.
   */
  private buildThemePalette(
    container: HTMLElement,
    theme: WorkbookTheme | null,
    type: "bg" | "text",
  ): void {
    container.innerHTML = "";
    if (!theme) return;

    const colors = theme.colorScheme.colors;
    // Use first 10 colors (lt1, dk1, lt2, dk2, accent1-6), skip hlink/folHlink
    const count = Math.min(colors.length, 10);
    if (count === 0) return;

    // Label
    const label = document.createElement("div");
    label.className = "color-picker-theme-label";
    label.textContent = "Theme Colors";
    container.appendChild(label);

    // Grid
    const grid = document.createElement("div");
    grid.className = "color-picker-theme-grid";
    container.appendChild(grid);

    // Row 0: base theme colors (tint = 0)
    for (let i = 0; i < count; i++) {
      const color = colors[i] || "#000000";
      const name = THEME_COLOR_NAMES[i] || `Theme ${i}`;
      const btn = this.createThemeColorButton(color, i, 0, name);
      grid.appendChild(btn);
    }

    // Rows 1-5: tint variations
    for (const tint of THEME_TINTS) {
      for (let i = 0; i < count; i++) {
        const baseColor = colors[i] || "#000000";
        const tinted = applyTint(baseColor, tint);
        const pct = Math.round(tint * 100);
        const tintLabel = pct > 0 ? `+${pct}%` : `${pct}%`;
        const name = THEME_COLOR_NAMES[i] || `Theme ${i}`;
        const btn = this.createThemeColorButton(tinted, i, tint, `${name}, ${tintLabel}`);
        grid.appendChild(btn);
      }
    }

    // Click handler for theme color buttons
    grid.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const btn = target.closest(".color-option") as HTMLElement;
      if (!btn) return;

      const themeIndex = parseInt(btn.dataset.themeIndex || "-1", 10);
      const themeTint = parseFloat(btn.dataset.themeTint || "0");
      if (themeIndex < 0) return;

      if (type === "bg") {
        this.applyBgThemeColor(themeIndex, themeTint);
      } else {
        this.applyTextThemeColor(themeIndex, themeTint);
      }
      this.closeColorPopups();
    });
  }

  private createThemeColorButton(color: string, themeIndex: number, tint: number, title: string): HTMLButtonElement {
    const btn = document.createElement("button");
    btn.className = "color-option";
    btn.style.background = color;
    btn.dataset.color = color.toUpperCase();
    btn.dataset.themeIndex = String(themeIndex);
    btn.dataset.themeTint = String(tint);
    btn.title = title;
    return btn;
  }

  private async applyBgThemeColor(themeIndex: number, tint: number): Promise<void> {
    if (!this.hasValidSelection() || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = {
      bgThemeIndex: themeIndex,
      bgThemeTint: tint,
    };

    try {
      await this.applyStyleToSelection(styleUpdate);

      // Update swatch with the resolved color for visual feedback
      const theme = await this.dataSource.getTheme();
      if (theme) {
        const baseColor = theme.colorScheme.colors[themeIndex] || "#000000";
        const resolved = tint === 0 ? baseColor : applyTint(baseColor, tint);
        this.currentStyle.bgColor = resolved;
        this.updateBgColorSwatch(resolved);
      }

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply theme background color:", error);
    }
  }

  private async applyTextThemeColor(themeIndex: number, tint: number): Promise<void> {
    if (!this.hasValidSelection() || !this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = {
      textThemeIndex: themeIndex,
      textThemeTint: tint,
    };

    try {
      await this.applyStyleToSelection(styleUpdate);

      // Update swatch with the resolved color for visual feedback
      const theme = await this.dataSource.getTheme();
      if (theme) {
        const baseColor = theme.colorScheme.colors[themeIndex] || "#000000";
        const resolved = tint === 0 ? baseColor : applyTint(baseColor, tint);
        this.currentStyle.textColor = resolved;
        this.updateTextColorSwatch(resolved);
      }

      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply theme text color:", error);
    }
  }

  // =========================================================================
  // Private Methods - Font Controls
  // =========================================================================

  private toggleFontDropdown(type: "family" | "size"): void {
    const dropdown = type === "family" ? this.fontFamilyDropdown : this.fontSizeDropdown;
    const btn = type === "family" ? this.fontFamilyBtn : this.fontSizeBtn;
    const menu = type === "family" ? this.fontFamilyMenu : this.fontSizeMenu;
    const menuId = type === "family" ? "fontFamily" : "fontSize";
    const menuState = getMenuStateManager();
    const isOpen = dropdown.classList.contains("open");

    // Close all font dropdowns first
    this.closeFontDropdowns();

    // Toggle the clicked one
    if (!isOpen) {
      dropdown.classList.add("open");
      // Position the menu to stay within viewport bounds
      const buttonRect = btn.getBoundingClientRect();
      positionDropdown(menu, buttonRect);
      // Notify MenuStateManager - this will close other menus via their callbacks
      menuState.openMenu(menuId);
    } else {
      // Notify MenuStateManager that this menu is now closed
      menuState.closeMenu(menuId);
    }
  }

  private closeFontDropdowns(): void {
    this.closeFontFamilyDropdown();
    this.closeFontSizeDropdown();
  }

  /** Close only the font family dropdown (for MenuStateManager callback) */
  private closeFontFamilyDropdown(): void {
    if (this.fontFamilyDropdown.classList.contains("open")) {
      this.fontFamilyDropdown.classList.remove("open");
    }
  }

  /** Close only the font size dropdown (for MenuStateManager callback) */
  private closeFontSizeDropdown(): void {
    if (this.fontSizeDropdown.classList.contains("open")) {
      this.fontSizeDropdown.classList.remove("open");
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
