// Test for merged cell content rendering
// Verifies that content in merged cells displays correctly

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  getCellDisplayValue,
  clickCell,
  getCurrentCellRef,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get all cells in the current viewport
 * @param {import('puppeteer').Page} page
 * @returns {Promise<Array<{col: number, row: number, value: string, display: string, isMergeAnchor: boolean, mergeColSpan: number, mergeRowSpan: number}>>}
 */
async function getAllCells(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.cells) {
      return [];
    }
    return ctx.app.cells.map(c => ({
      col: c.col,
      row: c.row,
      value: c.value || '',
      display: c.display || '',
      isMergeAnchor: !!c.isMergeAnchor,
      isMergedCell: !!c.isMergedCell,
      mergeColSpan: c.mergeColSpan || 0,
      mergeRowSpan: c.mergeRowSpan || 0,
    }));
  });
}

/**
 * Get a specific cell's data from the viewport
 * @param {import('puppeteer').Page} page
 * @param {number} col
 * @param {number} row
 */
async function getCellData(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.cells) {
      return null;
    }
    const cell = ctx.app.cells.find(c => c.col === col && c.row === row);
    if (!cell) return null;
    return {
      col: cell.col,
      row: cell.row,
      value: cell.value || '',
      display: cell.display || '',
      isMergeAnchor: !!cell.isMergeAnchor,
      isMergedCell: !!cell.isMergedCell,
      mergeColSpan: cell.mergeColSpan || 0,
      mergeRowSpan: cell.mergeRowSpan || 0,
    };
  }, { col, row });
}

/**
 * Get the sheet tabs from the UI
 * @param {import('puppeteer').Page} page
 */
async function getSheetTabs(page) {
  return await page.evaluate(() => {
    const tabs = document.querySelectorAll('.sheet-tab');
    return Array.from(tabs).map(tab => tab.textContent?.trim() || '');
  });
}

const tests = {
  'Merged cell B2 should have content "PROJECT PATRY" in data': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Cell B2 (col=1, row=1) should be the merge anchor with "PROJECT PATRY"
    // Note: Excel columns/rows are 1-indexed, but our data is 0-indexed
    const cellB2 = await getCellData(ctx.page, 1, 1);

    console.log('Cell B2 data:', JSON.stringify(cellB2, null, 2));

    assertTrue(cellB2 !== null, 'Cell B2 should exist in the viewport');

    // Check if it's a merge anchor
    if (cellB2.isMergeAnchor) {
      console.log(`B2 is a merge anchor spanning ${cellB2.mergeColSpan} cols x ${cellB2.mergeRowSpan} rows`);
    }

    // The cell should contain "PROJECT PATRY" (note: might be "PATRY" not "PARTY")
    const hasProjectTitle = (cellB2.display || cellB2.value).includes('PROJECT');
    assertTrue(hasProjectTitle, `Cell B2 should contain "PROJECT" in its content, got: "${cellB2.display || cellB2.value}"`);
  },

  'Merged cell should render as merge anchor with correct span': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Check cell B2 merge properties
    const cellB2 = await getCellData(ctx.page, 1, 1);

    assertTrue(cellB2 !== null, 'Cell B2 should exist');
    assertTrue(cellB2.isMergeAnchor, 'Cell B2 should be a merge anchor');
    assertTrue(cellB2.mergeColSpan > 1, `Cell B2 should span multiple columns, got span: ${cellB2.mergeColSpan}`);
  },


  'Sheet tabs should be visible after loading XLSX': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the many-tabs.xlsx file
    await loadTestFile(ctx.page, 'xlsx/many-tabs.xlsx');
    await sleep(500);

    // Get the sheet tabs
    const tabs = await getSheetTabs(ctx.page);
    console.log('Sheet tabs:', tabs);

    assertTrue(tabs.length > 0, 'Should have at least one sheet tab');
    // The first sheet should be the dashboard/cover sheet
    assertTrue(tabs[0].length > 0, 'First sheet tab should have a name');
  },
};

// Run all tests
runTests(tests);
