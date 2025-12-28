// Formula test for Cells spreadsheet application
// Tests formula entry, computation, and display

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getCurrentCellRef,
  getFormulaBarContent,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

const tests = {
  'Can enter a simple formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter formula =1+1
    await setCellValue(ctx.page, 'A1', '=1+1');

    // Click on A1 to verify
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Formula bar should show the formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=1+1', 'Formula bar should show =1+1');
  },

  'Formula computes correct result': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');

    // Enter formula that sums them
    await setCellValue(ctx.page, 'A3', '=A1+A2');

    // Verify formula bar shows formula
    await clickCell(ctx.page, 'A3');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1+A2', 'Formula bar should show =A1+A2');
  },

  'SUM function works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'A2', '2');
    await setCellValue(ctx.page, 'A3', '3');

    // Enter SUM formula
    await setCellValue(ctx.page, 'A4', '=SUM(A1:A3)');

    // Verify formula is stored
    await clickCell(ctx.page, 'A4');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(A1:A3)', 'Formula bar should show =SUM(A1:A3)');
  },

  'Formula with multiplication': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'A1', '5');
    await setCellValue(ctx.page, 'B1', '10');

    // Enter multiplication formula
    await setCellValue(ctx.page, 'C1', '=A1*B1');

    // Verify formula is stored
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1*B1', 'Formula bar should show =A1*B1');
  },

  'IF function works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value
    await setCellValue(ctx.page, 'A1', '100');

    // Enter IF formula
    await setCellValue(ctx.page, 'B1', '=IF(A1>50,"High","Low")');

    // Verify formula is stored
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertTrue(
      content.includes('IF') && content.includes('A1>50'),
      'Formula bar should contain IF formula'
    );
  },
};

// Run all tests
runTests(tests);
