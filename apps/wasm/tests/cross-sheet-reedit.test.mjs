// Cross-Sheet Formula Re-Edit Test (Bug A from plan)
// Tests that re-editing a cross-sheet formula without changes preserves the formula
//
// Bug A scenario:
// 1. Enter `=Sheet2!A1` in cell A1 (Sheet1)
// 2. Observe: correct value from Sheet2!A1 is displayed
// 3. Click on A1 to edit, don't change anything, press Enter
// 4. Observe: cell should still show the correct value (NOT #VALUE!)

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
 * Click a sheet tab by index (0-based)
 */
async function clickSheetTab(page, index) {
  await page.evaluate((idx) => {
    const tabs = document.querySelectorAll('.sheet-tab');
    if (tabs[idx]) {
      tabs[idx].click();
    }
  }, index);
  await sleep(300);
}

const tests = {
  // ============================================================================
  // Bug A: Cross-sheet formula becomes #VALUE! on re-edit
  // ============================================================================
  // This test verifies that clicking on a cross-sheet formula cell, not changing
  // anything, and pressing Enter does NOT cause the formula to become invalid.
  // ============================================================================

  'Cross-sheet formula survives re-edit without changes': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Set Sheet2!A1 = 42
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // Switch to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Enter cross-sheet formula in A1
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1');
    await sleep(300);

    // Verify the formula bar and displayed value are correct
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const initialFormula = await getFormulaBarContent(ctx.page);
    assertEqual(initialFormula, '=Sheet2!A1', 'Initial formula should be =Sheet2!A1');

    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'Initial value should be 42');

    // === BUG A TEST: Re-edit without changes ===
    // Double-click to enter edit mode
    await ctx.page.evaluate(() => {
      const canvas = document.getElementById('grid');
      const rect = canvas.getBoundingClientRect();
      const HEADER_WIDTH = 50;
      const HEADER_HEIGHT = 24;
      const DEFAULT_COL_WIDTH = 100;
      const DEFAULT_ROW_HEIGHT = 24;
      // Click on A1 (col 0, row 0)
      const x = rect.left + HEADER_WIDTH + DEFAULT_COL_WIDTH / 2;
      const y = rect.top + HEADER_HEIGHT + DEFAULT_ROW_HEIGHT / 2;
      canvas.dispatchEvent(new MouseEvent('dblclick', {
        bubbles: true,
        clientX: x,
        clientY: y
      }));
    });
    await sleep(200);

    // Press Enter without making any changes
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Click on A1 again to check the result
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // The formula should still be =Sheet2!A1
    const afterFormula = await getFormulaBarContent(ctx.page);
    assertEqual(afterFormula, '=Sheet2!A1', 'After re-edit, formula should still be =Sheet2!A1');

    // The value should still be 42 (NOT #VALUE!)
    const afterValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(afterValue, '42', 'After re-edit, value should still be 42 (NOT #VALUE!)');
  },

  'Cross-sheet SUM formula survives re-edit without changes': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Set values in Sheet2
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await setCellValue(ctx.page, 'A3', '30');
    await sleep(200);

    // Switch to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Enter cross-sheet SUM formula in B1
    await setCellValue(ctx.page, 'B1', '=SUM(Sheet2!A1:A3)');
    await sleep(300);

    // Verify initial state
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const initialFormula = await getFormulaBarContent(ctx.page);
    assertEqual(initialFormula, '=SUM(Sheet2!A1:A3)', 'Initial formula should be =SUM(Sheet2!A1:A3)');

    const initialValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(initialValue, '60', 'Initial value should be 60');

    // Re-edit without changes
    await ctx.page.evaluate(() => {
      const canvas = document.getElementById('grid');
      const rect = canvas.getBoundingClientRect();
      const HEADER_WIDTH = 50;
      const HEADER_HEIGHT = 24;
      const DEFAULT_COL_WIDTH = 100;
      const DEFAULT_ROW_HEIGHT = 24;
      // Click on B1 (col 1, row 0)
      const x = rect.left + HEADER_WIDTH + DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
      const y = rect.top + HEADER_HEIGHT + DEFAULT_ROW_HEIGHT / 2;
      canvas.dispatchEvent(new MouseEvent('dblclick', {
        bubbles: true,
        clientX: x,
        clientY: y
      }));
    });
    await sleep(200);

    // Press Enter without making any changes
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Click on B1 again to check the result
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // The formula should still be correct
    const afterFormula = await getFormulaBarContent(ctx.page);
    assertEqual(afterFormula, '=SUM(Sheet2!A1:A3)', 'After re-edit, formula should still be =SUM(Sheet2!A1:A3)');

    // The value should still be 60
    const afterValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(afterValue, '60', 'After re-edit, value should still be 60 (NOT #VALUE!)');
  },

  'Mixed formula (same-sheet + cross-sheet) survives re-edit': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Set Sheet2!A1 = 100
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(200);

    // Switch to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Set Sheet1!B1 = 50
    await setCellValue(ctx.page, 'B1', '50');
    await sleep(200);

    // Enter mixed formula in C1: =B1+Sheet2!A1
    await setCellValue(ctx.page, 'C1', '=B1+Sheet2!A1');
    await sleep(300);

    // Verify initial state
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const initialFormula = await getFormulaBarContent(ctx.page);
    assertEqual(initialFormula, '=B1+Sheet2!A1', 'Initial formula should be =B1+Sheet2!A1');

    const initialValue = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(initialValue, '150', 'Initial value should be 150');

    // Re-edit without changes
    await ctx.page.evaluate(() => {
      const canvas = document.getElementById('grid');
      const rect = canvas.getBoundingClientRect();
      const HEADER_WIDTH = 50;
      const HEADER_HEIGHT = 24;
      const DEFAULT_COL_WIDTH = 100;
      const DEFAULT_ROW_HEIGHT = 24;
      // Click on C1 (col 2, row 0)
      const x = rect.left + HEADER_WIDTH + 2 * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
      const y = rect.top + HEADER_HEIGHT + DEFAULT_ROW_HEIGHT / 2;
      canvas.dispatchEvent(new MouseEvent('dblclick', {
        bubbles: true,
        clientX: x,
        clientY: y
      }));
    });
    await sleep(200);

    // Press Enter without making any changes
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Click on C1 again to check the result
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    // The formula should still be correct
    const afterFormula = await getFormulaBarContent(ctx.page);
    assertEqual(afterFormula, '=B1+Sheet2!A1', 'After re-edit, formula should still be =B1+Sheet2!A1');

    // The value should still be 150
    const afterValue = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(afterValue, '150', 'After re-edit, value should still be 150 (NOT #VALUE!)');
  },
};

// Run tests
runTests(tests);
