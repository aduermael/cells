// Menu State Manager - Ensures only one menu can be open at a time
// Manages mutual exclusivity between all menus: Export dropdown, Collaborate panel,
// Context menu, and all toolbar menus (colors, fonts, format, merge, border)

// ============================================================================
// Types
// ============================================================================

/** Menu identifier type */
export type MenuId =
  | "export"
  | "collaborate"
  | "context"
  | "bgColor"
  | "textColor"
  | "fontFamily"
  | "fontSize"
  | "format"
  | "currency"
  | "customFormat"
  | "merge"
  | "border"
  | "namedRanges";

/** Callback for when a menu is closed externally */
export type MenuCloseCallback = () => void;

/** Registered menu with its close callback */
interface RegisteredMenu {
  id: MenuId;
  close: MenuCloseCallback;
}

// ============================================================================
// MenuStateManager Class
// ============================================================================

/**
 * Singleton manager that ensures only one menu is open at a time.
 *
 * When a menu is opened via `openMenu(id)`, all other registered menus
 * are automatically closed. This provides a consistent UX where dropdowns,
 * panels, and context menus don't overlap.
 */
class MenuStateManager {
  private static _instance: MenuStateManager | null = null;

  private _menus: Map<MenuId, RegisteredMenu> = new Map();
  private _currentOpenMenu: MenuId | null = null;

  private constructor() {
    // Private constructor for singleton pattern
  }

  /**
   * Get the singleton instance
   */
  static getInstance(): MenuStateManager {
    if (!MenuStateManager._instance) {
      MenuStateManager._instance = new MenuStateManager();
    }
    return MenuStateManager._instance;
  }

  /**
   * Register a menu with its close callback.
   * Call this once when the menu component is created.
   *
   * @param id - Unique identifier for the menu
   * @param closeCallback - Function to call to close this menu
   */
  registerMenu(id: MenuId, closeCallback: MenuCloseCallback): void {
    this._menus.set(id, {
      id,
      close: closeCallback,
    });
  }

  /**
   * Unregister a menu (e.g., when component is destroyed)
   *
   * @param id - The menu identifier to unregister
   */
  unregisterMenu(id: MenuId): void {
    this._menus.delete(id);
    if (this._currentOpenMenu === id) {
      this._currentOpenMenu = null;
    }
  }

  /**
   * Mark a menu as open. This closes all other registered menus.
   *
   * @param id - The menu identifier being opened
   */
  openMenu(id: MenuId): void {
    // Close all other menus
    for (const [menuId, menu] of this._menus) {
      if (menuId !== id) {
        menu.close();
      }
    }
    this._currentOpenMenu = id;
  }

  /**
   * Mark a menu as closed.
   *
   * @param id - The menu identifier being closed
   */
  closeMenu(id: MenuId): void {
    if (this._currentOpenMenu === id) {
      this._currentOpenMenu = null;
    }
  }

  /**
   * Close all registered menus.
   */
  closeAllMenus(): void {
    for (const menu of this._menus.values()) {
      menu.close();
    }
    this._currentOpenMenu = null;
  }

  /**
   * Check if a specific menu is currently open.
   *
   * @param id - The menu identifier to check
   * @returns true if the specified menu is open
   */
  isMenuOpen(id: MenuId): boolean {
    return this._currentOpenMenu === id;
  }

  /**
   * Get the currently open menu ID, if any.
   *
   * @returns The currently open menu ID, or null if none
   */
  getCurrentOpenMenu(): MenuId | null {
    return this._currentOpenMenu;
  }
}

// ============================================================================
// Singleton Export
// ============================================================================

/**
 * Get the global MenuStateManager instance.
 * Use this to register menus and manage their open/close state.
 */
export function getMenuStateManager(): MenuStateManager {
  return MenuStateManager.getInstance();
}
