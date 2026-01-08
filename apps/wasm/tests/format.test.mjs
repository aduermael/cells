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
  // ============================================================================
  // Formula bar shows raw value tests (Phase 1 of format system refactor)
  // ============================================================================

  'Formula bar shows raw value for percentage cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value (this auto-formats to 15%)
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Cell should display formatted value
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Cell should display formatted value 15%');

    // But formula bar should show raw value (like Excel)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '0.15', 'Formula bar should show raw value 0.15, not formatted 15%');
  },

  'Formula bar shows raw value for currency cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value (this auto-formats to $1,234.50)
    await setCellValue(ctx.page, 'A1', '$1234.50');
    await sleep(200);

    // Cell should display formatted value
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$1,234.50', 'Cell should display formatted value $1,234.50');

    // But formula bar should show raw value (like Excel)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '1234.5', 'Formula bar should show raw value 1234.5, not formatted $1,234.50');
  },

  'Formula bar shows formula for formula cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a formula
    await setCellValue(ctx.page, 'A1', '=10+5');
    await sleep(200);

    // Cell should display result
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15', 'Cell should display formula result 15');

    // Formula bar should show the formula (not the result)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=10+5', 'Formula bar should show formula =10+5');
  },

  // ============================================================================
  // Auto-format detection tests
  // ============================================================================

  'Percentage input auto-detects format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Check the displayed value shows percentage
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Cell should display 15%');

    // Formula bar should show raw value (like Excel)
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

    // Formula bar should show raw value (like Excel)
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

    // Formula bar should show raw value
    await clickCell(ctx.page, 'E1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '-0.25', 'Formula bar should show raw value -0.25');
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

    // Formula bar should show raw value
    await clickCell(ctx.page, 'F1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '500', 'Formula bar should show raw value 500');
  },

  'Percentage with 1 decimal preserves format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter percentage with 1 decimal place
    await setCellValue(ctx.page, 'G1', '12.5%');
    await sleep(200);

    // Check the displayed value preserves 1 decimal place
    const display = await getCellDisplayValue(ctx.page, 'G1');
    assertEqual(display, '12.5%', 'Cell should display 12.5% (preserving 1 decimal)');

    // Formula bar should show raw value
    await clickCell(ctx.page, 'G1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '0.125', 'Formula bar should show raw value 0.125');
  },

  'Percentage with 2 decimals preserves format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter percentage with 2 decimal places (use A1 - within visible viewport)
    await setCellValue(ctx.page, 'A1', '12.50%');
    await sleep(200);

    // Check the displayed value preserves 2 decimal places
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '12.50%', 'Cell should display 12.50% (preserving 2 decimals)');

    // Formula bar should show raw value
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '0.125', 'Formula bar should show raw value 0.125');
  },

  'Currency with 1 decimal preserves format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter currency with 1 decimal place (use A1 - within visible viewport)
    await setCellValue(ctx.page, 'A1', '$99.9');
    await sleep(200);

    // Check the displayed value preserves 1 decimal place
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$99.9', 'Cell should display $99.9 (preserving 1 decimal)');
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

  // ============================================================================
  // Decimal +/- button tests
  // ============================================================================

  'Decimal decrease button reduces decimal places': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number with 4 decimal places and format as Number
    await setCellValue(ctx.page, 'A1', '1234.5678');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown and select Number format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="NUMBER"]');
    await sleep(200);

    // Number format defaults to 0 decimal places, showing rounded value (no separator)
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1235', 'Number format should show 0 decimal places by default');

    // Click decimal increase to go to 1 decimal place
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.6', 'After increase should show 1 decimal place');

    // Click decimal increase to go to 2 decimal places
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.57', 'After second increase should show 2 decimal places');

    // Now click decimal decrease to go back to 1 decimal place
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.6', 'After decrease should show 1 decimal place');

    // Click decimal decrease to go back to 0 decimal places
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1235', 'After second decrease should show 0 decimal places');
  },

  'Decimal increase button adds decimal places': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '1234.5678');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown and select Number format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="NUMBER"]');
    await sleep(200);

    // Number format defaults to 0 decimal places (no separator)
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1235', 'Number format should start with 0 decimal places');

    // Click decimal increase to go from 0 to 1 decimal
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.6', 'After first increase should show 1 decimal place');

    // Click decimal increase to go from 1 to 2 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.57', 'After second increase should show 2 decimal places');

    // Click decimal increase to go from 2 to 3 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.568', 'After third increase should show 3 decimal places');

    // Click decimal increase to go from 3 to 4 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.5678', 'After fourth increase should show 4 decimal places');
  },

  // ============================================================================
  // Empty cell format tests
  // ============================================================================

  'Empty cell can have format applied': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on empty cell B5 (where B and row 5 don't exist yet)
    await clickCell(ctx.page, 'B5');
    await sleep(100);

    // Open format dropdown and select Currency format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="CURRENCY"]');
    await sleep(200);

    // Verify the format dropdown shows "Currency"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'Format dropdown should show Currency after selection');

    // Now type a value - it should be formatted as currency
    await setCellValue(ctx.page, 'B5', '100');
    await sleep(200);

    // Check the displayed value shows currency format
    const display = await getCellDisplayValue(ctx.page, 'B5');
    assertEqual(display, '$100.00', 'Value entered after format selection should display as $100.00');
  },

  // ============================================================================
  // Formula cell format tests
  // ============================================================================

  'Formula cell can have percentage format applied': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a formula that results in 0.15
    await setCellValue(ctx.page, 'A1', '=0.15');
    await sleep(200);

    // Check initial display is unformatted
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '0.15', 'Formula result should initially display as 0.15');

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check the format dropdown shows "General"
    let formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'General', 'Format dropdown should initially show General');

    // Open format dropdown and select Percent format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="PERCENTAGE"]');
    await sleep(300);

    // Check format dropdown now shows "Percent"
    formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Percent', 'Format dropdown should show Percent after selection');

    // Check the displayed value shows percentage format
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Formula result should display as 15% after format change');

    // Verify formula bar still shows the formula
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=0.15', 'Formula bar should still show =0.15');
  },

  'Formula cell can have currency format applied': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a formula that results in 1234.5
    await setCellValue(ctx.page, 'A1', '=1000+234.5');
    await sleep(200);

    // Check initial display is unformatted
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.5', 'Formula result should initially display as 1234.5');

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown and select Currency format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="CURRENCY"]');
    await sleep(300);

    // Check the displayed value shows currency format (2 decimal places by default)
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$1,234.50', 'Formula result should display as $1,234.50 (2 decimals, with separator)');
  },

  // ============================================================================
  // Currency dropdown selection tests
  // ============================================================================

  'Currency dropdown selects EUR format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open currency dropdown and select EUR
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-currency="EUR"]');
    await sleep(300);

    // Check the displayed value shows EUR format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '€100.00', 'Value should display as €100.00 with EUR currency');

    // Check the format dropdown shows "Currency"
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'Format dropdown should show Currency');

    // Check the currency dropdown shows EUR symbol
    const currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '€', 'Currency dropdown should show € symbol');
  },

  'Currency dropdown selects GBP format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open currency dropdown and select GBP
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-currency="GBP"]');
    await sleep(300);

    // Check the displayed value shows GBP format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '£100.00', 'Value should display as £100.00 with GBP currency');

    // Check the currency dropdown shows GBP symbol
    const currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '£', 'Currency dropdown should show £ symbol');
  },

  'Currency dropdown selects JPY format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '1000');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open currency dropdown and select JPY
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-currency="JPY"]');
    await sleep(300);

    // Check the displayed value shows JPY format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '¥1,000.00', 'Value should display as ¥1,000.00 with JPY currency');

    // Check the currency dropdown shows JPY symbol
    const currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '¥', 'Currency dropdown should show ¥ symbol');
  },

  'Decimal change preserves currency type (EUR)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '100.5678');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open currency dropdown and select EUR
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-currency="EUR"]');
    await sleep(300);

    // Check the displayed value shows EUR format with 2 decimals
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '€100.57', 'Value should display as €100.57 with EUR currency (2 decimals)');

    // Click decimal increase to go to 3 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    // Verify it's still EUR with 3 decimals
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '€100.568', 'After decimal increase, should still be EUR with 3 decimals');

    // Check the currency dropdown still shows EUR symbol
    let currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '€', 'Currency dropdown should still show € symbol after decimal change');

    // Click decimal decrease twice to go to 1 decimal
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    // Verify it's still EUR with 1 decimal
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '€100.6', 'After decimal decrease, should still be EUR with 1 decimal');

    // Check the currency dropdown still shows EUR symbol
    currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '€', 'Currency dropdown should still show € symbol');
  },

  'Decimal change preserves currency type (GBP)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '50.1234');
    await sleep(200);

    // Click on the cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open currency dropdown and select GBP
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-currency="GBP"]');
    await sleep(300);

    // Check the displayed value shows GBP format with 2 decimals
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '£50.12', 'Value should display as £50.12 with GBP currency');

    // Click decimal decrease to go to 1 decimal
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    // Verify it's still GBP with 1 decimal
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '£50.1', 'After decimal decrease, should still be GBP with 1 decimal');

    // Check the currency dropdown still shows GBP symbol
    const currencyLabel = await ctx.page.$eval('#currency-dropdown-label', el => el.textContent);
    assertEqual(currencyLabel, '£', 'Currency dropdown should still show £ symbol after decimal change');
  },
};

// Run all tests
runTests(tests);
