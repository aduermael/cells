// Editing Session - Centralized state management for cell editing
// Provides single source of truth for cursor position and value during editing.

// =============================================================================
// Types
// =============================================================================

/** Which editor initiated the editing session */
export type ActiveEditorType = "cell" | "formula";

/** State of an active editing session */
export interface EditingSessionState {
  /** Sheet ID being edited */
  sheetId: string;
  /** Column index of cell being edited */
  col: number;
  /** Row index of cell being edited */
  row: number;
  /** Current value/formula text */
  value: string;
  /** Cursor start position (selection start) */
  cursorStart: number;
  /** Cursor end position (selection end, same as start when no selection) */
  cursorEnd: number;
  /** Which editor initiated this session */
  activeEditor: ActiveEditorType;
}

/** Listener callback for session state changes */
export type EditingSessionListener = (state: EditingSessionState | null) => void;

// =============================================================================
// EditingSession Class
// =============================================================================

/**
 * EditingSession maintains the single source of truth for cell editing state.
 *
 * Key behaviors:
 * - State persists across focus changes between cell editor and formula bar
 * - Cursor position is never lost due to blur events
 * - Both CellEditor and FormulaBarEditor share the same session instance
 */
export class EditingSession {
  private state: EditingSessionState | null = null;
  private listeners: Set<EditingSessionListener> = new Set();

  // ===========================================================================
  // Session Lifecycle
  // ===========================================================================

  /**
   * Start a new editing session for a cell.
   * @param sheetId The sheet ID containing the cell
   * @param col Column index of the cell
   * @param row Row index of the cell
   * @param initialValue Initial value/formula text
   * @param activeEditor Which editor initiated this session ("cell" or "formula")
   */
  start(
    sheetId: string,
    col: number,
    row: number,
    initialValue: string,
    activeEditor: ActiveEditorType = "cell"
  ): void {
    this.state = {
      sheetId,
      col,
      row,
      value: initialValue,
      cursorStart: initialValue.length,
      cursorEnd: initialValue.length,
      activeEditor,
    };
    this.notifyListeners();
  }

  /**
   * Get the active editor type for this session.
   * Returns "cell" by default if no session is active.
   */
  getActiveEditor(): ActiveEditorType {
    return this.state?.activeEditor ?? "cell";
  }

  /**
   * Clear the current editing session.
   * Called when editing is committed or cancelled.
   */
  clear(): void {
    this.state = null;
    this.notifyListeners();
  }

  /**
   * Check if there is an active editing session.
   */
  isActive(): boolean {
    return this.state !== null;
  }

  /**
   * Get the current session state (read-only snapshot).
   * Returns null if no session is active.
   */
  getState(): EditingSessionState | null {
    return this.state ? { ...this.state } : null;
  }

  // ===========================================================================
  // Cursor Management
  // ===========================================================================

  /**
   * Set cursor position.
   * @param start Cursor start position
   * @param end Cursor end position (defaults to start for no selection)
   */
  setCursor(start: number, end?: number): void {
    if (!this.state) return;
    this.state.cursorStart = Math.max(0, Math.min(start, this.state.value.length));
    this.state.cursorEnd = Math.max(0, Math.min(end ?? start, this.state.value.length));
    this.notifyListeners();
  }

  /**
   * Get current cursor/selection range.
   */
  getSelection(): { start: number; end: number } {
    if (!this.state) {
      return { start: 0, end: 0 };
    }
    return { start: this.state.cursorStart, end: this.state.cursorEnd };
  }

  // ===========================================================================
  // Value Management
  // ===========================================================================

  /**
   * Get current value.
   */
  getValue(): string {
    return this.state?.value ?? "";
  }

  /**
   * Set value, preserving cursor position where possible.
   * If cursor is beyond new value length, it's clamped to end.
   * @param value New value
   */
  setValue(value: string): void {
    if (!this.state) return;
    this.state.value = value;
    // Clamp cursor to valid range
    this.state.cursorStart = Math.min(this.state.cursorStart, value.length);
    this.state.cursorEnd = Math.min(this.state.cursorEnd, value.length);
    this.notifyListeners();
  }

  // ===========================================================================
  // Text Manipulation
  // ===========================================================================

  /**
   * Insert text at a specific position.
   * Cursor is placed after the inserted text.
   * @param position Position to insert at
   * @param text Text to insert
   * @returns New cursor position (after inserted text)
   */
  insertAt(position: number, text: string): number {
    if (!this.state) return 0;

    const value = this.state.value;
    const clampedPos = Math.max(0, Math.min(position, value.length));
    const before = value.slice(0, clampedPos);
    const after = value.slice(clampedPos);
    const newValue = before + text + after;
    const newCursorPos = clampedPos + text.length;

    this.state.value = newValue;
    this.state.cursorStart = newCursorPos;
    this.state.cursorEnd = newCursorPos;
    this.notifyListeners();

    return newCursorPos;
  }

  /**
   * Replace a range of text with new text.
   * Cursor is placed after the replacement text.
   * @param start Start position of range to replace
   * @param end End position of range to replace
   * @param text Replacement text
   * @returns New cursor position (after replacement text)
   */
  replaceRange(start: number, end: number, text: string): number {
    if (!this.state) return 0;

    const value = this.state.value;
    const clampedStart = Math.max(0, Math.min(start, value.length));
    const clampedEnd = Math.max(clampedStart, Math.min(end, value.length));
    const before = value.slice(0, clampedStart);
    const after = value.slice(clampedEnd);
    const newValue = before + text + after;
    const newCursorPos = clampedStart + text.length;

    this.state.value = newValue;
    this.state.cursorStart = newCursorPos;
    this.state.cursorEnd = newCursorPos;
    this.notifyListeners();

    return newCursorPos;
  }

  /**
   * Insert text at the current cursor position, replacing any selection.
   * Equivalent to replaceRange(cursorStart, cursorEnd, text).
   * @param text Text to insert
   * @returns New cursor position (after inserted text)
   */
  insertAtCursor(text: string): number {
    if (!this.state) return 0;
    return this.replaceRange(this.state.cursorStart, this.state.cursorEnd, text);
  }

  // ===========================================================================
  // Event Subscription
  // ===========================================================================

  /**
   * Subscribe to session state changes.
   * @param listener Callback invoked when state changes
   * @returns Unsubscribe function
   */
  subscribe(listener: EditingSessionListener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  /**
   * Notify all listeners of state change.
   */
  private notifyListeners(): void {
    const state = this.getState();
    this.listeners.forEach((listener) => listener(state));
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

/** Global editing session instance shared between editors */
export const editingSession = new EditingSession();
