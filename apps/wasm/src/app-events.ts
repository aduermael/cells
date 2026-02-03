// =============================================================================
// Application Event Handlers - Coordinator
// =============================================================================
//
// Central event coordination for canvas interactions, keyboard shortcuts,
// and window events. Translates DOM events into UI state machine events.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// This module serves as the coordinator that:
// - Defines the configuration interface used by all event handlers
// - Sets up event listeners by delegating to specialized handler modules
// - Handles window events (resize)
//
// Event handling is split into:
// - mouse-events.ts: Mouse/pointer events (click, drag, resize, context menu)
// - keyboard-events.ts: Keyboard navigation and shortcuts
//
// =============================================================================

import type { UIStateMachine } from "./ui-state";
import type { WasmDataSource } from "./wasm-data-source";
import type { CppSyncAdapter } from "./cpp-sync-adapter";
import type { SheetInfo, Position } from "./types";
import type { CellEditor } from "./cell-editor";
import type { ColumnHeaderEditor, FormulaBarEditor } from "./header-editor";
import type { PresenceBroadcaster } from "./presence-broadcast";
import type { ClipboardManager } from "./clipboard";
import type { ScriptPanel } from "./script-panel";
import type { GridRenderer } from "./grid-renderer";
import { MouseEventHandlers } from "./mouse-events";
import { KeyboardEventHandlers } from "./keyboard-events";

// =============================================================================
// Types
// =============================================================================

/** Fill handle bounds for hit testing */
export interface FillHandleBounds {
    x: number;
    y: number;
    width: number;
    height: number;
}

/** Configuration for AppEventManager */
export interface AppEventManagerConfig {
    canvas: HTMLCanvasElement;
    uiStateMachine: UIStateMachine;
    cellEditor: CellEditor;
    columnHeaderEditor: ColumnHeaderEditor;
    formulaBarEditor: FormulaBarEditor;
    presenceBroadcaster: PresenceBroadcaster;
    clipboardManager: ClipboardManager;
    scriptPanel: ScriptPanel;
    formulaInput: HTMLInputElement;

    // State accessors
    getSheetInfo: () => SheetInfo | null;
    getZoomScale: () => number;  // Returns current zoom scale (100 = 100%)
    getSelectedCell: () => Position | null;
    getSelectionStart: () => Position | null;
    getSelectionEnd: () => Position | null;
    getScrollX: () => number;
    getScrollY: () => number;
    getColWidths: () => Map<number, number>;
    getRowHeights: () => Map<number, number>;
    getColumns: () => Array<{ id: string; pos: number }>;
    getRows: () => Array<{ id: string; pos: number }>;
    getDataSource: () => WasmDataSource | null;
    getSyncAdapter: () => CppSyncAdapter | null;
    getFillHandleBounds: () => FillHandleBounds | null;
    getGridRenderer: () => GridRenderer;

    // Resize state accessors/mutators
    getResizeColIndex: () => number;
    setResizeColIndex: (v: number) => void;
    getResizeStartX: () => number;
    setResizeStartX: (v: number) => void;
    getResizeStartWidth: () => number;
    setResizeStartWidth: (v: number) => void;
    getResizePreviewX: () => number;
    setResizePreviewX: (v: number) => void;
    getResizeRowIndex: () => number;
    setResizeRowIndex: (v: number) => void;
    getResizeStartY: () => number;
    setResizeStartY: (v: number) => void;
    getResizeStartHeight: () => number;
    setResizeStartHeight: (v: number) => void;
    getResizePreviewY: () => number;
    setResizePreviewY: (v: number) => void;

    // Drag state accessors/mutators
    getDragSourceIndex: () => number;
    setDragSourceIndex: (v: number) => void;
    getDragTargetIndex: () => number;
    setDragTargetIndex: (v: number) => void;
    getDragMouseX: () => number;
    setDragMouseX: (v: number) => void;
    getDragMouseY: () => number;
    setDragMouseY: (v: number) => void;
    getPendingDragColumn: () => boolean;
    setPendingDragColumn: (v: boolean) => void;
    getPendingDragRow: () => boolean;
    setPendingDragRow: (v: boolean) => void;
    getPendingDragStartX: () => number;
    setPendingDragStartX: (v: number) => void;
    getPendingDragStartY: () => number;
    setPendingDragStartY: (v: number) => void;

    // Selection mutators
    setSelectedCell: (cell: Position | null) => void;
    setSelectedColumn: (col: number | null) => void;
    setSelectedRow: (row: number | null) => void;
    setSelectionStart: (pos: Position | null) => void;
    setSelectionEnd: (pos: Position | null) => void;
    setSelection: (cell: Position, start: Position, end: Position) => void;

    // Scroll mutators
    setScrollX: (v: number) => void;
    setScrollY: (v: number) => void;

    // Virtual scrolling
    getDiscoveredRows: () => number;
    setDiscoveredRows: (v: number) => void;

    // Fill drag state accessors/mutators
    getIsFillDragging: () => boolean;
    setIsFillDragging: (v: boolean) => void;
    getFillPreviewRange: () => { minCol: number; maxCol: number; minRow: number; maxRow: number } | null;
    setFillPreviewRange: (v: { minCol: number; maxCol: number; minRow: number; maxRow: number } | null) => void;

    // Formula highlight hover
    getFormulaHighlights: () => import("./grid-constants").FormulaHighlight[];
    getHoveredGridRefIndex: () => number;
    setHoveredGridRefIndex: (v: number) => void;
    getColPixelOffsets: () => Map<number, number>;
    getRowPixelOffsets: () => Map<number, number>;
    updateFormulaBarHoverStyle: () => void;

    // Callbacks
    render: () => void;
    updateFormulaBar: () => void;
    clearFormulaHighlights: () => void;
    resizeCanvas: () => void;
    fetchViewportNow: () => void;
    toggleAstDebugPanel: () => void;
    commitFormulaBarEdit: () => Promise<void>;
    updateScrollbars: () => void;

    // Scroll coordination - check if scrollbar is being dragged
    // to avoid triggering expensive data fetches from wheel events
    isScrollbarDragging?: () => boolean;
}

// =============================================================================
// AppEventManager Class
// =============================================================================

/**
 * AppEventManager sets up and manages all event listeners for canvas
 * and document interactions.
 *
 * Responsibilities:
 * - Canvas wheel/scroll handling
 * - Mouse interactions (selection, resize, drag) - delegated to MouseEventHandlers
 * - Keyboard navigation and editing triggers - delegated to KeyboardEventHandlers
 * - Window resize handling
 */
export class AppEventManager {
    private config: AppEventManagerConfig;
    private mouseHandlers: MouseEventHandlers;
    private keyboardHandlers: KeyboardEventHandlers;

    constructor(config: AppEventManagerConfig) {
        this.config = config;
        this.mouseHandlers = new MouseEventHandlers(config);
        this.keyboardHandlers = new KeyboardEventHandlers(config);
    }

    // =========================================================================
    // Setup
    // =========================================================================

    /**
     * Set up all event listeners
     */
    setupEventListeners(): void {
        this.mouseHandlers.setupCanvasEvents();
        this.keyboardHandlers.setupKeyboardEvents();
        this.setupWindowEvents();
    }

    // =========================================================================
    // Window Events
    // =========================================================================

    private setupWindowEvents(): void {
        window.addEventListener("resize", () => {
            this.config.resizeCanvas();
            this.config.fetchViewportNow();
        });
    }
}
