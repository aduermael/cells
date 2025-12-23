// UI State Machine Module
// Manages UI interaction modes as a formal state machine

/**
 * UI States - mutually exclusive interaction modes
 * @readonly
 * @enum {string}
 */
export const UIState = Object.freeze({
    IDLE: 'IDLE',                           // Default viewing mode (at least one cell always selected)
    SELECTING: 'SELECTING',                 // Actively dragging to modify selection range
    CELL_EDITING: 'CELL_EDITING',           // Inline cell editor active
    FORMULA_BAR_EDITING: 'FORMULA_BAR_EDITING', // Formula bar focused for editing
    COLUMN_RESIZING: 'COLUMN_RESIZING',
    ROW_RESIZING: 'ROW_RESIZING',
    COLUMN_DRAGGING: 'COLUMN_DRAGGING',
    ROW_DRAGGING: 'ROW_DRAGGING',
    COLUMN_HEADER_EDITING: 'COLUMN_HEADER_EDITING',
    SHEET_TAB_EDITING: 'SHEET_TAB_EDITING',
    SHEET_TAB_DRAGGING: 'SHEET_TAB_DRAGGING'
});

/**
 * Events that trigger state transitions
 * @readonly
 * @enum {string}
 */
export const UIEvent = Object.freeze({
    // Selection events
    START_SELECTING: 'START_SELECTING',
    STOP_SELECTING: 'STOP_SELECTING',

    // Cell editing events
    START_CELL_EDIT: 'START_CELL_EDIT',
    COMMIT_CELL_EDIT: 'COMMIT_CELL_EDIT',
    CANCEL_CELL_EDIT: 'CANCEL_CELL_EDIT',

    // Formula bar events
    START_FORMULA_EDIT: 'START_FORMULA_EDIT',
    COMMIT_FORMULA_EDIT: 'COMMIT_FORMULA_EDIT',
    CANCEL_FORMULA_EDIT: 'CANCEL_FORMULA_EDIT',

    // Column resize events
    START_COLUMN_RESIZE: 'START_COLUMN_RESIZE',
    END_COLUMN_RESIZE: 'END_COLUMN_RESIZE',
    CANCEL_COLUMN_RESIZE: 'CANCEL_COLUMN_RESIZE',

    // Row resize events
    START_ROW_RESIZE: 'START_ROW_RESIZE',
    END_ROW_RESIZE: 'END_ROW_RESIZE',
    CANCEL_ROW_RESIZE: 'CANCEL_ROW_RESIZE',

    // Column drag events
    START_COLUMN_DRAG: 'START_COLUMN_DRAG',
    END_COLUMN_DRAG: 'END_COLUMN_DRAG',
    CANCEL_COLUMN_DRAG: 'CANCEL_COLUMN_DRAG',

    // Row drag events
    START_ROW_DRAG: 'START_ROW_DRAG',
    END_ROW_DRAG: 'END_ROW_DRAG',
    CANCEL_ROW_DRAG: 'CANCEL_ROW_DRAG',

    // Column header editing events
    START_COLUMN_HEADER_EDIT: 'START_COLUMN_HEADER_EDIT',
    COMMIT_COLUMN_HEADER_EDIT: 'COMMIT_COLUMN_HEADER_EDIT',
    CANCEL_COLUMN_HEADER_EDIT: 'CANCEL_COLUMN_HEADER_EDIT',

    // Sheet tab editing events
    START_SHEET_TAB_EDIT: 'START_SHEET_TAB_EDIT',
    COMMIT_SHEET_TAB_EDIT: 'COMMIT_SHEET_TAB_EDIT',
    CANCEL_SHEET_TAB_EDIT: 'CANCEL_SHEET_TAB_EDIT',

    // Sheet tab drag events
    START_SHEET_TAB_DRAG: 'START_SHEET_TAB_DRAG',
    END_SHEET_TAB_DRAG: 'END_SHEET_TAB_DRAG',
    CANCEL_SHEET_TAB_DRAG: 'CANCEL_SHEET_TAB_DRAG',

    // Generic events
    ESCAPE: 'ESCAPE',      // Cancel current action
    CLICK_AWAY: 'CLICK_AWAY'  // Click outside active element
});

/**
 * State transition table
 * Maps: currentState -> event -> { nextState, guard? }
 * guard is an optional function(context) => boolean
 */
const transitions = {
    [UIState.IDLE]: {
        [UIEvent.START_SELECTING]: { nextState: UIState.SELECTING },
        [UIEvent.START_CELL_EDIT]: { nextState: UIState.CELL_EDITING },
        [UIEvent.START_FORMULA_EDIT]: { nextState: UIState.FORMULA_BAR_EDITING },
        [UIEvent.START_COLUMN_RESIZE]: { nextState: UIState.COLUMN_RESIZING },
        [UIEvent.START_ROW_RESIZE]: { nextState: UIState.ROW_RESIZING },
        [UIEvent.START_COLUMN_DRAG]: { nextState: UIState.COLUMN_DRAGGING },
        [UIEvent.START_ROW_DRAG]: { nextState: UIState.ROW_DRAGGING },
        [UIEvent.START_COLUMN_HEADER_EDIT]: { nextState: UIState.COLUMN_HEADER_EDITING },
        [UIEvent.START_SHEET_TAB_EDIT]: { nextState: UIState.SHEET_TAB_EDITING },
        [UIEvent.START_SHEET_TAB_DRAG]: { nextState: UIState.SHEET_TAB_DRAGGING }
    },

    [UIState.SELECTING]: {
        [UIEvent.STOP_SELECTING]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    },

    [UIState.CELL_EDITING]: {
        [UIEvent.COMMIT_CELL_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_CELL_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE },
        [UIEvent.START_FORMULA_EDIT]: { nextState: UIState.FORMULA_BAR_EDITING },
        // Allow clicking another cell while editing (commits current edit)
        [UIEvent.START_SELECTING]: { nextState: UIState.SELECTING }
    },

    [UIState.FORMULA_BAR_EDITING]: {
        [UIEvent.COMMIT_FORMULA_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_FORMULA_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE },
        [UIEvent.CLICK_AWAY]: { nextState: UIState.IDLE },
        // Allow clicking into inline editor from formula bar
        [UIEvent.START_CELL_EDIT]: { nextState: UIState.CELL_EDITING },
        // Allow starting selection (commits formula bar)
        [UIEvent.START_SELECTING]: { nextState: UIState.SELECTING }
    },

    [UIState.COLUMN_RESIZING]: {
        [UIEvent.END_COLUMN_RESIZE]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_COLUMN_RESIZE]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    },

    [UIState.ROW_RESIZING]: {
        [UIEvent.END_ROW_RESIZE]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_ROW_RESIZE]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    },

    [UIState.COLUMN_DRAGGING]: {
        [UIEvent.END_COLUMN_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_COLUMN_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    },

    [UIState.ROW_DRAGGING]: {
        [UIEvent.END_ROW_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_ROW_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    },

    [UIState.COLUMN_HEADER_EDITING]: {
        [UIEvent.COMMIT_COLUMN_HEADER_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_COLUMN_HEADER_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE },
        [UIEvent.CLICK_AWAY]: { nextState: UIState.IDLE }
    },

    [UIState.SHEET_TAB_EDITING]: {
        [UIEvent.COMMIT_SHEET_TAB_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_SHEET_TAB_EDIT]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE },
        [UIEvent.CLICK_AWAY]: { nextState: UIState.IDLE }
    },

    [UIState.SHEET_TAB_DRAGGING]: {
        [UIEvent.END_SHEET_TAB_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.CANCEL_SHEET_TAB_DRAG]: { nextState: UIState.IDLE },
        [UIEvent.ESCAPE]: { nextState: UIState.IDLE }
    }
};

/**
 * Creates a new UI state machine instance
 * @param {Object} options - Configuration options
 * @param {boolean} options.debug - Enable debug logging (default: false)
 * @returns {Object} State machine API
 */
export function createUIStateMachine(options = {}) {
    const debug = options.debug ?? false;

    // Current state
    let currentState = UIState.IDLE;

    // Context data associated with current state
    let stateContext = {};

    // Modifier keys (always tracked regardless of state)
    const modifiers = {
        meta: false,
        shift: false,
        ctrl: false,
        alt: false
    };

    // Selection range (always valid; single cell = start equals end)
    let selectionRange = {
        start: { col: 0, row: 0 },
        end: { col: 0, row: 0 }
    };

    // Active sheet index
    let activeSheet = 0;

    // Sync context - tracks collaboration sync state (parallel to UI state)
    // This is not a UI interaction state but a context flag for sync status
    const syncContext = {
        enabled: false,    // Whether sync is enabled
        state: 'offline',  // Current sync state: 'offline', 'connecting', 'syncing', 'online', 'error'
        peerCount: 0       // Number of connected peers
    };

    // Listeners for state changes
    const listeners = new Set();

    // Listeners specifically for sync context changes
    const syncListeners = new Set();

    /**
     * Log debug messages
     */
    function log(...args) {
        if (debug) {
            console.log('[UIState]', ...args);
        }
    }

    /**
     * Get current state
     * @returns {UIState} Current state
     */
    function getState() {
        return currentState;
    }

    /**
     * Get context data for current state
     * @returns {Object} State-specific context
     */
    function getStateContext() {
        return { ...stateContext };
    }

    /**
     * Get full context (modifiers, selection, activeSheet, sync)
     * @returns {Object} Full context object
     */
    function getContext() {
        return {
            state: currentState,
            stateContext: { ...stateContext },
            modifiers: { ...modifiers },
            selectionRange: {
                start: { ...selectionRange.start },
                end: { ...selectionRange.end }
            },
            activeSheet,
            sync: { ...syncContext }
        };
    }

    /**
     * Check if in a specific state
     * @param {UIState} state - State to check
     * @returns {boolean}
     */
    function isInState(state) {
        return currentState === state;
    }

    /**
     * Check if currently editing (any edit mode)
     * @returns {boolean}
     */
    function isEditing() {
        return currentState === UIState.CELL_EDITING ||
               currentState === UIState.FORMULA_BAR_EDITING ||
               currentState === UIState.COLUMN_HEADER_EDITING ||
               currentState === UIState.SHEET_TAB_EDITING;
    }

    /**
     * Check if currently dragging/resizing (any drag mode)
     * @returns {boolean}
     */
    function isDragging() {
        return currentState === UIState.COLUMN_RESIZING ||
               currentState === UIState.ROW_RESIZING ||
               currentState === UIState.COLUMN_DRAGGING ||
               currentState === UIState.ROW_DRAGGING ||
               currentState === UIState.SHEET_TAB_DRAGGING ||
               currentState === UIState.SELECTING;
    }

    /**
     * Attempt a state transition
     * @param {UIEvent} event - Event to process
     * @param {Object} context - Optional context data for the new state
     *   - If context contains `selectedCell`, it updates selectionRange.start and selectionRange.end
     *   - If context contains `selectionStart`, it updates selectionRange.start
     *   - If context contains `selectionEnd`, it updates selectionRange.end
     * @returns {boolean} True if transition occurred, false otherwise
     */
    function transition(event, context = {}) {
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
        stateContext = context;

        // Merge selection-related context fields into selectionRange
        if (context.selectedCell) {
            // selectedCell sets both start and end (single cell selection)
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
                    context: getContext()
                });
            } catch (e) {
                console.error('[UIState] Listener error:', e);
            }
        }

        return true;
    }

    /**
     * Force state (use sparingly, bypasses transition rules)
     * @param {UIState} state - State to set
     * @param {Object} context - Context data
     */
    function forceState(state, context = {}) {
        const previousState = currentState;
        currentState = state;
        stateContext = context;
        log(`Force state: ${previousState} -> ${currentState}`, context);
    }

    /**
     * Reset to IDLE state
     */
    function reset() {
        const previousState = currentState;
        currentState = UIState.IDLE;
        stateContext = {};
        log(`Reset: ${previousState} -> IDLE`);
    }

    /**
     * Update modifier keys from keyboard event
     * @param {KeyboardEvent} event - Keyboard event
     */
    function updateModifiersFromEvent(event) {
        modifiers.meta = event.metaKey;
        modifiers.shift = event.shiftKey;
        modifiers.ctrl = event.ctrlKey;
        modifiers.alt = event.altKey;
    }

    /**
     * Set modifier keys directly
     * @param {Object} mods - Modifier state { meta?, shift?, ctrl?, alt? }
     */
    function setModifiers(mods) {
        if (mods.meta !== undefined) modifiers.meta = mods.meta;
        if (mods.shift !== undefined) modifiers.shift = mods.shift;
        if (mods.ctrl !== undefined) modifiers.ctrl = mods.ctrl;
        if (mods.alt !== undefined) modifiers.alt = mods.alt;
    }

    /**
     * Get current modifier state
     * @returns {Object} { meta, shift, ctrl, alt }
     */
    function getModifiers() {
        return { ...modifiers };
    }

    /**
     * Update selection range
     * @param {Object} start - Start cell { col, row }
     * @param {Object} end - End cell { col, row } (optional, defaults to start for single cell)
     */
    function setSelectionRange(start, end = null) {
        selectionRange.start = { ...start };
        selectionRange.end = end ? { ...end } : { ...start };
    }

    /**
     * Get selection range
     * @returns {Object} { start: {col, row}, end: {col, row} }
     */
    function getSelectionRange() {
        return {
            start: { ...selectionRange.start },
            end: { ...selectionRange.end }
        };
    }

    /**
     * Check if selection is a single cell
     * @returns {boolean}
     */
    function isSingleCellSelected() {
        return selectionRange.start.col === selectionRange.end.col &&
               selectionRange.start.row === selectionRange.end.row;
    }

    /**
     * Set active sheet index
     * @param {number} index - Sheet index
     */
    function setActiveSheet(index) {
        activeSheet = index;
    }

    /**
     * Get active sheet index
     * @returns {number}
     */
    function getActiveSheet() {
        return activeSheet;
    }

    // ========================================================================
    // Sync Context API
    // ========================================================================

    /**
     * Update sync context (called by sync adapter)
     * @param {Object} context - { enabled?, state?, peerCount? }
     */
    function setSyncContext(context) {
        let changed = false;
        if (context.enabled !== undefined && context.enabled !== syncContext.enabled) {
            syncContext.enabled = context.enabled;
            changed = true;
        }
        if (context.state !== undefined && context.state !== syncContext.state) {
            syncContext.state = context.state;
            changed = true;
        }
        if (context.peerCount !== undefined && context.peerCount !== syncContext.peerCount) {
            syncContext.peerCount = context.peerCount;
            changed = true;
        }

        if (changed) {
            log('Sync context changed:', syncContext);
            for (const listener of syncListeners) {
                try {
                    listener({ ...syncContext });
                } catch (e) {
                    console.error('[UIState] Sync listener error:', e);
                }
            }
        }
    }

    /**
     * Get current sync context
     * @returns {Object} { enabled, state, peerCount }
     */
    function getSyncContext() {
        return { ...syncContext };
    }

    /**
     * Check if sync is enabled (connected to a room)
     * @returns {boolean}
     */
    function isSyncEnabled() {
        return syncContext.enabled;
    }

    /**
     * Check if currently syncing (initial sync after joining)
     * @returns {boolean}
     */
    function isSyncing() {
        return syncContext.state === 'syncing';
    }

    /**
     * Check if sync is online (fully connected and synced)
     * @returns {boolean}
     */
    function isSyncOnline() {
        return syncContext.state === 'online';
    }

    /**
     * Subscribe to sync context changes
     * @param {Function} listener - Callback function({ enabled, state, peerCount })
     * @returns {Function} Unsubscribe function
     */
    function onSyncChange(listener) {
        syncListeners.add(listener);
        return () => syncListeners.delete(listener);
    }

    /**
     * Subscribe to state changes
     * @param {Function} listener - Callback function(event)
     * @returns {Function} Unsubscribe function
     */
    function subscribe(listener) {
        listeners.add(listener);
        return () => listeners.delete(listener);
    }

    /**
     * Alias for subscribe - register a state change listener
     * @param {Function} listener - Callback function({ previousState, currentState, event, context })
     * @returns {Function} Unsubscribe function
     */
    function onStateChange(listener) {
        return subscribe(listener);
    }

    /**
     * Get the currently selected cell (anchor of selection)
     * @returns {{col: number, row: number}} Selected cell coordinates
     */
    function getSelectedCell() {
        return { ...selectionRange.start };
    }

    /**
     * Get the selection start (anchor cell)
     * @returns {{col: number, row: number}} Selection start coordinates
     */
    function getSelectionStart() {
        return { ...selectionRange.start };
    }

    /**
     * Get the selection end (current end of range)
     * @returns {{col: number, row: number}} Selection end coordinates
     */
    function getSelectionEnd() {
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
        onStateChange
    };
}

// Convenience: Create a default instance
export const uiState = createUIStateMachine();
