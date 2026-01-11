// Named Ranges Dropdown - Shows named ranges when clicking cell reference button
// Displays available named ranges for quick insertion into formulas

import type { NamedRangeInfo } from "./types";
import type { WasmDataSource } from "./wasm-data-source";

/** Callback when a named range is selected */
export type OnSelectCallback = (name: string) => void;

/**
 * NamedRangesDropdown manages the named ranges popup for the formula bar.
 * It shows all named ranges when clicking the cell reference button.
 */
export class NamedRangesDropdown {
  private popup: HTMLElement;
  private dataSource: WasmDataSource | null = null;
  private namedRanges: NamedRangeInfo[] = [];
  private selectedIndex: number = 0;
  private visible: boolean = false;
  private onSelect: OnSelectCallback;
  private triggerElement: HTMLElement;

  constructor(
    container: HTMLElement,
    triggerElement: HTMLElement,
    onSelect: OnSelectCallback
  ) {
    this.onSelect = onSelect;
    this.triggerElement = triggerElement;

    // Create popup element
    this.popup = document.createElement("div");
    this.popup.className = "named-ranges-dropdown";
    this.popup.style.display = "none";
    container.appendChild(this.popup);

    // Setup click handler on trigger element
    this.triggerElement.addEventListener("click", (e) => {
      e.stopPropagation();
      if (this.visible) {
        this.hide();
      } else {
        this.show();
      }
    });

    // Close popup when clicking outside
    document.addEventListener("click", (e) => {
      if (this.visible && !this.popup.contains(e.target as Node) &&
          !this.triggerElement.contains(e.target as Node)) {
        this.hide();
      }
    });

    // Handle keyboard navigation when popup is visible
    document.addEventListener("keydown", (e) => {
      if (this.visible) {
        this.handleKeyDown(e);
      }
    });
  }

  /** Set the data source */
  public setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
  }

  /** Load named ranges from the data source */
  private async loadNamedRanges(): Promise<void> {
    if (!this.dataSource) {
      this.namedRanges = [];
      return;
    }
    try {
      this.namedRanges = await this.dataSource.getNamedRanges();
    } catch (err) {
      console.error("Failed to load named ranges:", err);
      this.namedRanges = [];
    }
  }

  /** Show the popup */
  public async show(): Promise<void> {
    if (!this.dataSource) {
      // Show message when no data source
      this.popup.innerHTML = '<div class="named-ranges-empty">No file loaded</div>';
      this.visible = true;
      this.popup.style.display = "block";
      this.positionPopup();
      this.triggerElement.classList.add("active");
      return;
    }

    await this.loadNamedRanges();

    if (this.namedRanges.length === 0) {
      // Show empty state
      this.popup.innerHTML = '<div class="named-ranges-empty">No named ranges defined</div>';
    } else {
      this.selectedIndex = 0;
      this.render();
    }

    this.visible = true;
    this.popup.style.display = "block";
    this.positionPopup();
    this.triggerElement.classList.add("active");
  }

  /** Hide the popup */
  public hide(): void {
    if (!this.visible) return;
    this.visible = false;
    this.popup.style.display = "none";
    this.triggerElement.classList.remove("active");
  }

  /** Check if popup is visible */
  public isVisible(): boolean {
    return this.visible;
  }

  /** Position the popup below the trigger element */
  private positionPopup(): void {
    const triggerRect = this.triggerElement.getBoundingClientRect();
    const containerRect = this.popup.parentElement?.getBoundingClientRect();
    if (!containerRect) return;

    // Position below the trigger
    this.popup.style.left = `${triggerRect.left - containerRect.left}px`;
    this.popup.style.top = `${triggerRect.bottom - containerRect.top + 2}px`;
    this.popup.style.minWidth = `${Math.max(triggerRect.width, 200)}px`;
  }

  /** Render the popup content */
  private render(): void {
    this.popup.innerHTML = "";

    // Header
    const header = document.createElement("div");
    header.className = "named-ranges-header";
    header.textContent = "Named Ranges";
    this.popup.appendChild(header);

    // List container
    const list = document.createElement("div");
    list.className = "named-ranges-list";

    this.namedRanges.forEach((nr, index) => {
      const item = document.createElement("div");
      item.className =
        "named-range-item" + (index === this.selectedIndex ? " selected" : "");
      item.dataset.index = String(index);

      // Name
      const name = document.createElement("span");
      name.className = "named-range-name";
      name.textContent = nr.name;
      item.appendChild(name);

      // Scope indicator
      if (nr.scope === "sheet") {
        const scope = document.createElement("span");
        scope.className = "named-range-scope";
        scope.textContent = "(Sheet)";
        item.appendChild(scope);
      }

      // Click handler
      item.addEventListener("click", () => {
        this.selectedIndex = index;
        this.selectCurrent();
      });

      // Hover handler
      item.addEventListener("mouseenter", () => {
        this.selectedIndex = index;
        this.updateSelection();
      });

      list.appendChild(item);
    });

    this.popup.appendChild(list);
  }

  /** Update the visual selection */
  private updateSelection(): void {
    const items = this.popup.querySelectorAll(".named-range-item");
    items.forEach((item, index) => {
      if (index === this.selectedIndex) {
        item.classList.add("selected");
        item.scrollIntoView({ block: "nearest" });
      } else {
        item.classList.remove("selected");
      }
    });
  }

  /** Handle keyboard navigation */
  private handleKeyDown(event: KeyboardEvent): void {
    if (this.namedRanges.length === 0) {
      if (event.key === "Escape") {
        event.preventDefault();
        this.hide();
      }
      return;
    }

    switch (event.key) {
      case "ArrowDown":
        event.preventDefault();
        this.selectedIndex = Math.min(
          this.selectedIndex + 1,
          this.namedRanges.length - 1
        );
        this.updateSelection();
        break;

      case "ArrowUp":
        event.preventDefault();
        this.selectedIndex = Math.max(this.selectedIndex - 1, 0);
        this.updateSelection();
        break;

      case "Tab":
      case "Enter":
        event.preventDefault();
        this.selectCurrent();
        break;

      case "Escape":
        event.preventDefault();
        this.hide();
        break;
    }
  }

  /** Select the currently highlighted named range */
  private selectCurrent(): void {
    if (this.namedRanges.length === 0) return;

    const nr = this.namedRanges[this.selectedIndex];
    if (!nr) return;
    this.onSelect(nr.name);
    this.hide();
  }

  /** Cleanup */
  public destroy(): void {
    this.popup.remove();
  }
}
