// =============================================================================
// Theme Gallery
// =============================================================================
//
// Dropdown gallery for switching workbook themes. Shows built-in theme palettes
// with color swatch previews. When a theme is selected, all theme-referenced
// colors update throughout the workbook.
//
// =============================================================================

import type { WasmDataSource } from "./wasm-data-source";
import type { WorkbookTheme } from "./types";
import { DropdownFrame } from "./dropdown-frame";
import type { MenuId } from "./menu-state";

// =============================================================================
// Types
// =============================================================================

export interface ThemeGalleryConfig {
  /** Button that toggles the gallery dropdown */
  galleryBtn: HTMLButtonElement;
  /** Container for the dropdown content */
  galleryContainer: HTMLElement;
}

export interface ThemeGalleryCallbacks {
  requestRender: () => void;
  /** Called after theme switch so other components can invalidate caches */
  onThemeChanged: () => void;
}

// =============================================================================
// ThemeGallery Class
// =============================================================================

export class ThemeGallery {
  private dataSource: WasmDataSource | null = null;
  private dropdown: DropdownFrame;
  private galleryContainer: HTMLElement;
  private builtinThemes: WorkbookTheme[] = [];
  private currentTheme: WorkbookTheme | null = null;
  private callbacks: ThemeGalleryCallbacks;

  constructor(config: ThemeGalleryConfig, callbacks: ThemeGalleryCallbacks) {
    this.callbacks = callbacks;
    this.galleryContainer = config.galleryContainer;

    this.dropdown = new DropdownFrame({
      anchor: config.galleryBtn,
      content: this.galleryContainer,
      menuId: "themes" as MenuId,
      onOpen: () => this.onOpen(),
    });

    config.galleryBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      this.dropdown.toggle();
    });
  }

  setDataSource(dataSource: WasmDataSource): void {
    this.dataSource = dataSource;
    // Clear cached themes so they are re-fetched
    this.builtinThemes = [];
    this.currentTheme = null;
    this.galleryContainer.innerHTML = "";
  }

  /** Invalidate cached data (call after theme switch) */
  invalidate(): void {
    this.builtinThemes = [];
    this.currentTheme = null;
    this.galleryContainer.innerHTML = "";
  }

  // ===========================================================================
  // Private Methods
  // ===========================================================================

  private async onOpen(): Promise<void> {
    // Always reload on open to get current theme state
    await this.loadThemes();
  }

  private async loadThemes(): Promise<void> {
    if (!this.dataSource) return;

    const [builtins, current] = await Promise.all([
      this.dataSource.getBuiltinThemes(),
      this.dataSource.getTheme(),
    ]);

    this.builtinThemes = builtins;
    this.currentTheme = current;
    this.buildGallery();
  }

  private buildGallery(): void {
    this.galleryContainer.innerHTML = "";

    // Check if current theme is a custom (non-builtin) theme
    const isCustomTheme = this.currentTheme &&
      !this.builtinThemes.some(t => t.name === this.currentTheme!.name);

    // Show custom theme at the top if it doesn't match any builtin
    if (isCustomTheme && this.currentTheme) {
      const header = document.createElement("div");
      header.className = "theme-gallery-category";
      header.textContent = "Current Theme";
      this.galleryContainer.appendChild(header);

      const row = this.createThemeRow(this.currentTheme, true);
      this.galleryContainer.appendChild(row);
    }

    // Built-in themes
    const header = document.createElement("div");
    header.className = "theme-gallery-category";
    header.textContent = "Built-in Themes";
    this.galleryContainer.appendChild(header);

    for (const theme of this.builtinThemes) {
      const isActive = this.isActiveTheme(theme);
      const row = this.createThemeRow(theme, isActive);
      this.galleryContainer.appendChild(row);
    }
  }

  private isActiveTheme(theme: WorkbookTheme): boolean {
    if (!this.currentTheme) return false;
    // Compare by name first, then by accent colors for accuracy
    if (this.currentTheme.name === theme.name) return true;
    // Compare accent colors (indices 4-9) as a fallback
    const currentColors = this.currentTheme.colorScheme.colors;
    const themeColors = theme.colorScheme.colors;
    if (currentColors.length >= 10 && themeColors.length >= 10) {
      for (let i = 4; i <= 9; i++) {
        if (currentColors[i]?.toUpperCase() !== themeColors[i]?.toUpperCase()) {
          return false;
        }
      }
      return true;
    }
    return false;
  }

  private createThemeRow(theme: WorkbookTheme, isActive: boolean): HTMLButtonElement {
    const row = document.createElement("button");
    row.className = "theme-gallery-row";
    if (isActive) {
      row.classList.add("theme-gallery-row-active");
    }

    // Checkmark indicator
    const check = document.createElement("span");
    check.className = "theme-gallery-check";
    check.textContent = isActive ? "\u2713" : "";
    row.appendChild(check);

    // Theme name
    const name = document.createElement("span");
    name.className = "theme-gallery-name";
    name.textContent = theme.name;
    row.appendChild(name);

    // Color swatches (6 accent colors: indices 4-9)
    const swatches = document.createElement("span");
    swatches.className = "theme-gallery-swatches";
    const colors = theme.colorScheme.colors;
    for (let i = 4; i <= 9; i++) {
      const swatch = document.createElement("span");
      swatch.className = "theme-gallery-swatch";
      swatch.style.backgroundColor = colors[i] || "#CCCCCC";
      swatches.appendChild(swatch);
    }
    row.appendChild(swatches);

    row.addEventListener("click", (e) => {
      e.stopPropagation();
      this.applyTheme(theme);
      this.dropdown.close();
    });

    return row;
  }

  private async applyTheme(theme: WorkbookTheme): Promise<void> {
    if (!this.dataSource) return;

    try {
      await this.dataSource.setTheme(theme);
      this.currentTheme = theme;

      // Notify other components to invalidate their caches
      this.callbacks.onThemeChanged();
      this.callbacks.requestRender();
    } catch (error) {
      console.error("Failed to apply theme:", error);
    }
  }
}
