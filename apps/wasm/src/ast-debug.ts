// AST Debug Panel - Formula AST visualization for debugging
// Provides a debug panel that shows the parsed AST of formulas in real-time.

import type { CellsClient } from "./client";

// =============================================================================
// Types
// =============================================================================

/** AST node type from parser */
interface AstNode {
  type?: string;
  [key: string]: unknown;
}

/** Parse result from WASM client */
interface ParseResult {
  ast?: AstNode;
  errors?: string[];
}

// =============================================================================
// AstDebugPanel Class
// =============================================================================

/**
 * AstDebugPanel manages the AST debug panel for formula visualization.
 *
 * Responsibilities:
 * - Toggling panel visibility
 * - Parsing formulas and displaying AST
 * - Formatting AST nodes with syntax highlighting
 */
export class AstDebugPanel {
  // =========================================================================
  // Dependencies
  // =========================================================================

  private panel: HTMLElement;
  private errorsEl: HTMLElement;
  private treeEl: HTMLElement;
  private ensureWasmClient: () => Promise<CellsClient>;

  // =========================================================================
  // State
  // =========================================================================

  private visible: boolean = false;

  // =========================================================================
  // Constructor
  // =========================================================================

  constructor(config: {
    panel: HTMLElement;
    errorsEl: HTMLElement;
    treeEl: HTMLElement;
    ensureWasmClient: () => Promise<CellsClient>;
  }) {
    this.panel = config.panel;
    this.errorsEl = config.errorsEl;
    this.treeEl = config.treeEl;
    this.ensureWasmClient = config.ensureWasmClient;
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
   * Toggle the panel visibility
   * @param formulaText - Optional formula text to display when showing
   */
  toggle(formulaText?: string): void {
    this.visible = !this.visible;
    if (this.visible) {
      this.panel.classList.remove("hidden");
      // Update with current formula if provided
      if (formulaText !== undefined) {
        this.update(formulaText);
      }
    } else {
      this.panel.classList.add("hidden");
    }
  }

  /**
   * Show the panel
   */
  show(): void {
    this.visible = true;
    this.panel.classList.remove("hidden");
  }

  /**
   * Hide the panel
   */
  hide(): void {
    this.visible = false;
    this.panel.classList.add("hidden");
  }

  // =========================================================================
  // Update
  // =========================================================================

  /**
   * Update the AST debug panel with the parse result for a formula
   * @param formulaText - The formula text to parse
   */
  async update(formulaText: string): Promise<void> {
    if (!this.visible) return;

    // If empty or doesn't start with =, show placeholder
    if (!formulaText || !formulaText.startsWith("=")) {
      this.errorsEl.textContent = "";
      this.treeEl.innerHTML =
        '<span style="color:#808080">Enter a formula starting with = to see the AST</span>';
      return;
    }

    try {
      const client = await this.ensureWasmClient();
      const result = (await client.debugParseFormula(formulaText)) as ParseResult;

      // Show errors if any
      if (result.errors && result.errors.length > 0) {
        this.errorsEl.textContent = result.errors.join("\n");
      } else {
        this.errorsEl.textContent = "";
      }

      // Show AST with syntax highlighting
      if (result.ast) {
        this.treeEl.innerHTML = this.formatAstNode(result.ast, 0);
      } else {
        this.treeEl.innerHTML =
          '<span style="color:#808080">No AST generated</span>';
      }
    } catch (err) {
      this.errorsEl.textContent = `Error: ${(err as Error).message}`;
      this.treeEl.innerHTML = "";
    }
  }

  // =========================================================================
  // Formatting
  // =========================================================================

  /**
   * Format an AST node as HTML with syntax highlighting
   * @param node - The AST node
   * @param indent - Current indentation level
   * @returns HTML string
   */
  private formatAstNode(node: unknown, indent: number): string {
    if (node === null) return '<span class="ast-null">null</span>';
    if (typeof node !== "object") {
      if (typeof node === "string") {
        return `<span class="ast-string">"${this.escapeHtml(node)}"</span>`;
      } else if (typeof node === "number") {
        return `<span class="ast-number">${node}</span>`;
      } else if (typeof node === "boolean") {
        return `<span class="ast-boolean">${node}</span>`;
      }
      return String(node);
    }

    const spaces = "  ".repeat(indent);
    const childSpaces = "  ".repeat(indent + 1);
    const obj = node as Record<string, unknown>;

    let html = "{";
    const keys = Object.keys(obj);
    for (let i = 0; i < keys.length; i++) {
      const key = keys[i]!;
      const value = obj[key];

      html += "\n" + childSpaces;

      // Highlight the 'type' key specially
      if (key === "type" && typeof value === "string") {
        html += `<span class="ast-key">"${key}"</span>: <span class="ast-type">"${this.escapeHtml(value)}"</span>`;
      } else {
        html += `<span class="ast-key">"${key}"</span>: `;
        if (Array.isArray(value)) {
          if (value.length === 0) {
            html += "[]";
          } else {
            html += "[";
            for (let j = 0; j < value.length; j++) {
              html += "\n" + childSpaces + "  ";
              html += this.formatAstNode(value[j], indent + 2);
              if (j < value.length - 1) html += ",";
            }
            html += "\n" + childSpaces + "]";
          }
        } else {
          html += this.formatAstNode(value, indent + 1);
        }
      }

      if (i < keys.length - 1) html += ",";
    }
    html += "\n" + spaces + "}";
    return html;
  }

  /**
   * Escape HTML special characters
   */
  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }
}
