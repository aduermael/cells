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
    await ctx.page.keyboard.type('replaced');
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
};

// Run all tests
runTests(tests);
