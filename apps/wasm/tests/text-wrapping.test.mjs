// Text wrapping tests for Cells spreadsheet application
// Tests the wrap text toggle button and text wrapping display

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  createNewWorkbook,
  typeInCell,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the effective style for a cell at a given position
 * @param {import('puppeteer').Page} page
 * @param {number} col
 * @param {number} row
 * @returns {Promise<object>}
 */
async function getCellStyle(page, col, row) {
  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return null;
    }
    try {
      const style = await ctx.app.dataSource.client.getEffectiveCellStyle(col, row);
      return style;
    } catch (e) {
      console.error('Error getting cell style:', e);
      return null;
    }
  }, { col, row });
}

/**
 * Toggle wrap text for the current selection using the toolbar button
 * @param {import('puppeteer').Page} page
 */
async function toggleWrapText(page) {
  await page.click('#style-wrap-text-btn');
  await sleep(200);
}

/**
 * Check if the wrap text button is active
 * @param {import('puppeteer').Page} page
 * @returns {Promise<boolean>}
 */
async function isWrapTextButtonActive(page) {
  return await page.evaluate(() => {
    const btn = document.getElementById('style-wrap-text-btn');
    return btn && btn.classList.contains('active');
  });
}

const tests = {
  'Wrap text button exists in toolbar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Check wrap text button exists
    const wrapTextBtn = await ctx.page.$('#style-wrap-text-btn');
    assertTrue(wrapTextBtn !== null, 'Wrap text button should exist in toolbar');
  },

  'Wrap text button toggles on click': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell A1
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Button should not be active initially
    let isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(!isActive, 'Wrap text button should not be active initially');

    // Toggle wrap text
    await toggleWrapText(ctx.page);

    // Button should now be active
    isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(isActive, 'Wrap text button should be active after clicking');

    // Toggle again
    await toggleWrapText(ctx.page);

    // Button should be inactive again
    isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(!isActive, 'Wrap text button should be inactive after second click');
  },

  'Wrap text applies to cell style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Get initial style (wrapText is omitted from JSON when false, so it's undefined)
    let style = await getCellStyle(ctx.page, 1, 1); // B2 = col 1, row 1
    assertTrue(!style?.wrapText, 'wrapText should initially be falsy');

    // Toggle wrap text on
    await toggleWrapText(ctx.page);

    // Check style
    style = await getCellStyle(ctx.page, 1, 1);
    assertEqual(style?.wrapText, true, 'wrapText should be true after toggling');

    // Toggle wrap text off
    await toggleWrapText(ctx.page);

    // Check style again (wrapText will be omitted from JSON when false)
    style = await getCellStyle(ctx.page, 1, 1);
    assertTrue(!style?.wrapText, 'wrapText should be falsy after second toggle');
  },

  'Wrap text button reflects cell style when selecting': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell A1 and enable wrap text
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await toggleWrapText(ctx.page);

    // Verify button is active
    let isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(isActive, 'Button should be active on A1');

    // Select a different cell (B1) without wrap text
    await clickCell(ctx.page, 'B1');
    await sleep(100);

    // Button should not be active
    isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(!isActive, 'Button should not be active on B1');

    // Select A1 again
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Button should be active again
    isActive = await isWrapTextButtonActive(ctx.page);
    assertTrue(isActive, 'Button should be active again when returning to A1');
  },

  'Long text wraps when wrapText is enabled': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Enter long text in A1
    const longText = 'This is a very long text that should wrap within the cell boundaries when wrap text is enabled';
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await typeInCell(ctx.page, longText);
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Select A1 and enable wrap text
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await toggleWrapText(ctx.page);

    // Verify wrapText is applied
    const style = await getCellStyle(ctx.page, 0, 0);
    assertEqual(style?.wrapText, true, 'wrapText should be true');

    // Note: Visual testing of text wrapping would require canvas pixel analysis
    // which is more complex. Here we just verify the style is applied correctly.
  },
};

// Run all tests
runTests(tests);
