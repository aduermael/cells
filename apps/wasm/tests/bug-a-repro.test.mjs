// Bug A Reproduction Test
// Simple test to reproduce: cross-sheet formula becomes #VALUE! on re-edit

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

async function clickSheetTab(page, index) {
  await page.evaluate((idx) => {
    const tabs = document.querySelectorAll('.sheet-tab');
    if (tabs[idx]) tabs[idx].click();
  }, index);
  await sleep(300);
}

async function doubleClickCell(page, cellRef) {
  // Parse cell ref like "A1" to col/row
  const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
  if (!match) throw new Error(`Invalid cell ref: ${cellRef}`);
  const colName = match[1].toUpperCase();
  const rowNum = parseInt(match[2], 10);
  const col = colName.charCodeAt(0) - 65; // A=0, B=1, etc
  const row = rowNum - 1; // 0-indexed

  await page.evaluate(({ col, row }) => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;
    const x = rect.left + HEADER_WIDTH + col * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
    const y = rect.top + HEADER_HEIGHT + row * DEFAULT_ROW_HEIGHT + DEFAULT_ROW_HEIGHT / 2;
    canvas.dispatchEvent(new MouseEvent('dblclick', {
      bubbles: true,
      clientX: x,
      clientY: y
    }));
  }, { col, row });
  await sleep(200);
}

const tests = {
  'Bug A (F2): Cross-sheet formula re-edit with F2 key': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Setup: Add Sheet2 and enter value
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // Enter cross-sheet formula in Sheet1
    await clickSheetTab(ctx.page, 0);
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1');
    await sleep(300);

    // Verify initial
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'Initial: A1 should show 42');

    // Re-edit with F2 key
    await ctx.page.keyboard.press('F2');
    await sleep(200);
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Check result
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const finalValue = await getCellDisplayValue(ctx.page, 'A1');
    console.log('F2 re-edit result:', finalValue);
    assertEqual(finalValue, '42', 'After F2 re-edit: should still show 42');
  },

  'Bug A (double-click): Cross-sheet formula re-edit with double-click': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Setup: Add Sheet2 and enter value
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // Enter cross-sheet formula in Sheet1
    await clickSheetTab(ctx.page, 0);
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1');
    await sleep(300);

    // Verify initial
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'Initial: A1 should show 42');

    // Re-edit with double-click
    await doubleClickCell(ctx.page, 'A1');
    await sleep(200);

    // Check what's being edited
    const editContent = await ctx.page.evaluate(() => {
      const cellDisplay = document.getElementById('cell-display');
      return cellDisplay?.textContent || '';
    });
    console.log('Double-click edit content:', editContent);

    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Check result
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    const finalValue = await getCellDisplayValue(ctx.page, 'A1');
    console.log('Double-click re-edit - Formula:', finalFormula, 'Value:', finalValue);
    assertEqual(finalValue, '42', 'After double-click re-edit: should still show 42');
  },

  'Bug A (formula bar): Cross-sheet formula re-edit via formula bar click': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Setup: Add Sheet2 and enter value
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    // Enter cross-sheet formula in Sheet1
    await clickSheetTab(ctx.page, 0);
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1');
    await sleep(300);

    // Verify initial
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'Initial: A1 should show 42');

    // Click on formula bar to edit
    await ctx.page.click('#formula-display');
    await sleep(200);

    // Check what's being edited
    const editContent = await ctx.page.evaluate(() => {
      const formulaDisplay = document.getElementById('formula-display');
      return formulaDisplay?.textContent || '';
    });
    console.log('Formula bar edit content:', editContent);

    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    // Check result
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    const finalValue = await getCellDisplayValue(ctx.page, 'A1');
    console.log('Formula bar re-edit - Formula:', finalFormula, 'Value:', finalValue);
    assertEqual(finalValue, '42', 'After formula bar re-edit: should still show 42');
  },
};

runTests(tests);
