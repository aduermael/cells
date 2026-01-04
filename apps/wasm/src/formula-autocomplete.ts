// Formula Autocomplete - Shows function dropdown when editing formulas
// Displays available functions when typing after =, (, or , in formula bar

import type { FunctionInfo } from "./types";
import type { WasmDataSource } from "./wasm-data-source";

/** Callback when a function is selected */
export type OnSelectCallback = (functionName: string) => void;

/**
 * FormulaAutocomplete manages the function dropdown popup for the formula bar.
 * It shows matching functions as the user types in a formula.
 */
export class FormulaAutocomplete {
  private popup: HTMLElement;
  private dataSource: WasmDataSource;
  private functions: FunctionInfo[] = [];
  private filteredFunctions: FunctionInfo[] = [];
  private selectedIndex: number = 0;
  private visible: boolean = false;
  private onSelect: OnSelectCallback;
  private prefix: string = "";
  private inputElement: HTMLInputElement | null = null;

  constructor(
    container: HTMLElement,
    dataSource: WasmDataSource,
    onSelect: OnSelectCallback
  ) {
    this.dataSource = dataSource;
    this.onSelect = onSelect;

    // Create popup element
    this.popup = document.createElement("div");
    this.popup.className = "autocomplete-popup formula-autocomplete";
    this.popup.style.display = "none";
    container.appendChild(this.popup);

    // Load functions immediately
    this.loadFunctions();
  }

  /** Load available functions from the data source */
  private async loadFunctions(): Promise<void> {
    try {
      this.functions = await this.dataSource.getFormulaFunctions();
    } catch (err) {
      console.error("Failed to load formula functions:", err);
      this.functions = [];
    }
  }

  /** Set the input element to position the popup relative to */
  public setInputElement(input: HTMLInputElement): void {
    this.inputElement = input;
  }

  /**
   * Update autocomplete based on current formula text and cursor position.
   * Returns true if autocomplete should be shown.
   */
  public update(formula: string, cursorPos: number): boolean {
    // Find the trigger context (after =, (, or ,)
    const context = this.getTriggerContext(formula, cursorPos);
    if (!context) {
      this.hide();
      return false;
    }

    this.prefix = context.prefix.toUpperCase();

    // Filter functions by prefix
    if (this.prefix.length > 0) {
      this.filteredFunctions = this.functions.filter(
        (fn) =>
          fn.name.startsWith(this.prefix) ||
          fn.name.includes(this.prefix) // Also match functions containing prefix
      );

      // Sort: exact prefix match first, then contains
      this.filteredFunctions.sort((a, b) => {
        const aStartsWith = a.name.startsWith(this.prefix);
        const bStartsWith = b.name.startsWith(this.prefix);
        if (aStartsWith && !bStartsWith) return -1;
        if (!aStartsWith && bStartsWith) return 1;
        return a.name.localeCompare(b.name);
      });
    } else {
      // Show all functions when no prefix
      this.filteredFunctions = [...this.functions].sort((a, b) =>
        a.name.localeCompare(b.name)
      );
    }

    // Limit to first 10 results
    this.filteredFunctions = this.filteredFunctions.slice(0, 10);

    if (this.filteredFunctions.length === 0) {
      this.hide();
      return false;
    }

    this.selectedIndex = 0;
    this.render();
    this.show();
    return true;
  }

  /**
   * Get the trigger context - the position after = ( or , where we're typing.
   * Returns null if not in a valid autocomplete position.
   */
  private getTriggerContext(
    formula: string,
    cursorPos: number
  ): { prefix: string; triggerPos: number } | null {
    // Must start with = to be a formula
    if (!formula.startsWith("=")) {
      return null;
    }

    // Look backwards from cursor to find trigger
    let triggerPos = -1;
    let inString = false;
    let parenDepth = 0;

    for (let i = 0; i < cursorPos; i++) {
      const c = formula[i];

      // Track string literals
      if (c === '"' && (i === 0 || formula[i - 1] !== "\\")) {
        inString = !inString;
        continue;
      }
      if (inString) continue;

      // Track parentheses
      if (c === "(") {
        parenDepth++;
        triggerPos = i;
      } else if (c === ")") {
        parenDepth--;
      } else if (c === "," && parenDepth > 0) {
        triggerPos = i;
      } else if (c === "=") {
        triggerPos = i;
      }
    }

    if (triggerPos < 0) {
      return null;
    }

    // Extract prefix (text after trigger until cursor)
    const prefix = formula.substring(triggerPos + 1, cursorPos).trim();

    // Only show autocomplete for valid identifier prefixes
    if (prefix.length > 0 && !/^[A-Za-z][A-Za-z0-9]*$/.test(prefix)) {
      return null;
    }

    return { prefix, triggerPos };
  }

  /** Render the popup content */
  private render(): void {
    this.popup.innerHTML = "";

    this.filteredFunctions.forEach((fn, index) => {
      const item = document.createElement("div");
      item.className =
        "autocomplete-item" + (index === this.selectedIndex ? " selected" : "");
      item.dataset.index = String(index);

      // Function icon
      const icon = document.createElement("span");
      icon.className = "autocomplete-icon";
      icon.textContent = "f";
      item.appendChild(icon);

      // Function name
      const label = document.createElement("span");
      label.className = "autocomplete-label";
      label.textContent = fn.name;
      item.appendChild(label);

      // Signature
      const signature = document.createElement("span");
      signature.className = "autocomplete-signature";
      signature.textContent = fn.signature;
      item.appendChild(signature);

      // Click handler
      item.addEventListener("click", () => {
        this.selectCurrent();
      });

      // Hover handler
      item.addEventListener("mouseenter", () => {
        this.selectedIndex = index;
        this.updateSelection();
      });

      this.popup.appendChild(item);
    });
  }

  /** Update the visual selection */
  private updateSelection(): void {
    const items = this.popup.querySelectorAll(".autocomplete-item");
    items.forEach((item, index) => {
      if (index === this.selectedIndex) {
        item.classList.add("selected");
        // Scroll into view if needed
        item.scrollIntoView({ block: "nearest" });
      } else {
        item.classList.remove("selected");
      }
    });
  }

  /** Show the popup */
  private show(): void {
    if (this.visible) return;
    this.visible = true;
    this.popup.style.display = "block";
    this.positionPopup();
  }

  /** Hide the popup */
  public hide(): void {
    if (!this.visible) return;
    this.visible = false;
    this.popup.style.display = "none";
    this.prefix = "";
  }

  /** Position the popup near the input */
  private positionPopup(): void {
    if (!this.inputElement) return;

    const inputRect = this.inputElement.getBoundingClientRect();
    const containerRect = this.popup.parentElement?.getBoundingClientRect();
    if (!containerRect) return;

    // Position below the input
    this.popup.style.left = `${inputRect.left - containerRect.left}px`;
    this.popup.style.top = `${inputRect.bottom - containerRect.top + 2}px`;
  }

  /** Check if autocomplete is visible */
  public isVisible(): boolean {
    return this.visible;
  }

  /** Handle keyboard navigation. Returns true if key was handled. */
  public handleKeyDown(event: KeyboardEvent): boolean {
    if (!this.visible) return false;

    switch (event.key) {
      case "ArrowDown":
        event.preventDefault();
        this.selectedIndex = Math.min(
          this.selectedIndex + 1,
          this.filteredFunctions.length - 1
        );
        this.updateSelection();
        return true;

      case "ArrowUp":
        event.preventDefault();
        this.selectedIndex = Math.max(this.selectedIndex - 1, 0);
        this.updateSelection();
        return true;

      case "Tab":
      case "Enter":
        if (this.filteredFunctions.length > 0) {
          event.preventDefault();
          this.selectCurrent();
          return true;
        }
        break;

      case "Escape":
        event.preventDefault();
        this.hide();
        return true;
    }

    return false;
  }

  /** Select the currently highlighted function */
  private selectCurrent(): void {
    if (this.filteredFunctions.length === 0) return;

    const fn = this.filteredFunctions[this.selectedIndex];
    if (!fn) return;
    this.onSelect(fn.name);
    this.hide();
  }

  /** Get the current prefix being typed */
  public getPrefix(): string {
    return this.prefix;
  }

  /** Cleanup */
  public destroy(): void {
    this.popup.remove();
  }
}
