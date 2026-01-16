// Auto-fit column width tests for Cells spreadsheet application
// Tests that double-clicking on column resize handle auto-fits the column width

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  assertEqual,
  assertTrue,
  sleep,
  clickCell,
  setCellValue,
  getCanvasInfo,
} from './helpers.mjs';

const HEADER_WIDTH = 50;
const HEADER_HEIGHT = 24;
const DEFAULT_COL_WIDTH = 100;

/**
 * Get the column boundary X position (right edge of column)
 */
async function getColumnBoundaryX(page, col) {
  return await page.evaluate((col) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const colWidths = ctx.app.colWidths;
    const HEADER_WIDTH = 50;
    const DEFAULT_COL_WIDTH = 100;

    // Sum up columns 0 through col (inclusive) to get right boundary
    let x = HEADER_WIDTH;
    for (let i = 0; i <= col; i++) {
      x += colWidths.get(i) ?? DEFAULT_COL_WIDTH;
    }

    return x;
  }, col);
}

/**
 * Get current column width
 */
async function getColumnWidth(page, col) {
  return await page.evaluate((col) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;
    return ctx.app.colWidths.get(col) ?? 100;
  }, col);
}

/**
 * Double-click at a specific position
 */
async function doubleClickAt(page, x, y) {
  await page.mouse.click(x, y, { clickCount: 2 });
  await sleep(200);
}

/**
 * Right-click to open context menu at position
 */
async function rightClickAt(page, x, y) {
  await page.mouse.click(x, y, { button: 'right' });
  await sleep(100);
}

const tests = {
  'Double-click on column resize handle auto-fits column width to content': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Put some content in column A that's narrower than default width
    await setCellValue(ctx.page, 'A1', 'Hi');
    await sleep(100);

    // Get initial column width (should be default 100)
    const initialWidth = await getColumnWidth(ctx.page, 0);
    assertEqual(initialWidth, DEFAULT_COL_WIDTH, 'Initial column width should be 100');

    // Get canvas position
    const canvasInfo = await getCanvasInfo(ctx.page);

    // Calculate position of resize handle (right edge of column A header)
    const boundaryX = HEADER_WIDTH + DEFAULT_COL_WIDTH;
    const handleX = canvasInfo.left + boundaryX - 2; // 2px before the boundary
    const headerY = canvasInfo.top + HEADER_HEIGHT / 2;

    // Double-click on the resize handle
    await doubleClickAt(ctx.page, handleX, headerY);
    await sleep(200);

    // Get new column width - should be smaller since "Hi" is short
    const newWidth = await getColumnWidth(ctx.page, 0);

    // The auto-fit width should be significantly smaller than the default 100
    // but at least the minimum (20) and large enough for "Hi" + padding
    assertTrue(newWidth !== null, 'Should get column width');
    assertTrue(newWidth < DEFAULT_COL_WIDTH, `Width (${newWidth}) should be less than default (${DEFAULT_COL_WIDTH})`);
    assertTrue(newWidth >= 20, `Width (${newWidth}) should be at least minimum (20)`);
  },

  'Auto-fit expands column for long content': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Put some long content in column A
    const longText = 'This is a very long text that should make the column wider';
    await setCellValue(ctx.page, 'A1', longText);
    await sleep(100);

    // Get canvas position
    const canvasInfo = await getCanvasInfo(ctx.page);

    // Calculate position of resize handle (right edge of column A header)
    const boundaryX = HEADER_WIDTH + DEFAULT_COL_WIDTH;
    const handleX = canvasInfo.left + boundaryX - 2;
    const headerY = canvasInfo.top + HEADER_HEIGHT / 2;

    // Double-click on the resize handle
    await doubleClickAt(ctx.page, handleX, headerY);
    await sleep(200);

    // Get new column width - should be larger
    const newWidth = await getColumnWidth(ctx.page, 0);

    // The auto-fit width should be larger than the default 100 for long text
    assertTrue(newWidth !== null, 'Should get column width');
    assertTrue(newWidth > DEFAULT_COL_WIDTH, `Width (${newWidth}) should be greater than default (${DEFAULT_COL_WIDTH}) for long text`);
    assertTrue(newWidth <= 500, `Width (${newWidth}) should not exceed maximum (500)`);
  },

  'Auto-fit via context menu works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Put short content in column A
    await setCellValue(ctx.page, 'A1', 'X');
    await sleep(100);

    // Get canvas position
    const canvasInfo = await getCanvasInfo(ctx.page);

    // Right-click on column A header to open context menu
    const colHeaderX = canvasInfo.left + HEADER_WIDTH + DEFAULT_COL_WIDTH / 2;
    const colHeaderY = canvasInfo.top + HEADER_HEIGHT / 2;
    await rightClickAt(ctx.page, colHeaderX, colHeaderY);

    // Click "Auto-fit column width" option
    const menuItemClicked = await ctx.page.evaluate(() => {
      const menuItems = document.querySelectorAll('.context-menu-item');
      for (const item of menuItems) {
        if (item.textContent && item.textContent.includes('Auto-fit')) {
          item.click();
          return true;
        }
      }
      return false;
    });

    assertTrue(menuItemClicked, 'Should find and click Auto-fit menu item');
    await sleep(200);

    // Get new column width
    const newWidth = await getColumnWidth(ctx.page, 0);

    // Should be smaller than default for just "X"
    assertTrue(newWidth !== null, 'Should get column width');
    assertTrue(newWidth < DEFAULT_COL_WIDTH, `Width (${newWidth}) should be less than default (${DEFAULT_COL_WIDTH})`);
  },

  'Auto-fit considers multiple cells in column': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Put content in multiple cells in column A
    // First cell is short, second cell is long
    await setCellValue(ctx.page, 'A1', 'Hi');
    await setCellValue(ctx.page, 'A2', 'This is a longer text');
    await sleep(100);

    // Get canvas position
    const canvasInfo = await getCanvasInfo(ctx.page);

    // Double-click on the resize handle
    const boundaryX = HEADER_WIDTH + DEFAULT_COL_WIDTH;
    const handleX = canvasInfo.left + boundaryX - 2;
    const headerY = canvasInfo.top + HEADER_HEIGHT / 2;
    await doubleClickAt(ctx.page, handleX, headerY);
    await sleep(200);

    // Get new column width
    const newWidth = await getColumnWidth(ctx.page, 0);

    // Width should be based on the longest cell content
    // "This is a longer text" needs more width than "Hi"
    assertTrue(newWidth !== null, 'Should get column width');
    assertTrue(newWidth > 50, `Width (${newWidth}) should be larger than minimum needed for short text`);
  },

  'Auto-fit considers header text width': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Leave column A empty (no cell content)
    // The auto-fit should still maintain minimum width for header "A"

    // Get canvas position
    const canvasInfo = await getCanvasInfo(ctx.page);

    // Double-click on the resize handle
    const boundaryX = HEADER_WIDTH + DEFAULT_COL_WIDTH;
    const handleX = canvasInfo.left + boundaryX - 2;
    const headerY = canvasInfo.top + HEADER_HEIGHT / 2;
    await doubleClickAt(ctx.page, handleX, headerY);
    await sleep(200);

    // Get new column width
    const newWidth = await getColumnWidth(ctx.page, 0);

    // Even with no content, should maintain minimum width for header
    assertTrue(newWidth !== null, 'Should get column width');
    assertTrue(newWidth >= 20, `Width (${newWidth}) should be at least minimum (20) for header`);
    // Header "A" is small, so width should be relatively narrow
    assertTrue(newWidth <= 50, `Width (${newWidth}) should be small for just header "A"`);
  },
};

// Run all tests
runTests(tests);
