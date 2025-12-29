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
 * Calculate pixel position for a cell
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
  const { x, y } = cellToPixel(col, row, canvasInfo);

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
  const { x, y } = cellToPixel(col, row, canvasInfo);

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
  await page.keyboard.type(text, { delay: 50 });
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
  // Small delay before confirming to ensure all characters are processed
  await sleep(100);
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
    throw new Error(message || `Expected ${expected}, got ${actual}`);
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
