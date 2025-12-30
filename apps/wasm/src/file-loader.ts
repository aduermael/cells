// File Loader - File loading, exporting, and new file creation
// Handles loading files from disk, auto-loading from persistence, exporting,
// and creating new empty workbooks.

import { CellsClient } from "./client";
import { WasmDataSource, type DataChangeType } from "./wasm-data-source";
import { detectFormat, getBaseName, downloadBlob } from "./utils";
import { getMenuStateManager } from "./menu-state";
import { showConfirm } from "./modal";
import type { FileFormat } from "./types";

// =============================================================================
// Types
// =============================================================================

/** Configuration for FileLoader */
export interface FileLoaderConfig {
  // DOM elements
  loading: HTMLElement;
  error: HTMLElement;
  canvas: HTMLCanvasElement;
  formulaBar: HTMLElement;
  bottomBar: HTMLElement;
  emptyState: HTMLElement;
  fileInput: HTMLInputElement;
  dropZone: HTMLElement;

  // Persistence callbacks
  saveFileToIndexedDB: (data: ArrayBuffer) => Promise<void>;
  saveFileMeta: (name: string, format: FileFormat) => void;
  loadFileFromIndexedDB: () => Promise<ArrayBuffer | undefined>;
  loadFileMeta: () => { name: string; format: FileFormat } | null;
  clearPersistedFile: () => Promise<void>;

  // State callbacks
  onDataSourceCreated: (dataSource: WasmDataSource) => void;
  onDataChanged: (changeType: DataChangeType) => void;
  getDataSource: () => WasmDataSource | null;

  // View callbacks
  resetViewState: () => void;
  resizeCanvas: () => void;
  fetchSheetInfo: () => Promise<void>;
  fetchViewport: () => Promise<void>;
  fetchSheets: () => Promise<void>;
  render: () => void;
  updateFormulaBar: () => void;
  checkAutoJoinRoom: () => void;
  leaveCollaborationRoom: () => void;
  clearRoomIdFromUrl: () => void;
  resetSheetState: () => void;
}

// =============================================================================
// FileLoader Class
// =============================================================================

/**
 * FileLoader manages file operations for the spreadsheet application.
 *
 * Responsibilities:
 * - Loading files from File objects (drag/drop, file picker)
 * - Auto-loading persisted files on startup
 * - Creating new empty workbooks
 * - Exporting workbooks to various formats
 * - Setting up drag-and-drop handlers
 * - Setting up file input handler
 */
export class FileLoader {
  private config: FileLoaderConfig;
  private wasmClient: CellsClient | null = null;
  private hasFileLoaded: boolean = false;
  private dragCounter: number = 0;

  constructor(config: FileLoaderConfig) {
    this.config = config;
  }

  // =========================================================================
  // WASM Client Management
  // =========================================================================

  /**
   * Ensure WASM client is initialized
   */
  async ensureWasmClient(): Promise<CellsClient> {
    if (this.wasmClient) return this.wasmClient;

    this.wasmClient = new CellsClient("./worker.js");
    await this.wasmClient.ready;

    // NOTE: Collaboration is NOT initialized here - it's done lazily
    // when user clicks "Copy Link" (requires a workbook to be loaded first)

    return this.wasmClient;
  }

  /**
   * Get the WASM client (may be null if not initialized)
   */
  getWasmClient(): CellsClient | null {
    return this.wasmClient;
  }

  /**
   * Check if a file has been loaded
   */
  getHasFileLoaded(): boolean {
    return this.hasFileLoaded;
  }

  // =========================================================================
  // File Loading
  // =========================================================================

  /**
   * Load a file from a File object (e.g., from file input or drag/drop)
   */
  async loadFile(file: File): Promise<void> {
    const { loading, error, emptyState, saveFileToIndexedDB, saveFileMeta } =
      this.config;

    loading.textContent = "";
    loading.innerHTML =
      '<span class="spinner"></span>Loading ' + file.name + "...";
    loading.style.display = "block";
    error.style.display = "none";
    emptyState.classList.add("hidden");

    // Set up progress callback to update loading indicator with cell count
    const client = await this.ensureWasmClient();
    const fileName = file.name;
    client.setOnLoadProgress((cellsLoaded: number, totalEstimate: number) => {
      const cellsText = cellsLoaded.toLocaleString();
      if (totalEstimate > 0 && cellsLoaded < totalEstimate) {
        const pct = Math.round((cellsLoaded / totalEstimate) * 100);
        loading.innerHTML =
          '<span class="spinner"></span>Loading ' +
          fileName +
          "... " +
          cellsText +
          " cells (" +
          pct +
          "%)";
      } else {
        loading.innerHTML =
          '<span class="spinner"></span>Loading ' +
          fileName +
          "... " +
          cellsText +
          " cells";
      }
    });

    try {
      const data = await file.arrayBuffer();
      const format = detectFormat(file.name, data);
      const baseName = getBaseName(file.name);

      // Persist file to IndexedDB BEFORE loading into WASM
      // (WASM may detach the ArrayBuffer, making it unclonable)
      try {
        await saveFileToIndexedDB(data);
        saveFileMeta(file.name, format);
      } catch (persistErr) {
        console.warn("Failed to persist file:", persistErr);
      }

      await this.loadFileData(data, format, baseName);

      loading.style.display = "none";
    } catch (e) {
      console.error("Error loading file:", e);
      loading.style.display = "none";
      error.textContent = "Failed to load: " + (e as Error).message;
      error.style.display = "block";
    } finally {
      // Remove progress callback
      client.removeOnLoadProgress();
    }
  }

  /**
   * Core file loading logic (shared by loadFile and auto-load from persistence)
   */
  async loadFileData(
    data: ArrayBuffer,
    format: FileFormat,
    baseName: string
  ): Promise<void> {
    const {
      canvas,
      formulaBar,
      bottomBar,
      emptyState,
      onDataSourceCreated,
      onDataChanged,
      resetViewState,
      resizeCanvas,
      fetchSheetInfo,
      fetchViewport,
      fetchSheets,
      render,
      updateFormulaBar,
      checkAutoJoinRoom,
    } = this.config;

    const client = await this.ensureWasmClient();

    if (format === "zcd") {
      const text = new TextDecoder().decode(data);
      await client.loadCells(text);
    } else if (format === "csv") {
      await client.loadCSV(data);
    } else if (format === "xlsx") {
      await client.loadXLSX(data);
    }

    const dataSource = new WasmDataSource(client);

    // For ZCD format, use the workbook name from the D line if available
    let workbookName = baseName;
    if (format === "zcd") {
      const parsedName = await client.getWorkbookName();
      if (parsedName && parsedName.trim()) {
        workbookName = parsedName;
      }
    }
    dataSource.setWorkbookName(workbookName);
    dataSource.setOnChange(onDataChanged);
    this.hasFileLoaded = true;

    // Notify app of new data source
    onDataSourceCreated(dataSource);

    // Reset state
    resetViewState();

    // Enable export button and show New button
    const exportBtn = document.getElementById("export-btn") as HTMLButtonElement | null;
    const newBtn = document.getElementById("new-btn");
    if (exportBtn) exportBtn.disabled = false;
    if (newBtn) newBtn.style.display = "";

    // Show the canvas, formula bar, and bottom bar (hide empty state)
    canvas.style.display = "block";
    formulaBar.classList.remove("hidden");
    bottomBar.classList.remove("hidden");
    emptyState.classList.add("hidden");
    resizeCanvas();

    await fetchSheetInfo();
    await fetchViewport();
    await fetchSheets();
    render();
    updateFormulaBar();

    // Check if we should auto-join a collaboration room from URL
    checkAutoJoinRoom();
  }

  /**
   * Try to auto-load a persisted file from IndexedDB on startup
   * @returns true if a file was loaded, false otherwise
   */
  async tryAutoLoadPersistedFile(): Promise<boolean> {
    const {
      loading,
      emptyState,
      loadFileFromIndexedDB,
      loadFileMeta,
      clearPersistedFile,
    } = this.config;

    const meta = loadFileMeta();
    if (!meta) return false;

    // Set up progress callback to update loading indicator with cell count
    const client = await this.ensureWasmClient();
    const fileName = meta.name;
    client.setOnLoadProgress((cellsLoaded: number, totalEstimate: number) => {
      const cellsText = cellsLoaded.toLocaleString();
      if (totalEstimate > 0 && cellsLoaded < totalEstimate) {
        const pct = Math.round((cellsLoaded / totalEstimate) * 100);
        loading.innerHTML =
          '<span class="spinner"></span>Restoring ' +
          fileName +
          "... " +
          cellsText +
          " cells (" +
          pct +
          "%)";
      } else {
        loading.innerHTML =
          '<span class="spinner"></span>Restoring ' +
          fileName +
          "... " +
          cellsText +
          " cells";
      }
    });

    try {
      const data = await loadFileFromIndexedDB();
      if (!data) {
        client.removeOnLoadProgress();
        return false;
      }

      loading.textContent = "";
      loading.innerHTML =
        '<span class="spinner"></span>Restoring ' + meta.name + "...";
      loading.style.display = "block";
      emptyState.classList.add("hidden");

      const baseName = getBaseName(meta.name);
      await this.loadFileData(data, meta.format, baseName);

      loading.style.display = "none";
      return true;
    } catch (e) {
      console.warn("Failed to auto-load persisted file:", e);
      // Clear corrupted data
      try {
        await clearPersistedFile();
      } catch (clearErr) {
        console.warn("Failed to clear corrupted persistence:", clearErr);
      }
      return false;
    } finally {
      client.removeOnLoadProgress();
    }
  }

  // =========================================================================
  // New File / Empty Workbook
  // =========================================================================

  /**
   * Create a new empty workbook
   */
  async createEmptyWorkbook(): Promise<void> {
    const {
      canvas,
      formulaBar,
      bottomBar,
      emptyState,
      onDataSourceCreated,
      onDataChanged,
      resetViewState,
      resizeCanvas,
      fetchSheetInfo,
      fetchViewport,
      fetchSheets,
      render,
      updateFormulaBar,
      checkAutoJoinRoom,
    } = this.config;

    const client = await this.ensureWasmClient();
    await client.createEmpty();

    const dataSource = new WasmDataSource(client);
    dataSource.setWorkbookName("Untitled");
    dataSource.setOnChange(onDataChanged);
    this.hasFileLoaded = true;

    // Notify app of new data source
    onDataSourceCreated(dataSource);

    // Reset state
    resetViewState();

    // Enable export button and show New button
    const exportBtn = document.getElementById("export-btn") as HTMLButtonElement | null;
    const newBtn = document.getElementById("new-btn");
    if (exportBtn) exportBtn.disabled = false;
    if (newBtn) newBtn.style.display = "";

    // Show the canvas, formula bar, and bottom bar (hide empty state)
    canvas.style.display = "block";
    formulaBar.classList.remove("hidden");
    bottomBar.classList.remove("hidden");
    emptyState.classList.add("hidden");
    resizeCanvas();

    await fetchSheetInfo();
    await fetchViewport();
    await fetchSheets();
    render();
    updateFormulaBar();

    // Check if we should auto-join a collaboration room from URL
    checkAutoJoinRoom();
  }

  /**
   * Create a new file (clears current state and creates empty workbook)
   */
  async newFile(): Promise<void> {
    const {
      error,
      clearPersistedFile,
      leaveCollaborationRoom,
      clearRoomIdFromUrl,
      resetSheetState,
    } = this.config;

    // Clear persisted file
    try {
      await clearPersistedFile();
    } catch (e) {
      console.warn("Failed to clear persisted file:", e);
    }

    // Leave collaboration room and clear URL parameter
    leaveCollaborationRoom();
    clearRoomIdFromUrl();

    // Reset sheet state
    resetSheetState();
    error.style.display = "none";

    // Create fresh empty workbook (keeps UI showing, not empty state)
    await this.createEmptyWorkbook();
  }

  // =========================================================================
  // Export
  // =========================================================================

  /**
   * Export the current workbook to a file format
   */
  async exportAs(format: FileFormat): Promise<void> {
    const dataSource = this.config.getDataSource();
    if (!dataSource) {
      alert("No file loaded");
      return;
    }
    try {
      // Warn when exporting to CSV if workbook contains formulas
      if (format === "csv") {
        const hasFormulas = await dataSource.hasFormulas();
        if (hasFormulas) {
          const proceed = await showConfirm({
            title: "Export to CSV",
            body:
              "This workbook contains formulas. CSV format only saves computed values, not the formulas themselves.<br><br>" +
              "Use <strong>ZCD</strong> or <strong>XLSX</strong> format to preserve formulas.",
            primaryLabel: "Export Anyway",
            secondaryLabel: "Cancel",
            warning: true,
          });
          if (!proceed) {
            return;
          }
        }
      }

      const result = await dataSource.exportAs(format);
      downloadBlob(result.blob, result.filename);
    } catch (e) {
      console.error("Export error:", e);
      alert("Export failed: " + (e as Error).message);
    }
  }

  // =========================================================================
  // Event Setup
  // =========================================================================

  /**
   * Set up file input change handler
   */
  setupFileInput(): void {
    const { fileInput } = this.config;

    fileInput.addEventListener("change", (e) => {
      const target = e.target as HTMLInputElement;
      const file = target.files?.[0];
      if (file) {
        this.loadFile(file);
        target.value = "";
      }
    });
  }

  /**
   * Open file picker
   */
  openFile(): void {
    this.config.fileInput.click();
  }

  /**
   * Set up drag and drop handlers on the document
   */
  setupDragAndDrop(): void {
    const { dropZone } = this.config;

    document.addEventListener("dragenter", (e) => {
      e.preventDefault();
      this.dragCounter++;
      if (this.dragCounter === 1) {
        dropZone.classList.add("visible");
      }
    });

    document.addEventListener("dragleave", (e) => {
      e.preventDefault();
      this.dragCounter--;
      if (this.dragCounter === 0) {
        dropZone.classList.remove("visible");
      }
    });

    document.addEventListener("dragover", (e) => {
      e.preventDefault();
    });

    document.addEventListener("drop", (e) => {
      e.preventDefault();
      this.dragCounter = 0;
      dropZone.classList.remove("visible");

      const file = e.dataTransfer?.files[0];
      if (file) {
        this.loadFile(file);
      }
    });
  }

  /**
   * Set up export dropdown handlers
   */
  setupExportDropdown(): void {
    const exportDropdown = document.getElementById("export-dropdown");
    const exportBtn = document.getElementById("export-btn");

    if (!exportDropdown || !exportBtn) return;

    const menuState = getMenuStateManager();

    // Register with menu state manager
    menuState.registerMenu("export", () => {
      exportDropdown.classList.remove("open");
    });

    exportBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      if ((exportBtn as HTMLButtonElement).disabled) return;

      const isOpen = exportDropdown.classList.contains("open");
      if (isOpen) {
        exportDropdown.classList.remove("open");
        menuState.closeMenu("export");
      } else {
        menuState.openMenu("export"); // This closes other menus
        exportDropdown.classList.add("open");
      }
    });

    // Close dropdown when clicking outside
    document.addEventListener("click", (e) => {
      if (!exportDropdown.contains(e.target as Node)) {
        exportDropdown.classList.remove("open");
        menuState.closeMenu("export");
      }
    });

    // Close dropdown when clicking a menu item
    exportDropdown.querySelectorAll(".dropdown-item").forEach((item) => {
      item.addEventListener("click", () => {
        exportDropdown.classList.remove("open");
        menuState.closeMenu("export");
      });
    });
  }
}
