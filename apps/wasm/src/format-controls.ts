// Format Controls - UI for cell number formatting
// Handles format dropdown, currency/percent toggles, and decimal +/- buttons

import type { WasmDataSource } from "./wasm-data-source";
import type { NumberFormat, NumberFormatCategory, Position, CellData } from "./types";

// =============================================================================
// Types
// =============================================================================

/** Format controls configuration */
export interface FormatControlsConfig {
  formatDropdown: HTMLElement;
  formatDropdownBtn: HTMLButtonElement;
  formatDropdownLabel: HTMLElement;
  formatDropdownMenu: HTMLElement;
  currencyBtn: HTMLButtonElement;
  percentBtn: HTMLButtonElement;
  decimalIncreaseBtn: HTMLButtonElement;
  decimalDecreaseBtn: HTMLButtonElement;
}

/** Callback signatures */
export interface FormatControlsCallbacks {
  /** Get the currently selected cell position */
  getSelectedCell: () => Position | null;
  /** Get the selected cell data (for current format) */
  getSelectedCellData: () => CellData | null;
  /** Request render after format change */
  requestRender: () => void;
  /** Update the formula bar display */
  updateFormulaBar: () => void;
}

// =============================================================================
// FormatControls Class
// =============================================================================

/**
 * FormatControls manages the format dropdown and formatting buttons in the formula bar.
 *
 * Responsibilities:
 * - Display current cell format in dropdown
 * - Handle format category selection
 * - Handle currency/percent quick toggles
 * - Handle decimal increase/decrease
 */
export class FormatControls {
  // =========================================================================
  // Elements
  // =========================================================================

  private formatDropdown: HTMLElement;
  private formatDropdownBtn: HTMLButtonElement;
  private formatDropdownLabel: HTMLElement;
  private formatDropdownMenu: HTMLElement;
  private currencyBtn: HTMLButtonElement;
  private percentBtn: HTMLButtonElement;
  private decimalIncreaseBtn: HTMLButtonElement;
  private decimalDecreaseBtn: HTMLButtonElement;

  // =========================================================================
  // Dependencies
  // =========================================================================

  private dataSource: WasmDataSource | null = null;

  // =========================================================================
  // Callbacks
  // =========================================================================

  private getSelectedCell: () => Position | null;
  private getSelectedCellData: () => CellData | null;
  private requestRender: () => void;
  private updateFormulaBar: () => void;

  // =========================================================================
  // State
  // =========================================================================

  private availableFormats: NumberFormat[] = [];
  private currentCategory: NumberFormatCategory = "GENERAL";
  private isDropdownOpen = false;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(
    config: FormatControlsConfig,
    callbacks: FormatControlsCallbacks
  ) {
    this.formatDropdown = config.formatDropdown;
    this.formatDropdownBtn = config.formatDropdownBtn;
    this.formatDropdownLabel = config.formatDropdownLabel;
    this.formatDropdownMenu = config.formatDropdownMenu;
    this.currencyBtn = config.currencyBtn;
    this.percentBtn = config.percentBtn;
    this.decimalIncreaseBtn = config.decimalIncreaseBtn;
    this.decimalDecreaseBtn = config.decimalDecreaseBtn;

    this.getSelectedCell = callbacks.getSelectedCell;
    this.getSelectedCellData = callbacks.getSelectedCellData;
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
    this.loadAvailableFormats();
  }

  /** Update the displayed format for the current cell selection */
  async updateForCurrentCell(): Promise<void> {
    const cellData = this.getSelectedCellData();

    if (!cellData || !this.dataSource) {
      // No cell selected or no data source - show General
      this.setDisplayedFormat("~", "GENERAL");
      return;
    }

    // Get format from cell data
    const formatId = cellData.formatId || "~";
    const category = await this.getCategoryForFormatId(formatId);
    this.setDisplayedFormat(formatId, category);
  }

  // =========================================================================
  // Private Methods - Setup
  // =========================================================================

  private setupEventListeners(): void {
    // Dropdown toggle
    this.formatDropdownBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleDropdown();
    });

    // Dropdown item selection
    this.formatDropdownMenu.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const item = target.closest("[data-format-category]") as HTMLElement;
      if (item) {
        const category = item.dataset.formatCategory as NumberFormatCategory;
        this.handleCategorySelect(category);
        this.closeDropdown();
      }
    });

    // Close dropdown on outside click
    document.addEventListener("click", (e) => {
      if (this.isDropdownOpen && !this.formatDropdown.contains(e.target as Node)) {
        this.closeDropdown();
      }
    });

    // Currency button
    this.currencyBtn.addEventListener("click", () => {
      this.handleCategorySelect("CURRENCY");
    });

    // Percent button
    this.percentBtn.addEventListener("click", () => {
      this.handleCategorySelect("PERCENTAGE");
    });

    // Decimal increase
    this.decimalIncreaseBtn.addEventListener("click", () => {
      this.handleDecimalChange(1);
    });

    // Decimal decrease
    this.decimalDecreaseBtn.addEventListener("click", () => {
      this.handleDecimalChange(-1);
    });

    // Keyboard shortcuts
    document.addEventListener("keydown", (e) => {
      // Only handle if no input/textarea is focused
      const active = document.activeElement;
      if (active && (active.tagName === "INPUT" || active.tagName === "TEXTAREA" ||
          (active as HTMLElement).contentEditable === "true")) {
        return;
      }

      // Ctrl/Cmd + Shift + 1-5 for format shortcuts
      if ((e.ctrlKey || e.metaKey) && e.shiftKey) {
        let category: NumberFormatCategory | null = null;
        switch (e.key) {
          case "1":
          case "!":
            category = "NUMBER";
            break;
          case "2":
          case "@":
            category = "TIME";
            break;
          case "3":
          case "#":
            category = "DATE";
            break;
          case "4":
          case "$":
            category = "CURRENCY";
            break;
          case "5":
          case "%":
            category = "PERCENTAGE";
            break;
          case "6":
          case "^":
            category = "SCIENTIFIC";
            break;
        }

        if (category) {
          e.preventDefault();
          this.handleCategorySelect(category);
        }
      }
    });
  }

  // =========================================================================
  // Private Methods - Dropdown
  // =========================================================================

  private toggleDropdown(): void {
    if (this.isDropdownOpen) {
      this.closeDropdown();
    } else {
      this.openDropdown();
    }
  }

  private openDropdown(): void {
    this.isDropdownOpen = true;
    this.formatDropdown.classList.add("open");
    this.updateDropdownActiveState();
  }

  private closeDropdown(): void {
    this.isDropdownOpen = false;
    this.formatDropdown.classList.remove("open");
  }

  private updateDropdownActiveState(): void {
    // Update active state on dropdown items
    const items = this.formatDropdownMenu.querySelectorAll("[data-format-category]");
    items.forEach((item) => {
      const category = (item as HTMLElement).dataset.formatCategory;
      item.classList.toggle("active", category === this.currentCategory);
    });
  }

  // =========================================================================
  // Private Methods - Format Operations
  // =========================================================================

  private async loadAvailableFormats(): Promise<void> {
    if (!this.dataSource) return;

    try {
      this.availableFormats = await this.dataSource.getAvailableFormats();
    } catch (error) {
      console.error("Failed to load available formats:", error);
    }
  }

  private async getCategoryForFormatId(formatId: string): Promise<NumberFormatCategory> {
    if (formatId === "~" || formatId === "") {
      return "GENERAL";
    }

    // Look up in available formats
    const format = this.availableFormats.find((f) => f.id === formatId);
    if (format) {
      return format.category;
    }

    // Fallback: query from data source
    if (this.dataSource) {
      const formats = await this.dataSource.getAvailableFormats();
      const found = formats.find((f) => f.id === formatId);
      if (found) {
        return found.category;
      }
    }

    return "GENERAL";
  }

  private getFormatIdForCategory(category: NumberFormatCategory): string {
    // Return the first format of the given category
    const format = this.availableFormats.find((f) => f.category === category);
    return format ? format.id : "~";
  }

  private setDisplayedFormat(_formatId: string, category: NumberFormatCategory): void {
    this.currentCategory = category;

    // Update dropdown label
    this.formatDropdownLabel.textContent = this.getCategoryDisplayName(category);

    // Update button active states
    this.currencyBtn.classList.toggle("active", category === "CURRENCY" || category === "ACCOUNTING");
    this.percentBtn.classList.toggle("active", category === "PERCENTAGE");

    // Update dropdown active state if open
    if (this.isDropdownOpen) {
      this.updateDropdownActiveState();
    }
  }

  private getCategoryDisplayName(category: NumberFormatCategory): string {
    const names: Record<NumberFormatCategory, string> = {
      GENERAL: "General",
      NUMBER: "Number",
      CURRENCY: "Currency",
      ACCOUNTING: "Accounting",
      PERCENTAGE: "Percent",
      DATE: "Date",
      TIME: "Time",
      DATE_TIME: "DateTime",
      SCIENTIFIC: "Scientific",
      FRACTION: "Fraction",
      TEXT: "Text",
    };
    return names[category] || category;
  }

  private async handleCategorySelect(category: NumberFormatCategory): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    // Get the format ID for this category
    const formatId = this.getFormatIdForCategory(category);

    try {
      // Apply format to cell
      await this.dataSource.setCellFormatAt(position.col, position.row, formatId);

      // Update display
      this.setDisplayedFormat(formatId, category);

      // Trigger re-render and formula bar update
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to set cell format:", error);
    }
  }

  private async handleDecimalChange(delta: number): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    // Get current format
    const cellData = this.getSelectedCellData();
    const currentFormatId = cellData?.formatId || "~";

    // Find current format's decimal places
    let currentFormat = this.availableFormats.find((f) => f.id === currentFormatId);
    if (!currentFormat) {
      // Default to NUMBER with 2 decimal places if no format
      currentFormat = this.availableFormats.find((f) => f.category === "NUMBER" && f.decimalPlaces === 2);
    }

    const currentDecimals = currentFormat?.decimalPlaces ?? 2;
    const currentCategory = currentFormat?.category ?? "NUMBER";
    const newDecimals = Math.max(0, Math.min(10, currentDecimals + delta));

    // Find a format with the new decimal places in the same category
    let newFormat = this.availableFormats.find(
      (f) => f.category === currentCategory && f.decimalPlaces === newDecimals
    );

    // If not found in same category, try NUMBER
    if (!newFormat) {
      newFormat = this.availableFormats.find(
        (f) => f.category === "NUMBER" && f.decimalPlaces === newDecimals
      );
    }

    if (!newFormat) {
      // Fallback: if we can't find a matching format, just use the first NUMBER format
      newFormat = this.availableFormats.find((f) => f.category === "NUMBER");
    }

    if (newFormat) {
      try {
        await this.dataSource.setCellFormatAt(position.col, position.row, newFormat.id);
        this.setDisplayedFormat(newFormat.id, newFormat.category);
        this.requestRender();
        this.updateFormulaBar();
      } catch (error) {
        console.error("Failed to change decimal places:", error);
      }
    }
  }
}
