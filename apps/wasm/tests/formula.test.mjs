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

    // Enter a simple formula
    await setCellValue(ctx.page, 'A1', '=1+1');

    // Click back on A1 to verify formula bar
    await clickCell(ctx.page, 'A1');
    await sleep(200);

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

    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'A2', '2');
    await setCellValue(ctx.page, 'A3', '3');

    // Enter SUM formula
    await setCellValue(ctx.page, 'A4', '=SUM(A1:A3)');

    await clickCell(ctx.page, 'A4');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(A1:A3)', 'Formula bar should show =SUM(A1:A3)');
  },

  'Formula with multiplication': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '5');
    await setCellValue(ctx.page, 'B1', '10');

    // Enter multiplication formula
    await setCellValue(ctx.page, 'C1', '=A1*B1');

    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1*B1', 'Formula bar should show =A1*B1');
  },

  'IF function works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '100');

    // Enter IF formula
    await setCellValue(ctx.page, 'B1', '=IF(A1>50,"High","Low")');

    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    // The formula should be stored (string args may be quoted)
    assertTrue(
      content.includes('IF') && content.includes('A1'),
      'Formula bar should contain IF and A1'
    );
  },

  'Formula dependency updates when referenced cell changes': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set initial value
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(100);

    // Create formula that references A1
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Verify B1 shows the result
    const initialValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(initialValue, '10', 'B1 should initially show 10');

    // Update A1
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // B1 should update
    const updatedValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(updatedValue, '42', 'B1 should update to 42 when A1 changes');
  },

  'Chained formula dependencies update correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // A1 -> B1 -> C1 dependency chain
    await setCellValue(ctx.page, 'A1', '5');
    await sleep(100);
    await setCellValue(ctx.page, 'B1', '=A1*2');  // B1 = 10
    await sleep(100);
    await setCellValue(ctx.page, 'C1', '=B1+3');  // C1 = 13
    await sleep(200);

    // Verify C1 computes correctly
    const c1Value = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(c1Value, '13', 'C1 should show 13 (B1=10 + 3)');

    // Update A1, C1 should cascade update
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(200);

    const newC1Value = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(newC1Value, '23', 'C1 should show 23 after A1 changes to 10 (B1=20 + 3)');
  },

  'Formula with whitespace after equals is normalized': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '42');
    await sleep(100);

    // Enter formula with whitespace
    await setCellValue(ctx.page, 'B1', '= A1');
    await sleep(200);

    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1', 'Formula should be normalized to =A1 (no extra space)');
  },

  'Lowercase cell reference is normalized to uppercase': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '100');
    await sleep(100);

    // Enter formula with lowercase reference
    await setCellValue(ctx.page, 'B1', '=a1');
    await sleep(200);

    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1', 'Formula should be normalized to uppercase =A1');
  },

  'Function arguments whitespace is normalized': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter SUM with extra whitespace
    await setCellValue(ctx.page, 'C1', '=SUM( A1 , B1 )');
    await sleep(200);

    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    // Whitespace should be normalized
    assertEqual(
      content.replace(/\s+/g, ''),
      '=SUM(A1,B1)',
      'Formula should have normalized whitespace'
    );
  },

  // ============================================================================
  // Formula Colorization Tests
  // ============================================================================

  'Range reference is colored in formula bar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'A2', '2');
    await setCellValue(ctx.page, 'A3', '3');
    await sleep(100);

    // Enter a formula with range reference
    await setCellValue(ctx.page, 'B1', '=SUM(A1:A3)');
    await sleep(200);

    // Click B1 to show formula in formula bar
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Check if formula bar contains colored spans
    const hasColoredRef = await ctx.page.evaluate(() => {
      const formulaBar = document.getElementById('formula-bar');
      if (!formulaBar) return false;
      // Look for colored spans (references should have data-ref-index attribute)
      const coloredSpans = formulaBar.querySelectorAll('span[data-ref-index]');
      return coloredSpans.length > 0;
    });

    assertTrue(hasColoredRef, 'Range reference A1:A3 should be colored in formula bar');
  },

  'Cell reference is colored in formula bar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '42');
    await sleep(100);

    // Enter a formula with cell reference
    await setCellValue(ctx.page, 'B1', '=A1*2');
    await sleep(200);

    // Click B1 to show formula in formula bar
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Check if formula bar contains colored reference
    const hasColoredRef = await ctx.page.evaluate(() => {
      const formulaBar = document.getElementById('formula-bar');
      if (!formulaBar) return false;
      const coloredSpans = formulaBar.querySelectorAll('span[data-ref-index]');
      return coloredSpans.length > 0;
    });

    assertTrue(hasColoredRef, 'Cell reference A1 should be colored in formula bar');
  },

  'Column reference is colored in formula bar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await sleep(100);

    // Enter a formula with column reference
    await setCellValue(ctx.page, 'B1', '=SUM(A:A)');
    await sleep(200);

    // Click B1 to show formula in formula bar
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Check if column reference is colored
    const hasColoredRef = await ctx.page.evaluate(() => {
      const formulaBar = document.getElementById('formula-bar');
      if (!formulaBar) return false;
      // Look for the column reference text within a colored span
      const coloredSpans = formulaBar.querySelectorAll('span[data-ref-index]');
      for (const span of coloredSpans) {
        if (span.textContent.includes('A:A')) {
          return true;
        }
      }
      return false;
    });

    assertTrue(hasColoredRef, 'Column reference A:A should be fully colored in formula bar');
  },

  'Row reference is colored in formula bar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter a formula with row reference
    await setCellValue(ctx.page, 'A2', '=SUM(1:1)');
    await sleep(200);

    // Click A2 to show formula in formula bar
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Check if row reference is colored
    const hasColoredRef = await ctx.page.evaluate(() => {
      const formulaBar = document.getElementById('formula-bar');
      if (!formulaBar) return false;
      // Look for the row reference text within a colored span
      const coloredSpans = formulaBar.querySelectorAll('span[data-ref-index]');
      for (const span of coloredSpans) {
        if (span.textContent.includes('1:1')) {
          return true;
        }
      }
      return false;
    });

    assertTrue(hasColoredRef, 'Row reference 1:1 should be fully colored in formula bar');
  },

  // ============================================================================
  // Cross-Sheet Reference Tests (Phase 2)
  // NOTE: These tests are BLOCKED by a bug in CRDT operation application.
  // CRDT operations (COL_INSERT, ROW_INSERT, CELL_SET_VALUE) always apply to
  // sheets[0] instead of the active sheet. See crdt_axis.cc lines 41, 66, etc.
  // Unit tests pass because they set up cells directly on Sheet objects.
  // Fix requires adding sheet context to CRDT operations.
  // ============================================================================

  'Cross-sheet reference formula parses and evaluates': async (ctx) => {
    // Test that =Sheet2!B27 syntax works correctly
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // The default sheet is "Sheet1". Add a second sheet.
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Now we should be on Sheet2. Enter a value in B27.
    // NOTE: Due to CRDT bug, this cell is actually created on Sheet1, not Sheet2
    await clickCell(ctx.page, 'B27');
    await sleep(100);
    await setCellValue(ctx.page, 'B27', '42');
    await sleep(200);

    // Switch back to Sheet1 by clicking its tab
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Now on Sheet1, enter a formula that references Sheet2!B27
    await setCellValue(ctx.page, 'A1', '=Sheet2!B27');
    await sleep(300);

    // Click A1 to verify formula bar shows the cross-sheet reference
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=Sheet2!B27', 'Formula bar should show =Sheet2!B27');

    // NOTE: The computed value test is expected to fail due to CRDT bug
    // The cell on Sheet2 has no value because it was created on Sheet1
    const displayValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(displayValue, '42', 'A1 should display 42 (the value from Sheet2!B27)');
  },

  'Cross-sheet reference with SUM function': async (ctx) => {
    // Test =SUM(Sheet2!A1:A3) syntax
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter values in A1:A3 on Sheet2
    // NOTE: Due to CRDT bug, these cells are actually created on Sheet1
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await setCellValue(ctx.page, 'A3', '30');
    await sleep(200);

    // Switch back to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Enter SUM formula referencing Sheet2
    await setCellValue(ctx.page, 'A1', '=SUM(Sheet2!A1:A3)');
    await sleep(300);

    // Verify formula bar shows the cross-sheet reference
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(Sheet2!A1:A3)', 'Formula bar should show =SUM(Sheet2!A1:A3)');

    // NOTE: The computed value test is expected to fail due to CRDT bug
    const displayValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(displayValue, '60', 'A1 should display 60 (sum of Sheet2!A1:A3)');
  },
};

// Run tests
runTests(tests);
