// Custom format tests for Cells spreadsheet application
// Tests custom format creation, persistence, and application
//
// NOTE: Collaboration sync tests for custom formats have been removed because
// custom format sync between peers is not yet implemented. The solo tests below
// verify that custom formats work correctly for a single user.

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Create a custom format via the client API and return the format properties
 */
async function createCustomFormat(page, formatCode) {
  return await page.evaluate(async (code) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) {
      throw new Error('App context not available');
    }
    const result = await ctx.app.dataSource.client.createCustomFormat(code);
    if (result.error) {
      throw new Error(result.error);
    }
    // Return the format properties (content-addressed format system)
    return result.format;
  }, formatCode);
}

/**
 * Apply a format to a cell by reference (e.g., "A1")
 * @param {Object} format - Format properties (content-addressed format)
 */
async function setCellFormat(page, cellRef, format) {
  return await page.evaluate(async ({ cellRef, format }) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) {
      throw new Error('App context not available');
    }
    // Parse cell reference to get col/row
    const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
    if (!match) throw new Error('Invalid cell reference');
    const colLetter = match[1].toUpperCase();
    const row = parseInt(match[2], 10) - 1; // 0-indexed
    let col = 0;
    for (let i = 0; i < colLetter.length; i++) {
      col = col * 26 + (colLetter.charCodeAt(i) - 64);
    }
    col -= 1; // 0-indexed

    // setCellFormatAt now takes format properties, not format ID
    const result = await ctx.app.dataSource.client.setCellFormatAt(col, row, format);
    return result.success;
  }, { cellRef, format });
}

/**
 * Get the list of available formats (including custom)
 */
async function getAvailableFormats(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) {
      throw new Error('App context not available');
    }
    return await ctx.app.dataSource.client.getAvailableFormats();
  });
}

/**
 * Save workbook to .cells format string
 */
async function exportToCells(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) {
      throw new Error('App context not available');
    }
    const result = await ctx.app.dataSource.client.exportCells();
    // Convert ArrayBuffer to string
    const decoder = new TextDecoder();
    return decoder.decode(result.data);
  });
}

/**
 * Load workbook from .cells format string
 */
async function loadFromCells(page, content) {
  return await page.evaluate(async (content) => {
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) {
      throw new Error('App context not available');
    }
    const result = await ctx.app.dataSource.client.loadCells(content);
    return result.success;
  }, content);
}

const tests = {
  'Create custom format and apply to cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number
    await setCellValue(ctx.page, 'A1', '1234.5678');
    await sleep(200);

    // Create a format with 3 decimal places and thousands separator
    // Note: #,##0.000 is parsed as NUMBER format (not CUSTOM) because it's a recognized pattern
    const format = await createCustomFormat(ctx.page, '#,##0.000');
    assertTrue(format, 'Should return format properties');
    assertTrue(format.category === 'NUMBER', 'Format category should be NUMBER (recognized format)');
    assertEqual(format.decimals, 3, 'Format should have 3 decimals');
    assertTrue(format.separator === true, 'Format should have thousands separator');

    // Apply the format to the cell
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const success = await setCellFormat(ctx.page, 'A1', format);
    assertTrue(success, 'setCellFormat should succeed');
    await sleep(200);

    // Verify the display value uses the format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1,234.568', 'Cell should display with format (3 decimals, thousands separator)');
  },

  'Create percentage format via createCustomFormat': async (ctx) => {
    // Note: With content-addressed formats, createCustomFormat doesn't register formats globally.
    // It simply parses a format code and returns the format properties.
    // getAvailableFormats returns predefined format templates.
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create a percentage format via createCustomFormat
    const format = await createCustomFormat(ctx.page, '0.00%');
    assertTrue(format, 'Should return format properties');
    assertEqual(format.category, 'PERCENTAGE', 'Format category should be PERCENTAGE');
    assertEqual(format.decimals, 2, 'Format should have 2 decimals');
  },

  'Custom format persists through save/load': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value and create custom format with prefix text
    await setCellValue(ctx.page, 'A1', '9876.54');
    await sleep(200);

    // This format has a custom prefix, so it should be category CUSTOM
    const format = await createCustomFormat(ctx.page, '"Value: "#,##0.00');
    assertTrue(format, 'Should return format properties');
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await setCellFormat(ctx.page, 'A1', format);
    await sleep(200);

    // Verify initial display
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, 'Value: 9,876.54', 'Cell should display with custom prefix format');

    // Save to .cells format
    const cellsContent = await exportToCells(ctx.page);
    assertTrue(cellsContent.length > 0, '.cells file should have content');

    // Reload the page and load the file
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await loadFromCells(ctx.page, cellsContent);
    await sleep(300);

    // Verify the format still works
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, 'Value: 9,876.54', 'After reload, cell should still display with custom format');
  },

  'Multiple formats work correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create multiple formats (returns format properties)
    const format1 = await createCustomFormat(ctx.page, '[Red]#,##0.00');
    const format2 = await createCustomFormat(ctx.page, '0.0000');
    const format3 = await createCustomFormat(ctx.page, '"$"#,##0" USD"');

    assertTrue(format1, 'format1 should be returned');
    assertTrue(format2, 'format2 should be returned');
    assertTrue(format3, 'format3 should be returned');

    // Enter values
    await setCellValue(ctx.page, 'A1', '100');
    await setCellValue(ctx.page, 'A2', '3.14159');
    await setCellValue(ctx.page, 'A3', '999');
    await sleep(200);

    // Apply different formats (using format properties)
    await setCellFormat(ctx.page, 'A1', format1);
    await setCellFormat(ctx.page, 'A2', format2);
    await setCellFormat(ctx.page, 'A3', format3);
    await sleep(200);

    // Verify displays
    const display1 = await getCellDisplayValue(ctx.page, 'A1');
    const display2 = await getCellDisplayValue(ctx.page, 'A2');
    const display3 = await getCellDisplayValue(ctx.page, 'A3');

    assertEqual(display1, '100.00', 'A1 should use number format with 2 decimals (color not visible in text)');
    assertEqual(display2, '3.1416', 'A2 should use 4 decimal format');
    assertEqual(display3, '$999 USD', 'A3 should use custom currency suffix format');
  },
};

runTests(tests);
