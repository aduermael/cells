// Formula test for Cells spreadsheet application
// Tests formula entry, computation, and display

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getCurrentCellRef,
  getFormulaBarContent,
  getCellDisplayValue,
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

  'Formula dependency updates when referenced cell changes': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Step 1: Set initial value in A1
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(200);

    // Step 2: Set formula =A1 in B1
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Step 3: Click B1 to ensure viewport cache is refreshed
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Step 4: Verify B1 shows the computed value (should be 10)
    let b1Value = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(b1Value, '10', 'B1 should initially show 10 (computed from =A1)');

    // Step 5: Modify A1 to a new value
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(300);

    // Step 6: Click B1 again to see if it was updated
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Step 7: Verify B1 is automatically updated with the new value
    // This is the critical test - B1 should now show 42
    b1Value = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(b1Value, '42', 'B1 should be updated to 42 after A1 changes');
  },

  'Chained formula dependencies update correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a chain: A1 -> B1 -> C1
    await setCellValue(ctx.page, 'A1', '5');
    await sleep(100);
    await setCellValue(ctx.page, 'B1', '=A1*2');  // B1 = 10
    await sleep(100);
    await setCellValue(ctx.page, 'C1', '=B1+3');  // C1 = 13
    await sleep(200);

    // Click on cells to ensure viewport is refreshed
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Verify initial chain values
    let b1Value = await getCellDisplayValue(ctx.page, 'B1');
    let c1Value = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(b1Value, '10', 'B1 should be 10 (=A1*2 where A1=5)');
    assertEqual(c1Value, '13', 'C1 should be 13 (=B1+3 where B1=10)');

    // Change A1 - should cascade through B1 to C1
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(300);

    // Click to refresh viewport
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Verify chain updated correctly
    b1Value = await getCellDisplayValue(ctx.page, 'B1');
    c1Value = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(b1Value, '200', 'B1 should update to 200 (=A1*2 where A1=100)');
    assertEqual(c1Value, '203', 'C1 should update to 203 (=B1+3 where B1=200)');
  },

  // ============================================================================
  // Formula Normalization Tests (Phase 5)
  // ============================================================================

  'Formula with whitespace after equals is normalized': async (ctx) => {
    // Entering "= A1" should display as "=A1" in formula bar
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // First, put a value in A1 so the formula has something to reference
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(100);

    // Enter formula with extra whitespace: "= A1"
    await setCellValue(ctx.page, 'B1', '= A1');
    await sleep(200);

    // Click B1 to verify
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Formula bar should show normalized formula "=A1" (no whitespace)
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1', 'Formula bar should show "=A1" (whitespace normalized)');
  },

  'Lowercase cell reference is normalized to uppercase': async (ctx) => {
    // Entering "=a1" should display as "=A1" in formula bar
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // First, put a value in A1 so the formula has something to reference
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(100);

    // Enter formula with lowercase: "=a1"
    await setCellValue(ctx.page, 'B1', '=a1');
    await sleep(200);

    // Click B1 to verify
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Formula bar should show normalized formula "=A1" (uppercase)
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1', 'Formula bar should show "=A1" (lowercase normalized to uppercase)');
  },

  'Function arguments whitespace is normalized': async (ctx) => {
    // Entering "=SUM( A1 , B1 )" should display as "=SUM(A1,B1)"
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up values
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter formula with extra whitespace in function args
    await setCellValue(ctx.page, 'C1', '=SUM( A1 , B1 )');
    await sleep(200);

    // Click C1 to verify
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    // Formula bar should show normalized formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(A1,B1)', 'Formula bar should show "=SUM(A1,B1)" (whitespace normalized)');
  },
};

// Run all tests
runTests(tests);
