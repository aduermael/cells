// =============================================================================
// Keyboard Event Handlers
// =============================================================================
//
// Keyboard navigation and shortcut handling for the spreadsheet grid.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Arrow keys: cell navigation with Shift for range selection
// - Tab/Enter: cell navigation (Tab moves right, Enter moves down)
// - Delete/Backspace: clear cell content or delete range
// - F2: enter edit mode on selected cell
// - Escape: cancel editing or collapse range selection
// - Printable characters: start replace mode (type to replace cell content)
// - Clipboard shortcuts: Ctrl/Cmd+C/X/V for copy/cut/paste
// - Debug shortcuts: Ctrl+Shift+D for AST debug panel
//
// =============================================================================

import {
    HEADER_WIDTH,
    HEADER_HEIGHT,
    DEFAULT_COL_WIDTH,
    DEFAULT_ROW_HEIGHT,
} from "./grid-renderer";
import { hasRangeSelection } from "./grid-utils";
import type { AppEventManagerConfig } from "./app-events";

// =============================================================================
// Keyboard Event Handler Mixin
// =============================================================================

/**
 * Mixin class containing all keyboard event handlers.
 * Designed to be used with AppEventManager via composition.
 */
export class KeyboardEventHandlers {
    protected config: AppEventManagerConfig;

    constructor(config: AppEventManagerConfig) {
        this.config = config;
    }

    // =========================================================================
    // Setup
    // =========================================================================

    setupKeyboardEvents(): void {
        document.addEventListener("keydown", (e) => this.handleKeyDown(e));
        document.addEventListener("keyup", (e) => this.handleKeyUp(e));
    }

    // =========================================================================
    // Key Down Handler
    // =========================================================================

    handleKeyDown(e: KeyboardEvent): void {
        const {
            uiStateMachine,
            cellEditor,
            getSheetInfo,
            getSelectedCell,
            getSelectionStart,
            getSelectionEnd,
            setSelectionStart,
            setSelectionEnd,
            setSelectedCell,
            getScrollX,
            getScrollY,
            setScrollX,
            setScrollY,
            getColWidths,
            getRowHeights,
            canvas,
            render,
            updateFormulaBar,
            fetchViewportNow,
            toggleAstDebugPanel,
        } = this.config;

        // Update modifier state for the state machine
        uiStateMachine.updateModifiersFromEvent(e);

        // Ctrl+Shift+D toggles AST debug panel (works even during editing)
        if (e.ctrlKey && e.shiftKey && e.key === "D") {
            e.preventDefault();
            toggleAstDebugPanel();
            return;
        }

        // Clipboard shortcuts (Cmd/Ctrl+C/V/X) - only when NOT editing
        const isMod = e.metaKey || e.ctrlKey;
        const { scriptPanel } = this.config;

        // Check if there's text selected outside the canvas
        const selection = window.getSelection();
        const hasTextSelection = selection && selection.toString().length > 0;
        const selectionInCanvas = selection?.anchorNode?.parentElement?.closest("canvas");

        // Check if focus is on an input/textarea (e.g., agent panel)
        const activeEl = document.activeElement;
        const isInputFocused =
            activeEl &&
            activeEl !== canvas &&
            (activeEl.tagName.toLowerCase() === "input" ||
                activeEl.tagName.toLowerCase() === "textarea" ||
                (activeEl as HTMLElement).isContentEditable);

        if (
            isMod &&
            !cellEditor.isEditing() &&
            !uiStateMachine.isInState("FORMULA_BAR_EDITING") &&
            !uiStateMachine.isInState("COLUMN_HEADER_EDITING") &&
            !scriptPanel.isEditorFocused() &&
            !isInputFocused &&
            !(hasTextSelection && !selectionInCanvas)
        ) {
            const { clipboardManager } = this.config;
            switch (e.key.toLowerCase()) {
                case "c":
                    e.preventDefault();
                    clipboardManager.copy();
                    return;
                case "x":
                    e.preventDefault();
                    clipboardManager.cut();
                    return;
                case "v":
                    e.preventDefault();
                    clipboardManager.paste();
                    return;
            }
        }

        if (
            cellEditor.isEditing() ||
            uiStateMachine.isInState("FORMULA_BAR_EDITING") ||
            uiStateMachine.isInState("COLUMN_HEADER_EDITING")
        ) {
            return;
        }

        // Ignore keyboard events when focus is on other editable elements
        // (reuses isInputFocused check from clipboard shortcuts above)
        if (isInputFocused) {
            return;
        }

        const selectedCell = getSelectedCell();
        const sheetInfo = getSheetInfo();
        if (!selectedCell || !sheetInfo) return;

        // Determine current end position for range extension
        const selEnd = getSelectionEnd();
        const currentEnd = selEnd || selectedCell;
        let newCol = currentEnd.col;
        let newRow = currentEnd.row;
        let handled = false;
        let isExtendingSelection = e.shiftKey;

        const colWidths = getColWidths();
        const rowHeights = getRowHeights();

        switch (e.key) {
            case "ArrowUp":
                newRow = Math.max(0, currentEnd.row - 1);
                handled = true;
                break;
            case "ArrowDown": {
                const discoveredRows = this.config.getDiscoveredRows();
                newRow = Math.min(
                    Math.max(sheetInfo.rowCount, discoveredRows) - 1,
                    currentEnd.row + 1,
                );
                handled = true;
                break;
            }
            case "ArrowLeft":
                newCol = Math.max(0, currentEnd.col - 1);
                handled = true;
                break;
            case "ArrowRight":
                newCol = Math.min(sheetInfo.colCount - 1, currentEnd.col + 1);
                handled = true;
                break;
            case "Tab":
                e.preventDefault();
                isExtendingSelection = false;
                if (e.shiftKey) {
                    newCol = Math.max(0, selectedCell.col - 1);
                } else {
                    newCol = Math.min(
                        sheetInfo.colCount - 1,
                        selectedCell.col + 1,
                    );
                }
                newRow = selectedCell.row;
                handled = true;
                break;
            case "F2":
                e.preventDefault();
                cellEditor.startEditing({ mode: "select" });
                return;
            case "Enter": {
                e.preventDefault();
                isExtendingSelection = false;
                const discoveredRows = this.config.getDiscoveredRows();
                const maxRow = Math.max(sheetInfo.rowCount, discoveredRows) - 1;
                if (e.shiftKey) {
                    newRow = Math.max(0, selectedCell.row - 1);
                } else {
                    newRow = Math.min(maxRow, selectedCell.row + 1);
                }
                newCol = selectedCell.col;
                handled = true;
                break;
            }
            case "Delete":
            case "Backspace":
                e.preventDefault();
                // Delete/Backspace always clears cells (matches Excel behavior)
                // For single cell or range selection, use deleteRangeCells
                cellEditor.deleteRangeCells();
                return;
            case "Escape":
                if (hasRangeSelection(getSelectionStart(), getSelectionEnd())) {
                    e.preventDefault();
                    const selStart = getSelectionStart();
                    if (selStart) {
                        setSelectionEnd({
                            col: selStart.col,
                            row: selStart.row,
                        });
                        setSelectedCell({
                            col: selStart.col,
                            row: selStart.row,
                        });
                    }
                    render();
                    updateFormulaBar();
                }
                return;
            default:
                // Printable character - start replace mode
                if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
                    e.preventDefault();
                    const selStart = getSelectionStart();
                    if (selStart && hasRangeSelection(selStart, getSelectionEnd())) {
                        setSelectedCell(selStart);
                        setSelectionStart(selStart);
                        setSelectionEnd(selStart);
                        render();
                    }
                    cellEditor.startEditing({
                        mode: "replace",
                        initialChar: e.key,
                    });
                    return;
                }
                return;
        }

        if (handled) {
            e.preventDefault();
        }

        if (isExtendingSelection) {
            let selStart = getSelectionStart();
            if (!selStart) {
                selStart = { col: selectedCell.col, row: selectedCell.row };
                setSelectionStart(selStart);
            }
            setSelectionEnd({ col: newCol, row: newRow });
            setSelectedCell({ col: selStart.col, row: selStart.row });
        } else {
            setSelectedCell({ col: newCol, row: newRow });
            setSelectionStart({ col: newCol, row: newRow });
            setSelectionEnd({ col: newCol, row: newRow });
        }

        // Scroll to keep selection visible
        let selX = HEADER_WIDTH;
        for (let i = 0; i < newCol; i++) {
            selX += colWidths.get(i) || DEFAULT_COL_WIDTH;
        }
        let selY = HEADER_HEIGHT;
        for (let i = 0; i < newRow; i++) {
            selY += rowHeights.get(i) || DEFAULT_ROW_HEIGHT;
        }
        const selW = colWidths.get(newCol) || DEFAULT_COL_WIDTH;
        const selH = rowHeights.get(newRow) || DEFAULT_ROW_HEIGHT;

        const viewWidth = canvas.clientWidth;
        const viewHeight = canvas.clientHeight;
        let scrollX = getScrollX();
        let scrollY = getScrollY();

        if (selX - scrollX < HEADER_WIDTH) {
            scrollX = selX - HEADER_WIDTH;
        } else if (selX + selW - scrollX > viewWidth) {
            scrollX = selX + selW - viewWidth;
        }

        if (selY - scrollY < HEADER_HEIGHT) {
            scrollY = selY - HEADER_HEIGHT;
        } else if (selY + selH - scrollY > viewHeight) {
            scrollY = selY + selH - viewHeight;
        }

        setScrollX(scrollX);
        setScrollY(scrollY);

        render();
        updateFormulaBar();
        fetchViewportNow();
    }

    // =========================================================================
    // Key Up Handler
    // =========================================================================

    handleKeyUp(e: KeyboardEvent): void {
        this.config.uiStateMachine.updateModifiersFromEvent(e);
    }
}
