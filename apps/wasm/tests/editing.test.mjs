// Editing operations tests for Cells spreadsheet application
// Tests basic editing behaviors that should always work

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  getCurrentCellRef,
  assertEqual,
  sleep,
  selectRange,
} from './helpers.mjs';

const tests = {
  'Can delete cell content with Delete key': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value
    await setCellValue(ctx.page, 'A1', 'delete me');
    await sleep(200);

    // Verify it's there
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    let val = await getFormulaBarContent(ctx.page);
    assertEqual(val, 'delete me', 'A1 should have value');

    // Press Delete key
    await ctx.page.keyboard.press('Delete');
    await sleep(200);

    // Verify it's gone
    val = await getFormulaBarContent(ctx.page);
    assertEqual(val, '', 'A1 should be empty after Delete');
  },

  'Can overwrite cell by typing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter initial value
    await setCellValue(ctx.page, 'B2', 'original');
    await sleep(200);

    // Click on the cell and type new value (without double-clicking)
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    // Add delay between keystrokes to prevent race conditions
    await ctx.page.keyboard.type('replaced', { delay: 50 });
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Verify the value was replaced
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    const val = await getFormulaBarContent(ctx.page);
    assertEqual(val, 'replaced', 'B2 should have new value');
  },

  'Arrow keys navigate between cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Press Right arrow
    await ctx.page.keyboard.press('ArrowRight');
    await sleep(100);
    let cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'C2', 'Right arrow should move to C2');

    // Press Up arrow
    await ctx.page.keyboard.press('ArrowUp');
    await sleep(100);
    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'C1', 'Up arrow should move to C1');

    // Press Left arrow
    await ctx.page.keyboard.press('ArrowLeft');
    await sleep(100);
    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B1', 'Left arrow should move to B1');

    // Press Down arrow
    await ctx.page.keyboard.press('ArrowDown');
    await sleep(100);
    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Down arrow should move back to B2');
  },

  'Escape cancels editing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter initial value
    await setCellValue(ctx.page, 'D1', 'keep this');
    await sleep(200);

    // Start editing and type something
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    await ctx.page.keyboard.type('discard');

    // Press Escape to cancel
    await ctx.page.keyboard.press('Escape');
    await sleep(200);

    // Verify original value is preserved
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    const val = await getFormulaBarContent(ctx.page);
    assertEqual(val, 'keep this', 'Escape should cancel edit');
  },

  'Can edit multiple cells in sequence': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values using setCellValue helper which handles editing properly
    await setCellValue(ctx.page, 'A1', 'one');
    await sleep(100);
    await setCellValue(ctx.page, 'A2', 'two');
    await sleep(100);
    await setCellValue(ctx.page, 'A3', 'three');
    await sleep(200);

    // Verify all values
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), 'one', 'A1 should be one');

    await clickCell(ctx.page, 'A2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), 'two', 'A2 should be two');

    await clickCell(ctx.page, 'A3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), 'three', 'A3 should be three');
  },

  'Numbers are stored correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter various number formats
    await setCellValue(ctx.page, 'A1', '42');
    await setCellValue(ctx.page, 'A2', '3.14159');
    await setCellValue(ctx.page, 'A3', '-100');
    await setCellValue(ctx.page, 'A4', '0');
    await sleep(200);

    // Verify all numbers
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '42', 'Integer should be stored');

    await clickCell(ctx.page, 'A2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '3.14159', 'Decimal should be stored');

    await clickCell(ctx.page, 'A3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '-100', 'Negative should be stored');

    await clickCell(ctx.page, 'A4');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '0', 'Zero should be stored');
  },

  'Empty cells remain empty': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on various empty cells
    await clickCell(ctx.page, 'Z99');
    await sleep(200);
    let val = await getFormulaBarContent(ctx.page);
    assertEqual(val, '', 'Unvisited cell should be empty');

    await clickCell(ctx.page, 'M50');
    await sleep(200);
    val = await getFormulaBarContent(ctx.page);
    assertEqual(val, '', 'Another unvisited cell should be empty');
  },

  'Can delete range of cells with Backspace': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a 2x2 range of cells with values
    await setCellValue(ctx.page, 'B2', 'one');
    await setCellValue(ctx.page, 'B3', 'two');
    await setCellValue(ctx.page, 'C2', 'three');
    await setCellValue(ctx.page, 'C3', 'four');
    await sleep(200);

    // Verify all values are set
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), 'one', 'B2 should have value before delete');

    // Select the range B2:C3
    await selectRange(ctx.page, 'B2', 'C3');
    await sleep(200);

    // Press Backspace to delete all cells in range
    await ctx.page.keyboard.press('Backspace');
    await sleep(300);

    // Verify all cells are now empty
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'B2 should be empty after Backspace');

    await clickCell(ctx.page, 'B3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'B3 should be empty after Backspace');

    await clickCell(ctx.page, 'C2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'C2 should be empty after Backspace');

    await clickCell(ctx.page, 'C3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'C3 should be empty after Backspace');
  },

  'Can delete range of cells with Delete key': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a 2x2 range of cells with values
    await setCellValue(ctx.page, 'D2', 'alpha');
    await setCellValue(ctx.page, 'D3', 'beta');
    await setCellValue(ctx.page, 'E2', 'gamma');
    await setCellValue(ctx.page, 'E3', 'delta');
    await sleep(200);

    // Select the range D2:E3
    await selectRange(ctx.page, 'D2', 'E3');
    await sleep(200);

    // Press Delete to delete all cells in range
    await ctx.page.keyboard.press('Delete');
    await sleep(300);

    // Verify all cells are now empty
    await clickCell(ctx.page, 'D2');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'D2 should be empty after Delete');

    await clickCell(ctx.page, 'E3');
    await sleep(100);
    assertEqual(await getFormulaBarContent(ctx.page), '', 'E3 should be empty after Delete');
  },

  'Title selection clears when clicking canvas': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on workbook title to focus it
    await ctx.page.click('#workbook-title');
    await sleep(100);

    // Select all text in the title (double-click or Ctrl+A)
    await ctx.page.click('#workbook-title', { clickCount: 2 });
    await sleep(100);

    // Verify there's a selection in the title
    const hasSelectionBefore = await ctx.page.evaluate(() => {
      const sel = window.getSelection();
      return sel && sel.toString().length > 0;
    });
    assertEqual(hasSelectionBefore, true, 'Should have selection in title after double-click');

    // Click on a cell in the canvas
    await clickCell(ctx.page, 'B2');
    await sleep(200);

    // Verify the selection is now cleared
    const hasSelectionAfter = await ctx.page.evaluate(() => {
      const sel = window.getSelection();
      return sel && sel.toString().length > 0;
    });
    assertEqual(hasSelectionAfter, false, 'Selection should be cleared after clicking canvas');

    // Verify cell selection works correctly
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Cell B2 should be selected');
  },
};

// Run all tests
runTests(tests);
