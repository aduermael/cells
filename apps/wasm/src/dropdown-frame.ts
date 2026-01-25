// =============================================================================
// Dropdown Frame Component
// =============================================================================
//
// Reusable dropdown container that provides consistent behavior across all
// dropdown menus in the application.
//
// Features:
// - Automatic registration with MenuStateManager (ensures only one dropdown open)
// - Consistent positioning via positionDropdown() utility
// - Outside click detection to close
// - Escape key handling to close
// - Applies .dropdown-frame CSS class for consistent styling
//
// Usage:
//   const frame = new DropdownFrame({
//     anchor: buttonElement,
//     content: menuElement,
//     menuId: 'myMenu',
//     onOpen: () => console.log('opened'),
//     onClose: () => console.log('closed'),
//   });
//
//   frame.toggle(); // or frame.open() / frame.close()
//
// =============================================================================

import { getMenuStateManager, type MenuId } from "./menu-state";
import { positionDropdown, type PositionDropdownOptions } from "./dropdown-utils";

// =============================================================================
// Types
// =============================================================================

/** Options for creating a DropdownFrame */
export interface DropdownFrameOptions {
  /** The element that triggers the dropdown (e.g., a button) */
  anchor: HTMLElement;
  /** The dropdown content element */
  content: HTMLElement;
  /** Unique menu ID for MenuStateManager registration */
  menuId: MenuId;
  /** Callback when dropdown opens */
  onOpen?: () => void;
  /** Callback when dropdown closes */
  onClose?: () => void;
  /** Positioning options passed to positionDropdown() */
  positionOptions?: PositionDropdownOptions;
}

// =============================================================================
// DropdownFrame Class
// =============================================================================

/**
 * DropdownFrame provides a consistent wrapper for dropdown menus.
 *
 * It handles:
 * - MenuStateManager integration (only one dropdown open at a time)
 * - Viewport-aware positioning
 * - Outside click and Escape key to close
 * - Consistent CSS class application
 */
export class DropdownFrame {
  private anchor: HTMLElement;
  private content: HTMLElement;
  private menuId: MenuId;
  private onOpenCallback?: () => void;
  private onCloseCallback?: () => void;
  private positionOptions?: PositionDropdownOptions;
  private _isOpen = false;

  // Bound event handlers (for cleanup)
  private boundHandleOutsideClick: (e: MouseEvent) => void;
  private boundHandleKeyDown: (e: KeyboardEvent) => void;

  constructor(options: DropdownFrameOptions) {
    this.anchor = options.anchor;
    this.content = options.content;
    this.menuId = options.menuId;
    this.onOpenCallback = options.onOpen;
    this.onCloseCallback = options.onClose;
    this.positionOptions = options.positionOptions;

    // Bind handlers for later removal
    this.boundHandleOutsideClick = this.handleOutsideClick.bind(this);
    this.boundHandleKeyDown = this.handleKeyDown.bind(this);

    // Apply base CSS class
    this.content.classList.add("dropdown-frame");

    // Ensure content is hidden initially
    this.content.classList.add("hidden");

    // Register with MenuStateManager
    const menuState = getMenuStateManager();
    menuState.registerMenu(this.menuId, () => this.close());
  }

  // ===========================================================================
  // Public Methods
  // ===========================================================================

  /**
   * Open the dropdown.
   * Notifies MenuStateManager which will close any other open menus.
   */
  open(): void {
    if (this._isOpen) return;

    // Notify MenuStateManager - this closes other menus
    const menuState = getMenuStateManager();
    menuState.openMenu(this.menuId);

    // Show content
    this.content.classList.remove("hidden");
    this._isOpen = true;

    // Position relative to anchor
    const anchorRect = this.anchor.getBoundingClientRect();
    positionDropdown(this.content, anchorRect, this.positionOptions);

    // Add event listeners for closing
    // Use setTimeout to avoid catching the click that opened the dropdown
    setTimeout(() => {
      document.addEventListener("click", this.boundHandleOutsideClick);
      document.addEventListener("keydown", this.boundHandleKeyDown);
    }, 0);

    // Invoke callback
    this.onOpenCallback?.();
  }

  /**
   * Close the dropdown.
   */
  close(): void {
    if (!this._isOpen) return;

    // Notify MenuStateManager
    const menuState = getMenuStateManager();
    menuState.closeMenu(this.menuId);

    // Hide content
    this.content.classList.add("hidden");
    this._isOpen = false;

    // Remove event listeners
    document.removeEventListener("click", this.boundHandleOutsideClick);
    document.removeEventListener("keydown", this.boundHandleKeyDown);

    // Invoke callback
    this.onCloseCallback?.();
  }

  /**
   * Toggle the dropdown open/closed state.
   */
  toggle(): void {
    if (this._isOpen) {
      this.close();
    } else {
      this.open();
    }
  }

  /**
   * Check if the dropdown is currently open.
   */
  isOpen(): boolean {
    return this._isOpen;
  }

  /**
   * Cleanup when the dropdown is no longer needed.
   * Unregisters from MenuStateManager and removes event listeners.
   */
  destroy(): void {
    this.close();
    const menuState = getMenuStateManager();
    menuState.unregisterMenu(this.menuId);
  }

  // ===========================================================================
  // Private Methods
  // ===========================================================================

  /**
   * Handle clicks outside the dropdown to close it.
   */
  private handleOutsideClick(e: MouseEvent): void {
    const target = e.target as Node;
    // Close if click is outside both anchor and content
    if (!this.anchor.contains(target) && !this.content.contains(target)) {
      this.close();
    }
  }

  /**
   * Handle Escape key to close the dropdown.
   */
  private handleKeyDown(e: KeyboardEvent): void {
    if (e.key === "Escape") {
      e.preventDefault();
      this.close();
    }
  }
}
