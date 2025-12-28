// Context Menu - Generic right-click context menu system
// Provides a reusable context menu that can be shown at any position
// with context-dependent menu items.

import { getMenuStateManager } from "./menu-state";

// =============================================================================
// Types
// =============================================================================

/**
 * Represents a single item in the context menu
 */
export interface ContextMenuItem {
  /** Display label for the menu item */
  label: string;
  /** Action to execute when clicked */
  action: () => void;
  /** Optional icon (emoji or text) */
  icon?: string;
  /** Whether the item is disabled */
  disabled?: boolean;
  /** Whether this is a destructive/danger action (shown in red) */
  danger?: boolean;
}

/**
 * Separator between menu item groups
 */
export interface ContextMenuSeparator {
  type: "separator";
}

export type ContextMenuEntry = ContextMenuItem | ContextMenuSeparator;

/**
 * Context types for determining what was right-clicked
 */
export type ContextType =
  | { type: "cell"; col: number; row: number; colId: string; rowId: string }
  | { type: "column-header"; col: number; colId: string }
  | { type: "row-header"; row: number; rowId: string }
  | { type: "corner" }
  | { type: "empty" };

// =============================================================================
// ContextMenuManager Class (Singleton)
// =============================================================================

/**
 * ContextMenuManager handles showing and hiding context menus.
 *
 * Features:
 * - Shows context menu at specified position
 * - Automatically positions menu to avoid screen edges
 * - Closes on click outside, Escape key, or scroll
 * - Supports disabled items and danger styling
 * - Animates menu appearance
 */
export class ContextMenuManager {
  private static instance: ContextMenuManager | null = null;

  private menuElement: HTMLElement | null = null;
  private isVisible: boolean = false;

  // Bound handlers for cleanup
  private boundHandleClickOutside: (e: MouseEvent) => void;
  private boundHandleKeydown: (e: KeyboardEvent) => void;
  private boundHandleScroll: () => void;

  // =========================================================================
  // Constructor (private for singleton)
  // =========================================================================

  private constructor() {
    this.boundHandleClickOutside = this.handleClickOutside.bind(this);
    this.boundHandleKeydown = this.handleKeydown.bind(this);
    this.boundHandleScroll = this.hide.bind(this);

    // Register with menu state manager
    const menuState = getMenuStateManager();
    menuState.registerMenu("context", () => {
      this.hide();
    });
  }

  // =========================================================================
  // Singleton Access
  // =========================================================================

  static getInstance(): ContextMenuManager {
    if (!ContextMenuManager.instance) {
      ContextMenuManager.instance = new ContextMenuManager();
    }
    return ContextMenuManager.instance;
  }

  // =========================================================================
  // Public Methods
  // =========================================================================

  /**
   * Show context menu at the specified position
   */
  show(x: number, y: number, items: ContextMenuEntry[]): void {
    // Notify menu state manager (closes other menus)
    const menuState = getMenuStateManager();
    menuState.openMenu("context");

    // Hide any existing menu first
    this.hide();

    // Filter out empty items
    const validItems = items.filter((item) => {
      if ("type" in item && item.type === "separator") return true;
      return "label" in item && item.label;
    });

    if (validItems.length === 0) return;

    // Create menu element
    this.menuElement = this.createMenuElement(validItems);
    document.body.appendChild(this.menuElement);

    // Position the menu (adjust for screen edges)
    this.positionMenu(x, y);

    // Trigger animation
    requestAnimationFrame(() => {
      if (this.menuElement) {
        this.menuElement.classList.add("visible");
      }
    });

    this.isVisible = true;

    // Add event listeners for closing
    setTimeout(() => {
      document.addEventListener("click", this.boundHandleClickOutside);
      document.addEventListener("keydown", this.boundHandleKeydown);
      window.addEventListener("scroll", this.boundHandleScroll, true);
    }, 0);
  }

  /**
   * Hide the context menu
   */
  hide(): void {
    if (this.menuElement) {
      this.menuElement.remove();
      this.menuElement = null;
    }
    this.isVisible = false;

    // Notify menu state manager
    const menuState = getMenuStateManager();
    menuState.closeMenu("context");

    // Remove event listeners
    document.removeEventListener("click", this.boundHandleClickOutside);
    document.removeEventListener("keydown", this.boundHandleKeydown);
    window.removeEventListener("scroll", this.boundHandleScroll, true);
  }

  /**
   * Check if menu is currently visible
   */
  getIsVisible(): boolean {
    return this.isVisible;
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Create the menu DOM element
   */
  private createMenuElement(items: ContextMenuEntry[]): HTMLElement {
    const menu = document.createElement("div");
    menu.className = "context-menu";

    items.forEach((item, index) => {
      if ("type" in item && item.type === "separator") {
        const separator = document.createElement("div");
        separator.className = "context-menu-separator";
        menu.appendChild(separator);
        return;
      }

      const menuItem = item as ContextMenuItem;
      const button = document.createElement("button");
      button.className = "context-menu-item";

      if (menuItem.disabled) {
        button.classList.add("disabled");
        button.disabled = true;
      }

      if (menuItem.danger) {
        button.classList.add("danger");
      }

      // Icon (if provided)
      if (menuItem.icon) {
        const iconSpan = document.createElement("span");
        iconSpan.className = "context-menu-icon";
        iconSpan.textContent = menuItem.icon;
        button.appendChild(iconSpan);
      }

      // Label
      const labelSpan = document.createElement("span");
      labelSpan.className = "context-menu-label";
      labelSpan.textContent = menuItem.label;
      button.appendChild(labelSpan);

      // Click handler
      button.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        this.hide();
        menuItem.action();
      });

      // First/last item border radius
      if (index === 0) {
        button.classList.add("first");
      }
      if (index === items.length - 1) {
        button.classList.add("last");
      }

      menu.appendChild(button);
    });

    return menu;
  }

  /**
   * Position the menu, adjusting for screen edges
   */
  private positionMenu(x: number, y: number): void {
    if (!this.menuElement) return;

    const menu = this.menuElement;
    const padding = 8; // Padding from screen edges

    // Initially position off-screen to measure
    menu.style.left = "-9999px";
    menu.style.top = "-9999px";

    // Get menu dimensions
    const menuRect = menu.getBoundingClientRect();
    const menuWidth = menuRect.width;
    const menuHeight = menuRect.height;

    // Get viewport dimensions
    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;

    // Adjust X position
    let finalX = x;
    if (x + menuWidth + padding > viewportWidth) {
      // Menu would overflow right, flip to left side
      finalX = x - menuWidth;
      if (finalX < padding) {
        finalX = padding;
      }
    }

    // Adjust Y position
    let finalY = y;
    if (y + menuHeight + padding > viewportHeight) {
      // Menu would overflow bottom, flip to top
      finalY = y - menuHeight;
      if (finalY < padding) {
        finalY = padding;
      }
    }

    menu.style.left = `${finalX}px`;
    menu.style.top = `${finalY}px`;
  }

  /**
   * Handle click outside the menu
   */
  private handleClickOutside(e: MouseEvent): void {
    if (this.menuElement && !this.menuElement.contains(e.target as Node)) {
      this.hide();
    }
  }

  /**
   * Handle keydown events
   */
  private handleKeydown(e: KeyboardEvent): void {
    if (e.key === "Escape") {
      e.preventDefault();
      this.hide();
    }
  }
}

// =============================================================================
// Convenience Function
// =============================================================================

/**
 * Show a context menu at the specified position
 */
export function showContextMenu(
  x: number,
  y: number,
  items: ContextMenuEntry[]
): void {
  ContextMenuManager.getInstance().show(x, y, items);
}

/**
 * Hide the context menu
 */
export function hideContextMenu(): void {
  ContextMenuManager.getInstance().hide();
}
