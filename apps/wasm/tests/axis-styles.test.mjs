// Axis styles E2E tests
// Tests column and row style application and inheritance:
// 1. Setting column style applies to all cells in column
// 2. Setting row style applies to all cells in row
// 3. Column style > row style at intersection
// 4. Cell style > column style (cell wins)
// 5. Range style > column style (range wins)

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  clickColumnHeader,
  clickRowHeader,
  selectRange,
  setCellValue,
  sleep,
  assertEqual,
  assertTrue,
} from './helpers.mjs';

/**
 * Apply a background color using the toolbar
 */
async function applyBackgroundColor(page, color) {
  await page.click('#style-bg-color-btn');
  await sleep(100);

  const colorSelector = `#bg-color-popup .color-option[data-color="${color.toUpperCase()}"]`;
  const hasColor = await page.$(colorSelector);

  if (hasColor) {
    await page.click(colorSelector);
  } else {
    const hexInput = await page.$('#bg-color-popup .color-hex-input');
    if (hexInput) {
      await hexInput.click({ clickCount: 3 });
      await page.keyboard.type(color);
      await page.keyboard.press('Enter');
    }
  }
  await sleep(200);
}

/**
 * Get the effective style for a cell via the data source
 */
async function getEffectiveCellStyle(page, col, row) {
  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return null;
    }
    return await ctx.app.dataSource.getEffectiveCellStyle(col, row);
  }, { col, row });
}

/**
 * Get the column style via the data source
 */
async function getColumnStyle(page, col) {
  return await page.evaluate(async ({ col }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return null;
    }
    return await ctx.app.dataSource.getColumnStyle(col);
  }, { col });
}

/**
 * Get the row style via the data source
 */
async function getRowStyle(page, row) {
  return await page.evaluate(async ({ row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return null;
    }
    return await ctx.app.dataSource.getRowStyle(row);
  }, { row });
}

const tests = {
  'Clicking column header selects the column': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click column A header to select entire column
    await clickColumnHeader(ctx.page, 'A');
    await sleep(100);

    // Verify the column is selected
    const selectedCol = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      return app?.selectedColumn;
    });
    assertEqual(selectedCol, 0, 'Column A (index 0) should be selected');
  },

  'Setting column style via header click applies style to column axis': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#3B82F6'; // Blue 500

    // Click column A header to select entire column
    await clickColumnHeader(ctx.page, 'A');
    await sleep(100);

    // Verify column is selected before applying style
    const selectedCol = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      return app?.selectedColumn;
    });
    assertTrue(selectedCol >= 0, 'Column should be selected before applying style');

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Verify column style was set
    const colStyle = await getColumnStyle(ctx.page, 0);
    assertTrue(colStyle !== null, 'Column style should exist');
    assertEqual(colStyle.bgColor?.toUpperCase(), testColor.toUpperCase(), 'Column bgColor should match');

    // Verify effective style at various cells in column A
    const styleA1 = await getEffectiveCellStyle(ctx.page, 0, 0);
    assertEqual(styleA1.bgColor?.toUpperCase(), testColor.toUpperCase(), 'A1 should inherit column style');

    const styleA5 = await getEffectiveCellStyle(ctx.page, 0, 4);
    assertEqual(styleA5.bgColor?.toUpperCase(), testColor.toUpperCase(), 'A5 should inherit column style');

    // Verify cells in other columns don't have the style
    const styleB1 = await getEffectiveCellStyle(ctx.page, 1, 0);
    assertTrue(!styleB1.bgColor, 'B1 should not have background color');
  },

  'Setting row style via header click applies style to row axis': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#22C55E'; // Green 500

    // Click row 2 header (index 1) to select entire row
    await clickRowHeader(ctx.page, 1);
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Verify row style was set
    const rowStyle = await getRowStyle(ctx.page, 1);
    assertTrue(rowStyle !== null, 'Row style should exist');
    assertEqual(rowStyle.bgColor?.toUpperCase(), testColor.toUpperCase(), 'Row bgColor should match');

    // Verify effective style at various cells in row 2
    const styleA2 = await getEffectiveCellStyle(ctx.page, 0, 1);
    assertEqual(styleA2.bgColor?.toUpperCase(), testColor.toUpperCase(), 'A2 should inherit row style');

    const styleD2 = await getEffectiveCellStyle(ctx.page, 3, 1);
    assertEqual(styleD2.bgColor?.toUpperCase(), testColor.toUpperCase(), 'D2 should inherit row style');

    // Verify cells in other rows don't have the style
    const styleA1 = await getEffectiveCellStyle(ctx.page, 0, 0);
    assertTrue(!styleA1.bgColor, 'A1 should not have background color');
  },

  'Column style takes precedence over row style at intersection': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const colColor = '#3B82F6'; // Blue
    const rowColor = '#22C55E'; // Green

    // First set row 2 style (lower priority)
    await clickRowHeader(ctx.page, 1);
    await sleep(100);
    await applyBackgroundColor(ctx.page, rowColor);
    await sleep(300);

    // Then set column B style (higher priority)
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await applyBackgroundColor(ctx.page, colColor);
    await sleep(300);

    // At intersection B2: column style should win
    const styleB2 = await getEffectiveCellStyle(ctx.page, 1, 1);
    assertEqual(styleB2.bgColor?.toUpperCase(), colColor.toUpperCase(),
      'B2 should have column style (column > row priority)');

    // A2 should have row style (no column style there)
    const styleA2 = await getEffectiveCellStyle(ctx.page, 0, 1);
    assertEqual(styleA2.bgColor?.toUpperCase(), rowColor.toUpperCase(),
      'A2 should have row style');

    // B1 should have column style (no row style there)
    const styleB1 = await getEffectiveCellStyle(ctx.page, 1, 0);
    assertEqual(styleB1.bgColor?.toUpperCase(), colColor.toUpperCase(),
      'B1 should have column style');
  },

  'Cell style takes precedence over column style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const colColor = '#3B82F6'; // Blue
    const cellColor = '#EF4444'; // Red

    // Set column A style
    await clickColumnHeader(ctx.page, 'A');
    await sleep(100);
    await applyBackgroundColor(ctx.page, colColor);
    await sleep(300);

    // Set cell A1 style (should override column style)
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await applyBackgroundColor(ctx.page, cellColor);
    await sleep(300);

    // A1 should have cell style
    const styleA1 = await getEffectiveCellStyle(ctx.page, 0, 0);
    assertEqual(styleA1.bgColor?.toUpperCase(), cellColor.toUpperCase(),
      'A1 should have cell style (cell > column priority)');

    // A2 should have column style
    const styleA2 = await getEffectiveCellStyle(ctx.page, 0, 1);
    assertEqual(styleA2.bgColor?.toUpperCase(), colColor.toUpperCase(),
      'A2 should have column style');
  },

  'Axis styles render correctly on cells with values': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Add some values first
    await setCellValue(ctx.page, 'B1', '100');
    await setCellValue(ctx.page, 'B2', '200');
    await setCellValue(ctx.page, 'B3', '300');
    await sleep(200);

    const testColor = '#3B82F6'; // Blue

    // Set column B style
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Verify all cells in column B have the style
    const styleB1 = await getEffectiveCellStyle(ctx.page, 1, 0);
    assertEqual(styleB1.bgColor?.toUpperCase(), testColor.toUpperCase(),
      'B1 should have column style');

    const styleB2 = await getEffectiveCellStyle(ctx.page, 1, 1);
    assertEqual(styleB2.bgColor?.toUpperCase(), testColor.toUpperCase(),
      'B2 should have column style');

    const styleB3 = await getEffectiveCellStyle(ctx.page, 1, 2);
    assertEqual(styleB3.bgColor?.toUpperCase(), testColor.toUpperCase(),
      'B3 should have column style');

    // Empty cells should also have the style
    const styleB10 = await getEffectiveCellStyle(ctx.page, 1, 9);
    assertEqual(styleB10.bgColor?.toUpperCase(), testColor.toUpperCase(),
      'Empty cell B10 should have column style');
  },

  'Range style takes precedence over column style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const colColor = '#3B82F6'; // Blue
    const rangeColor = '#F59E0B'; // Orange

    // Set column B style
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await applyBackgroundColor(ctx.page, colColor);
    await sleep(300);

    // Select B2:B4 and apply range style
    await selectRange(ctx.page, 'B2', 'B4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, rangeColor);
    await sleep(300);

    // B2-B4 should have range style (range > column)
    const styleB2 = await getEffectiveCellStyle(ctx.page, 1, 1);
    assertEqual(styleB2.bgColor?.toUpperCase(), rangeColor.toUpperCase(),
      'B2 should have range style (range > column priority)');

    const styleB3 = await getEffectiveCellStyle(ctx.page, 1, 2);
    assertEqual(styleB3.bgColor?.toUpperCase(), rangeColor.toUpperCase(),
      'B3 should have range style');

    const styleB4 = await getEffectiveCellStyle(ctx.page, 1, 3);
    assertEqual(styleB4.bgColor?.toUpperCase(), rangeColor.toUpperCase(),
      'B4 should have range style');

    // B1 should have column style (outside range)
    const styleB1 = await getEffectiveCellStyle(ctx.page, 1, 0);
    assertEqual(styleB1.bgColor?.toUpperCase(), colColor.toUpperCase(),
      'B1 should have column style (outside range)');

    // B5 should have column style (outside range)
    const styleB5 = await getEffectiveCellStyle(ctx.page, 1, 4);
    assertEqual(styleB5.bgColor?.toUpperCase(), colColor.toUpperCase(),
      'B5 should have column style (outside range)');
  },

  'Toolbar reflects column style when column is selected': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set column A to have right alignment
    await clickColumnHeader(ctx.page, 'A');
    await sleep(100);
    await ctx.page.click('#align-right-btn');
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'C3');
    await sleep(200);

    // Now re-select column A
    await clickColumnHeader(ctx.page, 'A');
    await sleep(300);

    // Check that the right alignment button is now active
    const isRightActive = await ctx.page.evaluate(() => {
      const btn = document.getElementById('align-right-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(isRightActive, 'Right align button should be active when column A is selected');
  },

  'Toolbar reflects row style when row is selected': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set row 2 to have center alignment
    await clickRowHeader(ctx.page, 1); // 0-indexed, so row 2 is index 1
    await sleep(100);
    await ctx.page.click('#align-center-btn');
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'C3');
    await sleep(200);

    // Now re-select row 2
    await clickRowHeader(ctx.page, 1);
    await sleep(200);

    // Check that the center alignment button is now active
    const isCenterActive = await ctx.page.evaluate(() => {
      const btn = document.getElementById('align-center-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(isCenterActive, 'Center align button should be active when row 2 is selected');
  },
};

runTests(tests);
