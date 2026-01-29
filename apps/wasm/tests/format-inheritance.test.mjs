// Format inheritance E2E tests
// Tests that format inheritance works correctly for columns, rows, and ranges:
// 1. Column format is inherited by cells in that column
// 2. Row format is inherited by cells in that row
// 3. Range format is inherited by cells in that range
// 4. Format priority order: cell > range > column > row

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getCellDisplayValue,
  assertEqual,
  sleep,
} from './helpers.mjs';

/**
 * Set column format via data source API
 */
async function setColumnFormat(page, colPosition, format) {
  return page.evaluate(async ({ col, fmt }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.setColumnFormat(col, fmt);
    return true;
  }, { col: colPosition, fmt: format });
}

/**
 * Set row format via data source API
 */
async function setRowFormat(page, rowPosition, format) {
  return page.evaluate(async ({ row, fmt }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.setRowFormat(row, fmt);
    return true;
  }, { row: rowPosition, fmt: format });
}

/**
 * Set range format via data source API
 */
async function setRangeFormat(page, startCol, startRow, endCol, endRow, format) {
  return page.evaluate(async ({ sc, sr, ec, er, fmt }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.setRangeFormat(sc, sr, ec, er, fmt);
    return true;
  }, { sc: startCol, sr: startRow, ec: endCol, er: endRow, fmt: format });
}

/**
 * Set cell format via data source API
 */
async function setCellFormat(page, cellRef, format) {
  return page.evaluate(async ({ ref, fmt }) => {
    const col = ref.charCodeAt(0) - 'A'.charCodeAt(0);
    const row = parseInt(ref.slice(1)) - 1;
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.setCellFormatAt(col, row, fmt);
    return true;
  }, { ref: cellRef, fmt: format });
}

/**
 * Clear column format via data source API
 */
async function clearColumnFormat(page, colPosition) {
  return page.evaluate(async ({ col }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.clearColumnFormat(col);
    return true;
  }, { col: colPosition });
}

/**
 * Clear row format via data source API
 */
async function clearRowFormat(page, rowPosition) {
  return page.evaluate(async ({ row }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.clearRowFormat(row);
    return true;
  }, { row: rowPosition });
}

const tests = {
  'Column format is inherited by cells in that column': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric values in column A
    await setCellValue(ctx.page, 'A1', '1234.5');
    await setCellValue(ctx.page, 'A2', '5678.9');
    await sleep(200);

    // Set currency format on column A (position 0)
    await setColumnFormat(ctx.page, 0, { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(300);

    // Verify cells display with currency format
    const display1 = await getCellDisplayValue(ctx.page, 'A1');
    const display2 = await getCellDisplayValue(ctx.page, 'A2');

    assertEqual(display1, '$1,234.50', 'A1 should inherit column currency format');
    assertEqual(display2, '$5,678.90', 'A2 should inherit column currency format');
  },

  'Row format is inherited by cells in that row': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric values in row 1
    await setCellValue(ctx.page, 'A1', '0.25');
    await setCellValue(ctx.page, 'B1', '0.75');
    await sleep(200);

    // Set percentage format on row 1 (position 0)
    await setRowFormat(ctx.page, 0, { category: 'PERCENTAGE', decimals: 0 });
    await sleep(300);

    // Verify cells display with percentage format
    const display1 = await getCellDisplayValue(ctx.page, 'A1');
    const display2 = await getCellDisplayValue(ctx.page, 'B1');

    assertEqual(display1, '25%', 'A1 should inherit row percentage format');
    assertEqual(display2, '75%', 'B1 should inherit row percentage format');
  },

  'Range format is inherited by cells in that range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric values in a 2x2 range
    await setCellValue(ctx.page, 'B2', '1000');
    await setCellValue(ctx.page, 'B3', '2000');
    await setCellValue(ctx.page, 'C2', '3000');
    await setCellValue(ctx.page, 'C3', '4000');
    await sleep(200);

    // Set number format with separator on range B2:C3 (cols 1-2, rows 1-2)
    await setRangeFormat(ctx.page, 1, 1, 2, 2, { category: 'NUMBER', decimals: 0, separator: true });
    await sleep(300);

    // Verify cells display with thousands separator
    const displayB2 = await getCellDisplayValue(ctx.page, 'B2');
    const displayB3 = await getCellDisplayValue(ctx.page, 'B3');
    const displayC2 = await getCellDisplayValue(ctx.page, 'C2');
    const displayC3 = await getCellDisplayValue(ctx.page, 'C3');

    assertEqual(displayB2, '1,000', 'B2 should inherit range number format');
    assertEqual(displayB3, '2,000', 'B3 should inherit range number format');
    assertEqual(displayC2, '3,000', 'C2 should inherit range number format');
    assertEqual(displayC3, '4,000', 'C3 should inherit range number format');
  },

  'Cell format overrides column format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric value
    await setCellValue(ctx.page, 'A1', '0.5');
    await sleep(200);

    // Set currency format on column A
    await setColumnFormat(ctx.page, 0, { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(200);

    // Set percentage format on cell A1 (overrides column)
    await setCellFormat(ctx.page, 'A1', { category: 'PERCENTAGE', decimals: 0 });
    await sleep(300);

    // Verify cell displays with its own format, not column format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '50%', 'Cell format should override column format');
  },

  'Range format overrides column format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric value
    await setCellValue(ctx.page, 'A1', '1234');
    await sleep(200);

    // Set currency format on column A
    await setColumnFormat(ctx.page, 0, { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(200);

    // Set percentage format on range A1:A1 (overrides column)
    await setRangeFormat(ctx.page, 0, 0, 0, 0, { category: 'PERCENTAGE', decimals: 1 });
    await sleep(300);

    // Verify cell displays with range format, not column format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '123400.0%', 'Range format should override column format');
  },

  'Row format overrides column format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric value at intersection of formatted column and row
    await setCellValue(ctx.page, 'A1', '0.25');
    await sleep(200);

    // Set currency format on column A (lower priority)
    await setColumnFormat(ctx.page, 0, { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(200);

    // Set percentage format on row 1 (higher priority - row > column)
    await setRowFormat(ctx.page, 0, { category: 'PERCENTAGE', decimals: 0 });
    await sleep(300);

    // Verify cell displays with row format (higher priority than column)
    // Priority order: cell > range > row > column
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '25%', 'Row format should override column format');
  },

  'Clearing row format reverts to column format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter numeric value
    await setCellValue(ctx.page, 'A1', '1234.56');
    await sleep(200);

    // Set currency format on column A (lower priority)
    await setColumnFormat(ctx.page, 0, { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(200);

    // Set percentage format on row 1 (higher priority - row > column)
    await setRowFormat(ctx.page, 0, { category: 'PERCENTAGE', decimals: 0 });
    await sleep(200);

    // Verify it shows percentage (row format wins)
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '123456%', 'Should show row format initially (row > column)');

    // Clear row format
    await clearRowFormat(ctx.page, 0);
    await sleep(300);

    // Verify it now shows column format (only column format remains)
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$1,234.56', 'Should revert to column format after clearing row format');
  },
};

runTests(tests, 'format-inheritance');
