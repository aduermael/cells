// =============================================================================
// Format Controls
// =============================================================================
//
// Toolbar UI for cell number formatting. Provides dropdowns and buttons for
// changing how numbers are displayed (currency, percentage, decimals).
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Format category dropdown (General, Number, Currency, Date, etc.)
// - Currency type dropdown (USD, EUR, GBP, JPY, CNY)
// - Decimal increase/decrease buttons
// - Percentage toggle button
// - Custom format input panel
// - Apply format to selected cells
//
// Format flow:
// - User selects format → FormatControls → WasmDataSource.setNumberFormat()
// - C++ applies CRDT op → notifies change → GridRenderer re-renders cell
//
// Format IDs follow pattern: FMT_<category><precision>
// Examples: FMT_N002 (number, 2 decimals), FMT_P001 (percent, 1 decimal)
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { NumberFormatCategory, Position, CellData } from "./types";

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
  percentBtn: HTMLButtonElement;
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
  private percentBtn: HTMLButtonElement;
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
    this.percentBtn = config.percentBtn;
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
  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
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

    // Currency button: apply currency when not in currency mode, toggle dropdown when in currency mode
    this.currencyDropdownBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      if (this.currentCategory === "CURRENCY" || this.currentCategory === "ACCOUNTING") {
        // Already in currency mode - toggle dropdown to change currency
        this.toggleCurrencyDropdown();
      } else {
        // Not in currency mode - apply currency format directly
        this.handleCurrencySelect(this.currentCurrency, this.getCurrencySymbol(this.currentCurrency));
      }
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

    // Percent button
    this.percentBtn.addEventListener("click", () => {
      this.handleCategorySelect("PERCENTAGE");
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

  private async getCategoryForFormatId(formatId: string): Promise<NumberFormatCategory> {
    if (formatId === "~" || formatId === "") {
      return "GENERAL";
    }

    // Query C++ for format details (single source of truth)
    if (this.dataSource) {
      const details = await this.dataSource.client.getFormatDetails(formatId);
      if (!details.error) {
        return details.category.toUpperCase() as NumberFormatCategory;
      }
    }

    return "GENERAL";
  }

  private async getFormatIdForCategory(category: NumberFormatCategory): Promise<string> {
    if (!this.dataSource) return "~";

    // Use sensible defaults for each category
    // Currency: 2 decimals (industry standard: $100.00)
    // Number: 2 decimals (matches Excel Number format default)
    // Percentage: 0 decimals (clean: 15%)
    let decimals = 0;
    let separator = false;
    let currency = "";

    const categoryLower = category.toLowerCase();

    switch (category) {
      case "CURRENCY":
        decimals = 2;
        separator = true;
        currency = this.currentCurrency; // Use currently selected currency
        break;
      case "NUMBER":
        decimals = 2;
        separator = false;
        break;
      case "PERCENTAGE":
        decimals = 0;
        separator = false;
        break;
      default:
        // For non-numeric categories (DATE, TIME, etc.), return well-known format IDs
        if (category === "DATE") return "FMT_DSHT";
        if (category === "TIME") return "FMT_T12H";
        if (category === "SCIENTIFIC") return "FMT_SCI2";
        if (category === "TEXT") return "FMT_TEXT";
        if (category === "GENERAL") return "~";
        // Fall back to number format (2 decimals, matches Excel)
        return "FMT_N002";
    }

    const result = await this.dataSource.client.makeFormatId(
      categoryLower,
      decimals,
      separator,
      currency
    );

    return result.formatId || "~";
  }

  private setDisplayedFormat(formatId: string, category: NumberFormatCategory): void {
    this.currentCategory = category;

    // Update dropdown label
    this.formatDropdownLabel.textContent = this.getCategoryDisplayName(category);

    // Update currency dropdown active state
    const isCurrency = category === "CURRENCY" || category === "ACCOUNTING";
    this.currencyDropdown.classList.toggle("active", isCurrency);

    // Update percent button active state
    const isPercentage = category === "PERCENTAGE";
    this.percentBtn.classList.toggle("active", isPercentage);

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
    const formatId = await this.getFormatIdForCategory(category);

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

    // Get format details from C++ (single source of truth)
    const details = await this.dataSource.client.getFormatDetails(currentFormatId);
    if (details.error) {
      console.error("Failed to get format details:", details.error);
      return;
    }

    // Calculate new decimals (0-15 range)
    const newDecimals = Math.max(0, Math.min(15, details.decimals + delta));

    // Generate the new format ID via C++ API
    const result = await this.dataSource.client.makeFormatId(
      details.category,
      newDecimals,
      details.separator,
      details.currency || ""
    );

    if (result.error || !result.formatId) {
      console.error("Failed to generate format ID:", result.error);
      return;
    }

    try {
      await this.dataSource.setCellFormatAt(position.col, position.row, result.formatId);
      this.setDisplayedFormat(result.formatId, details.category.toUpperCase() as NumberFormatCategory);
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to change decimal places:", error);
    }
  }

}
