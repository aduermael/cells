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

    // Formula bar should show formatted value (like Google Sheets)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '15%', 'Formula bar should show formatted value 15%');
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

    // Formula bar should show formatted value (like Google Sheets)
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '$1,234.56', 'Formula bar should show formatted value $1,234.56');
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

    // Formula bar should show formatted value
    await clickCell(ctx.page, 'E1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '-25%', 'Formula bar should show formatted value -25%');
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

    // Formula bar should show formatted value
    await clickCell(ctx.page, 'F1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '$500', 'Formula bar should show formatted value $500');
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

    // Formula bar should show formatted value
    await clickCell(ctx.page, 'G1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '12.50%', 'Formula bar should show formatted value 12.50%');
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

  // ============================================================================
  // Format selector dropdown tests
  // ============================================================================

  'Format dropdown shows Percent for percentage input': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check the format dropdown shows "Percent"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Percent', 'Format dropdown should show Percent');
  },

  'Format dropdown shows Currency for currency input': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value
    await setCellValue(ctx.page, 'A1', '$100');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check the format dropdown shows "Currency"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'Format dropdown should show Currency');
  },

  'Format dropdown shows General for plain numbers': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a plain number
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check the format dropdown shows "General"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'General', 'Format dropdown should show General');
  },

  'Format dropdown shows General for text': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text
    await setCellValue(ctx.page, 'A1', 'Hello');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check the format dropdown shows "General"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'General', 'Format dropdown should show General for text');
  },

  // ============================================================================
  // Percentage literal in formulas tests
  // ============================================================================

  'Percentage literal in formula: 1000*15% equals 150': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter formula with percentage literal
    await setCellValue(ctx.page, 'A1', '=1000*15%');
    await sleep(200);

    // Verify result is 150
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '150', 'Formula =1000*15% should evaluate to 150');
  },

  'Percentage literal in formula: 50%+25% equals 0.75': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter formula with percentage addition
    await setCellValue(ctx.page, 'A1', '=50%+25%');
    await sleep(200);

    // Verify result is 0.75
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '0.75', 'Formula =50%+25% should evaluate to 0.75');
  },

  'Percentage literal: 15% as standalone formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter standalone percentage formula
    await setCellValue(ctx.page, 'A1', '=15%');
    await sleep(200);

    // Verify result is 0.15
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '0.15', 'Formula =15% should evaluate to 0.15');
  },

  'Percentage literal with cell reference: A1*15%': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set A1 to 100
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(100);

    // Enter formula with percentage
    await setCellValue(ctx.page, 'B1', '=A1*15%');
    await sleep(200);

    // Verify result is 15
    const display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '15', 'Formula =A1*15% with A1=100 should evaluate to 15');
  },
};

// Run all tests
runTests(tests);
