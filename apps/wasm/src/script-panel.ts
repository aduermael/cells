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
  private lineNumbers: HTMLElement;
  private runBtn: HTMLElement;
  private statusEl: HTMLElement;
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
    lineNumbers: HTMLElement;
    runBtn: HTMLElement;
    statusEl: HTMLElement;
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
    this.lineNumbers = config.lineNumbers;
    this.runBtn = config.runBtn;
    this.statusEl = config.statusEl;
    this.resizeHandle = config.resizeHandle;
    this.executeScript = config.executeScript;
    this.onScriptExecuted = config.onScriptExecuted;

    // Initialize syntax highlighting and smart indent if tokenize function provided
    if (config.tokenize) {
      this.tokenize = config.tokenize;
      // SyntaxHighlighter sets up its own event listeners
      new SyntaxHighlighter({
        textarea: this.editor,
        backdrop: this.backdrop,
        highlight: this.highlight,
        lineNumbers: this.lineNumbers,
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
      this.showStatus("No script to run", "error");
      return;
    }

    this.showStatus("Running...", "");
    this.runBtn.setAttribute("disabled", "true");

    try {
      const result = await this.executeScript(script);

      if (result.success) {
        // Show output if any, otherwise just "Success"
        this.showStatus(result.output || "Success", "success");
        // Refresh the grid to show any changes
        this.onScriptExecuted();
      } else {
        this.showStatus(result.error || "Unknown error", "error");
      }
    } catch (e) {
      this.showStatus((e as Error).message, "error");
    } finally {
      this.runBtn.removeAttribute("disabled");
    }
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Show status message in the footer
   */
  private showStatus(text: string, state: "success" | "error" | ""): void {
    this.statusEl.textContent = text;
    this.statusEl.className = state;
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
      // Escape to hide panel
      if (e.key === "Escape") {
        e.preventDefault();
        this.hide();
        return;
      }
      // Tab: indent selection or insert tab
      if (e.key === "Tab" && !e.shiftKey) {
        e.preventDefault();
        this.handleTabIndent();
        return;
      }
      // Shift+Tab: dedent selection
      if (e.key === "Tab" && e.shiftKey) {
        e.preventDefault();
        this.handleTabDedent();
        return;
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
      const deltaY = e.clientY - this.startY;
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
  // Tab Indent/Dedent
  // =========================================================================

  // Keywords that increase indent for the next line
  private static readonly INDENT_INCREASE = new Set([
    "then",
    "do",
    "else",
    "repeat",
  ]);

  // Keywords that decrease indent for the current line
  private static readonly INDENT_DECREASE = new Set([
    "end",
    "else",
    "elseif",
    "until",
  ]);

  /**
   * Handle Tab key: insert tab at cursor, or smart-indent selection.
   */
  private handleTabIndent(): void {
    const start = this.editor.selectionStart;
    const end = this.editor.selectionEnd;
    const value = this.editor.value;

    // If no selection, just insert a tab
    if (start === end) {
      this.editor.value = value.substring(0, start) + "\t" + value.substring(end);
      this.editor.selectionStart = this.editor.selectionEnd = start + 1;
      this.editor.dispatchEvent(new Event("input", { bubbles: true }));
      return;
    }

    // With selection: smart re-indent
    this.smartIndentSelection();
  }

  /**
   * Handle Shift+Tab: dedent current line, or smart-indent selection.
   */
  private handleTabDedent(): void {
    const start = this.editor.selectionStart;
    const end = this.editor.selectionEnd;
    const value = this.editor.value;

    // If there's a selection, do smart re-indent (same as Tab)
    if (start !== end) {
      this.smartIndentSelection();
      return;
    }

    // No selection: dedent current line, keep cursor position
    const lineStart = value.lastIndexOf("\n", start - 1) + 1;
    const lineEnd = value.indexOf("\n", start);
    const actualEnd = lineEnd === -1 ? value.length : lineEnd;
    const line = value.substring(lineStart, actualEnd);

    let newLine = line;
    let removed = 0;

    if (line.startsWith("\t")) {
      newLine = line.substring(1);
      removed = 1;
    } else {
      const match = line.match(/^( {1,4})/);
      if (match?.[1]) {
        newLine = line.substring(match[1].length);
        removed = match[1].length;
      }
    }

    if (removed > 0) {
      this.editor.value =
        value.substring(0, lineStart) + newLine + value.substring(actualEnd);
      // Adjust cursor position
      const newCursor = Math.max(lineStart, start - removed);
      this.editor.selectionStart = this.editor.selectionEnd = newCursor;
      this.editor.dispatchEvent(new Event("input", { bubbles: true }));
    }
  }

  /**
   * Smart re-indent selected lines based on code block hierarchy.
   * Uses the tokenizer to analyze code structure.
   */
  private async smartIndentSelection(): Promise<void> {
    const start = this.editor.selectionStart;
    const end = this.editor.selectionEnd;
    const value = this.editor.value;

    // Find line boundaries for selection
    const selLineStart = value.lastIndexOf("\n", start - 1) + 1;
    const selLineEnd = value.indexOf("\n", end - 1);
    const actualEnd = selLineEnd === -1 ? value.length : selLineEnd;

    // Get the text before selection to determine starting indent level
    const textBefore = value.substring(0, selLineStart);

    // Get selected lines
    const selectedText = value.substring(selLineStart, actualEnd);
    const lines = selectedText.split("\n");

    // Calculate base indent from context before selection
    let baseIndent = 0;
    if (this.tokenize && textBefore) {
      try {
        const tokens = await this.tokenize(textBefore);
        baseIndent = this.calculateIndentLevel(tokens);
      } catch {
        // Fallback: no base indent
      }
    }

    // Re-indent each line based on its content
    const reindentedLines: string[] = [];
    let currentIndent = baseIndent;

    for (const line of lines) {
      // Strip existing leading whitespace
      const stripped = line.replace(/^[\t ]*/, "");

      if (!stripped) {
        // Empty line: keep empty
        reindentedLines.push("");
        continue;
      }

      // Check if this line starts with a dedent keyword
      let lineIndent = currentIndent;
      const firstWord = stripped.match(/^(\w+)/)?.[1];
      if (firstWord && ScriptPanel.INDENT_DECREASE.has(firstWord)) {
        lineIndent = Math.max(0, currentIndent - 1);
      }

      // Apply indent
      const tabs = "\t".repeat(lineIndent);
      reindentedLines.push(tabs + stripped);

      // Update indent for next line based on this line's content
      if (this.tokenize) {
        try {
          const lineTokens = await this.tokenize(stripped);
          currentIndent = lineIndent + this.getIndentDelta(lineTokens);
        } catch {
          // Keep current indent on error
        }
      }
    }

    const newText = reindentedLines.join("\n");

    // Replace the text
    this.editor.value =
      value.substring(0, selLineStart) + newText + value.substring(actualEnd);

    // Keep selection on the re-indented lines
    this.editor.selectionStart = selLineStart;
    this.editor.selectionEnd = selLineStart + newText.length;

    this.editor.dispatchEvent(new Event("input", { bubbles: true }));
  }

  /**
   * Calculate current indent level from tokens.
   * Counts net nesting from block openers/closers.
   */
  private calculateIndentLevel(tokens: LuauToken[]): number {
    let level = 0;

    for (const token of tokens) {
      if (token.type !== "keyword") continue;

      if (ScriptPanel.INDENT_INCREASE.has(token.text)) {
        level++;
      } else if (token.text === "function") {
        // function increases indent (will be followed by params and body)
        level++;
      } else if (ScriptPanel.INDENT_DECREASE.has(token.text)) {
        level = Math.max(0, level - 1);
      }
    }

    return level;
  }

  /**
   * Get indent delta for next line based on line's tokens.
   * Returns +1 if line ends with block opener, -1 if it's a closer, 0 otherwise.
   */
  private getIndentDelta(tokens: LuauToken[]): number {
    // Find last significant keyword
    let lastKeyword: string | null = null;
    let hasFunction = false;
    let lastToken: LuauToken | null = null;

    for (const token of tokens) {
      if (token.type === "comment") continue;
      lastToken = token;
      if (token.type === "keyword") {
        lastKeyword = token.text;
        if (token.text === "function") {
          hasFunction = true;
        }
      }
    }

    // Check for function definition ending with )
    if (hasFunction && lastToken?.type === "operator" && lastToken.text === ")") {
      return 1;
    }

    // Check last keyword
    if (lastKeyword) {
      if (ScriptPanel.INDENT_INCREASE.has(lastKeyword)) {
        return 1;
      }
      // end/until on their own line means next line stays same level
      // (the dedent was already applied to this line)
    }

    return 0;
  }
}
