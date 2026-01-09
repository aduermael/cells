// Format auto-detection tests for Cells spreadsheet application
// Tests that entering formatted values (%, $, dates, etc.) auto-applies formats

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  doubleClickCell,
  setCellValue,
  getFormulaBarContent,
  getCellEditorContent,
  getCellDisplayValue,
  assertEqual,
  sleep,
} from './helpers.mjs';

const tests = {
  // ============================================================================
  // Formula bar shows edit value tests (Excel-parity)
  // ============================================================================

  'Formula bar shows edit value for percentage cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value (this auto-formats to 15%)
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Cell should display formatted value
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Cell should display formatted value 15%');

    // Formula bar should show human-readable edit value (like Excel)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '15%', 'Formula bar should show edit value 15%');
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
  // In-cell editing shows edit value tests (Excel-parity)
  // ============================================================================

  'In-cell editing shows edit value for percentage cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value (this auto-formats to 15%)
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Cell should display formatted value
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'Cell should display formatted value 15%');

    // Double-click to enter edit mode - should show human-readable edit value
    await doubleClickCell(ctx.page, 'A1');
    await sleep(150);
    const editorContent = await getCellEditorContent(ctx.page);
    assertEqual(editorContent, '15%', 'In-cell editor should show edit value 15%');
  },

  'In-cell editing shows raw value for currency cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value (this auto-formats to $1,234.50)
    await setCellValue(ctx.page, 'A1', '$1234.50');
    await sleep(200);

    // Cell should display formatted value
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$1,234.50', 'Cell should display formatted value $1,234.50');

    // Double-click to enter edit mode - should show raw value
    await doubleClickCell(ctx.page, 'A1');
    await sleep(150);
    const editorContent = await getCellEditorContent(ctx.page);
    assertEqual(editorContent, '1234.5', 'In-cell editor should show raw value 1234.5, not formatted $1,234.50');
  },

  'In-cell editing shows formula for formula cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a formula
    await setCellValue(ctx.page, 'A1', '=10+5');
    await sleep(200);

    // Cell should display result
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15', 'Cell should display formula result 15');

    // Double-click to enter edit mode - should show formula
    await doubleClickCell(ctx.page, 'A1');
    await sleep(150);
    const editorContent = await getCellEditorContent(ctx.page);
    assertEqual(editorContent, '=10+5', 'In-cell editor should show formula =10+5');
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

    // Formula bar should show human-readable edit value (like Excel)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '15%', 'Formula bar should show edit value 15%');
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

    // Formula bar should show human-readable edit value
    await clickCell(ctx.page, 'E1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '-25%', 'Formula bar should show edit value -25%');
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

    // Formula bar should show human-readable edit value
    await clickCell(ctx.page, 'G1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '12.5%', 'Formula bar should show edit value 12.5%');
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

    // Formula bar should show human-readable edit value
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '12.50%', 'Formula bar should show edit value 12.50%');
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

    // Number format defaults to 2 decimal places (matches Excel)
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.57', 'Number format should show 2 decimal places by default');

    // Click decimal increase to go to 3 decimal places
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.568', 'After increase should show 3 decimal places');

    // Now click decimal decrease to go back to 2 decimal places
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.57', 'After decrease should show 2 decimal places');

    // Click decimal decrease to go to 1 decimal place
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.6', 'After second decrease should show 1 decimal place');

    // Click decimal decrease to go to 0 decimal places
    await ctx.page.click('#format-decimal-decrease');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1235', 'After third decrease should show 0 decimal places');
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

    // Number format defaults to 2 decimal places (matches Excel)
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.57', 'Number format should start with 2 decimal places');

    // Click decimal increase to go from 2 to 3 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.568', 'After first increase should show 3 decimal places');

    // Click decimal increase to go from 3 to 4 decimals
    await ctx.page.click('#format-decimal-increase');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1234.5678', 'After second increase should show 4 decimal places');
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

  // ============================================================================
  // Format inheritance tests (Phase 7 of format system refactor)
  // ============================================================================

  'Formula inherits currency format from referenced cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value in A1 (this auto-formats to currency)
    await setCellValue(ctx.page, 'A1', '$100');
    await sleep(200);

    // Verify A1 has currency format
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    let formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'A1 should have Currency format');

    // Enter formula in B1 that references A1
    await setCellValue(ctx.page, 'B1', '=A1*2');
    await sleep(200);

    // Verify B1 displays as currency (should inherit format)
    const display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '$200.00', 'Formula =A1*2 should inherit currency format and display as $200.00');

    // Verify B1's format dropdown shows Currency
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'B1 should inherit Currency format from A1');
  },

  'Formula inherits percentage format from referenced cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value in A1
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Verify A1 has percentage format
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    let formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Percent', 'A1 should have Percent format');

    // Enter formula in B1 that references A1
    await setCellValue(ctx.page, 'B1', '=A1+0.1');
    await sleep(200);

    // Verify B1 displays as percentage (should inherit format)
    // 0.15 + 0.1 = 0.25 → displayed as 25%
    const display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '25%', 'Formula =A1+0.1 should inherit percentage format and display as 25%');

    // Verify B1's format dropdown shows Percent
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Percent', 'B1 should inherit Percent format from A1');
  },

  'Explicit format on cell is not overridden by formula inheritance': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value in A1
    await setCellValue(ctx.page, 'A1', '$100');
    await sleep(200);

    // Click on B1 and explicitly set it to Number format before entering formula
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="NUMBER"]');
    await sleep(200);

    // Verify B1 now has Number format
    let formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Number', 'B1 should have Number format after explicit selection');

    // Now enter formula in B1 that references A1
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Verify B1 still has Number format (not inherited Currency)
    // The format inheritance should only apply when cell has GENERAL format
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Number', 'B1 should keep Number format (not inherit Currency) because it was explicitly set');

    // Verify display shows Number format, not Currency
    const display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '100.00', 'B1 should display as Number format (100.00), not Currency ($100.00)');
  },

  'Currency format wins over percentage in multi-ref formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value in A1
    await setCellValue(ctx.page, 'A1', '10%');
    await sleep(200);

    // Enter a currency value in B1
    await setCellValue(ctx.page, 'B1', '$100');
    await sleep(200);

    // Enter formula in C1 that references both A1 and B1
    await setCellValue(ctx.page, 'C1', '=A1+B1');
    await sleep(200);

    // Currency has higher priority than percentage, so C1 should inherit Currency
    // 0.1 + 100 = 100.1 displayed as $100.10
    const display = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(display, '$100.10', 'Formula =A1+B1 should inherit Currency format (higher priority than Percent)');

    // Verify C1's format dropdown shows Currency
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'C1 should inherit Currency format (higher priority than Percent)');
  },

  'Formula with no formatted references stays General': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter plain numbers in A1 and B1
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(200);
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(200);

    // Enter formula in C1 that references both
    await setCellValue(ctx.page, 'C1', '=A1+B1');
    await sleep(200);

    // C1 should stay General since neither A1 nor B1 have a format
    const display = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(display, '30', 'Formula result should display as 30 (General format)');

    // Verify C1's format dropdown shows General
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'General', 'C1 should stay General when references have no format');
  },

  'SUM function inherits format from range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter currency values in A1:A3
    await setCellValue(ctx.page, 'A1', '$10');
    await sleep(100);
    await setCellValue(ctx.page, 'A2', '$20');
    await sleep(100);
    await setCellValue(ctx.page, 'A3', '$30');
    await sleep(200);

    // Enter SUM formula in A4
    await setCellValue(ctx.page, 'A4', '=SUM(A1:A3)');
    await sleep(200);

    // A4 should inherit Currency format
    const display = await getCellDisplayValue(ctx.page, 'A4');
    assertEqual(display, '$60.00', 'SUM formula should inherit Currency format and display as $60.00');

    // Verify A4's format dropdown shows Currency
    await clickCell(ctx.page, 'A4');
    await sleep(100);
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'A4 should inherit Currency format from range');
  },

  // ============================================================================
  // Edit Value tests (Phase 2 of edit-value-excel-parity plan)
  // ============================================================================

  'Viewport API returns editValue for percentage cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a percentage value
    await setCellValue(ctx.page, 'A1', '15%');
    await sleep(200);

    // Get cell data from the app context (same method as getCellDisplayValue)
    const cellData = await ctx.page.evaluate(() => {
      if (window._appContext && window._appContext.app && window._appContext.app.cells) {
        return window._appContext.app.cells.find(c => c.col === 0 && c.row === 0);
      }
      return null;
    });

    if (!cellData) {
      throw new Error('A1 cell not found in viewport');
    }

    // Verify editValue is present and correct
    assertEqual(cellData.editValue, '15%', 'Viewport should return editValue "15%" for percentage cell');
    assertEqual(cellData.display, '15%', 'Display should be "15%"');
  },

  'Viewport API returns editValue for currency cells (raw number)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a currency value
    await setCellValue(ctx.page, 'A1', '$1234.50');
    await sleep(200);

    // Get cell data from the app context
    const cellData = await ctx.page.evaluate(() => {
      if (window._appContext && window._appContext.app && window._appContext.app.cells) {
        return window._appContext.app.cells.find(c => c.col === 0 && c.row === 0);
      }
      return null;
    });

    if (!cellData) {
      throw new Error('A1 cell not found in viewport');
    }

    // For currency, editValue should be raw number (like Excel)
    assertEqual(cellData.editValue, '1234.5', 'Viewport should return editValue "1234.5" for currency cell');
    assertEqual(cellData.display, '$1,234.50', 'Display should be "$1,234.50"');
  },
};

// Run all tests
runTests(tests);
