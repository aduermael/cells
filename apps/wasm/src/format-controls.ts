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
  // Custom format panel elements
  customFormatPanel: HTMLElement;
  customFormatInput: HTMLInputElement;
  customFormatPreview: HTMLElement;
  customFormatError: HTMLElement;
  customFormatApplyBtn: HTMLButtonElement;
  customFormatCancelBtn: HTMLButtonElement;
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
  private customFormatPanel: HTMLElement;
  private customFormatInput: HTMLInputElement;
  private customFormatPreview: HTMLElement;
  private customFormatError: HTMLElement;
  private customFormatApplyBtn: HTMLButtonElement;
  private customFormatCancelBtn: HTMLButtonElement;

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
  private isCustomFormatPanelOpen = false;
  private customFormatDebounceTimer: ReturnType<typeof setTimeout> | null = null;

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
    this.customFormatPanel = config.customFormatPanel;
    this.customFormatInput = config.customFormatInput;
    this.customFormatPreview = config.customFormatPreview;
    this.customFormatError = config.customFormatError;
    this.customFormatApplyBtn = config.customFormatApplyBtn;
    this.customFormatCancelBtn = config.customFormatCancelBtn;

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

    // Close dropdowns and custom panel on outside click
    document.addEventListener("click", (e) => {
      const target = e.target as Node;
      if (this.isDropdownOpen && !this.formatDropdown.contains(target)) {
        this.closeDropdown();
      }
      if (this.isCurrencyDropdownOpen && !this.currencyDropdown.contains(target)) {
        this.closeCurrencyDropdown();
      }
      if (this.isCustomFormatPanelOpen && !this.customFormatPanel.contains(target) &&
          !this.formatDropdown.contains(target)) {
        this.closeCustomFormatPanel();
      }
    });

    // Custom format panel events
    this.customFormatInput.addEventListener("input", () => {
      this.handleCustomFormatInputChange();
    });

    this.customFormatApplyBtn.addEventListener("click", () => {
      this.handleCustomFormatApply();
    });

    this.customFormatCancelBtn.addEventListener("click", () => {
      this.closeCustomFormatPanel();
    });

    // Template buttons
    this.customFormatPanel.addEventListener("click", (e) => {
      const target = e.target as HTMLElement;
      const templateBtn = target.closest(".template-btn") as HTMLElement;
      if (templateBtn && templateBtn.dataset.code) {
        this.customFormatInput.value = templateBtn.dataset.code;
        this.handleCustomFormatInputChange();
        this.customFormatInput.focus();
      }
    });

    // Enter key to apply custom format
    this.customFormatInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        this.handleCustomFormatApply();
      } else if (e.key === "Escape") {
        e.preventDefault();
        this.closeCustomFormatPanel();
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
  // Private Methods - Custom Format Panel
  // =========================================================================

  private openCustomFormatPanel(): void {
    this.isCustomFormatPanelOpen = true;
    this.customFormatPanel.classList.remove("hidden");
    this.customFormatInput.value = "";
    this.customFormatError.classList.add("hidden");
    this.customFormatError.textContent = "";

    // Initialize preview with current cell value
    this.updateCustomFormatPreview("");

    // Focus the input
    setTimeout(() => this.customFormatInput.focus(), 0);
  }

  private closeCustomFormatPanel(): void {
    this.isCustomFormatPanelOpen = false;
    this.customFormatPanel.classList.add("hidden");
    if (this.customFormatDebounceTimer) {
      clearTimeout(this.customFormatDebounceTimer);
      this.customFormatDebounceTimer = null;
    }
  }

  private handleCustomFormatInputChange(): void {
    // Debounce preview updates
    if (this.customFormatDebounceTimer) {
      clearTimeout(this.customFormatDebounceTimer);
    }
    this.customFormatDebounceTimer = setTimeout(() => {
      const formatCode = this.customFormatInput.value.trim();
      this.updateCustomFormatPreview(formatCode);
    }, 150);
  }

  private async updateCustomFormatPreview(formatCode: string): Promise<void> {
    const cellData = this.getSelectedCellData();

    // Get a preview value (either from cell or use a default)
    let previewValue = 1234.567;
    if (cellData && cellData.value) {
      const parsed = parseFloat(cellData.value);
      if (!isNaN(parsed)) {
        previewValue = parsed;
      }
    }

    if (!formatCode) {
      this.customFormatPreview.textContent = `Preview: ${previewValue}`;
      this.customFormatError.classList.add("hidden");
      return;
    }

    // Try to format the value with the given format code
    if (this.dataSource) {
      try {
        const result = await this.dataSource.client.formatWithCode(previewValue, formatCode);
        if (result.error) {
          this.customFormatPreview.textContent = `Preview: --`;
          this.customFormatError.textContent = result.error;
          this.customFormatError.classList.remove("hidden");
        } else {
          this.customFormatPreview.textContent = `Preview: ${result.text || previewValue}`;
          this.customFormatError.classList.add("hidden");
        }
      } catch {
        this.customFormatPreview.textContent = `Preview: ${previewValue}`;
        this.customFormatError.classList.add("hidden");
      }
    }
  }

  private async handleCustomFormatApply(): Promise<void> {
    const formatCode = this.customFormatInput.value.trim();
    if (!formatCode) {
      this.closeCustomFormatPanel();
      return;
    }

    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    try {
      // Create the custom format and get its ID
      const result = await this.dataSource.client.createCustomFormat(formatCode);
      if (result.error) {
        this.customFormatError.textContent = result.error;
        this.customFormatError.classList.remove("hidden");
        return;
      }

      if (result.formatId) {
        // Apply the format to the cell
        await this.dataSource.setCellFormatAt(position.col, position.row, result.formatId);

        // Update display
        this.setDisplayedFormat(result.formatId, "CUSTOM");

        // Trigger re-render and formula bar update
        this.requestRender();
        this.updateFormulaBar();

        // Reload available formats to include the new custom format
        await this.loadAvailableFormats();
      }

      this.closeCustomFormatPanel();
    } catch (error) {
      console.error("Failed to create custom format:", error);
      this.customFormatError.textContent = "Failed to create format";
      this.customFormatError.classList.remove("hidden");
    }
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

    // Otherwise return the first matching format (array is non-empty due to check at line 394)
    return matchingFormats[0]?.id ?? "~";
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
      CUSTOM: "Custom",
    };
    return names[category] || category;
  }

  private async handleCategorySelect(category: NumberFormatCategory): Promise<void> {
    const position = this.getSelectedCell();
    if (!position || !this.dataSource) return;

    // Handle custom format specially - open the panel instead
    if (category === "CUSTOM") {
      this.openCustomFormatPanel();
      return;
    }

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

    // Parse the current format to determine category, decimals, etc.
    const parsed = this.parseCurrentFormat(currentFormatId);

    // Calculate new decimals (0-15 range, dynamically generated)
    const newDecimals = Math.max(0, Math.min(15, parsed.decimals + delta));

    // Generate the new format ID dynamically based on category
    const newFormatId = this.generateFormatId(
      parsed.category,
      newDecimals,
      parsed.currency,
      parsed.hasSeparator
    );

    try {
      await this.dataSource.setCellFormatAt(position.col, position.row, newFormatId);
      this.setDisplayedFormat(newFormatId, parsed.category);
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to change decimal places:", error);
    }
  }

  /**
   * Parse a format ID to extract category, decimals, currency, and separator settings.
   */
  private parseCurrentFormat(formatId: string): {
    category: NumberFormatCategory;
    decimals: number;
    currency: CurrencyType | null;
    hasSeparator: boolean;
  } {
    // Try to find in available formats first
    const format = this.availableFormats.find((f) => f.id === formatId);
    if (format) {
      // Extract currency from format ID if applicable
      let currency: CurrencyType | null = null;
      const currencyMatch = formatId.match(/^C([A-Z]{3})_0\d{2}$/);
      if (currencyMatch) {
        currency = currencyMatch[1] as CurrencyType;
      } else if (formatId.startsWith("FMT_C")) {
        currency = "USD"; // Legacy USD format
      }

      return {
        category: format.category.toUpperCase() as NumberFormatCategory,
        decimals: format.decimalPlaces,
        currency,
        hasSeparator: format.useThousandsSeparator,
      };
    }

    // Try parsing dynamic format IDs directly
    // Pattern: FMT_P0XX (percentage)
    const pctMatch = formatId.match(/^FMT_P0(\d{2})$/);
    if (pctMatch && pctMatch[1]) {
      return {
        category: "PERCENTAGE",
        decimals: parseInt(pctMatch[1], 10),
        currency: null,
        hasSeparator: false,
      };
    }

    // Pattern: FMT_N0XX (number without separator)
    const numMatch = formatId.match(/^FMT_N0(\d{2})$/);
    if (numMatch && numMatch[1]) {
      return {
        category: "NUMBER",
        decimals: parseInt(numMatch[1], 10),
        currency: null,
        hasSeparator: false,
      };
    }

    // Pattern: FMT_NS0X (number with separator, single digit)
    const numSepMatch = formatId.match(/^FMT_NS0(\d)$/);
    if (numSepMatch && numSepMatch[1]) {
      return {
        category: "NUMBER",
        decimals: parseInt(numSepMatch[1], 10),
        currency: null,
        hasSeparator: true,
      };
    }

    // Pattern: CXXX_0YY (currency)
    const currMatch = formatId.match(/^C([A-Z]{3})_0(\d{2})$/);
    if (currMatch && currMatch[1] && currMatch[2]) {
      return {
        category: "CURRENCY",
        decimals: parseInt(currMatch[2], 10),
        currency: currMatch[1] as CurrencyType,
        hasSeparator: true,
      };
    }

    // Default to NUMBER with 2 decimals
    return {
      category: "NUMBER",
      decimals: 2,
      currency: null,
      hasSeparator: false,
    };
  }

  /**
   * Generate a format ID dynamically based on category, decimals, and options.
   *
   * Format ID patterns (all 8 chars):
   * - FMT_P0XX: Percentage with XX decimal places (00-15)
   * - FMT_N0XX: Number with XX decimal places (00-15)
   * - FMT_NS0X: Number with separator, X decimal places (0-9)
   * - CXXX_0YY: Currency with 3-letter code and YY decimal places (00-15)
   */
  private generateFormatId(
    category: NumberFormatCategory,
    decimals: number,
    currency: CurrencyType | null,
    hasSeparator: boolean
  ): string {
    const dec2 = decimals.toString().padStart(2, "0");
    const dec1 = decimals.toString();

    switch (category) {
      case "PERCENTAGE":
        return `FMT_P0${dec2}`;

      case "CURRENCY":
        if (currency) {
          return `C${currency}_0${dec2}`;
        }
        // Default to USD if no currency specified
        return `CUSD_0${dec2}`;

      case "NUMBER":
        if (hasSeparator) {
          // FMT_NS0X only supports single digit decimals (0-9)
          // For 10-15 decimals with separator, fall back to without separator
          if (decimals <= 9) {
            return `FMT_NS0${dec1}`;
          }
          // Fall through to number without separator for >9 decimals
        }
        return `FMT_N0${dec2}`;

      default:
        // For other categories (DATE, TIME, etc.), just return NUMBER format
        return `FMT_N0${dec2}`;
    }
  }
}
