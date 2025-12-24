// UI State Machine Module
// Manages UI interaction modes as a formal state machine

import type {
  UIStateType,
  UIEventType,
  UIContext,
  SyncContext,
  SyncStateType,
  ModifierKeys,
  SelectionRange,
  Position,
  StateTransitionEvent,
} from "./types.js";

/**
 * UI States - mutually exclusive interaction modes
 */
export const UIState = {
  IDLE: "IDLE",
  SELECTING: "SELECTING",
  CELL_EDITING: "CELL_EDITING",
  FORMULA_BAR_EDITING: "FORMULA_BAR_EDITING",
  COLUMN_RESIZING: "COLUMN_RESIZING",
  ROW_RESIZING: "ROW_RESIZING",
  COLUMN_DRAGGING: "COLUMN_DRAGGING",
  ROW_DRAGGING: "ROW_DRAGGING",
  COLUMN_HEADER_EDITING: "COLUMN_HEADER_EDITING",
  SHEET_TAB_EDITING: "SHEET_TAB_EDITING",
  SHEET_TAB_DRAGGING: "SHEET_TAB_DRAGGING",
} as const;

/**
 * Events that trigger state transitions
 */
export const UIEvent = {
  // Selection events
  START_SELECTING: "START_SELECTING",
  STOP_SELECTING: "STOP_SELECTING",

  // Cell editing events
  START_CELL_EDIT: "START_CELL_EDIT",
  COMMIT_CELL_EDIT: "COMMIT_CELL_EDIT",
  CANCEL_CELL_EDIT: "CANCEL_CELL_EDIT",

  // Formula bar events
  START_FORMULA_EDIT: "START_FORMULA_EDIT",
  COMMIT_FORMULA_EDIT: "COMMIT_FORMULA_EDIT",
  CANCEL_FORMULA_EDIT: "CANCEL_FORMULA_EDIT",

  // Column resize events
  START_COLUMN_RESIZE: "START_COLUMN_RESIZE",
  END_COLUMN_RESIZE: "END_COLUMN_RESIZE",
  CANCEL_COLUMN_RESIZE: "CANCEL_COLUMN_RESIZE",

  // Row resize events
  START_ROW_RESIZE: "START_ROW_RESIZE",
  END_ROW_RESIZE: "END_ROW_RESIZE",
  CANCEL_ROW_RESIZE: "CANCEL_ROW_RESIZE",

  // Column drag events
  START_COLUMN_DRAG: "START_COLUMN_DRAG",
  END_COLUMN_DRAG: "END_COLUMN_DRAG",
  CANCEL_COLUMN_DRAG: "CANCEL_COLUMN_DRAG",

  // Row drag events
  START_ROW_DRAG: "START_ROW_DRAG",
  END_ROW_DRAG: "END_ROW_DRAG",
  CANCEL_ROW_DRAG: "CANCEL_ROW_DRAG",

  // Column header editing events
  START_COLUMN_HEADER_EDIT: "START_COLUMN_HEADER_EDIT",
  COMMIT_COLUMN_HEADER_EDIT: "COMMIT_COLUMN_HEADER_EDIT",
  CANCEL_COLUMN_HEADER_EDIT: "CANCEL_COLUMN_HEADER_EDIT",

  // Sheet tab editing events
  START_SHEET_TAB_EDIT: "START_SHEET_TAB_EDIT",
  COMMIT_SHEET_TAB_EDIT: "COMMIT_SHEET_TAB_EDIT",
  CANCEL_SHEET_TAB_EDIT: "CANCEL_SHEET_TAB_EDIT",

  // Sheet tab drag events
  START_SHEET_TAB_DRAG: "START_SHEET_TAB_DRAG",
  END_SHEET_TAB_DRAG: "END_SHEET_TAB_DRAG",
  CANCEL_SHEET_TAB_DRAG: "CANCEL_SHEET_TAB_DRAG",

  // Generic events
  ESCAPE: "ESCAPE",
  CLICK_AWAY: "CLICK_AWAY",
} as const;

/** Transition definition */
interface TransitionDef {
  nextState: UIStateType;
  guard?: (context: UIContext) => boolean;
}

/** State transition table type */
type TransitionTable = {
  [state in UIStateType]?: {
    [event in UIEventType]?: TransitionDef;
  };
};

/**
 * State transition table
 * Maps: currentState -> event -> { nextState, guard? }
 */
const transitions: TransitionTable = {
  IDLE: {
    START_SELECTING: { nextState: "SELECTING" },
    START_CELL_EDIT: { nextState: "CELL_EDITING" },
    START_FORMULA_EDIT: { nextState: "FORMULA_BAR_EDITING" },
    START_COLUMN_RESIZE: { nextState: "COLUMN_RESIZING" },
    START_ROW_RESIZE: { nextState: "ROW_RESIZING" },
    START_COLUMN_DRAG: { nextState: "COLUMN_DRAGGING" },
    START_ROW_DRAG: { nextState: "ROW_DRAGGING" },
    START_COLUMN_HEADER_EDIT: { nextState: "COLUMN_HEADER_EDITING" },
    START_SHEET_TAB_EDIT: { nextState: "SHEET_TAB_EDITING" },
    START_SHEET_TAB_DRAG: { nextState: "SHEET_TAB_DRAGGING" },
  },

  SELECTING: {
    STOP_SELECTING: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },

  CELL_EDITING: {
    COMMIT_CELL_EDIT: { nextState: "IDLE" },
    CANCEL_CELL_EDIT: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
    START_FORMULA_EDIT: { nextState: "FORMULA_BAR_EDITING" },
    START_SELECTING: { nextState: "SELECTING" },
  },

  FORMULA_BAR_EDITING: {
    COMMIT_FORMULA_EDIT: { nextState: "IDLE" },
    CANCEL_FORMULA_EDIT: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
    CLICK_AWAY: { nextState: "IDLE" },
    START_CELL_EDIT: { nextState: "CELL_EDITING" },
    START_SELECTING: { nextState: "SELECTING" },
  },

  COLUMN_RESIZING: {
    END_COLUMN_RESIZE: { nextState: "IDLE" },
    CANCEL_COLUMN_RESIZE: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },

  ROW_RESIZING: {
    END_ROW_RESIZE: { nextState: "IDLE" },
    CANCEL_ROW_RESIZE: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },

  COLUMN_DRAGGING: {
    END_COLUMN_DRAG: { nextState: "IDLE" },
    CANCEL_COLUMN_DRAG: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },

  ROW_DRAGGING: {
    END_ROW_DRAG: { nextState: "IDLE" },
    CANCEL_ROW_DRAG: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },

  COLUMN_HEADER_EDITING: {
    COMMIT_COLUMN_HEADER_EDIT: { nextState: "IDLE" },
    CANCEL_COLUMN_HEADER_EDIT: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
    CLICK_AWAY: { nextState: "IDLE" },
  },

  SHEET_TAB_EDITING: {
    COMMIT_SHEET_TAB_EDIT: { nextState: "IDLE" },
    CANCEL_SHEET_TAB_EDIT: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
    CLICK_AWAY: { nextState: "IDLE" },
  },

  SHEET_TAB_DRAGGING: {
    END_SHEET_TAB_DRAG: { nextState: "IDLE" },
    CANCEL_SHEET_TAB_DRAG: { nextState: "IDLE" },
    ESCAPE: { nextState: "IDLE" },
  },
};

/** State machine options */
export interface UIStateMachineOptions {
  debug?: boolean;
}

/** State change listener */
export type StateChangeListener = (event: StateTransitionEvent) => void;

/** Sync change listener */
export type SyncChangeListener = (context: SyncContext) => void;

/** Transition context - optional data passed during state transitions */
export interface TransitionContext {
  selectedCell?: Position;
  selectionStart?: Position;
  selectionEnd?: Position;
  [key: string]: unknown;
}

/** UI State Machine interface */
export interface UIStateMachine {
  // State queries
  getState(): UIStateType;
  getStateContext(): Record<string, unknown>;
  getContext(): UIContext;
  isInState(state: UIStateType): boolean;
  isEditing(): boolean;
  isDragging(): boolean;

  // State transitions
  transition(event: UIEventType, context?: TransitionContext): boolean;
  forceState(state: UIStateType, context?: Record<string, unknown>): void;
  reset(): void;

  // Modifier tracking
  updateModifiersFromEvent(event: KeyboardEvent): void;
  setModifiers(mods: Partial<ModifierKeys>): void;
  getModifiers(): ModifierKeys;

  // Selection tracking
  setSelectionRange(start: Position, end?: Position | null): void;
  getSelectionRange(): SelectionRange;
  isSingleCellSelected(): boolean;
  getSelectedCell(): Position;
  getSelectionStart(): Position;
  getSelectionEnd(): Position;

  // Sheet tracking
  setActiveSheet(index: number): void;
  getActiveSheet(): number;

  // Sync context tracking
  setSyncContext(context: Partial<SyncContext>): void;
  getSyncContext(): SyncContext;
  isSyncEnabled(): boolean;
  isSyncing(): boolean;
  isSyncOnline(): boolean;
  onSyncChange(listener: SyncChangeListener): () => void;

  // Event subscription
  subscribe(listener: StateChangeListener): () => void;
  onStateChange(listener: StateChangeListener): () => void;
}

/**
 * Creates a new UI state machine instance
 */
export function createUIStateMachine(
  options: UIStateMachineOptions = {}
): UIStateMachine {
  const debug = options.debug ?? false;

  // Current state
  let currentState: UIStateType = "IDLE";

  // Context data associated with current state
  let stateContext: Record<string, unknown> = {};

  // Modifier keys (always tracked regardless of state)
  const modifiers: ModifierKeys = {
    meta: false,
    shift: false,
    ctrl: false,
    alt: false,
  };

  // Selection range (always valid; single cell = start equals end)
  const selectionRange: SelectionRange = {
    start: { col: 0, row: 0 },
    end: { col: 0, row: 0 },
  };

  // Active sheet index
  let activeSheet = 0;

  // Sync context - tracks collaboration sync state (parallel to UI state)
  const syncContext: SyncContext = {
    enabled: false,
    state: "offline",
    peerCount: 0,
  };

  // Listeners for state changes
  const listeners = new Set<StateChangeListener>();

  // Listeners specifically for sync context changes
  const syncListeners = new Set<SyncChangeListener>();

  /**
   * Log debug messages
   */
  function log(...args: unknown[]): void {
    if (debug) {
      console.log("[UIState]", ...args);
    }
  }

  /**
   * Get current state
   */
  function getState(): UIStateType {
    return currentState;
  }

  /**
   * Get context data for current state
   */
  function getStateContext(): Record<string, unknown> {
    return { ...stateContext };
  }

  /**
   * Get full context (modifiers, selection, activeSheet, sync)
   */
  function getContext(): UIContext {
    return {
      state: currentState,
      stateContext: { ...stateContext },
      modifiers: { ...modifiers },
      selectionRange: {
        start: { ...selectionRange.start },
        end: { ...selectionRange.end },
      },
      activeSheet,
      sync: { ...syncContext },
    };
  }

  /**
   * Check if in a specific state
   */
  function isInState(state: UIStateType): boolean {
    return currentState === state;
  }

  /**
   * Check if currently editing (any edit mode)
   */
  function isEditing(): boolean {
    return (
      currentState === "CELL_EDITING" ||
      currentState === "FORMULA_BAR_EDITING" ||
      currentState === "COLUMN_HEADER_EDITING" ||
      currentState === "SHEET_TAB_EDITING"
    );
  }

  /**
   * Check if currently dragging/resizing (any drag mode)
   */
  function isDragging(): boolean {
    return (
      currentState === "COLUMN_RESIZING" ||
      currentState === "ROW_RESIZING" ||
      currentState === "COLUMN_DRAGGING" ||
      currentState === "ROW_DRAGGING" ||
      currentState === "SHEET_TAB_DRAGGING" ||
      currentState === "SELECTING"
    );
  }

  /**
   * Attempt a state transition
   */
  function transition(
    event: UIEventType,
    context: TransitionContext = {}
  ): boolean {
    const stateTransitions = transitions[currentState];
    if (!stateTransitions) {
      log(`No transitions defined for state: ${currentState}`);
      return false;
    }

    const transitionDef = stateTransitions[event];
    if (!transitionDef) {
      log(`No transition for event ${event} in state ${currentState}`);
      return false;
    }

    // Check guard if present
    if (transitionDef.guard && !transitionDef.guard(getContext())) {
      log(`Guard rejected transition: ${currentState} -> ${event}`);
      return false;
    }

    const previousState = currentState;
    currentState = transitionDef.nextState;
    stateContext = context as Record<string, unknown>;

    // Merge selection-related context fields into selectionRange
    if (context.selectedCell) {
      selectionRange.start = { ...context.selectedCell };
      selectionRange.end = { ...context.selectedCell };
    }
    if (context.selectionStart) {
      selectionRange.start = { ...context.selectionStart };
    }
    if (context.selectionEnd) {
      selectionRange.end = { ...context.selectionEnd };
    }

    log(`Transition: ${previousState} --(${event})--> ${currentState}`, context);

    // Notify listeners
    for (const listener of listeners) {
      try {
        listener({
          previousState,
          currentState,
          event,
          context: getContext(),
        });
      } catch (e) {
        console.error("[UIState] Listener error:", e);
      }
    }

    return true;
  }

  /**
   * Force state (use sparingly, bypasses transition rules)
   */
  function forceState(
    state: UIStateType,
    context: Record<string, unknown> = {}
  ): void {
    const previousState = currentState;
    currentState = state;
    stateContext = context;
    log(`Force state: ${previousState} -> ${currentState}`, context);
  }

  /**
   * Reset to IDLE state
   */
  function reset(): void {
    const previousState = currentState;
    currentState = "IDLE";
    stateContext = {};
    log(`Reset: ${previousState} -> IDLE`);
  }

  /**
   * Update modifier keys from keyboard event
   */
  function updateModifiersFromEvent(event: KeyboardEvent): void {
    modifiers.meta = event.metaKey;
    modifiers.shift = event.shiftKey;
    modifiers.ctrl = event.ctrlKey;
    modifiers.alt = event.altKey;
  }

  /**
   * Set modifier keys directly
   */
  function setModifiers(mods: Partial<ModifierKeys>): void {
    if (mods.meta !== undefined) modifiers.meta = mods.meta;
    if (mods.shift !== undefined) modifiers.shift = mods.shift;
    if (mods.ctrl !== undefined) modifiers.ctrl = mods.ctrl;
    if (mods.alt !== undefined) modifiers.alt = mods.alt;
  }

  /**
   * Get current modifier state
   */
  function getModifiers(): ModifierKeys {
    return { ...modifiers };
  }

  /**
   * Update selection range
   */
  function setSelectionRange(start: Position, end: Position | null = null): void {
    selectionRange.start = { ...start };
    selectionRange.end = end ? { ...end } : { ...start };
  }

  /**
   * Get selection range
   */
  function getSelectionRange(): SelectionRange {
    return {
      start: { ...selectionRange.start },
      end: { ...selectionRange.end },
    };
  }

  /**
   * Check if selection is a single cell
   */
  function isSingleCellSelected(): boolean {
    return (
      selectionRange.start.col === selectionRange.end.col &&
      selectionRange.start.row === selectionRange.end.row
    );
  }

  /**
   * Set active sheet index
   */
  function setActiveSheet(index: number): void {
    activeSheet = index;
  }

  /**
   * Get active sheet index
   */
  function getActiveSheet(): number {
    return activeSheet;
  }

  // ========================================================================
  // Sync Context API
  // ========================================================================

  /**
   * Update sync context (called by sync adapter)
   */
  function setSyncContext(context: Partial<SyncContext>): void {
    let changed = false;
    if (context.enabled !== undefined && context.enabled !== syncContext.enabled) {
      syncContext.enabled = context.enabled;
      changed = true;
    }
    if (context.state !== undefined && context.state !== syncContext.state) {
      syncContext.state = context.state as SyncStateType;
      changed = true;
    }
    if (
      context.peerCount !== undefined &&
      context.peerCount !== syncContext.peerCount
    ) {
      syncContext.peerCount = context.peerCount;
      changed = true;
    }

    if (changed) {
      log("Sync context changed:", syncContext);
      for (const listener of syncListeners) {
        try {
          listener({ ...syncContext });
        } catch (e) {
          console.error("[UIState] Sync listener error:", e);
        }
      }
    }
  }

  /**
   * Get current sync context
   */
  function getSyncContext(): SyncContext {
    return { ...syncContext };
  }

  /**
   * Check if sync is enabled (connected to a room)
   */
  function isSyncEnabled(): boolean {
    return syncContext.enabled;
  }

  /**
   * Check if currently syncing (initial sync after joining)
   */
  function isSyncing(): boolean {
    return syncContext.state === "syncing";
  }

  /**
   * Check if sync is online (fully connected and synced)
   */
  function isSyncOnline(): boolean {
    return syncContext.state === "online";
  }

  /**
   * Subscribe to sync context changes
   */
  function onSyncChange(listener: SyncChangeListener): () => void {
    syncListeners.add(listener);
    return () => {
      syncListeners.delete(listener);
    };
  }

  /**
   * Subscribe to state changes
   */
  function subscribe(listener: StateChangeListener): () => void {
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  }

  /**
   * Alias for subscribe - register a state change listener
   */
  function onStateChange(listener: StateChangeListener): () => void {
    return subscribe(listener);
  }

  /**
   * Get the currently selected cell (anchor of selection)
   */
  function getSelectedCell(): Position {
    return { ...selectionRange.start };
  }

  /**
   * Get the selection start (anchor cell)
   */
  function getSelectionStart(): Position {
    return { ...selectionRange.start };
  }

  /**
   * Get the selection end (current end of range)
   */
  function getSelectionEnd(): Position {
    return { ...selectionRange.end };
  }

  return {
    // State queries
    getState,
    getStateContext,
    getContext,
    isInState,
    isEditing,
    isDragging,

    // State transitions
    transition,
    forceState,
    reset,

    // Modifier tracking
    updateModifiersFromEvent,
    setModifiers,
    getModifiers,

    // Selection tracking
    setSelectionRange,
    getSelectionRange,
    isSingleCellSelected,
    getSelectedCell,
    getSelectionStart,
    getSelectionEnd,

    // Sheet tracking
    setActiveSheet,
    getActiveSheet,

    // Sync context tracking
    setSyncContext,
    getSyncContext,
    isSyncEnabled,
    isSyncing,
    isSyncOnline,
    onSyncChange,

    // Event subscription
    subscribe,
    onStateChange,
  };
}

// Convenience: Create a default instance
export const uiState = createUIStateMachine();
