// Bug A: Cross-sheet formula re-edit must show sheet-qualified A1, not =#REF!
//
// When a cell holds e.g. =Sheet2!A1, double-click / F2 in-cell edit and the
// formula bar must open with that human-readable formula. Evaluated value and
// formula bar on single-click were already correct; getOrCreateCellAt used
// sheet-local RefConverter and returned #REF! for other-sheet cell UUIDs.

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  getCellEditorContent,
  doubleClickCell,
  assertEqual,
  assertTrue,
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

    await ctx.page.click('#add-sheet-btn');
    await sleep(300);
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(200);

    await clickSheetTab(ctx.page, 0);
    await setCellValue(ctx.page, 'A1', '=Sheet2!A1');
    await sleep(300);

    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const initialValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(initialValue, '42', 'Initial: A1 should show 42');

    const barFormula = await getFormulaBarContent(ctx.page);
    assertEqual(barFormula, '=Sheet2!A1', 'Formula bar should show =Sheet2!A1 on select');

    await ctx.page.click('#formula-display');
    await sleep(200);

    const editContent = await ctx.page.evaluate(() => {
      const formulaDisplay = document.getElementById('formula-display');
      return formulaDisplay?.textContent || '';
    });
    assertEqual(editContent, '=Sheet2!A1', 'Formula bar edit should show =Sheet2!A1, not =#REF!');
    assertTrue(!editContent.includes('#REF!'), 'Formula bar edit must not contain #REF!');

    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    const finalValue = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(finalValue, '42', 'After formula bar re-edit: should still show 42');
    assertEqual(finalFormula, '=Sheet2!A1', 'After re-edit: formula bar still =Sheet2!A1');
  },

  'Bug A (double-click): Cross-sheet formula in-cell edit shows A1 not #REF!': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await ctx.page.click('#add-sheet-btn');
    await sleep(300);
    await setCellValue(ctx.page, 'E1', '99');
    await sleep(200);

    await clickSheetTab(ctx.page, 0);
    await setCellValue(ctx.page, 'A1', '=Sheet2!E1');
    await sleep(300);

    await clickCell(ctx.page, 'A1');
    await sleep(200);
    assertEqual(await getCellDisplayValue(ctx.page, 'A1'), '99', 'A1 should show 99');
    assertEqual(
      await getFormulaBarContent(ctx.page),
      '=Sheet2!E1',
      'Formula bar on select should show =Sheet2!E1'
    );

    // Double-click uses getOrCreateCellAt — previously returned =#REF!
    await doubleClickCell(ctx.page, 'A1');
    await sleep(200);

    const editorContent = await getCellEditorContent(ctx.page);
    assertEqual(
      editorContent,
      '=Sheet2!E1',
      'In-cell editor should show =Sheet2!E1, not =#REF!'
    );
    assertTrue(
      editorContent != null && !editorContent.includes('#REF!'),
      'In-cell editor must not contain #REF! for a valid cross-sheet formula'
    );

    // Commit without changing; value and formula must remain correct
    await ctx.page.keyboard.press('Enter');
    await sleep(300);

    await clickCell(ctx.page, 'A1');
    await sleep(200);
    assertEqual(await getCellDisplayValue(ctx.page, 'A1'), '99', 'Value after re-edit commit');
    assertEqual(
      await getFormulaBarContent(ctx.page),
      '=Sheet2!E1',
      'Formula after re-edit commit'
    );
  },
};

runTests(tests);
