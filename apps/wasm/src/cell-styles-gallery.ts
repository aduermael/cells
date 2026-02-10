// =============================================================================
// Cell Styles Gallery
// =============================================================================
//
// Dropdown gallery of named cell style presets (like Excel's Home → Cell Styles).
// Groups presets by category with visual previews. Theme-aware: accent-based
// styles update when the workbook theme changes.
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { CellStyle, CellStylePreset, Position } from "./types";
import { DropdownFrame } from "./dropdown-frame";
import type { MenuId } from "./menu-state";

// =============================================================================
// Types
// =============================================================================

export interface CellStylesGalleryConfig {
  /** Button that toggles the gallery dropdown */
  galleryBtn: HTMLButtonElement;
  /** Container for the dropdown content (created dynamically) */
  galleryContainer: HTMLElement;
}

export interface CellStylesGalleryCallbacks {
  getSelectedCell: () => Position | null;
  getSelectionRange: () => { start: Position | null; end: Position | null };
  getSelectedAxis: () => { type: "column" | "row"; index: number } | null;
  requestRender: () => void;
  updateFormulaBar: () => void;
}

// =============================================================================
// CellStylesGallery Class
// =============================================================================

export class CellStylesGallery {
  private dataSource: WasmDataSource | null = null;
  private dropdown: DropdownFrame;
  private galleryContainer: HTMLElement;
  private presets: CellStylePreset[] = [];
  private callbacks: CellStylesGalleryCallbacks;

  constructor(config: CellStylesGalleryConfig, callbacks: CellStylesGalleryCallbacks) {
    this.callbacks = callbacks;
    this.galleryContainer = config.galleryContainer;

    this.dropdown = new DropdownFrame({
      anchor: config.galleryBtn,
      content: this.galleryContainer,
      menuId: "cellStyles" as MenuId,
      onOpen: () => this.onOpen(),
    });

    config.galleryBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.dropdown.toggle();
    });
  }

  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
    // Clear cached presets so they are re-fetched with the new workbook's theme
    this.presets = [];
    this.galleryContainer.innerHTML = "";
  }

  /** Reload presets (call after loading a new workbook with a different theme) */
  async loadPresets(): Promise<void> {
    if (!this.dataSource) return;
    this.presets = await this.dataSource.getCellStylePresets();
    this.buildGallery();
  }

  // ===========================================================================
  // Private Methods
  // ===========================================================================

  private async onOpen(): Promise<void> {
    if (this.presets.length === 0) {
      await this.loadPresets();
    }
  }

  private buildGallery(): void {
    this.galleryContainer.innerHTML = "";

    // Group presets by category
    const categories = new Map<string, CellStylePreset[]>();
    for (const preset of this.presets) {
      const list = categories.get(preset.category) || [];
      list.push(preset);
      categories.set(preset.category, list);
    }

    for (const [category, presets] of categories) {
      // Section header
      const header = document.createElement("div");
      header.className = "cell-styles-category";
      header.textContent = category;
      this.galleryContainer.appendChild(header);

      // Preset grid
      const grid = document.createElement("div");
      grid.className = "cell-styles-grid";
      this.galleryContainer.appendChild(grid);

      for (const preset of presets) {
        const chip = this.createPresetChip(preset);
        grid.appendChild(chip);
      }
    }
  }

  private createPresetChip(preset: CellStylePreset): HTMLButtonElement {
    const chip = document.createElement("button");
    chip.className = "cell-style-chip";
    chip.title = preset.name;

    // Apply visual preview styles
    const style = preset.style;

    if (style.bgColor) {
      chip.style.backgroundColor = style.bgColor;
    }
    if (style.textColor) {
      chip.style.color = style.textColor;
    } else if (style.bgColor) {
      // Ensure readable text on colored backgrounds
      chip.style.color = isLightColor(style.bgColor) ? "#000000" : "#FFFFFF";
    }
    if (style.bold) {
      chip.style.fontWeight = "bold";
    }
    if (style.italic) {
      chip.style.fontStyle = "italic";
    }
    if (style.underline) {
      chip.style.textDecoration = "underline";
    }
    if (style.fontSize && style.fontSize > 12) {
      // Scale down large fonts for the chip preview
      chip.style.fontSize = Math.min(style.fontSize, 14) + "px";
    }

    // Border preview
    if (style.border) {
      const borderCss = borderEdgeToCss(style.border.bottom);
      if (borderCss) {
        chip.style.borderBottom = borderCss;
      }
      const topCss = borderEdgeToCss(style.border.top);
      if (topCss) {
        chip.style.borderTop = topCss;
      }
    }

    chip.textContent = preset.name;

    chip.addEventListener("click", (e) => {
      e.stopPropagation();
      this.applyPreset(preset);
      this.dropdown.close();
    });

    return chip;
  }

  private async applyPreset(preset: CellStylePreset): Promise<void> {
    if (!this.dataSource) return;

    const styleUpdate: Partial<CellStyle> = {};

    const style = preset.style;

    // Copy all defined style properties
    if (style.bold) styleUpdate.bold = true;
    if (style.italic) styleUpdate.italic = true;
    if (style.underline) styleUpdate.underline = true;

    // Background color (theme or direct)
    if (style.bgThemeIndex !== undefined && style.bgThemeIndex >= 0) {
      styleUpdate.bgThemeIndex = style.bgThemeIndex;
      styleUpdate.bgThemeTint = style.bgThemeTint ?? 0;
    } else if (style.bgColor) {
      styleUpdate.bgColor = style.bgColor;
    }

    // Text color (theme or direct)
    if (style.textThemeIndex !== undefined && style.textThemeIndex >= 0) {
      styleUpdate.textThemeIndex = style.textThemeIndex;
      styleUpdate.textThemeTint = style.textThemeTint ?? 0;
    } else if (style.textColor) {
      styleUpdate.textColor = style.textColor;
    }

    if (style.fontFamily) styleUpdate.fontFamily = style.fontFamily;
    if (style.fontSize) styleUpdate.fontSize = style.fontSize;

    // Border
    if (style.border) {
      styleUpdate.border = style.border;
    }

    // Number format
    if (preset.formatCode) {
      // Format codes are applied separately via setFormat
      // For now, include in style update if format field is supported
    }

    try {
      await this.applyStyleToSelection(styleUpdate);
      this.callbacks.requestRender();
      this.callbacks.updateFormulaBar();
    } catch (error) {
      console.error("Failed to apply cell style preset:", error);
    }
  }

  private async applyStyleToSelection(styleUpdate: Partial<CellStyle>): Promise<void> {
    if (!this.dataSource) return;

    const selectedAxis = this.callbacks.getSelectedAxis();
    if (selectedAxis) {
      if (selectedAxis.type === "column") {
        await this.dataSource.setColumnStyle(selectedAxis.index, styleUpdate);
      } else {
        await this.dataSource.setRowStyle(selectedAxis.index, styleUpdate);
      }
      return;
    }

    const { start, end } = this.callbacks.getSelectionRange();
    const cell = this.callbacks.getSelectedCell();

    if (!start || !end || (start.col === end.col && start.row === end.row)) {
      if (cell) {
        await this.dataSource.setCellStyleAt(cell.col, cell.row, styleUpdate);
      }
      return;
    }

    const minCol = Math.min(start.col, end.col);
    const maxCol = Math.max(start.col, end.col);
    const minRow = Math.min(start.row, end.row);
    const maxRow = Math.max(start.row, end.row);

    await this.dataSource.setStyleForRange(minCol, minRow, maxCol, maxRow, styleUpdate);
  }
}

// =============================================================================
// Helpers
// =============================================================================

/** Check if a hex color is light (for choosing text contrast) */
function isLightColor(hex: string): boolean {
  if (!hex || hex.length < 7) return true;
  const r = parseInt(hex.substring(1, 3), 16);
  const g = parseInt(hex.substring(3, 5), 16);
  const b = parseInt(hex.substring(5, 7), 16);
  // Relative luminance formula
  const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  return luminance > 0.5;
}

/** Convert a BorderEdge to CSS border shorthand */
function borderEdgeToCss(edge: { style: string; color: string } | undefined): string {
  if (!edge || edge.style === "none") return "";
  const widthMap: Record<string, string> = {
    thin: "1px",
    medium: "2px",
    thick: "3px",
    double: "3px",
    dashed: "1px",
    dotted: "1px",
    hair: "1px",
  };
  const styleMap: Record<string, string> = {
    thin: "solid",
    medium: "solid",
    thick: "solid",
    double: "double",
    dashed: "dashed",
    dotted: "dotted",
    hair: "solid",
  };
  const width = widthMap[edge.style] || "1px";
  const cssStyle = styleMap[edge.style] || "solid";
  const color = edge.color || "#000000";
  return `${width} ${cssStyle} ${color}`;
}
