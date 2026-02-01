// Fill handle auto-commit tests for Cells spreadsheet application
// Tests that the fill handle automatically commits uncommitted cell values before fill operation

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  typeInCell,
  getFormulaBarContent,
  dragFillHandle,
  assertEqual,
  sleep,
} from './helpers.mjs';

const tests = {
  'Fill handle auto-commits uncommitted cell value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click cell A1 and type a value WITHOUT pressing Enter
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await typeInCell(ctx.page, '42');
    await sleep(100);

    // Drag fill handle from A1 to A3 (value should be auto-committed before fill)
    await dragFillHandle(ctx.page, 'A1', 'A3');
    await sleep(300);

    // Verify A1 has the value
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    let content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '42', 'A1 should have the typed value after fill');

    // Verify A2 has the filled value
    await clickCell(ctx.page, 'A2');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '42', 'A2 should have the filled value');

    // Verify A3 has the filled value
    await clickCell(ctx.page, 'A3');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '42', 'A3 should have the filled value');
  },

  'Fill handle auto-commits text value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click cell B1 and type text WITHOUT pressing Enter
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    await typeInCell(ctx.page, 'hello');
    await sleep(100);

    // Drag fill handle from B1 to B3
    await dragFillHandle(ctx.page, 'B1', 'B3');
    await sleep(300);

    // Verify all cells have the text value
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    let content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'hello', 'B1 should have the typed text');

    await clickCell(ctx.page, 'B2');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'hello', 'B2 should have the filled text');

    await clickCell(ctx.page, 'B3');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'hello', 'B3 should have the filled text');
  },

  'Fill handle auto-commits formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // First enter a value in A1 that the formula will reference
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await typeInCell(ctx.page, '10');
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Now click C1 and type a formula WITHOUT pressing Enter
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    await typeInCell(ctx.page, '=A1*2');
    await sleep(100);

    // Drag fill handle from C1 to C3
    await dragFillHandle(ctx.page, 'C1', 'C3');
    await sleep(300);

    // Verify C1 has the formula
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    let content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1*2', 'C1 should have the typed formula');

    // Verify C2 has the adjusted formula
    await clickCell(ctx.page, 'C2');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A2*2', 'C2 should have the adjusted formula');

    // Verify C3 has the adjusted formula
    await clickCell(ctx.page, 'C3');
    await sleep(100);
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A3*2', 'C3 should have the adjusted formula');
  },
};

// Run all tests
runTests(tests);
