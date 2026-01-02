// Script Panel - Luau scripting interface
// Provides a dedicated panel for writing and executing Luau scripts.

import { SyntaxHighlighter, type TokenizeFunction } from "./syntax-highlighter";
import type { LuauToken, AutocompleteResult, AutocompleteSuggestion } from "./client-types";

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

  // Console panel elements
  private consoleEl: HTMLElement;
  private consoleContentEl: HTMLElement;
  private consoleCloseBtn: HTMLElement;
  private consoleClearBtn: HTMLElement;
  private consoleResizeHandle: HTMLElement;

  private executeScript: (script: string) => Promise<ScriptResult>;
  private onScriptExecuted: () => void;
  private tokenize?: TokenizeFunction;
  private getAutocomplete?: (source: string, line: number, column: number) => Promise<AutocompleteResult>;

  // Autocomplete UI elements
  private autocompletePopup: HTMLElement | null = null;
  private autocompleteItems: AutocompleteSuggestion[] = [];
  private autocompleteSelectedIndex: number = 0;
  private autocompleteVisible: boolean = false;
  private autocompleteDebounceTimer: ReturnType<typeof setTimeout> | null = null;
  private autocompleteUserNavigated: boolean = false; // True if user navigated with arrows

  // =========================================================================
  // State
  // =========================================================================

  private visible: boolean = false;
  private isResizing: boolean = false;
  private startY: number = 0;
  private startHeight: number = 0;

  // Console panel state
  private consoleVisible: boolean = false;
  private isConsoleResizing: boolean = false;
  private consoleStartX: number = 0;
  private consoleStartWidth: number = 0;

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
    consoleEl: HTMLElement;
    consoleContentEl: HTMLElement;
    consoleCloseBtn: HTMLElement;
    consoleClearBtn: HTMLElement;
    consoleResizeHandle: HTMLElement;
    executeScript: (script: string) => Promise<ScriptResult>;
    onScriptExecuted: () => void;
    tokenize?: TokenizeFunction;
    getAutocomplete?: (source: string, line: number, column: number) => Promise<AutocompleteResult>;
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
    this.consoleEl = config.consoleEl;
    this.consoleContentEl = config.consoleContentEl;
    this.consoleCloseBtn = config.consoleCloseBtn;
    this.consoleClearBtn = config.consoleClearBtn;
    this.consoleResizeHandle = config.consoleResizeHandle;
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

    // Initialize autocomplete if provided
    if (config.getAutocomplete) {
      this.getAutocomplete = config.getAutocomplete;
      this.createAutocompletePopup();
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
  // Console Panel
  // =========================================================================

  /**
   * Show the console panel
   */
  showConsole(): void {
    this.consoleVisible = true;
    this.consoleEl.classList.remove("hidden");
    this.consoleResizeHandle.classList.remove("hidden");
  }

  /**
   * Hide the console panel
   */
  hideConsole(): void {
    this.consoleVisible = false;
    this.consoleEl.classList.add("hidden");
    this.consoleResizeHandle.classList.add("hidden");
  }

  /**
   * Clear console content
   */
  clearConsole(): void {
    this.consoleContentEl.innerHTML = "";
  }

  /**
   * Append output to the console
   */
  appendToConsole(text: string): void {
    // Split by newlines and create a line element for each
    const lines = text.split("\n");
    for (const line of lines) {
      if (line.trim() === "") continue; // Skip empty lines
      const lineEl = document.createElement("div");
      lineEl.className = "console-line";
      lineEl.textContent = line;
      this.consoleContentEl.appendChild(lineEl);
    }
    // Scroll to bottom
    this.consoleContentEl.scrollTop = this.consoleContentEl.scrollHeight;
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

    const startTime = performance.now();

    try {
      const result = await this.executeScript(script);
      const elapsed = performance.now() - startTime;

      if (result.success) {
        // Format time: use ms if < 1000ms, otherwise seconds
        const timeStr = elapsed < 1000
          ? `${Math.round(elapsed)}ms`
          : `${(elapsed / 1000).toFixed(2)}s`;

        // If there's print output, show it in the console panel
        if (result.output) {
          this.showConsole();
          this.appendToConsole(result.output);
        }

        // Status shows just time
        this.showStatus(`Success (${timeStr})`, "success");
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
      // Handle autocomplete navigation first
      if (this.handleAutocompleteKey(e)) {
        return;
      }

      // Cmd/Ctrl + Enter to run
      if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        this.run();
        return;
      }
      // Escape: hide autocomplete first, then console, then panel
      if (e.key === "Escape") {
        if (this.autocompleteVisible) {
          e.preventDefault();
          this.hideAutocomplete();
          return;
        }
        if (this.consoleVisible) {
          e.preventDefault();
          this.hideConsole();
          return;
        }
        e.preventDefault();
        this.hide();
        return;
      }
      // Tab: when autocomplete is visible, Tab is handled above
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

    // Trigger autocomplete as user types (debounced)
    this.editor.addEventListener("input", () => {
      this.triggerAutocompleteDebounced();
    });

    // Hide autocomplete when editor loses focus
    this.editor.addEventListener("blur", () => {
      // Small delay to allow click on autocomplete item
      setTimeout(() => {
        if (!this.editor.contains(document.activeElement)) {
          this.hideAutocomplete();
        }
      }, 100);
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
      if (this.isConsoleResizing) {
        this.isConsoleResizing = false;
        this.consoleResizeHandle.classList.remove("dragging");
        document.body.style.cursor = "";
        document.body.style.userSelect = "";
      }
    });

    // Console horizontal resize handling
    this.consoleResizeHandle.addEventListener("mousedown", (e) => {
      e.preventDefault();
      this.isConsoleResizing = true;
      this.consoleStartX = e.clientX;
      this.consoleStartWidth = this.consoleEl.offsetWidth;
      this.consoleResizeHandle.classList.add("dragging");
      document.body.style.cursor = "ew-resize";
      document.body.style.userSelect = "none";
    });

    document.addEventListener("mousemove", (e) => {
      if (!this.isConsoleResizing) return;
      // Moving left increases console width, moving right decreases it
      const deltaX = this.consoleStartX - e.clientX;
      const newWidth = Math.min(
        Math.max(this.consoleStartWidth + deltaX, 150), // min 150px
        this.panel.offsetWidth * 0.5 // max 50% of panel
      );
      this.consoleEl.style.width = newWidth + "px";
    });

    // Console close button
    this.consoleCloseBtn.addEventListener("click", () => {
      this.hideConsole();
    });

    // Console clear button
    this.consoleClearBtn.addEventListener("click", () => {
      this.clearConsole();
    });

    // Escape in console content area closes console
    this.consoleEl.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        e.preventDefault();
        this.hideConsole();
        this.editor.focus();
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

  // =========================================================================
  // Autocomplete
  // =========================================================================

  /**
   * Create the autocomplete popup element
   */
  private createAutocompletePopup(): void {
    this.autocompletePopup = document.createElement("div");
    this.autocompletePopup.className = "autocomplete-popup";
    this.autocompletePopup.style.display = "none";
    // Append to panel so it's positioned relative to the editor
    this.panel.appendChild(this.autocompletePopup);
  }

  /**
   * Get the current cursor position as (line, column) - 0-indexed
   */
  private getCursorPosition(): { line: number; column: number } {
    const value = this.editor.value;
    const cursorPos = this.editor.selectionStart;

    // Count lines before cursor
    const textBeforeCursor = value.substring(0, cursorPos);
    const lines = textBeforeCursor.split("\n");
    const line = lines.length - 1;
    const column = lines[lines.length - 1].length;

    return { line, column };
  }

  /**
   * Get pixel position for cursor (for popup placement)
   */
  private getCursorPixelPosition(): { x: number; y: number } {
    const value = this.editor.value;
    const cursorPos = this.editor.selectionStart;

    // Count lines and get column
    const textBeforeCursor = value.substring(0, cursorPos);
    const lines = textBeforeCursor.split("\n");
    const lineIndex = lines.length - 1;
    const column = lines[lines.length - 1].length;

    // Approximate character dimensions (monospace font)
    const lineHeight = 19.5; // 13px * 1.5 line-height
    const charWidth = 7.8; // approximate for 13px monospace

    // Account for padding (12px) and line numbers gutter (40px)
    const editorRect = this.editor.getBoundingClientRect();
    const x = editorRect.left + 12 + column * charWidth;
    const y = editorRect.top + 12 + (lineIndex + 1) * lineHeight - this.editor.scrollTop;

    return { x, y };
  }

  /**
   * Trigger autocomplete at current cursor position
   */
  private async triggerAutocomplete(): Promise<void> {
    if (!this.getAutocomplete) return;

    const source = this.editor.value;
    const { line, column } = this.getCursorPosition();

    try {
      const result = await this.getAutocomplete(source, line, column);
      console.log("[autocomplete]", { line, column, result });
      if (result.suggestions.length > 0) {
        this.showAutocomplete(result.suggestions);
      } else {
        this.hideAutocomplete();
      }
    } catch (e) {
      console.error("[autocomplete error]", e);
      this.hideAutocomplete();
    }
  }

  /**
   * Trigger autocomplete with debouncing (100ms delay)
   */
  private triggerAutocompleteDebounced(): void {
    if (!this.getAutocomplete) return;

    // Cancel any pending request
    if (this.autocompleteDebounceTimer) {
      clearTimeout(this.autocompleteDebounceTimer);
    }

    this.autocompleteDebounceTimer = setTimeout(() => {
      this.autocompleteDebounceTimer = null;
      this.triggerAutocomplete();
    }, 100);
  }

  /**
   * Show autocomplete popup with suggestions
   */
  private showAutocomplete(suggestions: AutocompleteSuggestion[]): void {
    if (!this.autocompletePopup) return;

    // Sort suggestions: type-correct first, then correctFunctionResult, then rest
    const sortedSuggestions = this.sortSuggestionsByTypeCorrectness(suggestions);

    this.autocompleteItems = sortedSuggestions;
    this.autocompleteSelectedIndex = 0;
    this.autocompleteVisible = true;
    this.autocompleteUserNavigated = false; // Reset navigation state for new suggestions

    // Build popup content
    this.autocompletePopup.innerHTML = "";
    sortedSuggestions.forEach((suggestion, index) => {
      const item = document.createElement("div");
      item.className = "autocomplete-item" + (index === 0 ? " selected" : "");
      // Add type-correct class for visual highlighting
      if (suggestion.typeCorrect === "correct") {
        item.classList.add("type-correct");
      } else if (suggestion.typeCorrect === "correctFunctionResult") {
        item.classList.add("type-correct-fn");
      }
      item.dataset.index = String(index);

      // Kind icon
      const icon = document.createElement("span");
      icon.className = "autocomplete-icon";
      icon.textContent = this.getKindIcon(suggestion.kind);
      item.appendChild(icon);

      // Label
      const label = document.createElement("span");
      label.className = "autocomplete-label";
      label.textContent = suggestion.label;
      if (suggestion.deprecated) {
        label.classList.add("deprecated");
      }
      item.appendChild(label);

      // Detail (type info)
      if (suggestion.detail) {
        const detail = document.createElement("span");
        detail.className = "autocomplete-detail";
        detail.textContent = suggestion.detail;
        item.appendChild(detail);
      }

      // Type correctness indicator
      if (suggestion.typeCorrect === "correct" || suggestion.typeCorrect === "correctFunctionResult") {
        const indicator = document.createElement("span");
        indicator.className = "autocomplete-type-indicator";
        indicator.textContent = "✓";
        indicator.title = suggestion.typeCorrect === "correct"
          ? "Matches expected type"
          : "Returns expected type";
        item.appendChild(indicator);
      }

      // Click handler
      item.addEventListener("mousedown", (e) => {
        e.preventDefault(); // Prevent blur
        this.acceptAutocomplete(index);
      });

      // Hover handler
      item.addEventListener("mouseenter", () => {
        this.selectAutocompleteItem(index);
      });

      this.autocompletePopup!.appendChild(item);
    });

    // Position popup near cursor
    const { x, y } = this.getCursorPixelPosition();
    const panelRect = this.panel.getBoundingClientRect();

    // Position relative to panel
    this.autocompletePopup.style.left = `${x - panelRect.left}px`;
    this.autocompletePopup.style.top = `${y - panelRect.top}px`;
    this.autocompletePopup.style.display = "block";

    // Ensure popup doesn't overflow viewport
    requestAnimationFrame(() => {
      if (!this.autocompletePopup) return;
      const popupRect = this.autocompletePopup.getBoundingClientRect();

      // Check right overflow
      if (popupRect.right > window.innerWidth - 8) {
        this.autocompletePopup.style.left = `${window.innerWidth - popupRect.width - panelRect.left - 8}px`;
      }

      // Check bottom overflow - show above cursor if needed
      if (popupRect.bottom > window.innerHeight - 8) {
        const lineHeight = 19.5;
        this.autocompletePopup.style.top = `${y - panelRect.top - popupRect.height - lineHeight}px`;
      }
    });
  }

  /**
   * Hide autocomplete popup
   */
  private hideAutocomplete(): void {
    if (!this.autocompletePopup) return;
    this.autocompletePopup.style.display = "none";
    this.autocompleteVisible = false;
    this.autocompleteItems = [];
  }

  /**
   * Select an autocomplete item by index
   */
  private selectAutocompleteItem(index: number): void {
    if (!this.autocompletePopup) return;

    // Update visual selection
    const items = this.autocompletePopup.querySelectorAll(".autocomplete-item");
    items.forEach((item, i) => {
      item.classList.toggle("selected", i === index);
    });

    this.autocompleteSelectedIndex = index;

    // Scroll into view if needed
    const selectedItem = items[index] as HTMLElement;
    if (selectedItem) {
      selectedItem.scrollIntoView({ block: "nearest" });
    }
  }

  /**
   * Accept the selected or specified autocomplete suggestion
   */
  private acceptAutocomplete(index?: number): void {
    const idx = index ?? this.autocompleteSelectedIndex;
    const suggestion = this.autocompleteItems[idx];
    if (!suggestion) {
      this.hideAutocomplete();
      return;
    }

    // Find the word being typed (to replace it)
    const value = this.editor.value;
    const cursorPos = this.editor.selectionStart;

    // Find word start (go back until non-identifier char)
    let wordStart = cursorPos;
    while (wordStart > 0 && /[a-zA-Z0-9_]/.test(value[wordStart - 1])) {
      wordStart--;
    }

    // Replace the partial word with the completion
    const before = value.substring(0, wordStart);
    const after = value.substring(cursorPos);
    const insertText = suggestion.insertText;

    this.editor.value = before + insertText + after;

    // Set cursor after inserted text
    const newCursorPos = wordStart + insertText.length;
    this.editor.selectionStart = this.editor.selectionEnd = newCursorPos;

    // Trigger input event to update syntax highlighting
    this.editor.dispatchEvent(new Event("input", { bubbles: true }));

    this.hideAutocomplete();
    this.editor.focus();
  }

  /**
   * Handle keyboard navigation in autocomplete popup
   * Returns true if the key was handled
   */
  private handleAutocompleteKey(e: KeyboardEvent): boolean {
    if (!this.autocompleteVisible) return false;

    switch (e.key) {
      case "ArrowDown":
        e.preventDefault();
        this.autocompleteUserNavigated = true;
        this.selectAutocompleteItem(
          Math.min(this.autocompleteSelectedIndex + 1, this.autocompleteItems.length - 1)
        );
        return true;

      case "ArrowUp":
        e.preventDefault();
        this.autocompleteUserNavigated = true;
        this.selectAutocompleteItem(
          Math.max(this.autocompleteSelectedIndex - 1, 0)
        );
        return true;

      case "Tab":
        // Tab always accepts (common IDE behavior)
        e.preventDefault();
        this.acceptAutocomplete();
        return true;

      case "Enter":
        // Enter only accepts if user has navigated with arrows
        // Otherwise, let Enter insert a newline (fall through)
        if (this.autocompleteUserNavigated) {
          e.preventDefault();
          this.acceptAutocomplete();
          return true;
        }
        // Hide autocomplete but don't prevent Enter
        this.hideAutocomplete();
        return false;

      case "Escape":
        e.preventDefault();
        this.hideAutocomplete();
        return true;

      default:
        return false;
    }
  }

  /**
   * Get icon for suggestion kind
   */
  private getKindIcon(kind: AutocompleteSuggestion["kind"]): string {
    switch (kind) {
      case "function": return "ƒ";
      case "property": return "●";
      case "variable": return "x";
      case "keyword": return "◆";
      case "module": return "◫";
      case "class": return "◇";
      case "text": return "T";
      case "path": return "/";
      default: return "·";
    }
  }

  /**
   * Sort suggestions by type correctness (Phase 4b)
   * Order: correct > correctFunctionResult > none
   */
  private sortSuggestionsByTypeCorrectness(
    suggestions: AutocompleteSuggestion[]
  ): AutocompleteSuggestion[] {
    return [...suggestions].sort((a, b) => {
      const scoreA = this.getTypeCorrectScore(a.typeCorrect);
      const scoreB = this.getTypeCorrectScore(b.typeCorrect);
      // Higher score first
      return scoreB - scoreA;
    });
  }

  /**
   * Get numeric score for type correctness (for sorting)
   */
  private getTypeCorrectScore(typeCorrect: AutocompleteSuggestion["typeCorrect"]): number {
    switch (typeCorrect) {
      case "correct": return 2;
      case "correctFunctionResult": return 1;
      default: return 0;
    }
  }
}
