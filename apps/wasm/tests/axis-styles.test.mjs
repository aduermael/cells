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
 * Apply a border to the current selection using the toolbar dropdown
 * @param {import('puppeteer').Page} page
 * @param {'all' | 'outer' | 'top' | 'bottom' | 'left' | 'right' | 'none'} borderType
 */
async function applyBorder(page, borderType) {
  // Click the border button to open the dropdown
  await page.click('#border-btn');
  await sleep(100);

  // Click the appropriate border option
  const buttonId = `#border-${borderType}-btn`;
  await page.click(buttonId);
  await sleep(200);
}

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

  // ==========================================================================
  // Visual rendering tests (canvas pixel verification)
  // ==========================================================================

  'Column style renders background on empty cells (visual)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#3B82F6'; // Blue 500

    // Set column B style
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'D1');
    await sleep(100);

    // Verify pixels at empty cells in column B show the background color
    // B5 (col 1, row 4) should have the blue background
    const posB5 = await getCellPosition(ctx.page, 1, 4);
    const pixelB5 = await getPixelColor(
      ctx.page,
      posB5.x + posB5.width / 2,
      posB5.y + posB5.height / 2
    );
    assertTrue(
      isColorApproximately(pixelB5, testColor, 30),
      `Empty cell B5 should visually show column background (got r=${pixelB5?.r}, g=${pixelB5?.g}, b=${pixelB5?.b})`
    );

    // B10 (col 1, row 9) should also have the blue background
    const posB10 = await getCellPosition(ctx.page, 1, 9);
    const pixelB10 = await getPixelColor(
      ctx.page,
      posB10.x + posB10.width / 2,
      posB10.y + posB10.height / 2
    );
    assertTrue(
      isColorApproximately(pixelB10, testColor, 30),
      `Empty cell B10 should visually show column background (got r=${pixelB10?.r}, g=${pixelB10?.g}, b=${pixelB10?.b})`
    );

    // Cells in other columns should NOT have the background
    const posC5 = await getCellPosition(ctx.page, 2, 4);
    const pixelC5 = await getPixelColor(
      ctx.page,
      posC5.x + posC5.width / 2,
      posC5.y + posC5.height / 2
    );
    assertTrue(
      !isColorApproximately(pixelC5, testColor, 30),
      `Cell C5 should NOT have the column B background color`
    );
  },

  'Row style renders background on empty cells (visual)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#22C55E'; // Green 500

    // Set row 3 style (index 2)
    await clickRowHeader(ctx.page, 2);
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Verify pixels at empty cells in row 3 show the background color
    // E3 (col 4, row 2) should have the green background
    const posE3 = await getCellPosition(ctx.page, 4, 2);
    const pixelE3 = await getPixelColor(
      ctx.page,
      posE3.x + posE3.width / 2,
      posE3.y + posE3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelE3, testColor, 30),
      `Empty cell E3 should visually show row background (got r=${pixelE3?.r}, g=${pixelE3?.g}, b=${pixelE3?.b})`
    );

    // G3 (col 6, row 2) should also have the green background
    const posG3 = await getCellPosition(ctx.page, 6, 2);
    const pixelG3 = await getPixelColor(
      ctx.page,
      posG3.x + posG3.width / 2,
      posG3.y + posG3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelG3, testColor, 30),
      `Empty cell G3 should visually show row background (got r=${pixelG3?.r}, g=${pixelG3?.g}, b=${pixelG3?.b})`
    );

    // Cells in other rows should NOT have the background
    const posE4 = await getCellPosition(ctx.page, 4, 3);
    const pixelE4 = await getPixelColor(
      ctx.page,
      posE4.x + posE4.width / 2,
      posE4.y + posE4.height / 2
    );
    assertTrue(
      !isColorApproximately(pixelE4, testColor, 30),
      `Cell E4 should NOT have the row 3 background color`
    );
  },

  'Cell style overrides column style visually': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const colColor = '#3B82F6'; // Blue
    const cellColor = '#EF4444'; // Red

    // Set column B style
    await clickColumnHeader(ctx.page, 'B');
    await sleep(100);
    await applyBackgroundColor(ctx.page, colColor);
    await sleep(300);

    // Set cell B3 style to override
    await clickCell(ctx.page, 'B3');
    await sleep(100);
    await applyBackgroundColor(ctx.page, cellColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'D1');
    await sleep(100);

    // B3 should show red (cell override)
    const posB3 = await getCellPosition(ctx.page, 1, 2);
    const pixelB3 = await getPixelColor(
      ctx.page,
      posB3.x + posB3.width / 2,
      posB3.y + posB3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelB3, cellColor, 30),
      `B3 should show cell color (red), not column color (got r=${pixelB3?.r}, g=${pixelB3?.g}, b=${pixelB3?.b})`
    );

    // B5 should show blue (column style, no cell override)
    const posB5 = await getCellPosition(ctx.page, 1, 4);
    const pixelB5 = await getPixelColor(
      ctx.page,
      posB5.x + posB5.width / 2,
      posB5.y + posB5.height / 2
    );
    assertTrue(
      isColorApproximately(pixelB5, colColor, 30),
      `B5 should show column color (blue) (got r=${pixelB5?.r}, g=${pixelB5?.g}, b=${pixelB5?.b})`
    );
  },

  'Range style overrides row style visually': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const rowColor = '#22C55E'; // Green
    const rangeColor = '#F59E0B'; // Orange

    // Set row 3 style (index 2)
    await clickRowHeader(ctx.page, 2);
    await sleep(100);
    await applyBackgroundColor(ctx.page, rowColor);
    await sleep(300);

    // Apply range style to C3:D3 (should override row style in that area)
    await selectRange(ctx.page, 'C3', 'D3');
    await sleep(100);
    await applyBackgroundColor(ctx.page, rangeColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // C3 should show orange (range override)
    const posC3 = await getCellPosition(ctx.page, 2, 2);
    const pixelC3 = await getPixelColor(
      ctx.page,
      posC3.x + posC3.width / 2,
      posC3.y + posC3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelC3, rangeColor, 30),
      `C3 should show range color (orange) (got r=${pixelC3?.r}, g=${pixelC3?.g}, b=${pixelC3?.b})`
    );

    // E3 should show green (row style, outside range)
    const posE3 = await getCellPosition(ctx.page, 4, 2);
    const pixelE3 = await getPixelColor(
      ctx.page,
      posE3.x + posE3.width / 2,
      posE3.y + posE3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelE3, rowColor, 30),
      `E3 should show row color (green) (got r=${pixelE3?.r}, g=${pixelE3?.g}, b=${pixelE3?.b})`
    );
  },

  // ==========================================================================
  // Axis border tests (Phase 3)
  // ==========================================================================

  'Setting column border via API applies border to column axis': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply border to column B using the API directly
    const borderStyle = {
      border: {
        top: { style: 'thin', color: '#000000' },
        bottom: { style: 'thin', color: '#000000' },
        left: { style: 'thin', color: '#000000' },
        right: { style: 'thin', color: '#000000' },
      }
    };
    await ctx.page.evaluate(async ({ style }) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setColumnStyle(1, style); // Column B = index 1
    }, { style: borderStyle });
    await sleep(300);

    // Verify column style includes border
    const colStyle = await getColumnStyle(ctx.page, 1); // Column B = index 1
    assertTrue(colStyle !== null, 'Column style should exist');
    assertTrue(colStyle.border !== undefined, 'Column should have border property');
    assertTrue(colStyle.border?.top?.style === 'thin', 'Column should have top border');
    assertTrue(colStyle.border?.bottom?.style === 'thin', 'Column should have bottom border');
    assertTrue(colStyle.border?.left?.style === 'thin', 'Column should have left border');
    assertTrue(colStyle.border?.right?.style === 'thin', 'Column should have right border');

    // Verify effective style at various cells in column B includes border
    const styleB5 = await getEffectiveCellStyle(ctx.page, 1, 4); // B5 = col 1, row 4
    assertTrue(styleB5.border !== undefined, 'B5 should have border');
    assertTrue(styleB5.border?.top?.style === 'thin', 'B5 should inherit column top border');

    // Verify cells in other columns don't have the border
    const styleC5 = await getEffectiveCellStyle(ctx.page, 2, 4); // C5 = col 2, row 4
    assertTrue(!styleC5.border || styleC5.border?.top?.style === 'none', 'C5 should not have border');
  },

  'Setting row border via API applies border to row axis': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply border to row 3 using the API directly
    const borderStyle = {
      border: {
        top: { style: 'thin', color: '#000000' },
        bottom: { style: 'thin', color: '#000000' },
        left: { style: 'thin', color: '#000000' },
        right: { style: 'thin', color: '#000000' },
      }
    };
    await ctx.page.evaluate(async ({ style }) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setRowStyle(2, style); // Row 3 = index 2
    }, { style: borderStyle });
    await sleep(300);

    // Verify row style includes border
    const rowStyle = await getRowStyle(ctx.page, 2); // Row 3 = index 2
    assertTrue(rowStyle !== null, 'Row style should exist');
    assertTrue(rowStyle.border !== undefined, 'Row should have border property');
    assertTrue(rowStyle.border?.top?.style === 'thin', 'Row should have top border');
    assertTrue(rowStyle.border?.bottom?.style === 'thin', 'Row should have bottom border');

    // Verify effective style at various cells in row 3 includes border
    const styleE3 = await getEffectiveCellStyle(ctx.page, 4, 2); // E3 = col 4, row 2
    assertTrue(styleE3.border !== undefined, 'E3 should have border');
    assertTrue(styleE3.border?.top?.style === 'thin', 'E3 should inherit row top border');

    // Verify cells in other rows don't have the border
    const styleE4 = await getEffectiveCellStyle(ctx.page, 4, 3); // E4 = col 4, row 3
    assertTrue(!styleE4.border || styleE4.border?.top?.style === 'none', 'E4 should not have border');
  },

  'Column border is included in axisStyles output': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply left border to column C using the API
    const borderStyle = {
      border: {
        top: { style: 'none', color: '' },
        bottom: { style: 'none', color: '' },
        left: { style: 'thin', color: '#000000' },
        right: { style: 'none', color: '' },
      }
    };
    await ctx.page.evaluate(async ({ style }) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setColumnStyle(2, style); // Column C = index 2
    }, { style: borderStyle });
    await sleep(300);

    // Query viewport and check that axisStyles includes the border
    const viewportResult = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return null;
      return await ctx.app.dataSource.client.queryViewport(0, 0, 10, 10);
    });

    assertTrue(viewportResult !== null, 'Viewport result should exist');
    assertTrue(viewportResult.axisStyles !== undefined, 'axisStyles should exist in viewport result');

    // Find column C (index 2) in axisStyles
    const colCStyle = viewportResult.axisStyles.find(
      s => s.type === 'column' && s.position === 2
    );
    assertTrue(colCStyle !== undefined, 'Column C should be in axisStyles');
    assertTrue(colCStyle.style.border !== undefined, 'Column C style should have border');
    assertTrue(colCStyle.style.border?.left?.style === 'thin', 'Column C should have left border');
  },

  'Row border is included in axisStyles output': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply bottom border to row 4 using the API
    const borderStyle = {
      border: {
        top: { style: 'none', color: '' },
        bottom: { style: 'thin', color: '#000000' },
        left: { style: 'none', color: '' },
        right: { style: 'none', color: '' },
      }
    };
    await ctx.page.evaluate(async ({ style }) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setRowStyle(3, style); // Row 4 = index 3
    }, { style: borderStyle });
    await sleep(300);

    // Query viewport and check that axisStyles includes the border
    const viewportResult = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return null;
      return await ctx.app.dataSource.client.queryViewport(0, 0, 10, 10);
    });

    assertTrue(viewportResult !== null, 'Viewport result should exist');
    assertTrue(viewportResult.axisStyles !== undefined, 'axisStyles should exist in viewport result');

    // Find row 4 (index 3) in axisStyles
    const row4Style = viewportResult.axisStyles.find(
      s => s.type === 'row' && s.position === 3
    );
    assertTrue(row4Style !== undefined, 'Row 4 should be in axisStyles');
    assertTrue(row4Style.style.border !== undefined, 'Row 4 style should have border');
    assertTrue(row4Style.style.border?.bottom?.style === 'thin', 'Row 4 should have bottom border');
  },

  'Axis border with background works together': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#3B82F6'; // Blue

    // Apply both background and border to column D using the API
    const combinedStyle = {
      bgColor: testColor,
      border: {
        top: { style: 'thin', color: '#000000' },
        bottom: { style: 'thin', color: '#000000' },
        left: { style: 'thin', color: '#000000' },
        right: { style: 'thin', color: '#000000' },
      }
    };
    await ctx.page.evaluate(async ({ style }) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setColumnStyle(3, style); // Column D = index 3
    }, { style: combinedStyle });
    await sleep(300);

    // Verify column style has both background and border
    const colStyle = await getColumnStyle(ctx.page, 3);
    assertTrue(colStyle !== null, 'Column style should exist');
    assertEqual(colStyle.bgColor?.toUpperCase(), testColor.toUpperCase(), 'Column should have background color');
    assertTrue(colStyle.border !== undefined, 'Column should have border');
    assertTrue(colStyle.border?.top?.style === 'thin', 'Column should have top border');

    // Verify effective style at cells includes both properties
    const styleD5 = await getEffectiveCellStyle(ctx.page, 3, 4); // D5 = col 3, row 4
    assertEqual(styleD5.bgColor?.toUpperCase(), testColor.toUpperCase(), 'D5 should have background color');
    assertTrue(styleD5.border !== undefined, 'D5 should have border');
    assertTrue(styleD5.border?.top?.style === 'thin', 'D5 should inherit column top border');
  },
};

/**
 * Get pixel color at a specific canvas coordinate
 */
async function getPixelColor(page, x, y) {
  return await page.evaluate(({ x, y }) => {
    const canvas = document.getElementById('grid');
    if (!canvas) return null;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;
    const imageData = ctx.getImageData(x * dpr, y * dpr, 1, 1);
    return {
      r: imageData.data[0],
      g: imageData.data[1],
      b: imageData.data[2],
      a: imageData.data[3],
    };
  }, { x, y });
}

/**
 * Get the position of a cell in canvas coordinates
 */
async function getCellPosition(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const x = HEADER_WIDTH + col * DEFAULT_COL_WIDTH;
    const y = HEADER_HEIGHT + row * DEFAULT_ROW_HEIGHT;

    return {
      x,
      y,
      width: DEFAULT_COL_WIDTH,
      height: DEFAULT_ROW_HEIGHT,
    };
  }, { col, row });
}

/**
 * Check if a color is approximately a certain hex color
 */
function isColorApproximately(pixel, hexColor, tolerance = 10) {
  if (!pixel) return false;

  const r = parseInt(hexColor.slice(1, 3), 16);
  const g = parseInt(hexColor.slice(3, 5), 16);
  const b = parseInt(hexColor.slice(5, 7), 16);

  return (
    Math.abs(pixel.r - r) <= tolerance &&
    Math.abs(pixel.g - g) <= tolerance &&
    Math.abs(pixel.b - b) <= tolerance
  );
}

runTests(tests);
