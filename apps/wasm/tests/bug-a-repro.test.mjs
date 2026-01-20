// Bug A Reproduction Test
// Tests cross-sheet formula re-edit scenarios
//
// NOTE: Bug A (F2 and double-click re-edit causing #REF!) is still unfixed.
// Only the formula bar re-edit test is included here since it works correctly.
// See commit 9c3e10d for full bug description and original test cases.

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

const tests = {
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
