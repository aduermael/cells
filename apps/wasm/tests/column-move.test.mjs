// Column move test for Cells spreadsheet application
// Tests column dragging and formula reference updates

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  dragColumn,
  assertEqual,
  sleep,
} from './helpers.mjs';

const tests = {
  'Creating cell in sparse column displays correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on D1 (column 3) in an empty sheet
    await clickCell(ctx.page, 'D1');
    await sleep(200);

    // Type a value
    await setCellValue(ctx.page, 'D1', 'test');
    await sleep(300);

    // Verify the cell reference shows D1 (not E1 or other wrong position)
    await clickCell(ctx.page, 'D1');
    await sleep(200);
    const formula = await getFormulaBarContent(ctx.page);
    assertEqual(formula, 'test', 'D1 should contain "test"');

    // Verify we can also type in A1 without issues
    await setCellValue(ctx.page, 'A1', 'first');
    await sleep(200);
    await clickCell(ctx.page, 'A1');
    await sleep(200);
    const a1Val = await getFormulaBarContent(ctx.page);
    assertEqual(a1Val, 'first', 'A1 should contain "first"');

    // Verify D1 still has its value
    await clickCell(ctx.page, 'D1');
    await sleep(200);
    const d1Val = await getFormulaBarContent(ctx.page);
    assertEqual(d1Val, 'test', 'D1 should still contain "test"');
  },

  'Moving column updates cell positions and formula references': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Step 1: Put "foo" in B1
    await setCellValue(ctx.page, 'B1', 'foo');
    await sleep(100);

    // Step 2: Put "bar" in A1
    await setCellValue(ctx.page, 'A1', 'bar');
    await sleep(100);

    // Step 3: Put =B1 in A2
    await setCellValue(ctx.page, 'A2', '=B1');
    await sleep(200);

    // Verify initial state
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    let b1Value = await getFormulaBarContent(ctx.page);
    assertEqual(b1Value, 'foo', 'B1 should contain "foo"');

    await clickCell(ctx.page, 'A2');
    await sleep(200);
    let a2Formula = await getFormulaBarContent(ctx.page);
    assertEqual(a2Formula, '=B1', 'A2 should contain formula =B1');

    let a2Display = await getCellDisplayValue(ctx.page, 'A2');
    assertEqual(a2Display, 'foo', 'A2 should display "foo" (from B1)');

    // Step 4: Drag column B to position C (drop on C header)
    // This should move column B to position C (index 2)
    await dragColumn(ctx.page, 'B', 'C');
    await sleep(300);

    // Step 5: Verify foo is now in C1 (B moved right by one position)
    await clickCell(ctx.page, 'C1');
    await sleep(200);
    const c1Value = await getFormulaBarContent(ctx.page);
    assertEqual(c1Value, 'foo', 'C1 should now contain "foo" (moved from B1)');

    // Step 6: Verify A2 formula was updated to reference C1
    await clickCell(ctx.page, 'A2');
    await sleep(200);
    a2Formula = await getFormulaBarContent(ctx.page);
    assertEqual(a2Formula, '=C1', 'A2 formula should be updated to =C1 (was =B1)');

    // Step 7: Verify A2 still shows the correct value
    a2Display = await getCellDisplayValue(ctx.page, 'A2');
    assertEqual(a2Display, 'foo', 'A2 should still display "foo" (now from C1)');
  },
};

// Run all tests
runTests(tests);
