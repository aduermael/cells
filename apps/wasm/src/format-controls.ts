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
import type { NumberFormatCategory, Position, CellData, FormatProperties } from "./types";
import { positionDropdown } from "./dropdown-utils";
import { getMenuStateManager } from "./menu-state";

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
  /** Get the current selection range (start and end) */
  getSelectionRange: () => { start: Position | null; end: Position | null };
  /** Get cell data at a specific position */
  getCellDataAt: (col: number, row: number) => CellData | null;
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
  private getSelectionRange: () => { start: Position | null; end: Position | null };
  private getCellDataAt: (col: number, row: number) => CellData | null;
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
    this.getSelectionRange = callbacks.getSelectionRange;
    this.getCellDataAt = callbacks.getCellDataAt;
    this.requestRender = callbacks.requestRender;
    this.updateFormulaBar = callbacks.updateFormulaBar;

    this.setupEventListeners();

    // Register menus with MenuStateManager for mutual exclusivity
    const menuState = getMenuStateManager();
    menuState.registerMenu("format", () => this.closeDropdown());
    menuState.registerMenu("currency", () => this.closeCurrencyDropdown());
    menuState.registerMenu("customFormat", () => this.closeCustomFormatPanel());
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
      this.setDisplayedFormat("GENERAL");
      return;
    }

    // Check if selection has mixed formats
    const { start, end } = this.getSelectionRange();
    if (start && end && (start.col !== end.col || start.row !== end.row)) {
      const hasMixed = this.checkMixedFormats(start, end);
      if (hasMixed) {
        this.setDisplayedFormat("MIXED");
        return;
      }
    }

    // Get format from cell data
    const formatBase64 = cellData.format || "";
    const category = await this.getCategoryForFormat(formatBase64);
    this.setDisplayedFormat(category);
  }

  /**
   * Check if cells in the selection range have different formats.
   */
  private checkMixedFormats(start: Position, end: Position): boolean {
    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    // Get the format of the first cell (anchor)
    const firstCell = this.getCellDataAt(minCol, minRow);
    const firstFormat = firstCell?.format || "";

    // Check all cells in range
    for (let col = minCol; col <= maxCol; col++) {
      for (let row = minRow; row <= maxRow; row++) {
        if (col === minCol && row === minRow) continue; // Skip first cell
        const cell = this.getCellDataAt(col, row);
        const cellFormat = cell?.format || "";
        if (cellFormat !== firstFormat) {
          return true; // Found different format
        }
      }
    }

    return false; // All cells have same format
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

    // Currency button: apply last-used currency when not in currency mode, toggle dropdown when in currency mode
    this.currencyDropdownBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      if (this.currentCategory === "CURRENCY" || this.currentCategory === "ACCOUNTING") {
        // Already in currency mode - toggle dropdown to change currency
        this.toggleCurrencyDropdown();
      } else {
        // Not in currency mode - apply last-used currency format directly
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
      // Escape key closes all format dropdowns
      if (e.key === "Escape") {
        const hasOpenDropdown =
          this.isDropdownOpen ||
          this.isCurrencyDropdownOpen ||
          this.isCustomFormatPanelOpen;

        if (hasOpenDropdown) {
          e.preventDefault();
          // Close all dropdowns and notify MenuStateManager
          const menuState = getMenuStateManager();
          if (this.isDropdownOpen) {
            this.closeDropdown();
            menuState.closeMenu("format");
          }
          if (this.isCurrencyDropdownOpen) {
            this.closeCurrencyDropdown();
            menuState.closeMenu("currency");
          }
          if (this.isCustomFormatPanelOpen) {
            this.closeCustomFormatPanel();
            menuState.closeMenu("customFormat");
          }
          return;
        }
      }

      // Only handle other shortcuts if no input/textarea is focused
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
    // Position the dropdown menu to stay within viewport bounds
    const buttonRect = this.formatDropdownBtn.getBoundingClientRect();
    positionDropdown(this.formatDropdownMenu, buttonRect);
    this.updateDropdownActiveState();
    // Notify MenuStateManager - this will close other menus via their callbacks
    getMenuStateManager().openMenu("format");
  }

  private closeDropdown(): void {
    if (this.isDropdownOpen) {
      this.isDropdownOpen = false;
      this.formatDropdown.classList.remove("open");
    }
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
    // Position the dropdown menu to stay within viewport bounds
    const buttonRect = this.currencyDropdownBtn.getBoundingClientRect();
    positionDropdown(this.currencyDropdownMenu, buttonRect);
    this.updateCurrencyDropdownActiveState();
    // Notify MenuStateManager - this will close other menus via their callbacks
    getMenuStateManager().openMenu("currency");
  }

  private closeCurrencyDropdown(): void {
    if (this.isCurrencyDropdownOpen) {
      this.isCurrencyDropdownOpen = false;
      this.currencyDropdown.classList.remove("open");
    }
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

    // Create format properties for this currency (with 2 decimal places, industry standard)
    const format: FormatProperties = {
      category: "CURRENCY",
      decimals: 2,
      separator: true,
      currency: symbol,
    };

    try {
      await this.applyFormatToSelection(format);
      this.currentCurrency = currency;
      this.currencyDropdownLabel.textContent = symbol;
      this.setDisplayedFormat("CURRENCY");
      this.currencyDropdown.classList.add("active");
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to set currency format:", error);
    }
  }

  private getCurrencySymbolForType(currency: CurrencyType): string {
    switch (currency) {
      case "USD": return "$";
      case "EUR": return "€";
      case "GBP": return "£";
      case "JPY": return "¥";
      case "CNY": return "¥";
      default: return "$";
    }
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

    // Notify MenuStateManager - this will close other menus via their callbacks
    getMenuStateManager().openMenu("customFormat");
  }

  private closeCustomFormatPanel(): void {
    if (this.isCustomFormatPanelOpen) {
      this.isCustomFormatPanelOpen = false;
      this.customFormatPanel.classList.add("hidden");
      if (this.customFormatDebounceTimer) {
        clearTimeout(this.customFormatDebounceTimer);
        this.customFormatDebounceTimer = null;
      }
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
      // Create the custom format and get its properties
      const result = await this.dataSource.client.createCustomFormat(formatCode);
      if (result.error) {
        this.customFormatError.textContent = result.error;
        this.customFormatError.classList.remove("hidden");
        return;
      }

      if (result.format) {
        // Apply the format to all cells in selection range
        await this.applyFormatToSelection(result.format);

        // Update display
        this.setDisplayedFormat("CUSTOM");

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

  private async getCategoryForFormat(formatBase64: string): Promise<NumberFormatCategory> {
    if (!formatBase64 || formatBase64 === "") {
      return "GENERAL";
    }

    // Query C++ for format details (single source of truth)
    if (this.dataSource) {
      const details = await this.dataSource.client.getFormatDetails(formatBase64);
      if (!details.error && details.category) {
        return details.category;
      }
    }

    return "GENERAL";
  }

  private getFormatForCategory(category: NumberFormatCategory): FormatProperties {
    // Create format properties for each category with sensible defaults
    switch (category) {
      case "CURRENCY":
        return {
          category: "CURRENCY",
          decimals: 2,
          separator: true,
          currency: this.getCurrencySymbolForType(this.currentCurrency),
        };
      case "ACCOUNTING":
        return {
          category: "ACCOUNTING",
          decimals: 2,
          separator: true,
          currency: this.getCurrencySymbolForType(this.currentCurrency),
        };
      case "NUMBER":
        return {
          category: "NUMBER",
          decimals: 2,
          separator: false,
        };
      case "PERCENTAGE":
        return {
          category: "PERCENTAGE",
          decimals: 0,
        };
      case "DATE":
        return {
          category: "DATE",
          formatCode: "m/d/yyyy",
        };
      case "TIME":
        return {
          category: "TIME",
          formatCode: "h:mm AM/PM",
        };
      case "DATE_TIME":
        return {
          category: "DATE_TIME",
          formatCode: "m/d/yyyy h:mm",
        };
      case "SCIENTIFIC":
        return {
          category: "SCIENTIFIC",
          decimals: 2,
        };
      case "FRACTION":
        return {
          category: "FRACTION",
          formatCode: "# ?/?",
        };
      case "TEXT":
        return {
          category: "TEXT",
        };
      case "GENERAL":
      default:
        return {
          category: "GENERAL",
        };
    }
  }

  private setDisplayedFormat(category: NumberFormatCategory | "MIXED"): void {
    // Store real category (not MIXED) for format operations
    if (category !== "MIXED") {
      this.currentCategory = category;
    }

    // Update dropdown label
    this.formatDropdownLabel.textContent = this.getCategoryDisplayName(category);

    // Update currency dropdown active state (inactive for MIXED)
    const isCurrency = category === "CURRENCY" || category === "ACCOUNTING";
    this.currencyDropdown.classList.toggle("active", isCurrency);

    // Update percent button active state (inactive for MIXED)
    const isPercentage = category === "PERCENTAGE";
    this.percentBtn.classList.toggle("active", isPercentage);

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

  private getCategoryDisplayName(category: NumberFormatCategory | "MIXED"): string {
    const names: Record<NumberFormatCategory | "MIXED", string> = {
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
      MIXED: "Multiple",
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

    // Get the format properties for this category
    const format = this.getFormatForCategory(category);

    try {
      // Apply format to all cells in selection range
      await this.applyFormatToSelection(format);

      // Update display
      this.setDisplayedFormat(category);

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
    const formatBase64 = cellData?.format || "";

    // Get format details from C++ (single source of truth)
    const details = await this.dataSource.client.getFormatDetails(formatBase64);
    if (details.error) {
      console.error("Failed to get format details:", details.error);
      return;
    }

    // Calculate new decimals (0-15 range)
    const currentDecimals = details.decimals ?? 0;
    const newDecimals = Math.max(0, Math.min(15, currentDecimals + delta));

    // Create new format with updated decimals
    const category = details.category || "NUMBER";
    const format: FormatProperties = {
      category: category,
      decimals: newDecimals,
      separator: details.separator,
      currency: details.currency,
    };

    try {
      await this.applyFormatToSelection(format);
      this.setDisplayedFormat(category);
      this.requestRender();
      this.updateFormulaBar();
    } catch (error) {
      console.error("Failed to change decimal places:", error);
    }
  }

  /**
   * Apply a format to all cells in the current selection range.
   */
  private async applyFormatToSelection(format: FormatProperties): Promise<void> {
    if (!this.dataSource) return;

    const { start, end } = this.getSelectionRange();
    const cell = this.getSelectedCell();

    // If no range selection, just apply to selected cell
    if (!start || !end || (start.col === end.col && start.row === end.row)) {
      if (cell) {
        await this.dataSource.setCellFormatAt(cell.col, cell.row, format);
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
        await this.dataSource.setCellFormatAt(col, row, format);
      }
    }
  }

}
