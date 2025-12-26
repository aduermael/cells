// Scrollbar Module
// Custom scrollbar implementation with virtual scrolling support

import { DEFAULT_COL_WIDTH, DEFAULT_ROW_HEIGHT, HEADER_HEIGHT, HEADER_WIDTH } from "./grid-constants.js";

/** Scrollbar configuration */
export interface ScrollbarConfig {
  /** Minimum thumb size in pixels */
  minThumbSize: number;
  /** Track width/height in pixels */
  trackSize: number;
  /** Corner size (where scrollbars meet) */
  cornerSize: number;
}

const DEFAULT_CONFIG: ScrollbarConfig = {
  minThumbSize: 30,
  trackSize: 12,
  cornerSize: 12,
};

/** State callbacks for scrollbar */
export interface ScrollbarCallbacks {
  getScrollX: () => number;
  getScrollY: () => number;
  setScrollX: (x: number) => void;
  setScrollY: (y: number) => void;
  getViewportWidth: () => number;
  getViewportHeight: () => number;
  getContentWidth: () => number;
  getContentHeight: () => number;
  onScroll: () => void;
}

/**
 * Creates and manages custom scrollbars for the grid
 */
export class ScrollbarManager {
  private config: ScrollbarConfig;
  private callbacks: ScrollbarCallbacks;

  // DOM elements
  private verticalTrack: HTMLElement;
  private verticalThumb: HTMLElement;
  private horizontalTrack: HTMLElement;
  private horizontalThumb: HTMLElement;
  private corner: HTMLElement;

  // Drag state
  private isDraggingVertical = false;
  private isDraggingHorizontal = false;
  private dragStartY = 0;
  private dragStartX = 0;
  private dragStartScrollY = 0;
  private dragStartScrollX = 0;

  // Bound event handlers for removal
  private boundMouseMove: (e: MouseEvent) => void;
  private boundMouseUp: (e: MouseEvent) => void;

  constructor(
    container: HTMLElement,
    callbacks: ScrollbarCallbacks,
    config: Partial<ScrollbarConfig> = {}
  ) {
    this.callbacks = callbacks;
    this.config = { ...DEFAULT_CONFIG, ...config };

    // Create scrollbar elements
    this.verticalTrack = this.createTrack("vertical");
    this.verticalThumb = this.createThumb("vertical");
    this.horizontalTrack = this.createTrack("horizontal");
    this.horizontalThumb = this.createThumb("horizontal");
    this.corner = this.createCorner();

    // Assemble DOM
    this.verticalTrack.appendChild(this.verticalThumb);
    this.horizontalTrack.appendChild(this.horizontalThumb);
    container.appendChild(this.verticalTrack);
    container.appendChild(this.horizontalTrack);
    container.appendChild(this.corner);

    // Bind handlers
    this.boundMouseMove = this.handleMouseMove.bind(this);
    this.boundMouseUp = this.handleMouseUp.bind(this);

    // Setup event listeners
    this.setupEventListeners();

    // Initial update
    this.update();
  }

  private createTrack(direction: "vertical" | "horizontal"): HTMLElement {
    const track = document.createElement("div");
    track.className = `scrollbar-track scrollbar-track-${direction}`;
    return track;
  }

  private createThumb(direction: "vertical" | "horizontal"): HTMLElement {
    const thumb = document.createElement("div");
    thumb.className = `scrollbar-thumb scrollbar-thumb-${direction}`;
    return thumb;
  }

  private createCorner(): HTMLElement {
    const corner = document.createElement("div");
    corner.className = "scrollbar-corner";
    return corner;
  }

  private setupEventListeners(): void {
    // Vertical scrollbar thumb drag
    this.verticalThumb.addEventListener("mousedown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.isDraggingVertical = true;
      this.dragStartY = e.clientY;
      this.dragStartScrollY = this.callbacks.getScrollY();
      this.verticalTrack.classList.add("active");
      document.addEventListener("mousemove", this.boundMouseMove);
      document.addEventListener("mouseup", this.boundMouseUp);
    });

    // Horizontal scrollbar thumb drag
    this.horizontalThumb.addEventListener("mousedown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      this.isDraggingHorizontal = true;
      this.dragStartX = e.clientX;
      this.dragStartScrollX = this.callbacks.getScrollX();
      this.horizontalTrack.classList.add("active");
      document.addEventListener("mousemove", this.boundMouseMove);
      document.addEventListener("mouseup", this.boundMouseUp);
    });

    // Track click (scroll to position)
    this.verticalTrack.addEventListener("mousedown", (e) => {
      if (e.target === this.verticalThumb) return;
      e.preventDefault();
      this.scrollToVerticalPosition(e);
    });

    this.horizontalTrack.addEventListener("mousedown", (e) => {
      if (e.target === this.horizontalThumb) return;
      e.preventDefault();
      this.scrollToHorizontalPosition(e);
    });
  }

  private handleMouseMove(e: MouseEvent): void {
    if (this.isDraggingVertical) {
      const deltaY = e.clientY - this.dragStartY;
      const trackHeight = this.verticalTrack.clientHeight;
      const thumbHeight = this.verticalThumb.clientHeight;
      const scrollableTrack = trackHeight - thumbHeight;

      if (scrollableTrack > 0) {
        const contentHeight = this.callbacks.getContentHeight();
        const viewportHeight = this.callbacks.getViewportHeight();
        const maxScrollY = Math.max(0, contentHeight - viewportHeight);
        const scrollRatio = deltaY / scrollableTrack;
        const newScrollY = Math.max(0, Math.min(maxScrollY, this.dragStartScrollY + scrollRatio * maxScrollY));
        this.callbacks.setScrollY(newScrollY);
        this.callbacks.onScroll();
      }
    }

    if (this.isDraggingHorizontal) {
      const deltaX = e.clientX - this.dragStartX;
      const trackWidth = this.horizontalTrack.clientWidth;
      const thumbWidth = this.horizontalThumb.clientWidth;
      const scrollableTrack = trackWidth - thumbWidth;

      if (scrollableTrack > 0) {
        const contentWidth = this.callbacks.getContentWidth();
        const viewportWidth = this.callbacks.getViewportWidth();
        const maxScrollX = Math.max(0, contentWidth - viewportWidth);
        const scrollRatio = deltaX / scrollableTrack;
        const newScrollX = Math.max(0, Math.min(maxScrollX, this.dragStartScrollX + scrollRatio * maxScrollX));
        this.callbacks.setScrollX(newScrollX);
        this.callbacks.onScroll();
      }
    }
  }

  private handleMouseUp(): void {
    this.isDraggingVertical = false;
    this.isDraggingHorizontal = false;
    this.verticalTrack.classList.remove("active");
    this.horizontalTrack.classList.remove("active");
    document.removeEventListener("mousemove", this.boundMouseMove);
    document.removeEventListener("mouseup", this.boundMouseUp);
  }

  private scrollToVerticalPosition(e: MouseEvent): void {
    const rect = this.verticalTrack.getBoundingClientRect();
    const clickY = e.clientY - rect.top;
    const trackHeight = this.verticalTrack.clientHeight;
    const thumbHeight = this.verticalThumb.clientHeight;

    // Calculate target scroll position (center thumb at click)
    const targetThumbTop = clickY - thumbHeight / 2;
    const scrollableTrack = trackHeight - thumbHeight;

    if (scrollableTrack > 0) {
      const contentHeight = this.callbacks.getContentHeight();
      const viewportHeight = this.callbacks.getViewportHeight();
      const maxScrollY = Math.max(0, contentHeight - viewportHeight);
      const scrollRatio = Math.max(0, Math.min(1, targetThumbTop / scrollableTrack));
      this.callbacks.setScrollY(scrollRatio * maxScrollY);
      this.callbacks.onScroll();
    }
  }

  private scrollToHorizontalPosition(e: MouseEvent): void {
    const rect = this.horizontalTrack.getBoundingClientRect();
    const clickX = e.clientX - rect.left;
    const trackWidth = this.horizontalTrack.clientWidth;
    const thumbWidth = this.horizontalThumb.clientWidth;

    // Calculate target scroll position (center thumb at click)
    const targetThumbLeft = clickX - thumbWidth / 2;
    const scrollableTrack = trackWidth - thumbWidth;

    if (scrollableTrack > 0) {
      const contentWidth = this.callbacks.getContentWidth();
      const viewportWidth = this.callbacks.getViewportWidth();
      const maxScrollX = Math.max(0, contentWidth - viewportWidth);
      const scrollRatio = Math.max(0, Math.min(1, targetThumbLeft / scrollableTrack));
      this.callbacks.setScrollX(scrollRatio * maxScrollX);
      this.callbacks.onScroll();
    }
  }

  /**
   * Update scrollbar positions and thumb sizes
   * Call this after any scroll or resize
   */
  update(): void {
    this.updateVertical();
    this.updateHorizontal();
  }

  private updateVertical(): void {
    const contentHeight = this.callbacks.getContentHeight();
    const viewportHeight = this.callbacks.getViewportHeight();
    const scrollY = this.callbacks.getScrollY();

    // Hide if content fits
    if (contentHeight <= viewportHeight) {
      this.verticalTrack.classList.add("hidden");
      return;
    }
    this.verticalTrack.classList.remove("hidden");

    // Calculate thumb size
    const trackHeight = this.verticalTrack.clientHeight;
    const thumbRatio = viewportHeight / contentHeight;
    const thumbHeight = Math.max(this.config.minThumbSize, trackHeight * thumbRatio);

    // Calculate thumb position
    const maxScrollY = contentHeight - viewportHeight;
    const scrollRatio = maxScrollY > 0 ? scrollY / maxScrollY : 0;
    const maxThumbTop = trackHeight - thumbHeight;
    const thumbTop = scrollRatio * maxThumbTop;

    this.verticalThumb.style.height = `${thumbHeight}px`;
    this.verticalThumb.style.top = `${thumbTop}px`;
  }

  private updateHorizontal(): void {
    const contentWidth = this.callbacks.getContentWidth();
    const viewportWidth = this.callbacks.getViewportWidth();
    const scrollX = this.callbacks.getScrollX();

    // Hide if content fits
    if (contentWidth <= viewportWidth) {
      this.horizontalTrack.classList.add("hidden");
      return;
    }
    this.horizontalTrack.classList.remove("hidden");

    // Calculate thumb size
    const trackWidth = this.horizontalTrack.clientWidth;
    const thumbRatio = viewportWidth / contentWidth;
    const thumbWidth = Math.max(this.config.minThumbSize, trackWidth * thumbRatio);

    // Calculate thumb position
    const maxScrollX = contentWidth - viewportWidth;
    const scrollRatio = maxScrollX > 0 ? scrollX / maxScrollX : 0;
    const maxThumbLeft = trackWidth - thumbWidth;
    const thumbLeft = scrollRatio * maxThumbLeft;

    this.horizontalThumb.style.width = `${thumbWidth}px`;
    this.horizontalThumb.style.left = `${thumbLeft}px`;
  }

  /**
   * Set visibility of scrollbars
   */
  setVisible(visible: boolean): void {
    const display = visible ? "" : "none";
    this.verticalTrack.style.display = display;
    this.horizontalTrack.style.display = display;
    this.corner.style.display = display;
  }

  /**
   * Clean up event listeners
   */
  destroy(): void {
    document.removeEventListener("mousemove", this.boundMouseMove);
    document.removeEventListener("mouseup", this.boundMouseUp);
    this.verticalTrack.remove();
    this.horizontalTrack.remove();
    this.corner.remove();
  }
}

/**
 * Calculate content dimensions based on sheet info
 * Used for scrollbar sizing with virtual scrolling
 */
export function calculateContentDimensions(
  colCount: number,
  rowCount: number,
  colWidths: Map<number, number>,
  rowHeights: Map<number, number>,
  maxVirtualRows = 1_000_000
): { width: number; height: number } {
  // For columns: fixed at 22 (A-V) or actual count, whichever is larger
  const effectiveColCount = Math.max(colCount, 22);

  // Calculate total width considering custom widths
  let width = 0;
  for (let c = 0; c < effectiveColCount; c++) {
    width += colWidths.get(c) ?? DEFAULT_COL_WIDTH;
  }
  width += HEADER_WIDTH;

  // For rows: use actual row count up to max virtual rows
  // The "discovered" row count expands as user scrolls down
  const effectiveRowCount = Math.min(rowCount, maxVirtualRows);

  // Calculate total height considering custom heights
  let height = 0;
  for (let r = 0; r < effectiveRowCount; r++) {
    height += rowHeights.get(r) ?? DEFAULT_ROW_HEIGHT;
  }
  height += HEADER_HEIGHT;

  return { width, height };
}

/**
 * Calculate discovered row count based on scroll position
 * Expands virtual row count as user scrolls near the bottom
 */
export function calculateDiscoveredRows(
  currentScrollY: number,
  viewportHeight: number,
  currentDiscovered: number,
  actualRowCount: number,
  maxVirtualRows = 1_000_000
): number {
  // Buffer rows to add when near the bottom
  const bufferRows = 100;

  // Calculate visible rows based on scroll
  const approximateVisibleRow = Math.ceil((currentScrollY + viewportHeight) / DEFAULT_ROW_HEIGHT);

  // If user is near the discovered boundary, expand
  if (approximateVisibleRow + bufferRows > currentDiscovered) {
    const newDiscovered = Math.min(
      maxVirtualRows,
      Math.max(actualRowCount, approximateVisibleRow + bufferRows * 2)
    );
    return newDiscovered;
  }

  return currentDiscovered;
}
