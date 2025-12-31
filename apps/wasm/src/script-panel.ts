// Script Panel - Luau scripting interface
// Provides a dedicated panel for writing and executing Luau scripts.

import { SyntaxHighlighter, type TokenizeFunction } from "./syntax-highlighter";

// =============================================================================
// Types
// =============================================================================

/** Script execution result */
export interface ScriptResult {
  success: boolean;
  output?: string;
  error?: string;
  instructions: number;
}

// =============================================================================
// ScriptPanel Class
// =============================================================================

/**
 * ScriptPanel manages the Luau scripting interface.
 *
 * Responsibilities:
 * - Toggling panel visibility
 * - Executing scripts via the Luau sandbox
 * - Displaying script output and errors
 * - Panel resizing
 */
export class ScriptPanel {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private panel: HTMLElement;
  private toggleBtn: HTMLElement;
  private editor: HTMLTextAreaElement;
  private backdrop: HTMLPreElement;
  private highlight: HTMLElement;
  private runBtn: HTMLElement;
  private statusEl: HTMLElement;
  private outputEl: HTMLElement;
  private resizeHandle: HTMLElement;

  private executeScript: (script: string) => Promise<ScriptResult>;
  private onScriptExecuted: () => void;

  // =========================================================================
  // State
  // =========================================================================

  private visible: boolean = false;
  private isResizing: boolean = false;
  private startY: number = 0;
  private startHeight: number = 0;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    panel: HTMLElement;
    toggleBtn: HTMLElement;
    editor: HTMLTextAreaElement;
    backdrop: HTMLPreElement;
    highlight: HTMLElement;
    runBtn: HTMLElement;
    statusEl: HTMLElement;
    outputEl: HTMLElement;
    resizeHandle: HTMLElement;
    executeScript: (script: string) => Promise<ScriptResult>;
    onScriptExecuted: () => void;
    tokenize?: TokenizeFunction;
  }) {
    this.panel = config.panel;
    this.toggleBtn = config.toggleBtn;
    this.editor = config.editor;
    this.backdrop = config.backdrop;
    this.highlight = config.highlight;
    this.runBtn = config.runBtn;
    this.statusEl = config.statusEl;
    this.outputEl = config.outputEl;
    this.resizeHandle = config.resizeHandle;
    this.executeScript = config.executeScript;
    this.onScriptExecuted = config.onScriptExecuted;

    // Initialize syntax highlighting if tokenize function provided
    if (config.tokenize) {
      // SyntaxHighlighter sets up its own event listeners
      new SyntaxHighlighter({
        textarea: this.editor,
        backdrop: this.backdrop,
        highlight: this.highlight,
        tokenize: config.tokenize,
      });
    }

    this.setupEventListeners();
  }

  // =========================================================================
  // Visibility
  // =========================================================================

  /**
   * Check if the panel is visible
   */
  isVisible(): boolean {
    return this.visible;
  }

  /**
   * Check if the script editor textarea has focus
   */
  isEditorFocused(): boolean {
    return document.activeElement === this.editor;
  }

  /**
   * Toggle the panel visibility
   */
  toggle(): void {
    if (this.visible) {
      this.hide();
    } else {
      this.show();
    }
  }

  /**
   * Show the panel
   */
  show(): void {
    this.visible = true;
    this.panel.classList.remove("hidden");
    this.toggleBtn.classList.add("active");
    this.editor.focus();
  }

  /**
   * Hide the panel
   */
  hide(): void {
    this.visible = false;
    this.panel.classList.add("hidden");
    this.toggleBtn.classList.remove("active");
  }

  // =========================================================================
  // Script Execution
  // =========================================================================

  /**
   * Run the current script in the editor
   */
  async run(): Promise<void> {
    const script = this.editor.value.trim();
    if (!script) {
      this.showOutput("No script to run", "error");
      return;
    }

    this.showStatus("Running...", "");
    this.showOutput("", "");
    this.runBtn.setAttribute("disabled", "true");

    try {
      const result = await this.executeScript(script);

      if (result.success) {
        const msg = result.output
          ? `${result.output} (${result.instructions} instructions)`
          : `Done (${result.instructions} instructions)`;
        this.showStatus("Success", "success");
        this.showOutput(msg, "success");
        // Refresh the grid to show any changes
        this.onScriptExecuted();
      } else {
        this.showStatus("Error", "error");
        this.showOutput(result.error || "Unknown error", "error");
      }
    } catch (e) {
      this.showStatus("Error", "error");
      this.showOutput((e as Error).message, "error");
    } finally {
      this.runBtn.removeAttribute("disabled");
    }
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Show status in the header
   */
  private showStatus(text: string, state: "success" | "error" | ""): void {
    this.statusEl.textContent = text;
    this.statusEl.className = state;
  }

  /**
   * Show output in the footer
   */
  private showOutput(text: string, state: "success" | "error" | ""): void {
    this.outputEl.textContent = text;
    this.outputEl.className = state;
  }

  /**
   * Set up event listeners
   */
  private setupEventListeners(): void {
    // Toggle button
    this.toggleBtn.addEventListener("click", () => {
      this.toggle();
    });

    // Run button
    this.runBtn.addEventListener("click", () => {
      this.run();
    });

    // Keyboard shortcuts in editor
    this.editor.addEventListener("keydown", (e) => {
      // Cmd/Ctrl + Enter to run
      if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        this.run();
      }
      // Escape to hide panel
      if (e.key === "Escape") {
        e.preventDefault();
        this.hide();
      }
      // Tab to insert spaces
      if (e.key === "Tab") {
        e.preventDefault();
        const start = this.editor.selectionStart;
        const end = this.editor.selectionEnd;
        const value = this.editor.value;
        this.editor.value = value.substring(0, start) + "  " + value.substring(end);
        this.editor.selectionStart = this.editor.selectionEnd = start + 2;
        // Trigger input event to update syntax highlighting
        this.editor.dispatchEvent(new Event("input", { bubbles: true }));
      }
    });

    // Resize handling
    this.resizeHandle.addEventListener("mousedown", (e) => {
      e.preventDefault();
      this.isResizing = true;
      this.startY = e.clientY;
      this.startHeight = this.panel.offsetHeight;
      document.body.style.cursor = "ns-resize";
      document.body.style.userSelect = "none";
    });

    document.addEventListener("mousemove", (e) => {
      if (!this.isResizing) return;
      const deltaY = this.startY - e.clientY;
      const newHeight = Math.min(
        Math.max(this.startHeight + deltaY, 150),
        window.innerHeight * 0.5
      );
      this.panel.style.height = newHeight + "px";
    });

    document.addEventListener("mouseup", () => {
      if (this.isResizing) {
        this.isResizing = false;
        document.body.style.cursor = "";
        document.body.style.userSelect = "";
      }
    });
  }
}
