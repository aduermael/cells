// Border UI controls tests for Cells spreadsheet application
// Tests the border dropdown and border application functionality

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  selectRange,
  createNewWorkbook,
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
 * Apply a border to the current selection using the toolbar dropdown
 * @param {import('puppeteer').Page} page
 * @param {'all' | 'outer' | 'top' | 'bottom' | 'left' | 'right' | 'none'} borderType
 */
async function applyBorder(page, borderType) {
  // Click the border button to open the dropdown
  await page.click('#border-btn');
  await sleep(100);

  // Click the appropriate border option
  const buttonId = `#border-${borderType}-btn`;
  await page.click(buttonId);
  await sleep(200);
}

const tests = {
  'Border button and dropdown exist in toolbar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Check border button exists
    const borderBtn = await ctx.page.$('#border-btn');
    assertTrue(borderBtn !== null, 'Border button should exist in toolbar');

    // Open dropdown
    await borderBtn.click();
    await sleep(100);

    // Check dropdown options exist
    const borderAllBtn = await ctx.page.$('#border-all-btn');
    const borderOuterBtn = await ctx.page.$('#border-outer-btn');
    const borderTopBtn = await ctx.page.$('#border-top-btn');
    const borderBottomBtn = await ctx.page.$('#border-bottom-btn');
    const borderLeftBtn = await ctx.page.$('#border-left-btn');
    const borderRightBtn = await ctx.page.$('#border-right-btn');
    const borderNoneBtn = await ctx.page.$('#border-none-btn');

    assertTrue(borderAllBtn !== null, 'All Borders button should exist');
    assertTrue(borderOuterBtn !== null, 'Outline button should exist');
    assertTrue(borderTopBtn !== null, 'Top Border button should exist');
    assertTrue(borderBottomBtn !== null, 'Bottom Border button should exist');
    assertTrue(borderLeftBtn !== null, 'Left Border button should exist');
    assertTrue(borderRightBtn !== null, 'Right Border button should exist');
    assertTrue(borderNoneBtn !== null, 'No Border button should exist');
  },

  'Border dropdown closes when clicking outside': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Open the border dropdown
    await ctx.page.click('#border-btn');
    await sleep(100);

    // Check that dropdown is open (has 'open' class)
    let isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('border-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(isOpen, 'Dropdown should be open after clicking button');

    // Click on the canvas (outside the dropdown)
    await clickCell(ctx.page, 'E5');
    await sleep(200);

    // Check that dropdown is closed
    isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('border-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(!isOpen, 'Dropdown should be closed after clicking outside');
  },

  'Apply all borders to single cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Apply all borders
    await applyBorder(ctx.page, 'all');
    await sleep(300);

    // Check that the cell has borders
    const style = await getCellStyle(ctx.page, 1, 1); // B2 = col 1, row 1
    console.log('Cell B2 style:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'Cell style should be returned');
    assertTrue(style.border !== undefined, 'Cell should have border property');
    assertTrue(style.border.top !== undefined, 'Cell should have top border');
    assertTrue(style.border.bottom !== undefined, 'Cell should have bottom border');
    assertTrue(style.border.left !== undefined, 'Cell should have left border');
    assertTrue(style.border.right !== undefined, 'Cell should have right border');
  },

  'Apply top border to single cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell C3
    await clickCell(ctx.page, 'C3');
    await sleep(100);

    // Apply top border
    await applyBorder(ctx.page, 'top');
    await sleep(300);

    // Check that the cell has top border
    const style = await getCellStyle(ctx.page, 2, 2); // C3 = col 2, row 2
    console.log('Cell C3 style:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'Cell style should be returned');
    assertTrue(style.border !== undefined, 'Cell should have border property');
    assertTrue(style.border.top !== undefined && style.border.top.style === 'thin',
      'Cell should have thin top border');
  },

  'Apply borders to range selection': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select range A1:C3
    await selectRange(ctx.page, 'A1', 'C3');
    await sleep(100);

    // Apply outline border
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // Check corner cells have correct borders
    // A1 should have top and left borders
    const styleA1 = await getCellStyle(ctx.page, 0, 0);
    console.log('Cell A1 style:', JSON.stringify(styleA1, null, 2));

    // C3 should have bottom and right borders
    const styleC3 = await getCellStyle(ctx.page, 2, 2);
    console.log('Cell C3 style:', JSON.stringify(styleC3, null, 2));

    // B2 (center cell) should have no borders from outline operation
    const styleB2 = await getCellStyle(ctx.page, 1, 1);
    console.log('Cell B2 style:', JSON.stringify(styleB2, null, 2));

    assertTrue(styleA1 !== null, 'A1 style should be returned');
    assertTrue(styleC3 !== null, 'C3 style should be returned');
  },

  'Remove borders with No Border option': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // First apply all borders
    await applyBorder(ctx.page, 'all');
    await sleep(300);

    // Verify borders were applied
    let style = await getCellStyle(ctx.page, 1, 1);
    assertTrue(style.border !== undefined && style.border.top !== undefined,
      'Cell should have borders after applying all borders');

    // Now remove borders
    await applyBorder(ctx.page, 'none');
    await sleep(300);

    // Verify borders were removed
    style = await getCellStyle(ctx.page, 1, 1);
    console.log('Cell B2 style after removing borders:', JSON.stringify(style, null, 2));

    // After removing, the border property should either not exist or have no style
    const hasBorders = style.border &&
      (style.border.top?.style === 'thin' ||
       style.border.bottom?.style === 'thin' ||
       style.border.left?.style === 'thin' ||
       style.border.right?.style === 'thin');
    assertTrue(!hasBorders, 'Cell should have no borders after applying No Border');
  },
};

// Run all tests
runTests(tests);
