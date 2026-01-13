// =============================================================================
// Zoom Controls
// =============================================================================
//
// Manages zoom in/out buttons and displays current zoom level in the bottom bar.
//
// This is a UI-ONLY module. Zoom state is stored in the GridRenderer.
//
// =============================================================================

import type { GridRenderer } from "./grid-renderer";

/** Zoom level increments (matching common spreadsheet applications) */
const ZOOM_LEVELS = [10, 25, 50, 75, 100, 125, 150, 175, 200, 250, 300, 400];

/**
 * ZoomControls manages the zoom buttons and display in the bottom bar.
 */
export class ZoomControls {
  private zoomInBtn: HTMLButtonElement;
  private zoomOutBtn: HTMLButtonElement;
  private zoomLevelSpan: HTMLSpanElement;
  private zoomSlider: HTMLInputElement;
  private renderer: GridRenderer | null = null;
  private onZoomChange: (() => void) | null = null;

  constructor() {
    // Get DOM elements - these are optional since they may not exist in all contexts
    const zoomInBtn = document.getElementById("zoom-in-btn") as HTMLButtonElement | null;
    const zoomOutBtn = document.getElementById("zoom-out-btn") as HTMLButtonElement | null;
    const zoomLevelSpan = document.getElementById("zoom-level") as HTMLSpanElement | null;
    const zoomSlider = document.getElementById("zoom-slider") as HTMLInputElement | null;

    // Store whatever elements we found (may be null)
    this.zoomInBtn = zoomInBtn!;
    this.zoomOutBtn = zoomOutBtn!;
    this.zoomLevelSpan = zoomLevelSpan!;
    this.zoomSlider = zoomSlider!;

    // Set up event listeners if elements exist
    if (zoomInBtn) {
      zoomInBtn.addEventListener("click", () => this.zoomIn());
    }
    if (zoomOutBtn) {
      zoomOutBtn.addEventListener("click", () => this.zoomOut());
    }
    if (zoomSlider) {
      zoomSlider.addEventListener("input", () => this.onSliderInput());
    }
  }

  /**
   * Set the grid renderer reference
   */
  setRenderer(renderer: GridRenderer): void {
    this.renderer = renderer;
    this.updateDisplay();
  }

  /**
   * Set callback for zoom changes (for re-rendering)
   */
  setOnZoomChange(callback: () => void): void {
    this.onZoomChange = callback;
  }

  /**
   * Get current zoom level
   */
  getZoomLevel(): number {
    return this.renderer?.getZoomScale() ?? 100;
  }

  /**
   * Set zoom level directly
   */
  setZoomLevel(level: number): void {
    if (!this.renderer) return;
    level = Math.max(10, Math.min(400, level));
    this.renderer.setZoomScale(level);
    this.updateDisplay();
    this.onZoomChange?.();
  }

  /**
   * Zoom in to next level
   */
  zoomIn(): void {
    const current = this.getZoomLevel();
    // Find next higher zoom level
    const nextLevel = ZOOM_LEVELS.find((level) => level > current) ?? 400;
    this.setZoomLevel(nextLevel);
  }

  /**
   * Zoom out to previous level
   */
  zoomOut(): void {
    const current = this.getZoomLevel();
    // Find next lower zoom level
    const nextLevel = [...ZOOM_LEVELS].reverse().find((level) => level < current) ?? 10;
    this.setZoomLevel(nextLevel);
  }

  /**
   * Reset zoom to 100%
   */
  resetZoom(): void {
    this.setZoomLevel(100);
  }

  /**
   * Handle slider input change
   */
  private onSliderInput(): void {
    if (!this.zoomSlider) return;
    const level = parseInt(this.zoomSlider.value, 10);
    this.setZoomLevel(level);
  }

  /**
   * Update the zoom level display
   */
  updateDisplay(): void {
    const level = this.getZoomLevel();
    if (this.zoomLevelSpan) {
      this.zoomLevelSpan.textContent = `${level}%`;
    }

    // Sync slider position
    if (this.zoomSlider) {
      this.zoomSlider.value = String(level);
    }

    // Update button states
    if (this.zoomInBtn) this.zoomInBtn.disabled = level >= 400;
    if (this.zoomOutBtn) this.zoomOutBtn.disabled = level <= 10;
  }

  /**
   * Sync display with renderer (call after sheet change)
   */
  syncWithRenderer(): void {
    this.updateDisplay();
  }
}
