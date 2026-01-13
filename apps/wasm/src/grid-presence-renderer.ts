// Grid Presence Renderer Module
// Handles rendering of remote user presence (cursors, selections, mouse pointers)

import type { Position, SelectionRange, Point, EditingState } from "./types.js";
import {
  PRESENCE_LABEL_FONT,
  PRESENCE_LABEL_PADDING,
  PRESENCE_LABEL_HEIGHT,
  getZoomedHeaderWidth,
  getZoomedHeaderHeight,
  type RemotePresenceRender,
} from "./grid-constants.js";
import { getCellBounds, getRangeBounds } from "./grid-utils.js";

/** Interface for grid state needed by presence renderer */
export interface PresenceRendererState {
  scrollX: number;
  scrollY: number;
  colWidths: Map<number, number>;
  rowHeights: Map<number, number>;
}

/**
 * Draw remote user presence (cursors and selections)
 * This should be called after render() to draw presence on top of local selection
 */
export function drawRemotePresence(
  ctx: CanvasRenderingContext2D,
  canvas: HTMLCanvasElement,
  state: PresenceRendererState,
  remotePresence: RemotePresenceRender[]
): void {
  if (remotePresence.length === 0) return;

  const container = canvas.parentElement;
  if (!container) return;

  const viewWidth = container.clientWidth;
  const viewHeight = container.clientHeight;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Clip to cells area (exclude headers)
  ctx.save();
  ctx.beginPath();
  ctx.rect(
    zoomedHeaderWidth,
    zoomedHeaderHeight,
    viewWidth - zoomedHeaderWidth,
    viewHeight - zoomedHeaderHeight
  );
  ctx.clip();

  for (const presence of remotePresence) {
    const mouseOpacity =
      presence.mouseOpacity !== undefined ? presence.mouseOpacity : 1.0;
    const color = presence.color || "#888888";

    // Draw selection range first (behind cursor) - just color, no name
    if (
      presence.selection &&
      presence.selection.start &&
      presence.selection.end
    ) {
      drawRemoteSelection(ctx, canvas, state, presence.selection, color, 1.0);
    }

    // Draw cursor (active cell) - just color border, no name label
    if (
      presence.cursor &&
      presence.cursor.col !== undefined &&
      presence.cursor.row !== undefined
    ) {
      drawRemoteCursor(
        ctx,
        state,
        presence.cursor,
        color,
        1.0,
        viewWidth,
        viewHeight
      );
    }

    // Draw ephemeral editing text (what peer is currently typing)
    if (
      presence.editing &&
      presence.editing.col !== undefined &&
      presence.editing.row !== undefined
    ) {
      drawRemoteEditing(
        ctx,
        state,
        presence.editing,
        presence.name,
        color,
        viewWidth,
        viewHeight
      );
    }

    // Draw mouse pointer with name (Figma-style) - fades out after 3 seconds
    if (
      presence.mouse &&
      presence.mouse.x !== undefined &&
      presence.mouse.y !== undefined &&
      mouseOpacity > 0
    ) {
      drawRemoteMouse(
        ctx,
        presence.mouse,
        presence.name,
        color,
        mouseOpacity,
        viewWidth,
        viewHeight
      );
    }
  }

  ctx.restore();
}

/**
 * Draw a remote user's selection range
 */
function drawRemoteSelection(
  ctx: CanvasRenderingContext2D,
  canvas: HTMLCanvasElement,
  state: PresenceRendererState,
  selection: SelectionRange,
  color: string,
  opacity: number
): void {
  const start = selection.start;
  const end = selection.end;

  // Use zoom-aware range bounds calculation
  const bounds = getRangeBounds(
    start.col, start.row, end.col, end.row,
    state.scrollX, state.scrollY,
    state.colWidths, state.rowHeights
  );
  const { x: selX, y: selY, width: selW, height: selH } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if visible
  const container = canvas.parentElement;
  if (!container) return;
  const viewWidth = container.clientWidth;
  const viewHeight = container.clientHeight;

  if (
    selX + selW <= zoomedHeaderWidth ||
    selX >= viewWidth ||
    selY + selH <= zoomedHeaderHeight ||
    selY >= viewHeight
  ) {
    return; // Off screen
  }

  // Clip to visible area
  const clipX = Math.max(zoomedHeaderWidth, selX);
  const clipY = Math.max(zoomedHeaderHeight, selY);
  const clipW = Math.min(selW, selX + selW - clipX);
  const clipH = Math.min(selH, selY + selH - clipY);

  // Draw semi-transparent fill
  ctx.globalAlpha = opacity * 0.15;
  ctx.fillStyle = color;
  ctx.fillRect(clipX, clipY, clipW, clipH);

  // Draw border
  ctx.globalAlpha = opacity * 0.5;
  ctx.strokeStyle = color;
  ctx.lineWidth = 1;
  ctx.strokeRect(clipX + 0.5, clipY + 0.5, clipW - 1, clipH - 1);

  ctx.globalAlpha = 1;
}

/**
 * Draw a remote user's cursor (active cell) - just colored border, no name label
 * Name is shown on the mouse cursor instead (Figma-style)
 */
function drawRemoteCursor(
  ctx: CanvasRenderingContext2D,
  state: PresenceRendererState,
  cursor: Position,
  color: string,
  opacity: number,
  viewWidth: number,
  viewHeight: number
): void {
  // Use zoom-aware cell bounds calculation
  const bounds = getCellBounds(
    cursor.col, cursor.row,
    state.scrollX, state.scrollY,
    state.colWidths, state.rowHeights
  );
  const { x: cellX, y: cellY, width: cellW, height: cellH } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if visible
  if (
    cellX + cellW <= zoomedHeaderWidth ||
    cellX >= viewWidth ||
    cellY + cellH <= zoomedHeaderHeight ||
    cellY >= viewHeight
  ) {
    return; // Off screen
  }

  // Draw cursor border (thicker, colored)
  ctx.globalAlpha = opacity;
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;

  const clipX = Math.max(zoomedHeaderWidth, cellX);
  const clipY = Math.max(zoomedHeaderHeight, cellY);
  const clipW = Math.min(cellW, cellX + cellW - clipX);
  const clipH = Math.min(cellH, cellY + cellH - clipY);

  ctx.strokeRect(clipX + 1, clipY + 1, clipW - 2, clipH - 2);

  ctx.globalAlpha = 1;
}

/**
 * Draw a remote user's mouse pointer (Figma-style)
 */
function drawRemoteMouse(
  ctx: CanvasRenderingContext2D,
  mouse: Point,
  name: string,
  color: string,
  opacity: number,
  viewWidth: number,
  viewHeight: number
): void {
  // Mouse coordinates are relative to canvas (already in screen space)
  const mouseX = mouse.x;
  const mouseY = mouse.y;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if visible (within cells area)
  if (
    mouseX < zoomedHeaderWidth ||
    mouseX >= viewWidth ||
    mouseY < zoomedHeaderHeight ||
    mouseY >= viewHeight
  ) {
    return;
  }

  ctx.globalAlpha = opacity;

  // Figma-style pointer - simple clean triangle
  const size = 14;

  // Draw shadow
  ctx.save();
  ctx.shadowColor = "rgba(0, 0, 0, 0.3)";
  ctx.shadowBlur = 3;
  ctx.shadowOffsetX = 1;
  ctx.shadowOffsetY = 1;

  // Simple triangular pointer shape
  ctx.beginPath();
  ctx.moveTo(mouseX, mouseY); // Tip
  ctx.lineTo(mouseX, mouseY + size); // Down
  ctx.lineTo(mouseX + size * 0.7, mouseY + size * 0.7); // Diagonal
  ctx.closePath();

  // White outline
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 2;
  ctx.lineJoin = "round";
  ctx.stroke();

  ctx.restore();

  // Colored fill
  ctx.beginPath();
  ctx.moveTo(mouseX, mouseY);
  ctx.lineTo(mouseX, mouseY + size);
  ctx.lineTo(mouseX + size * 0.7, mouseY + size * 0.7);
  ctx.closePath();
  ctx.fillStyle = color;
  ctx.fill();

  // Draw name label next to pointer if provided
  if (name) {
    ctx.font = PRESENCE_LABEL_FONT;
    const textWidth = ctx.measureText(name).width;
    const labelWidth = textWidth + PRESENCE_LABEL_PADDING * 2;
    const labelHeight = PRESENCE_LABEL_HEIGHT;

    // Position label at bottom-right of cursor
    let labelX = mouseX + size * 0.7 + 3;
    let labelY = mouseY + size * 0.5;

    // Keep label within visible area
    if (labelX + labelWidth > viewWidth) {
      labelX = mouseX - labelWidth - 4;
    }
    if (labelY + labelHeight > viewHeight) {
      labelY = viewHeight - labelHeight - 2;
    }
    if (labelX < zoomedHeaderWidth) {
      labelX = zoomedHeaderWidth + 2;
    }
    if (labelY < zoomedHeaderHeight) {
      labelY = zoomedHeaderHeight + 2;
    }

    // Draw label with shadow
    ctx.save();
    ctx.shadowColor = "rgba(0, 0, 0, 0.15)";
    ctx.shadowBlur = 3;
    ctx.shadowOffsetX = 0;
    ctx.shadowOffsetY = 1;

    // Draw label background
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.roundRect(labelX, labelY, labelWidth, labelHeight, 4);
    ctx.fill();

    ctx.restore();

    // Draw label text
    ctx.fillStyle = "#ffffff";
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";
    ctx.fillText(name, labelX + PRESENCE_LABEL_PADDING, labelY + labelHeight / 2);
  }

  ctx.globalAlpha = 1;
}

/**
 * Draw a remote user's editing text (ephemeral, what they're typing)
 */
function drawRemoteEditing(
  ctx: CanvasRenderingContext2D,
  state: PresenceRendererState,
  editing: EditingState,
  _name: string,
  _color: string,
  viewWidth: number,
  viewHeight: number
): void {
  const col = editing.col;
  const row = editing.row;
  const text = editing.text || "";

  // Use zoom-aware cell bounds calculation
  const bounds = getCellBounds(
    col, row,
    state.scrollX, state.scrollY,
    state.colWidths, state.rowHeights
  );
  const { x: cellX, y: cellY, width: cellW, height: cellH } = bounds;
  const zoomedHeaderWidth = getZoomedHeaderWidth();
  const zoomedHeaderHeight = getZoomedHeaderHeight();

  // Check if visible
  if (
    cellX + cellW <= zoomedHeaderWidth ||
    cellX >= viewWidth ||
    cellY + cellH <= zoomedHeaderHeight ||
    cellY >= viewHeight
  ) {
    return; // Off screen
  }

  // Clip to visible portion
  const clipX = Math.max(zoomedHeaderWidth, cellX);
  const clipY = Math.max(zoomedHeaderHeight, cellY);
  const clipW = Math.min(cellW, cellX + cellW - clipX);
  const clipH = Math.min(cellH, cellY + cellH - clipY);

  // Draw the editing text (no background, name shown on cursor already)
  ctx.globalAlpha = 0.7;
  ctx.fillStyle = "#000000";
  ctx.font = '13px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
  ctx.textAlign = "left";
  ctx.textBaseline = "middle";

  // Truncate text if too long
  let displayText = text;
  const maxWidth = clipW - 8;
  let textWidth = ctx.measureText(displayText).width;
  if (textWidth > maxWidth && displayText.length > 0) {
    while (textWidth > maxWidth && displayText.length > 1) {
      displayText = displayText.slice(0, -1);
      textWidth = ctx.measureText(displayText + "…").width;
    }
    displayText = displayText + "…";
  }

  ctx.fillText(displayText, clipX + 4, clipY + clipH / 2);
  ctx.globalAlpha = 1;
}
