// Script Panel - Luau scripting interface
// Provides a dedicated panel for writing and executing Luau scripts.

import { SyntaxHighlighter, type TokenizeFunction } from "./syntax-highlighter";
import type { LuauToken } from "./client-types";

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
  private tokenize?: TokenizeFunction;

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

    // Initialize syntax highlighting and auto-indent if tokenize function provided
    if (config.tokenize) {
      this.tokenize = config.tokenize;
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
        return;
      }
      // Enter without modifier: auto-indent
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        this.handleAutoIndent();
        return;
      }
      // Escape to hide panel
      if (e.key === "Escape") {
        e.preventDefault();
        this.hide();
        return;
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

  // =========================================================================
  // Auto-Indent
  // =========================================================================

  // Keywords that increase indent on next line
  private static readonly INDENT_INCREASE_KEYWORDS = new Set([
    "then",
    "do",
    "else",
    "repeat",
  ]);

  // "function" increases indent when followed by params (not immediately by end)
  private static readonly FUNCTION_KEYWORD = "function";

  // Keywords that decrease indent (when typed, the line itself should dedent)
  // Note: Currently unused - future enhancement for auto-dedent on typing "end"
  // private static readonly INDENT_DECREASE_KEYWORDS = new Set([
  //   "end",
  //   "else",
  //   "elseif",
  //   "until",
  // ]);

  /**
   * Calculate the indent level for a new line based on the current line's content.
   *
   * @param lineText - The text of the current line
   * @param tokens - Tokens for the current line only
   * @returns The indent string to use for the new line
   */
  getIndentForNewLine(lineText: string, tokens: LuauToken[]): string {
    // Get current line's leading whitespace
    const leadingMatch = lineText.match(/^(\s*)/);
    const currentIndent = leadingMatch?.[1] ?? "";

    // Find the last significant token (keyword/operator, ignoring comments)
    let lastSignificant: LuauToken | null = null;
    let hasFunctionKeyword = false;

    for (const token of tokens) {
      if (token.type === "comment") continue;

      if (token.type === "keyword") {
        if (token.text === ScriptPanel.FUNCTION_KEYWORD) {
          hasFunctionKeyword = true;
        }
        lastSignificant = token;
      } else if (token.type === "operator" || token.type === "name") {
        lastSignificant = token;
      }
    }

    // Check if we should increase indent
    if (lastSignificant && lastSignificant.type === "keyword") {
      const kw = lastSignificant.text;

      // "function ... )" pattern - increase indent after closing paren
      // But check if the last token indicates we should increase
      if (hasFunctionKeyword) {
        // Find the very last non-comment token
        let lastToken: LuauToken | null = null;
        for (const token of tokens) {
          if (token.type !== "comment") {
            lastToken = token;
          }
        }
        // After function(...) line, increase indent
        if (lastToken && lastToken.type === "operator" && lastToken.text === ")") {
          return currentIndent + "  ";
        }
      }

      if (ScriptPanel.INDENT_INCREASE_KEYWORDS.has(kw)) {
        return currentIndent + "  ";
      }
    }

    // Check for function definition ending with )
    if (hasFunctionKeyword) {
      let lastToken: LuauToken | null = null;
      for (const token of tokens) {
        if (token.type !== "comment") {
          lastToken = token;
        }
      }
      if (lastToken && lastToken.type === "operator" && lastToken.text === ")") {
        return currentIndent + "  ";
      }
    }

    // Default: maintain current indent
    return currentIndent;
  }

  /**
   * Handle Enter key press to insert newline with auto-indent.
   * Returns true if handled, false otherwise.
   */
  private async handleAutoIndent(): Promise<boolean> {
    if (!this.tokenize) {
      return false;
    }

    const start = this.editor.selectionStart;
    const value = this.editor.value;

    // Find the current line
    const lineStart = value.lastIndexOf("\n", start - 1) + 1;
    const textBeforeCursor = value.substring(lineStart, start);

    try {
      // Tokenize just the text up to the cursor on the current line
      const tokens = await this.tokenize(textBeforeCursor);

      // Calculate the indent for the new line
      const newIndent = this.getIndentForNewLine(textBeforeCursor, tokens);

      // Insert newline with indent
      const before = value.substring(0, start);
      const after = value.substring(start);
      this.editor.value = before + "\n" + newIndent + after;

      // Position cursor after the indent
      const newPos = start + 1 + newIndent.length;
      this.editor.selectionStart = this.editor.selectionEnd = newPos;

      // Trigger input event to update syntax highlighting
      this.editor.dispatchEvent(new Event("input", { bubbles: true }));

      return true;
    } catch {
      return false;
    }
  }
}
