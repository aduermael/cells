// Modal - Reusable modal dialog system
// Provides styled modal dialogs that replace native browser dialogs.

// =============================================================================
// Types
// =============================================================================

/**
 * Options for creating a modal dialog
 */
export interface ModalOptions {
  /** Title shown in the modal header */
  title: string;
  /** Body content (HTML string) */
  body: string;
  /** Primary (confirm) button label */
  primaryLabel?: string;
  /** Secondary (cancel) button label */
  secondaryLabel?: string;
  /** Optional icon (emoji or text) */
  icon?: string;
  /** Whether to show a warning style */
  warning?: boolean;
}

// =============================================================================
// ModalManager Class (Singleton)
// =============================================================================

/**
 * ModalManager handles showing and hiding modal dialogs.
 *
 * Features:
 * - Shows modal dialogs with customizable content
 * - Returns Promise that resolves when user clicks a button
 * - Supports keyboard navigation (Enter to confirm, Escape to cancel)
 * - Closes on backdrop click
 * - Traps focus within modal
 */
export class ModalManager {
  private static instance: ModalManager | null = null;

  private overlayElement: HTMLElement | null = null;
  private modalElement: HTMLElement | null = null;
  private isVisible: boolean = false;
  private resolvePromise: ((value: boolean) => void) | null = null;

  // Bound handlers for cleanup
  private boundHandleKeydown: (e: KeyboardEvent) => void;
  private boundHandleBackdropClick: (e: MouseEvent) => void;

  // =========================================================================
  // Constructor (private for singleton)
  // =========================================================================

  private constructor() {
    this.boundHandleKeydown = this.handleKeydown.bind(this);
    this.boundHandleBackdropClick = this.handleBackdropClick.bind(this);
  }

  // =========================================================================
  // Singleton Access
  // =========================================================================

  /**
   * Get the singleton instance
   */
  public static getInstance(): ModalManager {
    if (!ModalManager.instance) {
      ModalManager.instance = new ModalManager();
    }
    return ModalManager.instance;
  }

  // =========================================================================
  // Public API
  // =========================================================================

  /**
   * Show a modal dialog and return a Promise that resolves when the user responds.
   * @returns true if primary button clicked, false if secondary/cancelled
   */
  public show(options: ModalOptions): Promise<boolean> {
    // Close any existing modal
    if (this.isVisible) {
      this.close(false);
    }

    return new Promise<boolean>((resolve) => {
      this.resolvePromise = resolve;
      this.createModal(options);
      this.addEventListeners();
      this.isVisible = true;

      // Focus the primary button
      requestAnimationFrame(() => {
        const primaryBtn = this.modalElement?.querySelector(
          ".modal-btn-primary"
        ) as HTMLButtonElement;
        primaryBtn?.focus();
      });
    });
  }

  /**
   * Close the modal with a result
   */
  public close(result: boolean): void {
    if (!this.isVisible) return;

    this.removeEventListeners();

    // Animate out
    if (this.overlayElement) {
      this.overlayElement.classList.remove("visible");
    }

    // Remove after animation
    setTimeout(() => {
      this.overlayElement?.remove();
      this.overlayElement = null;
      this.modalElement = null;
    }, 150);

    this.isVisible = false;

    // Resolve the promise
    if (this.resolvePromise) {
      this.resolvePromise(result);
      this.resolvePromise = null;
    }
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Create the modal DOM elements
   */
  private createModal(options: ModalOptions): void {
    const {
      title,
      body,
      primaryLabel = "OK",
      secondaryLabel = "Cancel",
      icon,
      warning = false,
    } = options;

    // Create overlay
    this.overlayElement = document.createElement("div");
    this.overlayElement.className = "modal-overlay";

    // Create modal container
    this.modalElement = document.createElement("div");
    this.modalElement.className = `modal${warning ? " modal-warning" : ""}`;
    this.modalElement.setAttribute("role", "dialog");
    this.modalElement.setAttribute("aria-modal", "true");
    this.modalElement.setAttribute("aria-labelledby", "modal-title");

    // Build modal content
    let iconHtml = "";
    if (icon) {
      iconHtml = `<span class="modal-icon">${icon}</span>`;
    } else if (warning) {
      iconHtml = `<span class="modal-icon modal-icon-warning">&#9888;</span>`;
    }

    this.modalElement.innerHTML = `
      <div class="modal-header">
        ${iconHtml}
        <h2 id="modal-title" class="modal-title">${title}</h2>
      </div>
      <div class="modal-body">${body}</div>
      <div class="modal-footer">
        <button class="modal-btn modal-btn-secondary" type="button">${secondaryLabel}</button>
        <button class="modal-btn modal-btn-primary" type="button">${primaryLabel}</button>
      </div>
    `;

    // Add click handlers for buttons
    const secondaryBtn = this.modalElement.querySelector(".modal-btn-secondary");
    const primaryBtn = this.modalElement.querySelector(".modal-btn-primary");

    secondaryBtn?.addEventListener("click", () => this.close(false));
    primaryBtn?.addEventListener("click", () => this.close(true));

    // Assemble and add to DOM
    this.overlayElement.appendChild(this.modalElement);
    document.body.appendChild(this.overlayElement);

    // Animate in
    requestAnimationFrame(() => {
      this.overlayElement?.classList.add("visible");
    });
  }

  /**
   * Add event listeners for keyboard and backdrop
   */
  private addEventListeners(): void {
    document.addEventListener("keydown", this.boundHandleKeydown);
    this.overlayElement?.addEventListener("click", this.boundHandleBackdropClick);
  }

  /**
   * Remove event listeners
   */
  private removeEventListeners(): void {
    document.removeEventListener("keydown", this.boundHandleKeydown);
    this.overlayElement?.removeEventListener("click", this.boundHandleBackdropClick);
  }

  /**
   * Handle keyboard events
   */
  private handleKeydown(e: KeyboardEvent): void {
    if (e.key === "Escape") {
      e.preventDefault();
      this.close(false);
    } else if (e.key === "Enter") {
      e.preventDefault();
      this.close(true);
    } else if (e.key === "Tab") {
      // Trap focus within modal
      this.trapFocus(e);
    }
  }

  /**
   * Handle backdrop click (close on click outside modal)
   */
  private handleBackdropClick(e: MouseEvent): void {
    if (e.target === this.overlayElement) {
      this.close(false);
    }
  }

  /**
   * Trap focus within the modal
   */
  private trapFocus(e: KeyboardEvent): void {
    if (!this.modalElement) return;

    const focusableElements = this.modalElement.querySelectorAll<HTMLElement>(
      'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
    );

    if (focusableElements.length === 0) return;

    const firstElement = focusableElements[0];
    const lastElement = focusableElements[focusableElements.length - 1];

    if (e.shiftKey && document.activeElement === firstElement) {
      e.preventDefault();
      lastElement.focus();
    } else if (!e.shiftKey && document.activeElement === lastElement) {
      e.preventDefault();
      firstElement.focus();
    }
  }
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Show a confirmation dialog.
 * @returns true if user confirmed, false if cancelled
 */
export async function showConfirm(options: ModalOptions): Promise<boolean> {
  return ModalManager.getInstance().show(options);
}

/**
 * Get the singleton modal manager instance
 */
export function getModalManager(): ModalManager {
  return ModalManager.getInstance();
}
