// =============================================================================
// Sheet Tabs
// =============================================================================
//
// UI component for the sheet tabs bar at the bottom of the spreadsheet.
// Manages multi-sheet workbooks with tab-based navigation.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Render sheet tabs with active indicator
// - Switch between sheets on tab click
// - Inline editing for sheet renaming
// - Add new sheet button
// - Delete sheet with confirmation dialog
// - Drag-and-drop sheet reordering
// - Context menu for sheet operations
//
// Sheet operations flow:
// - User action → SheetTabsManager → WasmDataSource → C++ CRDT
// - C++ notifies change → DataSource listener → SheetTabsManager.update()
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { UIStateMachine } from "./ui-state";
import { UIEvent } from "./ui-state";
import type { SheetData } from "./app";
import { editingSession } from "./editing-session";

// =============================================================================
// SheetTabsManager Class
// =============================================================================

/**
 * SheetTabsManager handles sheet tab UI and operations.
 *
 * Responsibilities:
 * - Rendering sheet tabs
 * - Switching between sheets
 * - Renaming sheets (inline editing)
 * - Deleting sheets with confirmation
 * - Adding new sheets
 * - Drag-and-drop sheet reordering
 * - Context menu for sheet operations
 */
export class SheetTabsManager {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private uiStateMachine: UIStateMachine;
  private sheetTabsContainer: HTMLElement;
  private addSheetBtn: HTMLButtonElement;

  // Nullable dependencies (set after construction)
  private dataSource: WasmDataSource | null = null;

  // =========================================================================
  // State
  // =========================================================================

  /** All sheets in the workbook */
  private sheets: SheetData[] = [];

  /** Index of active sheet */
  private activeSheetIndex: number = 0;

  /** Reference to context menu element */
  private contextMenu: HTMLElement | null = null;

  /** Sheet drag state */
  private dragSheetIndex: number = -1;
  private dragSheetTargetIndex: number = -1;

  // =========================================================================
  // Callbacks
  // =========================================================================

  private onSetActiveSheetIndex: (index: number) => void;
  private onSetEditingSheetIndex: (index: number) => void;
  private onResetViewState: () => void;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    uiStateMachine: UIStateMachine;
    sheetTabsContainer: HTMLElement;
    addSheetBtn: HTMLButtonElement;
    onSetActiveSheetIndex: (index: number) => void;
    onSetEditingSheetIndex: (index: number) => void;
    onResetViewState: () => void;
  }) {
    this.uiStateMachine = config.uiStateMachine;
    this.sheetTabsContainer = config.sheetTabsContainer;
    this.addSheetBtn = config.addSheetBtn;
    this.onSetActiveSheetIndex = config.onSetActiveSheetIndex;
    this.onSetEditingSheetIndex = config.onSetEditingSheetIndex;
    this.onResetViewState = config.onResetViewState;

    this.setupAddSheetButton();
  }

  // =========================================================================
  // Configuration
  // =========================================================================

  setDataSource(dataSource: WasmDataSource | null): void {
    this.dataSource = dataSource;
  }

  // =========================================================================
  // State Accessors
  // =========================================================================

  getSheets(): SheetData[] {
    return this.sheets;
  }

  getActiveSheetIndex(): number {
    return this.activeSheetIndex;
  }

  setSheets(sheets: SheetData[]): void {
    this.sheets = sheets;
  }

  setActiveSheetIndex(index: number): void {
    this.activeSheetIndex = index;
    this.onSetActiveSheetIndex(index);
  }

  // =========================================================================
  // State Helpers
  // =========================================================================

  isEditingSheetTab(): boolean {
    return this.uiStateMachine.isInState("SHEET_TAB_EDITING");
  }

  isDraggingSheetTab(): boolean {
    return this.uiStateMachine.isInState("SHEET_TAB_DRAGGING");
  }

  // =========================================================================
  // Sheet Tab Rendering
  // =========================================================================

  /**
   * Render all sheet tabs
   */
  renderSheetTabs(): void {
    this.sheetTabsContainer.innerHTML = "";

    this.sheets.forEach((sheet, idx) => {
      const tab = document.createElement("div");
      // Prefer manager index over sheet.active (network refreshes can desync flags).
      tab.className = "sheet-tab" + (idx === this.activeSheetIndex ? " active" : "");
      tab.textContent = sheet.name;
      tab.dataset.index = idx.toString();
      tab.draggable = true;

      // Click to switch sheet
      tab.addEventListener("click", async (e) => {
        if (this.isEditingSheetTab()) return;
        e.preventDefault();
        await this.switchToSheet(idx);
      });

      // Double-click to rename
      tab.addEventListener("dblclick", (e) => {
        e.preventDefault();
        e.stopPropagation();
        this.startEditingSheetTab(idx);
      });

      // Context menu (right-click)
      tab.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        this.showContextMenu(e.clientX, e.clientY, idx);
      });

      // Drag start
      tab.addEventListener("dragstart", (e) => {
        this.uiStateMachine.transition(UIEvent.START_SHEET_TAB_DRAG);
        this.dragSheetIndex = idx;
        tab.classList.add("dragging");
        if (e.dataTransfer) {
          e.dataTransfer.effectAllowed = "move";
          e.dataTransfer.setData("text/plain", idx.toString());
        }
      });

      // Drag end
      tab.addEventListener("dragend", () => {
        this.uiStateMachine.transition(UIEvent.END_SHEET_TAB_DRAG);
        this.dragSheetIndex = -1;
        tab.classList.remove("dragging");
        // Remove any drop indicators
        this.sheetTabsContainer.querySelectorAll(".sheet-tab").forEach((t) => {
          (t as HTMLElement).style.marginLeft = "";
          (t as HTMLElement).style.marginRight = "";
        });
      });

      // Drag over
      tab.addEventListener("dragover", (e) => {
        if (!this.isDraggingSheetTab() || this.dragSheetIndex === idx) return;
        e.preventDefault();
        if (e.dataTransfer) {
          e.dataTransfer.dropEffect = "move";
        }

        const rect = tab.getBoundingClientRect();
        const midX = rect.left + rect.width / 2;
        const insertBefore = e.clientX < midX;

        // Reset all margins
        this.sheetTabsContainer.querySelectorAll(".sheet-tab").forEach((t) => {
          (t as HTMLElement).style.marginLeft = "";
          (t as HTMLElement).style.marginRight = "";
        });

        // Show drop indicator
        if (insertBefore) {
          tab.style.marginLeft = "20px";
          this.dragSheetTargetIndex = idx;
        } else {
          tab.style.marginRight = "20px";
          this.dragSheetTargetIndex = idx + 1;
        }
      });

      // Drop
      tab.addEventListener("drop", async (e) => {
        e.preventDefault();
        if (!this.isDraggingSheetTab() || this.dragSheetIndex < 0) return;

        const fromIdx = this.dragSheetIndex;
        const toIdx = this.dragSheetTargetIndex;

        if (fromIdx !== toIdx && fromIdx + 1 !== toIdx) {
          try {
            if (this.dataSource) {
              const result = await this.dataSource.moveSheet(fromIdx, toIdx);
              this.setActiveSheetIndex(result.activeIndex);
              // Listener handles fetchSheets
            }
          } catch (err) {
            console.error("Error moving sheet:", err);
          }
        }

        this.uiStateMachine.transition(UIEvent.END_SHEET_TAB_DRAG);
        this.dragSheetIndex = -1;
        this.dragSheetTargetIndex = -1;
      });

      this.sheetTabsContainer.appendChild(tab);
    });
  }

  // =========================================================================
  // Sheet Operations
  // =========================================================================

  /**
   * Switch to a different sheet.
   *
   * When in formula editing mode (value starts with "="), preserve the editing state
   * to allow cross-sheet reference picking (Excel-like behavior).
   */
  async switchToSheet(index: number): Promise<void> {
    if (!this.dataSource || index === this.activeSheetIndex) return;

    // Check if we're in formula editing mode - if so, preserve the edit state
    const isFormulaEditing = editingSession.isActive() && editingSession.isFormulaEditing();

    try {
      if (isFormulaEditing) {
        // Cross-sheet formula editing mode:
        // - Keep the editing session active
        // - Don't reset the UI state
        // - Just switch the view to the new sheet
        // The user can click cells on this sheet to insert cross-sheet references
        await this.dataSource.setActiveSheet(index);
        this.setActiveSheetIndex(index);
        // Update UI state machine's active sheet tracking (but don't reset state)
        this.uiStateMachine.setActiveSheet(index);
        // Note: we don't call onResetViewState() to preserve formula editor state
        // The listener will handle fetchSheetInfo, fetchSheets, fetchViewport, render
      } else {
        // Normal sheet switching:
        // Reset view state and UI state before switching
        this.onResetViewState();
        this.uiStateMachine.reset(); // Reset to IDLE state

        await this.dataSource.setActiveSheet(index);
        this.setActiveSheetIndex(index);
        // Listener handles fetchSheetInfo, fetchSheets, fetchViewport, render, updateFormulaBar
      }
    } catch (e) {
      console.error("Error switching sheet:", e);
    }
  }

  /**
   * Add a new sheet
   */
  async addSheet(): Promise<void> {
    if (!this.dataSource) return;
    try {
      const result = await this.dataSource.addSheet();
      // Listener handles fetchSheets, then we switch to the new sheet
      await this.switchToSheet(result.index);
    } catch (err) {
      console.error("Error adding sheet:", err);
    }
  }

  /**
   * Delete a sheet with confirmation handling
   */
  async deleteSheetWithConfirm(index: number): Promise<void> {
    if (!this.dataSource) return;

    // If this is the last sheet, create a new one first
    if (this.sheets.length <= 1) {
      try {
        await this.dataSource.addSheet("Sheet1");
        await this.dataSource.deleteSheet(index);
        this.setActiveSheetIndex(0);
        // Listener handles refresh for both operations
      } catch (err) {
        console.error("Error handling last sheet delete:", err);
      }
      return;
    }

    try {
      // Reset view state before deleting
      this.onResetViewState();

      const result = await this.dataSource.deleteSheet(index);
      this.setActiveSheetIndex(result.activeIndex);
      // Listener handles fetchSheetInfo, fetchSheets, fetchViewport, render, updateFormulaBar
    } catch (err) {
      console.error("Error deleting sheet:", err);
    }
  }

  // =========================================================================
  // Sheet Tab Editing
  // =========================================================================

  /**
   * Start inline editing of a sheet tab
   */
  startEditingSheetTab(index: number): void {
    this.uiStateMachine.transition(UIEvent.START_SHEET_TAB_EDIT);
    this.onSetEditingSheetIndex(index);

    const tab = this.sheetTabsContainer.children[index] as HTMLElement;
    if (!tab) return;

    const sheet = this.sheets[index];
    if (!sheet) return;
    const currentName = sheet.name;

    // Create inline editor
    const editor = document.createElement("input");
    editor.type = "text";
    editor.className = "sheet-tab-editor";
    editor.value = currentName;

    // Replace tab content with editor
    tab.textContent = "";
    tab.appendChild(editor);
    editor.focus();
    editor.select();

    const finishEditing = async (save: boolean) => {
      if (!this.isEditingSheetTab()) return;
      this.uiStateMachine.transition(
        save ? UIEvent.COMMIT_SHEET_TAB_EDIT : UIEvent.CANCEL_SHEET_TAB_EDIT
      );
      this.onSetEditingSheetIndex(-1);

      const newName = editor.value.trim();
      tab.textContent = save && newName ? newName : currentName;

      if (save && newName && newName !== currentName && this.dataSource) {
        try {
          await this.dataSource.renameSheet(index, newName);
          // Listener handles fetchSheets
        } catch (err) {
          console.error("Error renaming sheet:", err);
          tab.textContent = currentName;
        }
      }
    };

    editor.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        finishEditing(true);
      } else if (e.key === "Escape") {
        e.preventDefault();
        finishEditing(false);
      }
      e.stopPropagation();
    });

    editor.addEventListener("blur", () => {
      finishEditing(true);
    });
  }

  // =========================================================================
  // Context Menu
  // =========================================================================

  /**
   * Show context menu for a sheet tab
   */
  showContextMenu(x: number, y: number, sheetIndex: number): void {
    this.hideContextMenu();

    const menu = document.createElement("div");
    menu.className = "sheet-tab-context-menu";
    menu.style.left = x + "px";
    menu.style.top = y + "px";

    // Rename option
    const renameBtn = document.createElement("button");
    renameBtn.className = "sheet-tab-context-menu-item";
    renameBtn.textContent = "Rename";
    renameBtn.addEventListener("click", () => {
      this.hideContextMenu();
      this.startEditingSheetTab(sheetIndex);
    });
    menu.appendChild(renameBtn);

    // Delete option
    const deleteBtn = document.createElement("button");
    deleteBtn.className = "sheet-tab-context-menu-item danger";
    deleteBtn.textContent = "Delete";
    deleteBtn.addEventListener("click", async () => {
      this.hideContextMenu();
      await this.deleteSheetWithConfirm(sheetIndex);
    });
    menu.appendChild(deleteBtn);

    document.body.appendChild(menu);
    this.contextMenu = menu;

    // Close on click outside
    setTimeout(() => {
      document.addEventListener("click", this.hideContextMenu.bind(this), {
        once: true,
      });
    }, 0);
  }

  /**
   * Hide the context menu
   */
  hideContextMenu(): void {
    if (this.contextMenu) {
      this.contextMenu.remove();
      this.contextMenu = null;
    }
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Set up the add sheet button event listener
   */
  private setupAddSheetButton(): void {
    this.addSheetBtn.addEventListener("click", async () => {
      await this.addSheet();
    });
  }
}
