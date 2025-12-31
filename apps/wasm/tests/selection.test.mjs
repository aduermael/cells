// Selection tests for Cells spreadsheet application
// Tests range selection visibility and behavior

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  selectRange,
  getFormulaBarContent,
  getCurrentCellRef,
  getCanvasCursor,
  moveToFillHandle,
  getCanvasInfo,
  cellToPixel,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

const tests = {
  'Range selection shows anchor cell value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values in a range
    await setCellValue(ctx.page, 'A1', 'First');
    await setCellValue(ctx.page, 'B1', 'Second');
    await setCellValue(ctx.page, 'A2', 'Third');
    await setCellValue(ctx.page, 'B2', 'Fourth');
    await sleep(200);

    // Select range A1:B2
    await selectRange(ctx.page, 'A1', 'B2');
    await sleep(200);

    // Verify the anchor cell (A1) formula bar shows the value
    // This confirms the anchor cell's value is accessible
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'First', 'Formula bar should show anchor cell value');

    // The visual test is implicit - if the anchor cell's background
    // was covering the text, users would see a blank cell
    // We can't directly test canvas rendering, but we verify the state is correct
  },

  'Range selection maintains anchor cell reference': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Select a range from B2 to D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);

    // The cell reference should show the anchor cell
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Cell reference should show anchor cell B2');
  },

  'Can navigate within range selection with Tab': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'B1', '2');
    await setCellValue(ctx.page, 'C1', '3');
    await sleep(200);

    // Select range A1:C1
    await selectRange(ctx.page, 'A1', 'C1');
    await sleep(100);

    // Tab should move through the selection
    await ctx.page.keyboard.press('Tab');
    await sleep(100);
    let cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B1', 'Tab should move to B1');

    await ctx.page.keyboard.press('Tab');
    await sleep(100);
    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'C1', 'Tab should move to C1');
  },

  'Shift+Arrow extends selection': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Shift+Right to extend selection
    await ctx.page.keyboard.down('Shift');
    await ctx.page.keyboard.press('ArrowRight');
    await ctx.page.keyboard.up('Shift');
    await sleep(100);

    // Cell ref should still show anchor
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Anchor cell should remain B2');

    // Verify we can still see the formula bar (anchor cell data)
    // If we enter a value, it should go in the anchor cell
    // Use delay between characters to allow cell editor to start
    await ctx.page.keyboard.type('test', { delay: 20 });
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    await clickCell(ctx.page, 'B2');
    await sleep(100);
    const val = await getFormulaBarContent(ctx.page);
    assertEqual(val, 'test', 'Value should be in anchor cell B2');
  },

  'Fill handle shows crosshair cursor': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Select a cell
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Move to fill handle position (bottom-right corner of selection)
    await moveToFillHandle(ctx.page, 'B2');
    await sleep(100);

    // Check cursor is crosshair
    const cursor = await getCanvasCursor(ctx.page);
    assertEqual(cursor, 'crosshair', 'Cursor should be crosshair when hovering fill handle');

    // Move away from fill handle (to center of another cell)
    const canvasInfo = await getCanvasInfo(ctx.page);
    const { x, y } = cellToPixel(3, 3, canvasInfo);
    await ctx.page.mouse.move(x, y);
    await sleep(100);

    // Cursor should be default now
    const cursor2 = await getCanvasCursor(ctx.page);
    assertEqual(cursor2, 'default', 'Cursor should be default when not on fill handle');
  },

  'Fill handle visible on range selection': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Select a range
    await selectRange(ctx.page, 'A1', 'C3');
    await sleep(100);

    // Move to fill handle position (bottom-right corner of range = C3)
    await moveToFillHandle(ctx.page, 'C3');
    await sleep(100);

    // Check cursor is crosshair
    const cursor = await getCanvasCursor(ctx.page);
    assertEqual(cursor, 'crosshair', 'Cursor should be crosshair on range selection fill handle');
  },
};

// Run all tests
runTests(tests);
