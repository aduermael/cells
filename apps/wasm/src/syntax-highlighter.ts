// Syntax Highlighter - Provides Luau syntax highlighting using the WASM tokenizer
// Uses a backdrop approach: transparent textarea over highlighted pre/code element

import type { LuauToken, LuauTokenType } from "./client-types";

// =============================================================================
// Types
// =============================================================================

/** Tokenize function type (injected dependency) */
export type TokenizeFunction = (source: string) => Promise<LuauToken[]>;

// =============================================================================
// SyntaxHighlighter Class
// =============================================================================

/**
 * SyntaxHighlighter manages syntax highlighting for a textarea.
 *
 * Architecture:
 * - Textarea is transparent (text color = transparent)
 * - Backdrop pre/code element shows highlighted code
 * - Both must have identical font, padding, line-height
 * - Scroll positions are synchronized
 */
export class SyntaxHighlighter {
  private textarea: HTMLTextAreaElement;
  private backdrop: HTMLPreElement;
  private highlight: HTMLElement;
  private tokenize: TokenizeFunction;
  private lineNumbers: HTMLElement | null;

  private lastSource: string = "";
  private lastLineCount: number = 0;
  private debounceTimeout: ReturnType<typeof setTimeout> | null = null;

  constructor(config: {
    textarea: HTMLTextAreaElement;
    backdrop: HTMLPreElement;
    highlight: HTMLElement;
    tokenize: TokenizeFunction;
    lineNumbers?: HTMLElement;
  }) {
    this.textarea = config.textarea;
    this.backdrop = config.backdrop;
    this.highlight = config.highlight;
    this.tokenize = config.tokenize;
    this.lineNumbers = config.lineNumbers ?? null;

    this.setupEventListeners();
  }

  // =========================================================================
  // Public API
  // =========================================================================

  /**
   * Update the highlighted display to match the textarea content
   */
  async update(): Promise<void> {
    const source = this.textarea.value;

    // Update line numbers regardless of content change (for empty state)
    this.updateLineNumbers(source);

    // Skip syntax highlighting if content hasn't changed
    if (source === this.lastSource) {
      return;
    }
    this.lastSource = source;

    // Handle empty content
    if (!source) {
      this.highlight.innerHTML = "";
      return;
    }

    try {
      const tokens = await this.tokenize(source);
      this.renderTokens(source, tokens);
    } catch (e) {
      // Fallback: show plain text on error
      this.highlight.textContent = source;
    }
  }

  /**
   * Force an immediate update (e.g., after programmatic changes)
   */
  forceUpdate(): void {
    this.lastSource = ""; // Reset to force update
    this.update();
  }

  // =========================================================================
  // Private Methods
  // =========================================================================

  /**
   * Render tokens as highlighted HTML
   */
  private renderTokens(source: string, tokens: LuauToken[]): void {
    // Build highlighted HTML
    // We need to handle gaps between tokens (whitespace, etc.)
    let html = "";
    let lastEnd = 0;

    for (const token of tokens) {
      // Add any text between tokens (whitespace)
      if (token.start > lastEnd) {
        html += this.escapeHtml(source.substring(lastEnd, token.start));
      }

      // Add the token with its class
      const className = this.getTokenClass(token.type);
      const escapedText = this.escapeHtml(token.text);
      html += `<span class="${className}">${escapedText}</span>`;

      lastEnd = token.end;
    }

    // Add any remaining text after the last token
    if (lastEnd < source.length) {
      html += this.escapeHtml(source.substring(lastEnd));
    }

    this.highlight.innerHTML = html;
  }

  /**
   * Get CSS class for token type
   */
  private getTokenClass(type: LuauTokenType): string {
    return `luau-${type}`;
  }

  /**
   * Escape HTML special characters
   */
  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }

  /**
   * Schedule a debounced update
   */
  private scheduleUpdate(): void {
    if (this.debounceTimeout) {
      clearTimeout(this.debounceTimeout);
    }
    this.debounceTimeout = setTimeout(() => {
      this.update();
    }, 16); // 16ms debounce (~1 frame) - tokenizer is fast
  }

  /**
   * Update line numbers display
   */
  private updateLineNumbers(source: string): void {
    if (!this.lineNumbers) return;

    const lineCount = source ? source.split("\n").length : 1;

    // Only update DOM if line count changed
    if (lineCount === this.lastLineCount) return;
    this.lastLineCount = lineCount;

    // Generate line numbers
    const lines: string[] = [];
    for (let i = 1; i <= lineCount; i++) {
      lines.push(String(i));
    }
    this.lineNumbers.textContent = lines.join("\n");
  }

  /**
   * Sync scroll position from textarea to backdrop (and line numbers)
   */
  private syncScroll(): void {
    this.backdrop.scrollTop = this.textarea.scrollTop;
    this.backdrop.scrollLeft = this.textarea.scrollLeft;

    // Sync line numbers scroll (vertical only)
    if (this.lineNumbers) {
      this.lineNumbers.scrollTop = this.textarea.scrollTop;
    }
  }

  /**
   * Set up event listeners
   */
  private setupEventListeners(): void {
    // Update highlighting on input
    this.textarea.addEventListener("input", () => {
      this.scheduleUpdate();
    });

    // Sync scroll position
    this.textarea.addEventListener("scroll", () => {
      this.syncScroll();
    });

    // Initial update
    this.update();
  }
}
