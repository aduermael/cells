// Format Controls - UI for cell number formatting
// Handles format dropdown, currency/percent toggles, and decimal +/- buttons

import type { WasmDataSource } from "./wasm-data-source";
import type { NumberFormat, NumberFormatCategory, Position, CellData } from "./types";

// =============================================================================
// Types
// =============================================================================

/** Currency type identifiers */
export type CurrencyType = "USD" | "EUR" | "GBP" | "JPY" | "CNY";

/** Currency info */
export interface CurrencyInfo {
  type: CurrencyType;
  symbol: string;
}

/** Format controls configuration */
export interface FormatControlsConfig {
  formatDropdown: HTMLElement;
  formatDropdownBtn: HTMLButtonElement;
  formatDropdownLabel: HTMLElement;
  formatDropdownMenu: HTMLElement;
  currencyDropdown: HTMLElement;
  currencyDropdownBtn: HTMLButtonElement;
  currencyDropdownLabel: HTMLElement;
  currencyDropdownMenu: HTMLElement;
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
  private currencyDropdown: HTMLElement;
  private currencyDropdownBtn: HTMLButtonElement;
  private currencyDropdownLabel: HTMLElement;
  private currencyDropdownMenu: HTMLElement;
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
  private currentCurrency: CurrencyType = "USD";
  private isDropdownOpen = false;
  private isCurrencyDropdownOpen = false;

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
    this.currencyDropdown = config.currencyDropdown;
    this.currencyDropdownBtn = config.currencyDropdownBtn;
    this.currencyDropdownLabel = config.currencyDropdownLabel;
    this.currencyDropdownMenu = config.currencyDropdownMenu;
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
  async setDataSource(dataSource: WasmDataSource): Promise<void> {
    this.dataSource = dataSource;
    await this.loadAvailableFormats();
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
      if (this.isCurrencyDropdownOpen && !this.currencyDropdown.contains(e.target as Node)) {
        this.closeCurrencyDropdown();
      }
    });

    // Currency dropdown toggle
    this.currencyDropdownBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.toggleCurrencyDropdown();
    });

    // Currency item selection
    this.currencyDropdownMenu.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const item = target.closest("[data-currency]") as HTMLElement;
      if (item) {
        const currency = item.dataset.currency as CurrencyType;
        const symbol = item.dataset.symbol || "$";
        this.handleCurrencySelect(currency, symbol);
        this.closeCurrencyDropdown();
      }
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
  // Private Methods - Currency Dropdown
  // =========================================================================

  private toggleCurrencyDropdown(): void {
    if (this.isCurrencyDropdownOpen) {
      this.closeCurrencyDropdown();
    } else {
      this.openCurrencyDropdown();
    }
  }

  private openCurrencyDropdown(): void {
    this.isCurrencyDropdownOpen = true;
    this.currencyDropdown.classList.add("open");
    this.updateCurrencyDropdownActiveState();
  }

  private closeCurrencyDropdown(): void {
    this.isCurrencyDropdownOpen = false;
    this.currencyDropdown.classList.remove("open");
  }

  private updateCurrencyDropdownActiveState(): void {
    const items = this.currencyDropdownMenu.querySelectorAll("[data-currency]");
    items.forEach((item) => {
      const currency = (item as HTMLElement).dataset.currency;
      item.classList.toggle("active", currency === this.currentCurrency);
    });
  }

  private async handleCurrencySelect(currency: CurrencyType, symbol: string): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    // Get the format ID for this currency (with 2 decimal places, industry standard)
    const formatId = this.getFormatIdForCurrency(currency, 2);

    try {
      await this.dataSource.setCellFormatAt(position.col, position.row, formatId);
      this.currentCurrency = currency;
      this.currencyDropdownLabel.textContent = symbol;
      this.setDisplayedFormat(formatId, "CURRENCY");
      this.currencyDropdown.classList.add("active");
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to set currency format:", error);
    }
  }

  private getFormatIdForCurrency(currency: CurrencyType, decimalPlaces: number): string {
    // Format ID pattern: C<CURRENCY>_0<DECIMAL_PLACES> (8 chars total)
    // e.g., CUSD_002 for USD with 2 decimal places
    const decStr = decimalPlaces.toString().padStart(2, "0");
    return `C${currency}_0${decStr}`;
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
      // C++ returns lowercase categories, convert to uppercase for TypeScript
      return format.category.toUpperCase() as NumberFormatCategory;
    }

    // Fallback: query from data source
    if (this.dataSource) {
      const formats = await this.dataSource.getAvailableFormats();
      const found = formats.find((f) => f.id === formatId);
      if (found) {
        return found.category.toUpperCase() as NumberFormatCategory;
      }
    }

    return "GENERAL";
  }

  private getFormatIdForCategory(category: NumberFormatCategory): string {
    // Find formats matching the category (C++ returns lowercase)
    const matchingFormats = this.availableFormats.filter(
      (f) => f.category.toUpperCase() === category
    );

    if (matchingFormats.length === 0) {
      return "~";
    }

    // Currency is special - default to 2 decimal places (industry standard: $100.00)
    if (category === "CURRENCY") {
      const twoDecimal = matchingFormats.find((f) => f.decimalPlaces === 2);
      if (twoDecimal) {
        return twoDecimal.id;
      }
    }

    // For other categories, prefer 0 decimal places without thousands separator.
    // This makes "15%" instead of "15.00%" for Percent, and "1235" instead of "1,235" for Number.
    const zeroDecimalNoSeparator = matchingFormats.find(
      (f) => f.decimalPlaces === 0 && !f.useThousandsSeparator
    );
    if (zeroDecimalNoSeparator) {
      return zeroDecimalNoSeparator.id;
    }

    // Fall back to 0 decimal places (with separator if that's all we have)
    const zeroDecimal = matchingFormats.find((f) => f.decimalPlaces === 0);
    if (zeroDecimal) {
      return zeroDecimal.id;
    }

    // Fall back to format with 2 decimal places (common default)
    const twoDecimal = matchingFormats.find((f) => f.decimalPlaces === 2);
    if (twoDecimal) {
      return twoDecimal.id;
    }

    // Otherwise return the first matching format
    return matchingFormats[0].id;
  }

  private setDisplayedFormat(formatId: string, category: NumberFormatCategory): void {
    this.currentCategory = category;

    // Update dropdown label
    this.formatDropdownLabel.textContent = this.getCategoryDisplayName(category);

    // Update currency dropdown active state
    const isCurrency = category === "CURRENCY" || category === "ACCOUNTING";
    this.currencyDropdown.classList.toggle("active", isCurrency);

    // If this is a currency format, update the currency dropdown to show the right currency
    // Format ID pattern: C<CURRENCY>_0XX (e.g., CUSD_002) or legacy FMT_C0XX
    if (isCurrency) {
      const match = formatId.match(/^C([A-Z]{3})_0\d{2}$/);
      if (match) {
        const currency = match[1] as CurrencyType;
        this.currentCurrency = currency;
        const symbol = this.getCurrencySymbol(currency);
        this.currencyDropdownLabel.textContent = symbol;
      } else if (formatId.startsWith("FMT_C")) {
        // Legacy USD format
        this.currentCurrency = "USD";
        this.currencyDropdownLabel.textContent = "$";
      }
    }

    // Update dropdown active state if open
    if (this.isDropdownOpen) {
      this.updateDropdownActiveState();
    }
    if (this.isCurrencyDropdownOpen) {
      this.updateCurrencyDropdownActiveState();
    }
  }

  private getCurrencySymbol(currency: CurrencyType): string {
    const symbols: Record<CurrencyType, string> = {
      USD: "$",
      EUR: "€",
      GBP: "£",
      JPY: "¥",
      CNY: "¥",
    };
    return symbols[currency] || "$";
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
    // C++ returns lowercase categories, so compare case-insensitively
    let currentFormat = this.availableFormats.find((f) => f.id === currentFormatId);
    if (!currentFormat) {
      // Default to NUMBER with 2 decimal places if no format
      currentFormat = this.availableFormats.find(
        (f) => f.category.toUpperCase() === "NUMBER" && f.decimalPlaces === 2
      );
    }

    const currentDecimals = currentFormat?.decimalPlaces ?? 2;
    const currentCategory = currentFormat?.category.toUpperCase() ?? "NUMBER";
    const currentHasSeparator = currentFormat?.useThousandsSeparator ?? false;
    // Max 4 decimal places (matching available formats)
    const newDecimals = Math.max(0, Math.min(4, currentDecimals + delta));

    // Find a format with the new decimal places in the same category, preserving separator setting
    let newFormat = this.availableFormats.find(
      (f) =>
        f.category.toUpperCase() === currentCategory &&
        f.decimalPlaces === newDecimals &&
        f.useThousandsSeparator === currentHasSeparator
    );

    // If not found with same separator, try without separator preference
    if (!newFormat) {
      newFormat = this.availableFormats.find(
        (f) => f.category.toUpperCase() === currentCategory && f.decimalPlaces === newDecimals
      );
    }

    // If not found in same category, try NUMBER with same separator preference
    if (!newFormat) {
      newFormat = this.availableFormats.find(
        (f) =>
          f.category.toUpperCase() === "NUMBER" &&
          f.decimalPlaces === newDecimals &&
          f.useThousandsSeparator === currentHasSeparator
      );
    }

    // If still not found, try NUMBER without separator preference
    if (!newFormat) {
      newFormat = this.availableFormats.find(
        (f) => f.category.toUpperCase() === "NUMBER" && f.decimalPlaces === newDecimals
      );
    }

    if (!newFormat) {
      // Fallback: if we can't find a matching format, just use the first NUMBER format
      newFormat = this.availableFormats.find((f) => f.category.toUpperCase() === "NUMBER");
    }

    if (newFormat) {
      try {
        await this.dataSource.setCellFormatAt(position.col, position.row, newFormat.id);
        this.setDisplayedFormat(newFormat.id, newFormat.category.toUpperCase() as NumberFormatCategory);
        this.requestRender();
        this.updateFormulaBar();
      } catch (error) {
        console.error("Failed to change decimal places:", error);
      }
    }
  }
}
