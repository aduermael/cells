// Spill + Style interaction test for Cells spreadsheet application
// Tests that setting styles on spilled cells doesn't break the spill

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  clickColumnHeader,
  setCellValue,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Click alignment button to set cell alignment
 */
async function clickAlignButton(page, align) {
  const buttonId = `align-${align}-btn`;
  await page.click(`#${buttonId}`);
  await sleep(200);
}

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
 * Get the effective cell style from the engine
 */
async function getEffectiveCellStyle(page, col, row) {
  return await page.evaluate(async ({ col, row }) => {
    if (window._appContext && window._appContext.app && window._appContext.app.dataSource) {
      return await window._appContext.app.dataSource.getEffectiveCellStyle(col, row);
    }
    return {};
  }, { col, row });
}

/**
 * Click alignment button to set cell/column alignment
 */
async function clickAlignRight(page) {
  await page.click('#align-right-btn');
  await sleep(200);
}

const tests = {
  'Column style alignment applies to virtual spilled cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a spill with SEQUENCE(3)
    await setCellValue(ctx.page, 'B1', '=SEQUENCE(3)');
    await sleep(300);

    // Verify spill is working - B2 should show 2
    let val2 = await getCellDisplayValue(ctx.page, 'B2');
    assertEqual(val2, '2', 'B2 should display 2');

    // Select column B and set right alignment
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await clickAlignRight(ctx.page);
    await sleep(300);

    // Check that B2's effective style has right alignment
    const styleB2 = await getEffectiveCellStyle(ctx.page, 1, 1);
    assertEqual(styleB2.hAlign, 'right', 'B2 should have right alignment from column style');

    // Verify the value is still displayed correctly
    val2 = await getCellDisplayValue(ctx.page, 'B2');
    assertEqual(val2, '2', 'B2 should still display 2 after column styling');
  },

  'Setting alignment on spilled cell should NOT break spill': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a spill with SEQUENCE(3)
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Verify spill is working
    let val1 = await getCellDisplayValue(ctx.page, 'A1');
    let val2 = await getCellDisplayValue(ctx.page, 'A2');
    let val3 = await getCellDisplayValue(ctx.page, 'A3');
    assertEqual(val1, '1', 'A1 should display 1');
    assertEqual(val2, '2', 'A2 should display 2');
    assertEqual(val3, '3', 'A3 should display 3');

    // Click on spilled cell A2
    await clickCell(ctx.page, 'A2');
    await sleep(200);

    // Verify A2 is marked as spilled
    const isSpilled = await getCellIsSpilled(ctx.page, 'A2');
    assertTrue(isSpilled, 'A2 should be marked as spilled before styling');

    // Click center align button to set style on A2
    await clickAlignButton(ctx.page, 'center');
    await sleep(300);

    // Verify spill is still intact - A1 should NOT show #SPILL! error
    val1 = await getCellDisplayValue(ctx.page, 'A1');
    val2 = await getCellDisplayValue(ctx.page, 'A2');
    val3 = await getCellDisplayValue(ctx.page, 'A3');

    // The key test: A1 should still show 1, not #SPILL!
    assertEqual(val1, '1', 'A1 should still display 1 after styling A2');
    assertEqual(val2, '2', 'A2 should still display 2 (spilled value)');
    assertEqual(val3, '3', 'A3 should still display 3');
  },

  'Setting alignment on spilled cell via UNIQUE should NOT break spill': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up data for UNIQUE
    await setCellValue(ctx.page, 'A1', 'apple');
    await setCellValue(ctx.page, 'A2', 'banana');
    await setCellValue(ctx.page, 'A3', 'apple');
    await setCellValue(ctx.page, 'A4', 'cherry');

    // Create UNIQUE spill
    await setCellValue(ctx.page, 'B1', '=UNIQUE(A1:A4)');
    await sleep(300);

    // Verify spill is working
    let val1 = await getCellDisplayValue(ctx.page, 'B1');
    let val2 = await getCellDisplayValue(ctx.page, 'B2');
    let val3 = await getCellDisplayValue(ctx.page, 'B3');
    assertEqual(val1, 'apple', 'B1 should display apple');
    assertEqual(val2, 'banana', 'B2 should display banana');
    assertEqual(val3, 'cherry', 'B3 should display cherry');

    // Click on spilled cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(200);

    // Click right align button to set style on B2
    await clickAlignButton(ctx.page, 'right');
    await sleep(300);

    // Verify spill is still intact
    val1 = await getCellDisplayValue(ctx.page, 'B1');
    val2 = await getCellDisplayValue(ctx.page, 'B2');
    val3 = await getCellDisplayValue(ctx.page, 'B3');

    // The key test: B1 should still show apple, not #SPILL!
    assertEqual(val1, 'apple', 'B1 should still display apple after styling B2');
    assertEqual(val2, 'banana', 'B2 should still display banana (spilled value)');
    assertEqual(val3, 'cherry', 'B3 should still display cherry');
  },

  'Removing style from spilled cell should keep spill working': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a spill with SEQUENCE(3)
    await setCellValue(ctx.page, 'A1', '=SEQUENCE(3)');
    await sleep(300);

    // Click on spilled cell A2 and set alignment
    await clickCell(ctx.page, 'A2');
    await sleep(200);
    await clickAlignButton(ctx.page, 'center');
    await sleep(300);

    // Now click the same button again to toggle off (or click another one)
    // Since this clicks the active button, it might toggle, or we'll just set to left
    await clickAlignButton(ctx.page, 'left');
    await sleep(300);

    // Verify spill is still intact
    const val1 = await getCellDisplayValue(ctx.page, 'A1');
    const val2 = await getCellDisplayValue(ctx.page, 'A2');
    const val3 = await getCellDisplayValue(ctx.page, 'A3');

    assertEqual(val1, '1', 'A1 should still display 1 after changing alignment');
    assertEqual(val2, '2', 'A2 should still display 2');
    assertEqual(val3, '3', 'A3 should still display 3');
  },
};

runTests(tests);
