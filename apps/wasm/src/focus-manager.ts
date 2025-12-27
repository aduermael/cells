// Focus Manager - Centralized focus and blur management for the grid
//
// This module provides a unified approach to handling focus transitions
// within the grid container. It ensures that:
// 1. Clicking anywhere in the grid (canvas, scrollbars, editors) preserves editing mode
// 2. Blur commits only happen when focus moves OUTSIDE the grid container
// 3. Formula editing can click on cells to insert references without losing focus

/**
 * FocusManager handles focus boundaries for the grid container.
 *
 * Design principles:
 * - The grid container is the "focus boundary"
 * - Any mousedown inside the container suppresses blur commits
 * - Blur handlers use relatedTarget to check if focus stays in container
 * - Scrollbar interactions preserve editing focus
 */
export class FocusManager {
  private container: HTMLElement;
  private suppressBlurCommit = false;
  private activeEditor: HTMLElement | null = null;

  // Callbacks for state queries
  private isEditingCellFn: () => boolean;
  private isEditingFormulaBarFn: () => boolean;

  constructor(config: {
    container: HTMLElement;
    isEditingCell: () => boolean;
    isEditingFormulaBar: () => boolean;
  }) {
    this.container = config.container;
    this.isEditingCellFn = config.isEditingCell;
    this.isEditingFormulaBarFn = config.isEditingFormulaBar;

    this.setupContainerListeners();
  }

  /**
   * Set up capture-phase mousedown on the entire container.
   * This ensures ANY click within the container (canvas, scrollbars, editors)
   * sets suppressBlurCommit BEFORE blur events fire.
   */
  private setupContainerListeners(): void {
    // Capture-phase mousedown - runs before any other handlers
    this.container.addEventListener(
      "mousedown",
      () => {
        // Set flag to suppress blur commits
        this.suppressBlurCommit = true;
      },
      { capture: true }
    );

    // Reset suppress flag on mouseup (after all blur handlers have run)
    document.addEventListener("mouseup", () => {
      // Clear the flag after a microtask to ensure blur handlers see it
      queueMicrotask(() => {
        this.suppressBlurCommit = false;
      });
    });
  }

  /**
   * Check if currently in any editing mode
   */
  isEditing(): boolean {
    return this.isEditingCellFn() || this.isEditingFormulaBarFn();
  }

  /**
   * Check if blur should be suppressed.
   * Call this from blur handlers before committing edits.
   *
   * @param relatedTarget - The element receiving focus (from blur event)
   * @returns true if blur commit should be suppressed
   */
  shouldSuppressBlur(relatedTarget: EventTarget | null): boolean {
    // If we detected a container mousedown, suppress
    if (this.suppressBlurCommit) {
      return true;
    }

    // If focus is moving to another element inside the container, suppress
    if (relatedTarget instanceof HTMLElement) {
      if (this.container.contains(relatedTarget)) {
        return true;
      }
    }

    return false;
  }

  /**
   * Consume the suppress flag (call after checking shouldSuppressBlur).
   * This resets the flag so subsequent blurs aren't suppressed.
   */
  consumeSuppressFlag(): void {
    this.suppressBlurCommit = false;
  }

  /**
   * Set the currently active editor element.
   * Called when starting to edit.
   */
  setActiveEditor(editor: HTMLElement | null): void {
    this.activeEditor = editor;
  }

  /**
   * Get the currently active editor element.
   */
  getActiveEditor(): HTMLElement | null {
    return this.activeEditor;
  }

  /**
   * Refocus the active editor if one exists.
   * Useful after scrollbar interactions.
   */
  refocusActiveEditor(): void {
    if (this.activeEditor && this.isEditing()) {
      // Use requestAnimationFrame to ensure DOM has settled
      requestAnimationFrame(() => {
        if (this.activeEditor) {
          this.activeEditor.focus();
        }
      });
    }
  }

  /**
   * Check if an element is inside the focus boundary (container).
   */
  isInsideContainer(element: HTMLElement | null): boolean {
    if (!element) return false;
    return this.container.contains(element);
  }

  /**
   * Force suppress blur for the next blur event.
   * Use this when programmatically triggering actions that shouldn't commit.
   */
  suppressNextBlur(): void {
    this.suppressBlurCommit = true;
  }
}
