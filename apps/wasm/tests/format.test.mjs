// Format auto-detection tests for Cells spreadsheet application
// Tests that entering formatted values (%, $, dates, etc.) auto-applies formats

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  assertEqual,
  sleep,
} from './helpers.mjs';

const tests = {
  'Percentage input auto-detects format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Check the displayed value shows percentage
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Cell should display 15%');

    // Check the formula bar shows the raw value (0.15)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '0.15', 'Formula bar should show raw value 0.15');
  },

  'Currency input auto-detects format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value
    await setCellValue(ctx.page, 'B1', '$1,234.56');
    await sleep(200);

    // Check the displayed value shows currency
    const display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '$1,234.56', 'Cell should display $1,234.56');

    // Check the formula bar shows the raw value
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '1234.56', 'Formula bar should show raw value 1234.56');
  },

  'Plain numbers are not auto-formatted': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a plain number
    await setCellValue(ctx.page, 'C1', '42');
    await sleep(200);

    // Check the displayed value is the same
    const display = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(display, '42', 'Cell should display 42');

    // Check the formula bar shows the same value
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '42', 'Formula bar should show 42');
  },

  'Text values are preserved as-is': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text
    await setCellValue(ctx.page, 'D1', 'Hello World');
    await sleep(200);

    // Check the displayed value
    const display = await getCellDisplayValue(ctx.page, 'D1');
    assertEqual(display, 'Hello World', 'Cell should display Hello World');

    // Check the formula bar
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, 'Hello World', 'Formula bar should show Hello World');
  },

  'Negative percentage works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a negative percentage
    await setCellValue(ctx.page, 'E1', '-25%');
    await sleep(200);

    // Check the displayed value
    const display = await getCellDisplayValue(ctx.page, 'E1');
    assertEqual(display, '-25%', 'Cell should display -25%');

    // Check the formula bar
    await clickCell(ctx.page, 'E1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '-0.25', 'Formula bar should show -0.25');
  },

  'Currency without decimals works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter currency without decimals
    await setCellValue(ctx.page, 'F1', '$500');
    await sleep(200);

    // Check the displayed value
    const display = await getCellDisplayValue(ctx.page, 'F1');
    assertEqual(display, '$500', 'Cell should display $500');

    // Check the formula bar
    await clickCell(ctx.page, 'F1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '500', 'Formula bar should show 500');
  },

  'Percentage with decimals works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter percentage with decimals
    await setCellValue(ctx.page, 'G1', '12.5%');
    await sleep(200);

    // Check the displayed value
    const display = await getCellDisplayValue(ctx.page, 'G1');
    assertEqual(display, '12.50%', 'Cell should display 12.50%');

    // Check the formula bar
    await clickCell(ctx.page, 'G1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '0.125', 'Formula bar should show 0.125');
  },

  'Formula results are not auto-formatted': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values for formula
    await setCellValue(ctx.page, 'A1', '100');
    await setCellValue(ctx.page, 'A2', '200');
    await sleep(100);

    // Enter a formula
    await setCellValue(ctx.page, 'A3', '=A1+A2');
    await sleep(200);

    // Check the displayed value shows formula result
    const display = await getCellDisplayValue(ctx.page, 'A3');
    assertEqual(display, '300', 'Cell should display formula result 300');

    // Check the formula bar shows the formula
    await clickCell(ctx.page, 'A3');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=A1+A2', 'Formula bar should show formula');
  },
};

// Run all tests
runTests(tests);
