// =============================================================================
// Application Initializer
// =============================================================================
//
// Creates and wires together all application components from a CellsClient.
// This is the main entry point called from index.html after WASM loads.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Initialize theme
// - Create App instance with all DOM references
// - Wire components together via factory modules
// - Set up collaboration and data listeners
// - Handle room joining from URL parameters
//
// Component wiring (delegated to init-components.ts):
// - CellsClient → WasmDataSource → App → GridRenderer
// - App → CellEditor, ColumnHeaderEditor, FormulaBarEditor, SheetTabsManager
// - App → FileLoader, ClipboardManager, ScriptPanel, AgentPanel
// - App → CppSyncAdapter → CollabUI, PresenceBroadcaster
//
// =============================================================================

import { App, createApp } from "./app";
import { initTheme } from "./theme";
import { createComponents, type Components } from "./init-components";
import { setupDataListeners } from "./init-listeners";
import { setupCollaboration, clearRoomIdFromUrl } from "./init-collab";
import { onFontLoaded, preloadCommonFonts } from "./font-loader";

// Re-export types
import type { CellEditor } from "./cell-editor";
import type { ColumnHeaderEditor, FormulaBarEditor } from "./header-editor";
import type { SheetTabsManager } from "./sheet-tabs";
import type { PresenceBroadcaster } from "./presence-broadcast";
import type { AppEventManager } from "./app-events";
import type { FileLoader } from "./file-loader";
import type { AstDebugPanel } from "./ast-debug";
import type { ScriptPanel } from "./script-panel";
import type { AgentPanel } from "./agent-panel";
import type { WorkbookTitleEditor } from "./workbook-title-editor";
import type { ClipboardManager } from "./clipboard";
import type { FormatControls } from "./format-controls";
import type { StyleControls } from "./style-controls";

// =============================================================================
// Types
// =============================================================================

/** Application context returned by initApp */
export interface AppContext {
  app: App;
  fileLoader: FileLoader;
  cellEditor: CellEditor;
  columnHeaderEditor: ColumnHeaderEditor;
  formulaBarEditor: FormulaBarEditor;
  sheetTabsManager: SheetTabsManager;
  presenceBroadcaster: PresenceBroadcaster;
  eventManager: AppEventManager;
  astDebugPanel: AstDebugPanel;
  scriptPanel: ScriptPanel;
  agentPanel: AgentPanel;
  workbookTitleEditor: WorkbookTitleEditor;
  clipboardManager: ClipboardManager;
  formatControls: FormatControls;
  styleControls: StyleControls;

  // Methods exposed for index.html onclick handlers
  openFile: () => void;
  newFile: () => Promise<void>;
  exportAs: (format: "csv" | "xlsx" | "zcd") => Promise<void>;

  // Start the application
  init: () => Promise<void>;
}

// =============================================================================
// Initialization
// =============================================================================

/**
 * Initialize the application and wire all modules together
 * @returns AppContext with all modules and exposed methods
 */
export function initApp(): AppContext {
  // =========================================================================
  // Initialize theme (must be early to avoid flash of wrong theme)
  // =========================================================================

  initTheme();

  // =========================================================================
  // Create App and core state
  // =========================================================================

  const app = createApp();
  const elements = app.elements;

  // =========================================================================
  // Set up data listeners (forward declaration for components)
  // =========================================================================

  // These will be initialized after components are created
  let dataListeners: ReturnType<typeof setupDataListeners>;
  let collabFunctions: ReturnType<typeof setupCollaboration>;
  let components: Components;

  // Temporary render function for data listeners
  const renderProxy = () => components?.render();
  const updateFormulaBarProxy = () => components?.updateFormulaBar();
  const updateScrollbarsProxy = () => components?.updateScrollbars();

  // =========================================================================
  // Create components (with proxied callbacks)
  // =========================================================================

  // First, create a minimal data listeners object with proxied functions
  // This allows circular dependencies between components and listeners
  const dataListenersProxy = {
    fetchViewportNow: () => dataListeners?.fetchViewportNow(),
    fetchViewport: async () => dataListeners?.fetchViewport(),
    fetchSheetInfo: async () => dataListeners?.fetchSheetInfo(),
    fetchSheets: async () => dataListeners?.fetchSheets(),
    handleDataChanged: (changeType: import("./wasm-data-source").DataChangeType) =>
      dataListeners?.handleDataChanged(changeType),
  };

  const collabProxy = {
    checkAutoJoinRoom: async () => collabFunctions?.checkAutoJoinRoom(),
  };

  // Now create the components
  components = createComponents({
    app,
    elements,
    fetchViewportNow: () => dataListenersProxy.fetchViewportNow(),
    fetchViewport: () => dataListenersProxy.fetchViewport(),
    fetchSheetInfo: () => dataListenersProxy.fetchSheetInfo(),
    fetchSheets: () => dataListenersProxy.fetchSheets(),
    handleDataChanged: (changeType) => dataListenersProxy.handleDataChanged(changeType),
    checkAutoJoinRoom: () => collabProxy.checkAutoJoinRoom(),
    clearRoomIdFromUrl,
  });

  // =========================================================================
  // Set up data listeners (now with real components)
  // =========================================================================

  dataListeners = setupDataListeners({
    app,
    sheetTabsManager: components.sheetTabsManager,
    render: renderProxy,
    updateFormulaBar: updateFormulaBarProxy,
    updateScrollbars: updateScrollbarsProxy,
  });

  // =========================================================================
  // Set up collaboration
  // =========================================================================

  collabFunctions = setupCollaboration({
    app,
    cellEditor: components.cellEditor,
    formulaBarEditor: components.formulaBarEditor,
    presenceBroadcaster: components.presenceBroadcaster,
    fileLoader: components.fileLoader,
    renderPresenceOnly: components.renderPresenceOnly,
  });

  // =========================================================================
  // Main init
  // =========================================================================

  async function init(): Promise<void> {
    // Check for debug noPrune URL parameter
    const urlParams = new URLSearchParams(window.location.search);
    if (urlParams.get("noPrune") === "true") {
      app.client.engine.setDebugNoPrune(true);
      console.log("[Debug] OpLog pruning disabled via ?noPrune=true");
    }

    // Set up font loading - re-render when fonts are loaded
    onFontLoaded(() => {
      components.render();
    });
    preloadCommonFonts();

    // Set up event listeners
    components.eventManager.setupEventListeners();
    components.fileLoader.setupFileInput();
    components.fileLoader.setupDragAndDrop();
    components.fileLoader.setupExportDropdown();

    // Initialize scrollbars
    components.initScrollbars();

    // Connect focus manager to scrollbar for editor refocusing after scroll
    if (app.scrollbarManager) {
      app.scrollbarManager.setFocusManager(components.focusManager);
    }

    components.resizeCanvas();

    // Try to auto-load persisted file
    const loaded = await components.fileLoader.tryAutoLoadPersistedFile();

    if (!loaded) {
      // Create empty workbook on startup
      await components.fileLoader.createEmptyWorkbook();
    } else {
      // If file loaded, ensure we have a default selection
      if (!app.selectedCell) {
        components.setDefaultSelection();
        components.render();
        components.updateFormulaBar();
      }
    }
  }

  // =========================================================================
  // Return AppContext
  // =========================================================================

  return {
    app,
    fileLoader: components.fileLoader,
    cellEditor: components.cellEditor,
    columnHeaderEditor: components.columnHeaderEditor,
    formulaBarEditor: components.formulaBarEditor,
    sheetTabsManager: components.sheetTabsManager,
    presenceBroadcaster: components.presenceBroadcaster,
    eventManager: components.eventManager,
    astDebugPanel: components.astDebugPanel,
    scriptPanel: components.scriptPanel,
    agentPanel: components.agentPanel,
    workbookTitleEditor: components.workbookTitleEditor,
    clipboardManager: components.clipboardManager,
    formatControls: components.formatControls,
    styleControls: components.styleControls,

    openFile: () => components.fileLoader.openFile(),
    newFile: () => components.fileLoader.newFile(),
    exportAs: (format) => components.fileLoader.exportAs(format),

    init,
  };
}
