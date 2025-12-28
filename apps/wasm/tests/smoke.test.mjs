// Smoke test for Cells spreadsheet application
// Tests basic functionality: page load, cell selection, value entry

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  createNewWorkbook,
  clickCell,
  setCellValue,
  getCurrentCellRef,
  getFormulaBarContent,
  getWorkbookName,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

const tests = {
  'Page loads successfully': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Check that canvas exists and is visible
    const canvas = await ctx.page.$('#grid');
    assertTrue(canvas, 'Canvas element should exist');
  },

  'Default workbook name is displayed': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const name = await getWorkbookName(ctx.page);
    assertTrue(name, 'Workbook name should be displayed');
  },

  'Can select a cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(200);

    // Check that cell reference is updated
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Cell reference should show B2');
  },

  'Can enter a value in a cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter value in cell A1
    await setCellValue(ctx.page, 'A1', '42');

    // Click on A1 again to verify
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Check formula bar content
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '42', 'Formula bar should show 42');
  },

  'Can enter text in a cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text in cell A1
    await setCellValue(ctx.page, 'A1', 'Hello World');

    // Click on A1 again to verify
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Check formula bar content
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'Hello World', 'Formula bar should show Hello World');
  },

  'Navigate between cells with arrow keys': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at A1
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Press Right arrow
    await ctx.page.keyboard.press('ArrowRight');
    await sleep(100);

    let cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B1', 'Should move to B1 after right arrow');

    // Press Down arrow
    await ctx.page.keyboard.press('ArrowDown');
    await sleep(100);

    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Should move to B2 after down arrow');
  },
};

// Run all tests
runTests(tests);
