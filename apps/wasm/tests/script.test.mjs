// Luau script execution tests for Cells spreadsheet application
// Tests script entry via formula bar with '/' prefix

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

/**
 * Type a script command directly into the formula bar
 * Scripts start with '/' and are executed on Enter
 */
async function typeScript(page, script) {
  // Focus the formula bar
  await page.click('#formula-display');
  await sleep(100);

  // Type the script (with / prefix)
  await page.keyboard.type(script, { delay: 20 });
  await sleep(100);

  // Press Enter to execute
  await page.keyboard.press('Enter');
  await sleep(300);
}

/**
 * Check if the formula bar container has the script-mode class
 */
async function hasScriptModeClass(page) {
  return await page.evaluate(() => {
    const container = document.getElementById('formula-input-container');
    return container ? container.classList.contains('script-mode') : false;
  });
}

const tests = {
  'Script mode activates when typing /': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click somewhere to ensure canvas has focus
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Focus the formula bar
    await ctx.page.click('#formula-display');
    await sleep(100);

    // Type / to enter script mode
    await ctx.page.keyboard.type('/');
    await sleep(100);

    // Check that script-mode class is added
    const hasClass = await hasScriptModeClass(ctx.page);
    assertEqual(hasClass, true, 'Formula bar should have script-mode class');

    // Cancel to reset
    await ctx.page.keyboard.press('Escape');
    await sleep(100);

    // Verify script-mode class is removed
    const hasClassAfter = await hasScriptModeClass(ctx.page);
    assertEqual(hasClassAfter, false, 'Script mode class should be removed after cancel');
  },

  'Can execute cellSet script': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on a cell first
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Execute a script to set a cell value
    await typeScript(ctx.page, '/cellSet("B2", 42)');

    // Wait for script to execute and refresh
    await sleep(500);

    // Click on B2 and verify the value
    await clickCell(ctx.page, 'B2');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '42', 'B2 should have value 42 from script');
  },

  'Can execute multiple cellSet commands': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Execute script with multiple commands (using semicolons)
    await typeScript(ctx.page, '/cellSet("A1", 10) cellSet("A2", 20) cellSet("A3", 30)');
    await sleep(500);

    // Verify all values
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '10', 'A1 should be 10');

    await clickCell(ctx.page, 'A2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '20', 'A2 should be 20');

    await clickCell(ctx.page, 'A3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '30', 'A3 should be 30');
  },

  'Script with string value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Execute script to set a string value
    await typeScript(ctx.page, '/cellSet("C1", "hello world")');
    await sleep(500);

    // Verify the string value
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'hello world', 'C1 should have string value');
  },

  'Script does not modify cell when cancelled': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set initial value
    await setCellValue(ctx.page, 'D1', 'original');
    await sleep(200);

    // Start typing a script but cancel
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    await ctx.page.click('#formula-display');
    await sleep(100);
    await ctx.page.keyboard.type('/cellSet("D1", "modified")');
    await sleep(100);

    // Cancel without executing
    await ctx.page.keyboard.press('Escape');
    await sleep(200);

    // Verify original value is preserved
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'original', 'D1 should still have original value');
  },

  'cellGet returns cell value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up initial value
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(200);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Execute script that reads and writes using cellGet
    // cellGet returns a cell object with .value property
    await typeScript(ctx.page, '/local c = cellGet("A1") if c then cellSet("B1", c.value * 2) end');
    await sleep(500);

    // Verify B1 has the doubled value
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '200', 'B1 should have doubled value 200');
  },
};

// Run all tests
runTests(tests);
