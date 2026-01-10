// Spill (Dynamic Array) test for Cells spreadsheet application
// Tests spill range highlighting, formula bar behavior, and edit prevention

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
 * Get the isSpilled flag for a cell
 */
async function getCellIsSpilled(page, cellRef) {
  const col = cellRef.charCodeAt(0) - 65;
  const row = parseInt(cellRef.slice(1), 10) - 1;
  return await page.evaluate(({ col, row }) => {
    if (window._appContext && window._appContext.app && window._appContext.app.cells) {
      const cells = window._appContext.app.cells;
      for (const cell of cells) {
        if (cell.col === col && cell.row === row) {
          return cell.isSpilled === true;
        }
      }
    }
    return false;
  }, { col, row });
}

/**
 * Get the isSpillMaster flag for a cell
 */
async function getCellIsSpillMaster(page, cellRef) {
  const col = cellRef.charCodeAt(0) - 65;
  const row = parseInt(cellRef.slice(1), 10) - 1;
  return await page.evaluate(({ col, row }) => {
    if (window._appContext && window._appContext.app && window._appContext.app.cells) {
      const cells = window._appContext.app.cells;
      for (const cell of cells) {
        if (cell.col === col && cell.row === row) {
          return cell.isSpillMaster === true;
        }
      }
    }
    return false;
  }, { col, row });
}

/**
 * Check if the formula bar has the spilled-cell class (grayed out)
 */
async function isFormulaBarSpilledStyle(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('formula-display');
    return el ? el.classList.contains('spilled-cell') : false;
  });
}

/**
 * Check if the cell editor container is visible
 */
async function isCellEditorVisible(page) {
  return await page.evaluate(() => {
    const el = document.getElementById('cell-editor-container');
    return el ? el.style.display === 'block' : false;
  });
}

/**
 * Get the current spill range highlight state from the app
 */
async function getSpillRangeHighlight(page) {
  return await page.evaluate(() => {
    if (window._appContext && window._appContext.app) {
      return window._appContext.app.spillRangeHighlight;
    }
    return null;
  });
}

const tests = {
  'SEQUENCE creates a spill range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula that creates 3 rows
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Verify the master cell shows the formula
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const masterFormula = await getFormulaBarContent(ctx.page);
    assertEqual(masterFormula, '=SEQUENCE(3)', 'Master cell should show the formula');

    // Verify it's a spill master
    const isMaster = await getCellIsSpillMaster(ctx.page, 'A1');
    assertTrue(isMaster, 'A1 should be marked as spill master');

    // Verify spilled cells have values
    const val1 = await getCellDisplayValue(ctx.page, 'A1');
    const val2 = await getCellDisplayValue(ctx.page, 'A2');
    const val3 = await getCellDisplayValue(ctx.page, 'A3');
    assertEqual(val1, '1', 'A1 should display 1');
    assertEqual(val2, '2', 'A2 should display 2');
    assertEqual(val3, '3', 'A3 should display 3');
  },

  'Spilled cell shows grayed formula bar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on a spilled cell (A2)
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Verify it's marked as spilled
    const isSpilled = await getCellIsSpilled(ctx.page, 'A2');
    assertTrue(isSpilled, 'A2 should be marked as spilled');

    // Verify formula bar shows the master formula
    const formula = await getFormulaBarContent(ctx.page);
    assertEqual(formula, '=SEQUENCE(3)', 'Spilled cell should show master formula');

    // Verify formula bar has spilled-cell class (grayed out)
    const isGrayed = await isFormulaBarSpilledStyle(ctx.page);
    assertTrue(isGrayed, 'Formula bar should have spilled-cell class');
  },

  'Cannot edit spilled cell by typing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on a spilled cell (A2)
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Try to type - this should not open the cell editor
    await ctx.page.keyboard.type('test');
    await sleep(200);

    // Verify cell editor is not visible
    const editorVisible = await isCellEditorVisible(ctx.page);
    assertTrue(!editorVisible, 'Cell editor should not be visible for spilled cell');

    // Verify the spilled value is unchanged
    const val = await getCellDisplayValue(ctx.page, 'A2');
    assertEqual(val, '2', 'Spilled cell value should be unchanged');
  },

  'Cannot edit spilled cell with F2': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on a spilled cell (A2)
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Press F2 - should not open cell editor
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    // Verify cell editor is not visible
    const editorVisible = await isCellEditorVisible(ctx.page);
    assertTrue(!editorVisible, 'Cell editor should not open with F2 on spilled cell');
  },

  'Cannot delete spilled cell with Backspace': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on a spilled cell (A2)
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Press Backspace
    await ctx.page.keyboard.press('Backspace');
    await sleep(300);

    // Verify the spill range is still intact
    const val2 = await getCellDisplayValue(ctx.page, 'A2');
    assertEqual(val2, '2', 'Spilled cell value should be unchanged after Backspace');
  },

  'Master cell can be edited': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on the master cell (A1)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Verify formula bar does NOT have spilled-cell class
    const isGrayed = await isFormulaBarSpilledStyle(ctx.page);
    assertTrue(!isGrayed, 'Master cell formula bar should not be grayed');

    // Change the formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(2)');
    await sleep(300);

    // Verify the spill range changed
    const val1 = await getCellDisplayValue(ctx.page, 'A1');
    const val2 = await getCellDisplayValue(ctx.page, 'A2');
    const val3 = await getCellDisplayValue(ctx.page, 'A3');
    assertEqual(val1, '1', 'A1 should still display 1');
    assertEqual(val2, '2', 'A2 should still display 2');
    // A3 should now be empty (sequence reduced to 2)
    assertTrue(val3 === null || val3 === '', 'A3 should be empty after formula change');
  },

  'UNIQUE creates spill range with unique values': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter duplicate values
    await setCellValue(ctx.page, 'A1', 'apple');
    await setCellValue(ctx.page, 'A2', 'banana');
    await setCellValue(ctx.page, 'A3', 'apple');
    await setCellValue(ctx.page, 'A4', 'cherry');

    // Enter UNIQUE formula
    await setCellValue(ctx.page, 'B1', '=UNIQUE(A1:A4)');
    await sleep(300);

    // Verify unique values spilled
    const val1 = await getCellDisplayValue(ctx.page, 'B1');
    const val2 = await getCellDisplayValue(ctx.page, 'B2');
    const val3 = await getCellDisplayValue(ctx.page, 'B3');
    assertEqual(val1, 'apple', 'B1 should be apple');
    assertEqual(val2, 'banana', 'B2 should be banana');
    assertEqual(val3, 'cherry', 'B3 should be cherry');
  },

  'Spill highlight shows when selecting master cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula that creates 3 rows
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on the master cell (A1)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Verify spill highlight is shown
    const highlight = await getSpillRangeHighlight(ctx.page);
    assertTrue(highlight !== null, 'Spill highlight should be shown when master cell is selected');
    assertEqual(highlight.minCol, 0, 'Spill highlight minCol should be 0 (column A)');
    assertEqual(highlight.maxCol, 0, 'Spill highlight maxCol should be 0 (column A)');
    assertEqual(highlight.minRow, 0, 'Spill highlight minRow should be 0 (row 1)');
    assertEqual(highlight.maxRow, 2, 'Spill highlight maxRow should be 2 (row 3)');
    assertEqual(highlight.masterCol, 0, 'Master cell column should be 0');
    assertEqual(highlight.masterRow, 0, 'Master cell row should be 0');
  },

  'Spill highlight shows when selecting spilled cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on a spilled cell (A2)
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Verify spill highlight is shown
    const highlight = await getSpillRangeHighlight(ctx.page);
    assertTrue(highlight !== null, 'Spill highlight should be shown when spilled cell is selected');
    assertEqual(highlight.minCol, 0, 'Spill highlight minCol should be 0');
    assertEqual(highlight.maxCol, 0, 'Spill highlight maxCol should be 0');
    assertEqual(highlight.minRow, 0, 'Spill highlight minRow should be 0');
    assertEqual(highlight.maxRow, 2, 'Spill highlight maxRow should be 2');
    // Master cell should be A1
    assertEqual(highlight.masterCol, 0, 'Master cell column should be 0');
    assertEqual(highlight.masterRow, 0, 'Master cell row should be 0');
  },

  'Spill highlight clears when selecting cell outside spill range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter SEQUENCE formula
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // First click on spilled cell to verify highlight is shown
    await clickCell(ctx.page, 'A2');
    await sleep(200);
    let highlight = await getSpillRangeHighlight(ctx.page);
    assertTrue(highlight !== null, 'Spill highlight should be shown when spilled cell is selected');

    // Now click on a cell outside the spill range
    await clickCell(ctx.page, 'B1');
    await sleep(400); // Wait for async highlight update and re-render

    // Verify spill highlight is cleared
    highlight = await getSpillRangeHighlight(ctx.page);
    assertTrue(highlight === null, 'Spill highlight should be null when selecting cell outside spill range');
  },
};

runTests(tests);
