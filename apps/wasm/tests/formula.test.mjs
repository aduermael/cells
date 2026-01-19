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
    await sleep(300);

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
  // These tests verify that cross-sheet references (e.g., =Sheet2!B5) work
  // correctly. CRDT operations now include sheet context to ensure cells are
  // created on the correct sheet.
  // ============================================================================

  'Cross-sheet reference formula parses and evaluates': async (ctx) => {
    // Test that =Sheet2!B5 syntax works correctly
    // Note: Use B5 instead of B27 to stay within default viewport (row 27 would be ~684px which exceeds 600px height)
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // The default sheet is "Sheet1". Add a second sheet.
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Now we should be on Sheet2. Enter a value in B5.
    await clickCell(ctx.page, 'B5');
    await sleep(100);
    await setCellValue(ctx.page, 'B5', '42');
    await sleep(200);

    // Switch back to Sheet1 by clicking its tab
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Now on Sheet1, enter a formula that references Sheet2!B5
    await setCellValue(ctx.page, 'A1', '=Sheet2!B5');
    await sleep(300);

    // Click A1 to verify formula bar shows the cross-sheet reference
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=Sheet2!B5', 'Formula bar should show =Sheet2!B5');

    // The cross-sheet reference should now evaluate correctly with the CRDT fix
    const displayValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(displayValue, '42', 'A1 should display 42 (the value from Sheet2!B5)');
  },

  'Cross-sheet reference with SUM function': async (ctx) => {
    // Test =SUM(Sheet2!A1:A3) syntax
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter values in A1:A3 on Sheet2
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

    // The cross-sheet SUM should evaluate correctly
    const displayValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(displayValue, '60', 'A1 should display 60 (sum of Sheet2!A1:A3)');
  },

  // ============================================================================
  // Cross-Sheet Dependency Tests (Phase 3)
  // These tests verify that cross-sheet dependencies trigger re-evaluation
  // when source cells change.
  // ============================================================================

  'Cross-sheet dependency updates when source cell changes': async (ctx) => {
    // BUG TEST: Cross-sheet formulas should update when their source changes
    // Currently, the dependency graph only tracks within-sheet dependencies
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Set Sheet2!A1 = 10
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(200);

    // Switch to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // In Sheet1!B1, enter formula =Sheet2!A1
    await setCellValue(ctx.page, 'B1', '=Sheet2!A1');
    await sleep(300);

    // Verify Sheet1!B1 shows 10
    const initialValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(initialValue, '10', 'B1 should initially show 10 from Sheet2!A1');

    // Switch to Sheet2 and change A1 to 20
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 1) {
        tabs[1].click();
      }
    });
    await sleep(300);

    await setCellValue(ctx.page, 'A1', '20');
    await sleep(200);

    // Switch back to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Verify Sheet1!B1 updates to 20
    const updatedValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(updatedValue, '20', 'B1 should update to 20 when Sheet2!A1 changes');
  },

  'Cross-sheet range dependency updates when source cell changes': async (ctx) => {
    // Test that cross-sheet range formulas update when ANY cell in the range changes
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Set Sheet2!A1:A3 = [1, 2, 3]
    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'A2', '2');
    await setCellValue(ctx.page, 'A3', '3');
    await sleep(200);

    // Switch to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // In Sheet1!B1, enter formula =SUM(Sheet2!A1:A3)
    await setCellValue(ctx.page, 'B1', '=SUM(Sheet2!A1:A3)');
    await sleep(300);

    // Verify Sheet1!B1 shows 6 (1+2+3)
    const initialValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(initialValue, '6', 'B1 should initially show 6 (sum of 1+2+3)');

    // Switch to Sheet2 and change A2 to 10
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 1) {
        tabs[1].click();
      }
    });
    await sleep(300);

    await setCellValue(ctx.page, 'A2', '10');
    await sleep(200);

    // Switch back to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Verify Sheet1!B1 updates to 14 (1+10+3)
    const updatedValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(updatedValue, '14', 'B1 should update to 14 when Sheet2!A2 changes (1+10+3)');
  },

  // ============================================================================
  // Cross-Sheet Formula Round-Trip Tests (Phase 15)
  // These tests verify that cross-sheet formulas survive re-submission without
  // changes (e.g., focusing formula bar and pressing Enter).
  // ============================================================================

  'Cross-sheet formula survives re-submission': async (ctx) => {
    // BUG: Re-submitting a cross-sheet formula without changes causes #REF!
    // Reproduction:
    // 1. Create Sheet2, put 42 in Sheet2!B1
    // 2. In Sheet1!A1, enter =Sheet2!B1
    // 3. Select A1, focus formula bar, press Enter (re-submit)
    // 4. A1 should still show 42, not #REF!
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Now on Sheet2, enter 42 in B1
    await setCellValue(ctx.page, 'B1', '42');
    await sleep(200);

    // Switch back to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // In Sheet1!A1, enter =Sheet2!B1
    await setCellValue(ctx.page, 'A1', '=Sheet2!B1');
    await sleep(300);

    // Verify A1 shows 42 initially
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'A1 should initially show 42 from Sheet2!B1');

    // Select A1 and verify formula bar
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const formulaBeforeResubmit = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBeforeResubmit, '=Sheet2!B1', 'Formula bar should show =Sheet2!B1');

    // Now re-submit the formula by clicking the formula display and pressing Enter
    // This simulates the user focusing the formula bar and confirming it unchanged
    // NOTE: Must click #formula-display (the contenteditable) not #formula-bar (the container)
    await ctx.page.click('#formula-display');
    await sleep(200);
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Verify A1 still shows 42 (not #REF!)
    const valueAfterResubmit = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(valueAfterResubmit, '42', 'A1 should still show 42 after re-submitting the formula (not #REF!)');

    // Verify formula bar still shows the correct formula
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const formulaAfterResubmit = await getFormulaBarContent(ctx.page);
    assertEqual(formulaAfterResubmit, '=Sheet2!B1', 'Formula bar should still show =Sheet2!B1 after re-submit');

    // Verify the formula still updates when the source cell changes
    // Switch to Sheet2
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 1) {
        tabs[1].click();
      }
    });
    await sleep(300);

    // Change B1 to 99
    await setCellValue(ctx.page, 'B1', '99');
    await sleep(200);

    // Switch back to Sheet1
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // Verify A1 updates to 99
    const valueAfterSourceChange = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(valueAfterSourceChange, '99', 'A1 should update to 99 when Sheet2!B1 changes');
  },

  'Cross-sheet range formula survives re-submission': async (ctx) => {
    // Similar test but for range references: =SUM(Sheet2!A1:A3)
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter values in Sheet2!A1:A3
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

    // Enter =SUM(Sheet2!A1:A3)
    await setCellValue(ctx.page, 'A1', '=SUM(Sheet2!A1:A3)');
    await sleep(300);

    // Verify A1 shows 60 initially
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '60', 'A1 should initially show 60 (sum of 10+20+30)');

    // Re-submit the formula by clicking formula display and pressing Enter
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    await ctx.page.click('#formula-display');
    await sleep(200);
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Verify A1 still shows 60
    const valueAfterResubmit = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(valueAfterResubmit, '60', 'A1 should still show 60 after re-submitting the range formula');

    // Verify formula bar still shows the correct formula
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const formulaAfterResubmit = await getFormulaBarContent(ctx.page);
    assertEqual(formulaAfterResubmit, '=SUM(Sheet2!A1:A3)', 'Formula bar should still show =SUM(Sheet2!A1:A3)');
  },

  'Multi-hop cross-sheet dependencies update correctly': async (ctx) => {
    // Test chain: Sheet3!A1 -> Sheet2!A1 -> Sheet1!A1
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add Sheet2
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Add Sheet3
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Now on Sheet3, set A1 = 5
    await setCellValue(ctx.page, 'A1', '5');
    await sleep(200);

    // Switch to Sheet2 (second tab)
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 1) {
        tabs[1].click();
      }
    });
    await sleep(300);

    // In Sheet2!A1, enter =Sheet3!A1 * 2 (should be 10)
    await setCellValue(ctx.page, 'A1', '=Sheet3!A1*2');
    await sleep(300);

    // Verify Sheet2!A1 shows 10
    const sheet2Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(sheet2Value, '10', 'Sheet2!A1 should show 10 (Sheet3!A1=5 * 2)');

    // Switch to Sheet1 (first tab)
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    // In Sheet1!A1, enter =Sheet2!A1 + 1 (should be 11)
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1+1');
    await sleep(300);

    // Verify Sheet1!A1 shows 11
    const sheet1InitialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(sheet1InitialValue, '11', 'Sheet1!A1 should show 11 (Sheet2!A1=10 + 1)');

    // Switch to Sheet3 and change A1 to 10
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 2) {
        tabs[2].click();
      }
    });
    await sleep(300);

    await setCellValue(ctx.page, 'A1', '10');
    await sleep(300);

    // Verify Sheet3!A1 shows 10
    const sheet3Value = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(sheet3Value, '10', 'Sheet3!A1 should show 10');

    // Switch to Sheet2 and verify it updated
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 1) {
        tabs[1].click();
      }
    });
    await sleep(300);

    const sheet2UpdatedValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(sheet2UpdatedValue, '20', 'Sheet2!A1 should update to 20 (Sheet3!A1=10 * 2)');

    // Switch to Sheet1 and verify it updated
    await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      if (tabs.length > 0) {
        tabs[0].click();
      }
    });
    await sleep(300);

    const sheet1UpdatedValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(sheet1UpdatedValue, '21', 'Sheet1!A1 should update to 21 (Sheet2!A1=20 + 1)');
  },
};

// Run tests
runTests(tests);
