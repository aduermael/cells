// Test helper functions for interacting with the Cells spreadsheet UI
// These helpers abstract common operations like clicking cells, entering values, etc.

import { CONFIG } from './harness.mjs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

/**
 * Wait for the application to be fully loaded
 */
export async function waitForAppReady(page, timeout = CONFIG.timeout) {
  // Wait for canvas to be visible and loading indicator to be hidden
  await page.waitForSelector('#grid', { visible: true, timeout });

  // Wait for loading indicator to disappear
  await page.waitForFunction(() => {
    const loading = document.getElementById('loading');
    return !loading || loading.style.display === 'none' ||
           !loading.classList.contains('visible');
  }, { timeout });

  // Give WASM time to initialize
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 500)));
}

/**
 * Create a new workbook
 */
export async function createNewWorkbook(page) {
  // Click the "New" button if visible
  const newBtn = await page.$('#new-btn');
  if (newBtn) {
    const isVisible = await page.evaluate(el => {
      const style = window.getComputedStyle(el);
      return style.display !== 'none';
    }, newBtn);
    if (isVisible) {
      await newBtn.click();
      await waitForAppReady(page);
      return;
    }
  }

  // If new button not available, navigate to base URL (creates new workbook)
  await page.goto(`http://localhost:${CONFIG.serverPort}/`);
  await waitForAppReady(page);
}

/**
 * Get canvas and its dimensions
 */
export async function getCanvasInfo(page) {
  return await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return {
      width: rect.width,
      height: rect.height,
      left: rect.left,
      top: rect.top,
    };
  });
}

/**
 * Convert column index to letter (0 -> A, 1 -> B, etc.)
 */
export function colToLetter(col) {
  let result = '';
  let n = col;
  while (n >= 0) {
    result = String.fromCharCode(65 + (n % 26)) + result;
    n = Math.floor(n / 26) - 1;
  }
  return result;
}

/**
 * Parse cell reference (e.g., "A1" -> { col: 0, row: 0 })
 */
export function parseCellRef(ref) {
  const match = ref.match(/^([A-Z]+)(\d+)$/i);
  if (!match) {
    throw new Error(`Invalid cell reference: ${ref}`);
  }

  const colStr = match[1].toUpperCase();
  const row = parseInt(match[2], 10) - 1;

  let col = 0;
  for (let i = 0; i < colStr.length; i++) {
    col = col * 26 + (colStr.charCodeAt(i) - 64);
  }
  col -= 1;

  return { col, row };
}

/**
 * Calculate pixel position for a cell using actual column/row sizes.
 * This properly accounts for scroll position, zoom, and custom column/row sizes,
 * and always calculates from the current colWidths/rowHeights Maps rather than
 * relying on cached pixel offsets (which may be stale after resizing).
 */
export async function cellToPixelFromRenderer(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const ctx = window._appContext;
    if (!ctx || !ctx.app) {
      // Fallback to basic calculation if context not available
      return {
        x: HEADER_WIDTH + col * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2,
        y: HEADER_HEIGHT + row * DEFAULT_ROW_HEIGHT + DEFAULT_ROW_HEIGHT / 2,
      };
    }

    const app = ctx.app;
    const renderer = app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    // Calculate X position directly from colWidths (no cached offsets)
    // This ensures accuracy even after column resizes
    let offsetX = 0;
    for (let i = 0; i < col; i++) {
      offsetX += app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    const cellWidth = app.colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const cellX = Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) - Math.round(app.scrollX * zoomFactor);

    // Calculate Y position directly from rowHeights (no cached offsets)
    // This ensures accuracy even after row resizes
    let offsetY = 0;
    for (let i = 0; i < row; i++) {
      offsetY += app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    }
    const cellHeight = app.rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const cellY = Math.round(HEADER_HEIGHT * zoomFactor) + Math.round(offsetY * zoomFactor) - Math.round(app.scrollY * zoomFactor);

    // Return center of cell
    const width = Math.round(cellWidth * zoomFactor);
    const height = Math.round(cellHeight * zoomFactor);
    return { x: cellX + width / 2, y: cellY + height / 2 };
  }, { col, row });
}

/**
 * Calculate pixel position for a cell (sync version)
 * Uses approximate default dimensions from the app
 */
export function cellToPixel(col, row, canvasInfo) {
  const HEADER_WIDTH = 50;
  const HEADER_HEIGHT = 24;
  const DEFAULT_COL_WIDTH = 100;
  const DEFAULT_ROW_HEIGHT = 24;

  // Calculate position (assuming default sizes, no custom widths)
  const x = HEADER_WIDTH + col * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
  const y = HEADER_HEIGHT + row * DEFAULT_ROW_HEIGHT + DEFAULT_ROW_HEIGHT / 2;

  return {
    x: canvasInfo.left + x,
    y: canvasInfo.top + y,
  };
}

/**
 * Click on a cell by reference (e.g., "A1")
 */
export async function clickCell(page, cellRef) {
  const { col, row } = parseCellRef(cellRef);
  const canvasInfo = await getCanvasInfo(page);

  // Use renderer coordinates for proper scroll/zoom handling
  const cellPos = await cellToPixelFromRenderer(page, col, row);
  const x = canvasInfo.left + cellPos.x;
  const y = canvasInfo.top + cellPos.y;

  await page.mouse.click(x, y);
  // Ensure canvas has focus for keyboard events
  await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    if (canvas) canvas.focus();
  });
  // Wait for cell selection to update
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 100)));
}

/**
 * Double-click on a cell to enter edit mode
 */
export async function doubleClickCell(page, cellRef) {
  const { col, row } = parseCellRef(cellRef);
  const canvasInfo = await getCanvasInfo(page);

  // Use renderer coordinates for proper scroll/zoom handling
  const cellPos = await cellToPixelFromRenderer(page, col, row);
  const x = canvasInfo.left + cellPos.x;
  const y = canvasInfo.top + cellPos.y;

  await page.mouse.click(x, y, { clickCount: 2 });
  // Wait for editor to appear
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 100)));
}

/**
 * Type text into the currently selected cell editor
 */
export async function typeInCell(page, text) {
  // The cell editor should be active after clicking
  // Use delay between key presses to ensure all characters are captured
  // Increased from 50ms to 75ms to reduce flakiness under system load
  await page.keyboard.type(text, { delay: 75 });
}

/**
 * Press Enter to confirm cell edit
 */
export async function confirmEdit(page) {
  await page.keyboard.press('Enter');
  // Wait for edit to be processed
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 200)));
}

/**
 * Press Escape to cancel cell edit
 */
export async function cancelEdit(page) {
  await page.keyboard.press('Escape');
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 100)));
}

/**
 * Set a cell value by clicking and typing
 */
export async function setCellValue(page, cellRef, value) {
  await clickCell(page, cellRef);
  // Small delay to ensure cell is selected and ready for input
  await sleep(100);
  await typeInCell(page, value);
  // Increased delay before confirming to ensure all characters are processed
  // This helps prevent flakiness when system is under load
  await sleep(150);
  await confirmEdit(page);
}

/**
 * Get the current cell reference display
 */
export async function getCurrentCellRef(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('cell-reference');
    return el ? el.textContent : null;
  });
}

/**
 * Get the formula bar content
 */
export async function getFormulaBarContent(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('formula-display');
    return el ? el.textContent : null;
  });
}

/**
 * Get the cell editor content (the in-cell editor that appears on double-click)
 */
export async function getCellEditorContent(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('cell-display');
    return el ? el.textContent : null;
  });
}

/**
 * Get the displayed value of a cell (the computed result, not the formula)
 * This reads directly from the engine via the app's data cache
 */
export async function getCellDisplayValue(page, cellRef) {
  const { col, row } = parseCellRef(cellRef);
  return await page.evaluate(({ col, row }) => {
    // Access the app's cell cache
    // The app stores cells from the viewport in an array
    if (window._appContext && window._appContext.app && window._appContext.app.cells) {
      const cells = window._appContext.app.cells;
      for (const cell of cells) {
        if (cell.col === col && cell.row === row) {
          // Prefer display (formatted value) if available,
          // otherwise fall back to value (raw value)
          return cell.display || cell.value || '';
        }
      }
    }
    return null;
  }, { col, row });
}

/**
 * Get workbook name from header
 */
export async function getWorkbookName(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('workbook-title');
    return el ? el.textContent : null;
  });
}

/**
 * Trigger export via the dropdown
 */
export async function triggerExport(page, format) {
  // Click export dropdown button
  await page.click('#export-btn');
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 100)));

  // Click the format option
  const selector = `.dropdown-item[onclick="exportAs('${format}')"]`;
  await page.click(selector);

  // Wait for export to process
  await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 500)));
}

/**
 * Check if a file was downloaded (by intercepting download events)
 */
export async function setupDownloadInterceptor(page) {
  const downloads = [];

  // Listen for download events (CDP-based)
  const client = await page.target().createCDPSession();
  await client.send('Page.setDownloadBehavior', {
    behavior: 'allow',
    downloadPath: '/tmp/cells-test-downloads',
  });

  return downloads;
}

/**
 * Assert helper
 */
export function assert(condition, message) {
  if (!condition) {
    throw new Error(message || 'Assertion failed');
  }
}

/**
 * Assert equality
 */
export function assertEqual(actual, expected, message) {
  if (actual !== expected) {
    throw new Error(`${message || 'Assertion failed'}: expected "${expected}", got "${actual}"`);
  }
}

/**
 * Assert truthy
 */
export function assertTrue(value, message) {
  if (!value) {
    throw new Error(message || `Expected truthy value, got ${value}`);
  }
}

/**
 * Sleep for specified ms
 */
export function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * Calculate the fill handle position for a cell using actual dimensions.
 * The fill handle is a 6x6px square positioned at the bottom-right corner of the selection.
 * Its position is: x = selectionRight - 3, y = selectionBottom - 3
 * So to hit the center, we go to selectionRight and selectionBottom (corner of the cell)
 */
export async function cellFillHandlePosition(page, col, row) {
  const canvasInfo = await getCanvasInfo(page);
  const pos = await page.evaluate(({ col, row }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const ctx = window._appContext;
    if (!ctx || !ctx.app) {
      // Fallback to basic calculation
      return {
        x: HEADER_WIDTH + (col + 1) * DEFAULT_COL_WIDTH,
        y: HEADER_HEIGHT + (row + 1) * DEFAULT_ROW_HEIGHT,
      };
    }

    const app = ctx.app;
    const zoomFactor = app.renderer.getZoomFactor();

    // Calculate X position (right edge of cell)
    let offsetX = 0;
    for (let i = 0; i <= col; i++) {
      offsetX += app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    const cellRightX = Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) - Math.round(app.scrollX * zoomFactor);

    // Calculate Y position (bottom edge of cell)
    let offsetY = 0;
    for (let i = 0; i <= row; i++) {
      offsetY += app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    }
    const cellBottomY = Math.round(HEADER_HEIGHT * zoomFactor) + Math.round(offsetY * zoomFactor) - Math.round(app.scrollY * zoomFactor);

    return { x: cellRightX, y: cellBottomY };
  }, { col, row });

  return {
    x: canvasInfo.left + pos.x,
    y: canvasInfo.top + pos.y,
  };
}

/**
 * Get the canvas cursor style
 */
export async function getCanvasCursor(page) {
  return await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    return canvas ? canvas.style.cursor : null;
  });
}

/**
 * Move mouse to the fill handle position of a cell
 * Uses dispatchEvent for more reliable canvas event handling in tests
 */
export async function moveToFillHandle(page, cellRef) {
  const { col, row } = parseCellRef(cellRef);
  const canvasInfo = await getCanvasInfo(page);
  const { x, y } = await cellFillHandlePosition(page, col, row);

  // Dispatch a pointermove event directly on the canvas
  // Must use PointerEvent since canvas handlers listen for pointer events, not mouse events
  await page.evaluate(({ canvasX, canvasY }) => {
    const canvas = document.getElementById('grid');
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const event = new PointerEvent('pointermove', {
      bubbles: true,
      clientX: rect.left + canvasX,
      clientY: rect.top + canvasY,
      pointerId: 1,
      pointerType: 'mouse',
    });
    canvas.dispatchEvent(event);
  }, { canvasX: x - canvasInfo.left, canvasY: y - canvasInfo.top });

  await sleep(50);
}

/**
 * Get the collaboration sync state from the page
 * Returns 'offline', 'connecting', 'syncing', or 'online'
 */
export async function getCollabState(page) {
  return await page.evaluate(() => {
    // Access the global sync adapter state (exposed via window for testing)
    if (window._syncAdapter) {
      return window._syncAdapter.state;
    }
    // Fallback: check the UI status dot class
    const statusDot = document.querySelector('.collab-status-dot');
    if (statusDot) {
      if (statusDot.classList.contains('online')) return 'online';
      if (statusDot.classList.contains('syncing')) return 'syncing';
      if (statusDot.classList.contains('connecting')) return 'connecting';
    }
    return 'offline';
  });
}

/**
 * Wait for collaboration to initialize (window._syncAdapter to be set)
 * @param {import('puppeteer').Page} page
 * @param {number} timeout - Maximum time to wait in ms (default 10000)
 * @returns {Promise<boolean>} - true if initialized, false if timeout
 */
export async function waitForCollabInitialized(page, timeout = 10000) {
  const start = Date.now();

  while (Date.now() - start < timeout) {
    const hasAdapter = await page.evaluate(() => !!window._syncAdapter);
    if (hasAdapter) {
      return true;
    }
    await sleep(100);
  }

  console.warn('[Collab] Timeout waiting for collaboration to initialize');
  return false;
}

/**
 * Wait for collaboration to be ready (data channel open)
 * This waits for the sync state to reach 'online' or 'syncing' status
 * @param {import('puppeteer').Page} page
 * @param {number} timeout - Maximum time to wait in ms (default 15000)
 * @returns {Promise<boolean>} - true if connected, false if timeout
 */
export async function waitForCollabReady(page, timeout = 15000) {
  const start = Date.now();
  let lastState = null;

  // First wait for collaboration to initialize (adapter to be set)
  const initialized = await waitForCollabInitialized(page, Math.min(timeout / 2, 5000));
  if (!initialized) {
    console.warn('[Collab] Collaboration never initialized');
    return false;
  }

  while (Date.now() - start < timeout) {
    const state = await getCollabState(page);
    if (state !== lastState) {
      lastState = state;
      if (process.env.DEBUG) {
        console.log(`[Collab] State: ${state}`);
      }
    }

    // 'online' means data channel is established and ready
    // 'syncing' means initial sync is in progress but channel is open
    if (state === 'online' || state === 'syncing') {
      // Give a small buffer for the connection to stabilize
      await sleep(200);
      return true;
    }

    await sleep(100);
  }

  console.warn(`[Collab] Timeout waiting for ready state (last state: ${lastState})`);
  return false;
}

/**
 * Wait for peer connection (at least one remote peer)
 * @param {import('puppeteer').Page} page
 * @param {number} timeout - Maximum time to wait in ms (default 15000)
 * @returns {Promise<boolean>} - true if peer connected, false if timeout
 */
export async function waitForPeerConnection(page, timeout = 15000) {
  const start = Date.now();

  while (Date.now() - start < timeout) {
    const peerCount = await page.evaluate(() => {
      // Access the global sync adapter (exposed via window for testing)
      if (window._syncAdapter) {
        return window._syncAdapter.getConnectedPeerCount?.() ?? 0;
      }
      // Fallback: check the peers display in the UI
      const peersEl = document.querySelector('#collab-detail-peers');
      return peersEl ? parseInt(peersEl.textContent, 10) : 0;
    });

    if (peerCount > 0) {
      return true;
    }

    await sleep(200);
  }

  console.warn('[Collab] Timeout waiting for peer connection');
  return false;
}

/**
 * Retry an assertion multiple times with exponential backoff
 * Useful for flaky assertions that depend on async operations
 * @param {Function} assertFn - The assertion function to run (should throw on failure)
 * @param {Object} options - Options
 * @param {number} options.retries - Number of retries (default 3)
 * @param {number} options.initialDelay - Initial delay in ms (default 500)
 * @param {number} options.maxDelay - Maximum delay in ms (default 2000)
 * @returns {Promise<void>}
 */
export async function assertWithRetry(assertFn, { retries = 3, initialDelay = 500, maxDelay = 2000 } = {}) {
  let lastError;
  let delay = initialDelay;

  for (let attempt = 1; attempt <= retries; attempt++) {
    try {
      await assertFn();
      return; // Success
    } catch (err) {
      lastError = err;
      if (attempt < retries) {
        if (process.env.DEBUG) {
          console.log(`[Retry] Attempt ${attempt} failed, retrying in ${delay}ms...`);
        }
        await sleep(delay);
        delay = Math.min(delay * 2, maxDelay);
      }
    }
  }

  throw lastError;
}

/**
 * Calculate pixel position for a column header using actual column sizes.
 * @param {import('puppeteer').Page} page
 * @param {number} col - Column index (0-based)
 */
export async function colHeaderToPixel(page, col) {
  const canvasInfo = await getCanvasInfo(page);
  const pos = await page.evaluate(({ col }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;

    const ctx = window._appContext;
    if (!ctx || !ctx.app) {
      // Fallback to basic calculation
      return {
        x: HEADER_WIDTH + col * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2,
        y: HEADER_HEIGHT / 2,
      };
    }

    const app = ctx.app;
    const zoomFactor = app.renderer.getZoomFactor();

    // Calculate X position (center of column header)
    let offsetX = 0;
    for (let i = 0; i < col; i++) {
      offsetX += app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    const colWidth = app.colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const x = Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) + Math.round(colWidth * zoomFactor / 2) - Math.round(app.scrollX * zoomFactor);
    const y = Math.round(HEADER_HEIGHT * zoomFactor / 2);

    return { x, y };
  }, { col });

  return {
    x: canvasInfo.left + pos.x,
    y: canvasInfo.top + pos.y,
  };
}

/**
 * Drag a column to a new position
 * @param {import('puppeteer').Page} page
 * @param {string} sourceCol - Source column letter (e.g., "B")
 * @param {string} targetCol - Target column letter to drop before (e.g., "D")
 */
export async function dragColumn(page, sourceCol, targetCol) {
  const sourceColIndex = sourceCol.toUpperCase().charCodeAt(0) - 65;
  const targetColIndex = targetCol.toUpperCase().charCodeAt(0) - 65;

  const source = await colHeaderToPixel(page, sourceColIndex);
  const target = await colHeaderToPixel(page, targetColIndex);

  // Start drag on the column header
  await page.mouse.move(source.x, source.y);
  await page.mouse.down();

  // Move to trigger drag threshold (need to move at least 5px)
  await page.mouse.move(source.x + 10, source.y, { steps: 5 });

  // Move to target position
  await page.mouse.move(target.x, target.y, { steps: 10 });

  // Release
  await page.mouse.up();

  // Wait for the move operation to complete
  await sleep(300);
}

/**
 * Drag the fill handle from the current selection to a target cell
 * @param {import('puppeteer').Page} page
 * @param {string} fromCellRef - Cell at bottom-right of current selection (where fill handle is)
 * @param {string} toCellRef - Target cell to drag to
 */
export async function dragFillHandle(page, fromCellRef, toCellRef) {
  const from = parseCellRef(fromCellRef);
  const to = parseCellRef(toCellRef);

  // Get fill handle positions using actual cell dimensions
  const { x: startX, y: startY } = await cellFillHandlePosition(page, from.col, from.row);
  const { x: endX, y: endY } = await cellFillHandlePosition(page, to.col, to.row);

  // Start at fill handle
  await page.mouse.move(startX, startY);
  await page.mouse.down();

  // Move to target - but constrain to single axis for clarity
  // If rows differ more than cols, keep X fixed (vertical drag)
  // If cols differ more than rows, keep Y fixed (horizontal drag)
  const rowDiff = Math.abs(to.row - from.row);
  const colDiff = Math.abs(to.col - from.col);
  const targetX = colDiff > rowDiff ? endX : startX;
  const targetY = rowDiff >= colDiff ? endY : startY;

  await page.mouse.move(targetX, targetY, { steps: 10 });

  // Release
  await page.mouse.up();

  // Wait for the operation to complete
  await sleep(100);
}

/**
 * Select a range of cells by clicking start and shift-clicking end
 * @param {import('puppeteer').Page} page
 * @param {string} startRef - Start cell reference (e.g., "A1")
 * @param {string} endRef - End cell reference (e.g., "C3")
 */
export async function selectRange(page, startRef, endRef) {
  // Click start cell
  await clickCell(page, startRef);
  await sleep(100);

  // Shift-click end cell to create range
  const { col, row } = parseCellRef(endRef);
  const canvasInfo = await getCanvasInfo(page);

  // Use renderer coordinates for proper scroll/zoom handling
  const cellPos = await cellToPixelFromRenderer(page, col, row);
  const x = canvasInfo.left + cellPos.x;
  const y = canvasInfo.top + cellPos.y;

  await page.keyboard.down('Shift');
  await page.mouse.click(x, y);
  await page.keyboard.up('Shift');
  await sleep(100);
}

/**
 * Resize a column by dragging its right edge
 * @param {import('puppeteer').Page} page
 * @param {string} colLetter - Column letter (e.g., "A")
 * @param {number} newWidth - New width in pixels
 */
export async function resizeColumn(page, colLetter, newWidth) {
  const colIndex = colLetter.toUpperCase().charCodeAt(0) - 65;

  const info = await page.evaluate(({ colIndex }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;

    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();

    const ctx = window._appContext;
    const app = ctx?.app;
    const zoomFactor = app?.renderer?.getZoomFactor() ?? 1;

    // Calculate current column right edge using actual widths
    let rightEdge = HEADER_WIDTH * zoomFactor;
    for (let i = 0; i <= colIndex; i++) {
      const w = app?.colWidths?.get(i) ?? DEFAULT_COL_WIDTH;
      rightEdge += w * zoomFactor;
    }
    if (app) {
      rightEdge -= app.scrollX * zoomFactor;
    }

    // Get current width
    const currentWidth = app?.colWidths?.get(colIndex) ?? DEFAULT_COL_WIDTH;

    return {
      canvasLeft: rect.left,
      canvasTop: rect.top,
      rightEdge,
      currentWidth,
      headerHeight: HEADER_HEIGHT * zoomFactor,
      zoomFactor
    };
  }, { colIndex });

  // Position to start drag: right edge of column, in header area
  const startX = info.canvasLeft + info.rightEdge;
  const startY = info.canvasTop + info.headerHeight / 2;

  // Calculate how much to drag (account for zoom)
  const dragDelta = (newWidth - info.currentWidth) * info.zoomFactor;
  const endX = startX + dragDelta;

  // Simulate the drag
  await page.mouse.move(startX, startY);
  await page.mouse.down();
  await page.mouse.move(endX, startY, { steps: 10 });
  await page.mouse.up();

  await sleep(300);
}

/**
 * Resize a row by dragging its bottom edge
 * @param {import('puppeteer').Page} page
 * @param {number} rowNumber - Row number (1-based)
 * @param {number} newHeight - New height in pixels
 */
export async function resizeRow(page, rowNumber, newHeight) {
  const rowIndex = rowNumber - 1;

  const info = await page.evaluate(({ rowIndex }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_ROW_HEIGHT = 24;

    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();

    const ctx = window._appContext;
    const app = ctx?.app;
    const zoomFactor = app?.renderer?.getZoomFactor() ?? 1;

    // Calculate current row bottom edge using actual heights
    let bottomEdge = HEADER_HEIGHT * zoomFactor;
    for (let i = 0; i <= rowIndex; i++) {
      const h = app?.rowHeights?.get(i) ?? DEFAULT_ROW_HEIGHT;
      bottomEdge += h * zoomFactor;
    }
    if (app) {
      bottomEdge -= app.scrollY * zoomFactor;
    }

    // Get current height
    const currentHeight = app?.rowHeights?.get(rowIndex) ?? DEFAULT_ROW_HEIGHT;

    return {
      canvasLeft: rect.left,
      canvasTop: rect.top,
      bottomEdge,
      currentHeight,
      headerWidth: HEADER_WIDTH * zoomFactor,
      zoomFactor
    };
  }, { rowIndex });

  // Position to start drag: bottom edge of row, in header area
  const startX = info.canvasLeft + info.headerWidth / 2;
  const startY = info.canvasTop + info.bottomEdge;

  // Calculate how much to drag (account for zoom)
  const dragDelta = (newHeight - info.currentHeight) * info.zoomFactor;
  const endY = startY + dragDelta;

  // Simulate the drag
  await page.mouse.move(startX, startY);
  await page.mouse.down();
  await page.mouse.move(startX, endY, { steps: 10 });
  await page.mouse.up();

  await sleep(300);
}

/**
 * Load a file from testdata directory by setting up file input
 * @param {import('puppeteer').Page} page
 * @param {string} filename - Name of file in testdata directory
 */
export async function loadTestFile(page, filename) {
  // Compute path to testdata relative to project root
  // The server serves from dist/, but we need the testdata path for the file chooser
  const testdataPath = join(__dirname, '..', '..', '..', 'testdata', filename);

  // Trigger file input
  const fileInput = await page.$('#file-input');
  if (!fileInput) {
    throw new Error('File input not found');
  }

  // Upload the file through the hidden file input
  await fileInput.uploadFile(testdataPath);

  // Wait for file to load
  await waitForAppReady(page);
}

/**
 * Get all named ranges from the workbook
 * @param {import('puppeteer').Page} page
 * @returns {Promise<Array<{name: string, scope: string, targetType: string, id1: string, id2?: string, sheetId?: string}>>}
 */
export async function getNamedRanges(page) {
  const result = await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return [];
    }
    return await ctx.app.dataSource.getNamedRanges();
  });
  return result;
}

/**
 * Export workbook to ZCD format
 * @param {import('puppeteer').Page} page
 * @returns {Promise<string>} ZCD content
 */
export async function exportToZCD(page) {
  const result = await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return '';
    }
    const exportResult = await ctx.app.dataSource.client.exportCells();
    // Convert ArrayBuffer to string
    const decoder = new TextDecoder('utf-8');
    return decoder.decode(exportResult.data);
  });
  return result;
}

/**
 * Click on a column header to select the entire column
 * @param {import('puppeteer').Page} page
 * @param {number|string} col - Column index (0-based) or letter (e.g., "A")
 */
export async function clickColumnHeader(page, col) {
  const colIndex = typeof col === 'string' ? parseCellRef(col + '1').col : col;
  const canvasInfo = await getCanvasInfo(page);

  // Calculate column header position
  const pos = await page.evaluate(({ col }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;

    const ctx = window._appContext;
    if (!ctx || !ctx.app) {
      // Fallback to basic calculation
      return {
        x: HEADER_WIDTH + col * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2,
        y: HEADER_HEIGHT / 2, // Middle of header row
      };
    }

    const app = ctx.app;
    const renderer = app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    // Calculate X position from colWidths
    let offsetX = 0;
    for (let i = 0; i < col; i++) {
      offsetX += app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }
    const cellWidth = app.colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const cellX = Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) - Math.round(app.scrollX * zoomFactor);

    return {
      x: cellX + Math.round(cellWidth * zoomFactor) / 2,
      y: Math.round(HEADER_HEIGHT * zoomFactor) / 2, // Middle of header row
    };
  }, { col: colIndex });

  const x = canvasInfo.left + pos.x;
  const y = canvasInfo.top + pos.y;

  await page.mouse.click(x, y);
  await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    if (canvas) canvas.focus();
  });
  await sleep(100);
}

/**
 * Click on a row header to select the entire row
 * @param {import('puppeteer').Page} page
 * @param {number} row - Row index (0-based)
 */
export async function clickRowHeader(page, row) {
  const canvasInfo = await getCanvasInfo(page);

  // Calculate row header position
  const pos = await page.evaluate(({ row }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_ROW_HEIGHT = 24;

    const ctx = window._appContext;
    if (!ctx || !ctx.app) {
      // Fallback to basic calculation
      return {
        x: HEADER_WIDTH / 2, // Middle of header column
        y: HEADER_HEIGHT + row * DEFAULT_ROW_HEIGHT + DEFAULT_ROW_HEIGHT / 2,
      };
    }

    const app = ctx.app;
    const renderer = app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    // Calculate Y position from rowHeights
    let offsetY = 0;
    for (let i = 0; i < row; i++) {
      offsetY += app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
    }
    const cellHeight = app.rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const cellY = Math.round(HEADER_HEIGHT * zoomFactor) + Math.round(offsetY * zoomFactor) - Math.round(app.scrollY * zoomFactor);

    return {
      x: Math.round(HEADER_WIDTH * zoomFactor) / 2, // Middle of header column
      y: cellY + Math.round(cellHeight * zoomFactor) / 2,
    };
  }, { row });

  const x = canvasInfo.left + pos.x;
  const y = canvasInfo.top + pos.y;

  await page.mouse.click(x, y);
  await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    if (canvas) canvas.focus();
  });
  await sleep(100);
}
