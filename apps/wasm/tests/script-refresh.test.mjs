// Script-set formula refresh tests for Cells spreadsheet application
// Tests that formulas set via Luau scripts properly track dependencies and refresh

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
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

const tests = {
  'Script-set formula creates referenced cells': async (ctx) => {
    // This test verifies that when a script sets a formula like =B1+C1,
    // the referenced cells (B1, C1) are created even if they don't exist yet
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Execute script to set a formula referencing non-existent cells
    await typeScript(ctx.page, '/setCell("D1", "=B1+C1")');
    await sleep(500);

    // Click D1 and verify it has a formula (not an error)
    await clickCell(ctx.page, 'D1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=B1+C1', 'D1 should have formula =B1+C1');

    // Verify B1 and C1 exist by clicking them
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    // Cell should exist (no error clicking on it)

    await clickCell(ctx.page, 'C1');
    await sleep(100);
    // Cell should exist (no error clicking on it)
  },

  'Script-set formula refreshes when dependency changes': async (ctx) => {
    // CRITICAL TEST: This tests the main bug fix
    // When a script sets a formula, changing the dependency should update the formula result
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Step 1: Set a formula in A1 that depends on B1
    await typeScript(ctx.page, '/setCell("A1", "=B1*2")');
    await sleep(500);

    // Step 2: Set B1 to 10
    await typeScript(ctx.page, '/setCell("B1", 10)');
    await sleep(500);

    // Step 3: Verify A1 shows 20 (B1*2 = 10*2)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    let a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '20', 'A1 should show 20 after script sets B1=10');

    // Step 4: Change B1 to 50 via script
    await typeScript(ctx.page, '/setCell("B1", 50)');
    await sleep(500);

    // Step 5: Verify A1 updated to 100 (B1*2 = 50*2)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '100', 'A1 should show 100 after script sets B1=50');
  },

  'Script-set formula with SUM updates on range changes': async (ctx) => {
    // Test that SUM formulas set via script update when range values change
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Set values in B1, B2, B3
    await typeScript(ctx.page, '/setCell("B1", 10)');
    await sleep(300);
    await typeScript(ctx.page, '/setCell("B2", 20)');
    await sleep(300);
    await typeScript(ctx.page, '/setCell("B3", 30)');
    await sleep(300);

    // Set SUM formula in A1
    await typeScript(ctx.page, '/setCell("A1", "=SUM(B1:B3)")');
    await sleep(500);

    // Verify A1 = 60
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    let a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '60', 'A1 should show SUM(10,20,30)=60');

    // Change B2 to 100
    await typeScript(ctx.page, '/setCell("B2", 100)');
    await sleep(500);

    // Verify A1 updated to 140
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '140', 'A1 should show SUM(10,100,30)=140 after B2 changes');
  },

  'Script-set chained formulas update correctly': async (ctx) => {
    // Test chain of dependencies: A1 -> B1 -> C1
    // All set via script
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Set C1 = 5 (base value)
    await typeScript(ctx.page, '/setCell("C1", 5)');
    await sleep(300);

    // Set B1 = C1 * 2
    await typeScript(ctx.page, '/setCell("B1", "=C1*2")');
    await sleep(300);

    // Set A1 = B1 + 10
    await typeScript(ctx.page, '/setCell("A1", "=B1+10")');
    await sleep(500);

    // Verify chain: C1=5, B1=10, A1=20
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    let a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '20', 'A1 should be B1+10 = 10+10 = 20');

    await clickCell(ctx.page, 'B1');
    await sleep(200);
    let b1Value = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(b1Value, '10', 'B1 should be C1*2 = 5*2 = 10');

    // Change C1 to 100 - should cascade
    await typeScript(ctx.page, '/setCell("C1", 100)');
    await sleep(500);

    // Verify A1 updated: C1=100, B1=200, A1=210
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '210', 'A1 should cascade update to B1+10 = 200+10 = 210');

    await clickCell(ctx.page, 'B1');
    await sleep(200);
    b1Value = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(b1Value, '200', 'B1 should cascade update to C1*2 = 100*2 = 200');
  },

  'Script-set value triggers dependent formula via UI': async (ctx) => {
    // Set formula via UI, change value via script
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set formula via UI
    await setCellValue(ctx.page, 'A1', '=B1+5');
    await sleep(300);

    // Set B1 via script
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await typeScript(ctx.page, '/setCell("B1", 15)');
    await sleep(500);

    // Verify A1 = 20
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '20', 'A1 should be B1+5 = 15+5 = 20 after script sets B1');
  },

  'UI-set value triggers script-set formula': async (ctx) => {
    // Set formula via script, change value via UI
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Set formula via script
    await typeScript(ctx.page, '/setCell("A1", "=B1*3")');
    await sleep(500);

    // Set B1 via UI
    await setCellValue(ctx.page, 'B1', '7');
    await sleep(300);

    // Verify A1 = 21
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const a1Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(a1Value, '21', 'A1 should be B1*3 = 7*3 = 21 after UI sets B1');
  },
};

// Run all tests
runTests(tests);
