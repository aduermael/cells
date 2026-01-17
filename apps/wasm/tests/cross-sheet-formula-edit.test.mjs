// Cross-Sheet Formula Editing Test
// Tests Excel-like behavior: when editing a formula, clicking cells on other sheets
// should insert cross-sheet references while maintaining formula edit mode.
//
// Phase 4 of UI, Style, and Formula Bugs plan

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

/**
 * Get the number of sheet tabs
 */
async function getSheetTabCount(page) {
  return await page.evaluate(() => {
    return document.querySelectorAll('.sheet-tab').length;
  });
}

/**
 * Check if the formula bar is in editing mode
 */
async function isFormulaBarEditing(page) {
  return await page.evaluate(() => {
    const formulaBar = document.getElementById('formula-bar');
    if (!formulaBar) return false;
    // Check if formula-display has contenteditable="true" or is focused
    const display = document.getElementById('formula-display');
    if (!display) return false;
    return display.contentEditable === 'true' || document.activeElement === display;
  });
}

/**
 * Start typing a formula in the selected cell (partial entry, don't press Enter)
 */
async function startFormula(page, cellRef, formulaStart) {
  await clickCell(page, cellRef);
  await sleep(100);
  // Type the partial formula
  await page.keyboard.type(formulaStart, { delay: 50 });
  await sleep(100);
}

/**
 * Get the current formula text being edited
 */
async function getEditingFormulaText(page) {
  return await page.evaluate(() => {
    // Check cell editor first
    const cellDisplay = document.getElementById('cell-display');
    if (cellDisplay && document.activeElement === cellDisplay) {
      return cellDisplay.textContent || '';
    }
    // Check formula bar
    const formulaDisplay = document.getElementById('formula-display');
    if (formulaDisplay) {
      return formulaDisplay.textContent || '';
    }
    return '';
  });
}

const tests = {
  // ============================================================================
  // Phase 4: Cross-Sheet Formula Editing Tests
  // ============================================================================
  // These tests verify Excel-like behavior where users can:
  // 1. Start editing a formula on one sheet
  // 2. Navigate to another sheet
  // 3. Click cells to insert cross-sheet references
  // 4. Return to the original sheet and commit the formula
  // ============================================================================

  'Can start editing formula on Sheet1, switch to Sheet2, and click cell to insert cross-sheet reference': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter a value in Sheet2!B5
    await setCellValue(ctx.page, 'B5', '42');
    await sleep(200);

    // Switch back to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Start editing a formula in A1 (but don't commit yet)
    await startFormula(ctx.page, 'A1', '=');
    await sleep(200);

    // Switch to Sheet2 tab - this should preserve formula edit mode
    await clickSheetTab(ctx.page, 1);

    // Click on B5 to insert a cross-sheet reference
    await clickCell(ctx.page, 'B5');
    await sleep(300);

    // The formula should now be "=Sheet2!B5"
    const formulaText = await getEditingFormulaText(ctx.page);
    assertEqual(formulaText, '=Sheet2!B5', 'Formula should show cross-sheet reference =Sheet2!B5');

    // Press Enter to commit
    await ctx.page.keyboard.press('Enter');
    await sleep(500);

    // Should return to Sheet1 (the origin sheet)
    // Click A1 and verify the formula
    await clickSheetTab(ctx.page, 0);
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=Sheet2!B5', 'Committed formula should be =Sheet2!B5');

    // Verify the value displays correctly
    const displayValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(displayValue, '42', 'A1 should display 42 from Sheet2!B5');
  },

  'Formula with SUM: insert cross-sheet range by clicking cells on another sheet': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter values in Sheet2
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await setCellValue(ctx.page, 'A3', '30');
    await sleep(200);

    // Switch back to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Start editing a SUM formula in B1
    await startFormula(ctx.page, 'B1', '=SUM(');
    await sleep(200);

    // Switch to Sheet2
    await clickSheetTab(ctx.page, 1);

    // Click on A1 to start the range
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Shift+click on A3 to complete the range
    const { col: colA3, row: rowA3 } = { col: 0, row: 2 }; // A3
    const canvasInfo = await ctx.page.evaluate(() => {
      const canvas = document.getElementById('grid');
      const rect = canvas.getBoundingClientRect();
      return { left: rect.left, top: rect.top };
    });
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;
    const x = canvasInfo.left + HEADER_WIDTH + colA3 * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
    const y = canvasInfo.top + HEADER_HEIGHT + rowA3 * DEFAULT_ROW_HEIGHT + DEFAULT_ROW_HEIGHT / 2;

    await ctx.page.keyboard.down('Shift');
    await ctx.page.mouse.click(x, y);
    await ctx.page.keyboard.up('Shift');
    await sleep(200);

    // Type closing paren
    await ctx.page.keyboard.type(')', { delay: 50 });
    await sleep(100);

    // The formula should be "=SUM(Sheet2!A1:A3)"
    const formulaText = await getEditingFormulaText(ctx.page);
    assertEqual(formulaText, '=SUM(Sheet2!A1:A3)', 'Formula should show =SUM(Sheet2!A1:A3)');

    // Press Enter to commit
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Switch to Sheet1 and verify
    await clickSheetTab(ctx.page, 0);
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(Sheet2!A1:A3)', 'Committed formula should be =SUM(Sheet2!A1:A3)');

    const displayValue = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(displayValue, '60', 'B1 should display 60 (sum of Sheet2!A1:A3)');
  },

  'Mixed references: same-sheet and cross-sheet cells in one formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Enter a value in Sheet2!A1
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(200);

    // Switch back to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Enter a value in Sheet1!B1
    await setCellValue(ctx.page, 'B1', '50');
    await sleep(200);

    // Start editing a formula in C1: =
    await startFormula(ctx.page, 'C1', '=');
    await sleep(200);

    // Click B1 (same sheet, no prefix needed)
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Type +
    await ctx.page.keyboard.type('+', { delay: 50 });
    await sleep(100);

    // Switch to Sheet2
    await clickSheetTab(ctx.page, 1);

    // Click A1 (cross-sheet, needs Sheet2! prefix)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Formula should be "=B1+Sheet2!A1"
    const formulaText = await getEditingFormulaText(ctx.page);
    assertEqual(formulaText, '=B1+Sheet2!A1', 'Formula should show =B1+Sheet2!A1');

    // Press Enter to commit
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Switch to Sheet1 and verify
    await clickSheetTab(ctx.page, 0);
    await clickCell(ctx.page, 'C1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=B1+Sheet2!A1', 'Committed formula should be =B1+Sheet2!A1');

    const displayValue = await getCellDisplayValue(ctx.page, 'C1');
    assertEqual(displayValue, '150', 'C1 should display 150 (50+100)');
  },

  'Escape cancels formula edit and returns to origin sheet': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Switch back to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Start editing a formula in A1
    await startFormula(ctx.page, 'A1', '=');
    await sleep(200);

    // Switch to Sheet2
    await clickSheetTab(ctx.page, 1);

    // Click on B5
    await clickCell(ctx.page, 'B5');
    await sleep(200);

    // Press Escape to cancel
    await ctx.page.keyboard.press('Escape');
    await sleep(300);

    // Should return to Sheet1
    const activeTab = await ctx.page.evaluate(() => {
      const tabs = document.querySelectorAll('.sheet-tab');
      for (let i = 0; i < tabs.length; i++) {
        if (tabs[i].classList.contains('active')) return i;
      }
      return -1;
    });
    assertEqual(activeTab, 0, 'Should return to Sheet1 (index 0) after Escape');

    // A1 should be empty (formula was cancelled)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const content = await getFormulaBarContent(ctx.page);
    assertTrue(content === '' || content === null, 'A1 should be empty after cancelling formula');
  },

  'Sheet tab click during formula edit preserves edit state': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Switch back to Sheet1
    await clickSheetTab(ctx.page, 0);

    // Start editing a formula in A1
    await startFormula(ctx.page, 'A1', '=SUM(');
    await sleep(200);

    // Verify we're in editing mode
    const editingBefore = await isFormulaBarEditing(ctx.page);
    assertTrue(editingBefore, 'Should be in formula editing mode before switching sheets');

    // Switch to Sheet2 - should preserve edit mode
    await clickSheetTab(ctx.page, 1);

    // Check we're still in editing mode
    const editingAfter = await isFormulaBarEditing(ctx.page);
    assertTrue(editingAfter, 'Should still be in formula editing mode after switching sheets');

    // The partial formula should still be there
    const formulaText = await getEditingFormulaText(ctx.page);
    assertEqual(formulaText, '=SUM(', 'Formula text should be preserved: =SUM(');
  },
};

// Run tests
runTests(tests);
