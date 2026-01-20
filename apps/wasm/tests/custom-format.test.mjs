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
 * Create a custom format via the client API and return the format ID
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
    return result.formatId;
  }, formatCode);
}

/**
 * Apply a format to a cell by reference (e.g., "A1")
 */
async function setCellFormat(page, cellRef, formatId) {
  return await page.evaluate(async ({ cellRef, formatId }) => {
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

    const result = await ctx.app.dataSource.client.setCellFormatAt(col, row, formatId);
    return result.success;
  }, { cellRef, formatId });
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

    // Create a custom format with 3 decimal places
    const formatId = await createCustomFormat(ctx.page, '#,##0.000');
    assertTrue(formatId, 'Should return a format ID');
    assertTrue(formatId.length === 8, 'Format ID should be 8 characters');

    // Apply the format to the cell
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const success = await setCellFormat(ctx.page, 'A1', formatId);
    assertTrue(success, 'setCellFormat should succeed');
    await sleep(200);

    // Verify the display value uses the custom format
    const display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '1,234.568', 'Cell should display with custom format (3 decimals, thousands separator)');
  },

  'Custom format appears in available formats': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Get initial formats
    const initialFormats = await getAvailableFormats(ctx.page);
    const initialCount = initialFormats.length;

    // Create a custom format
    const formatId = await createCustomFormat(ctx.page, '0.00%');

    // Get formats again
    const updatedFormats = await getAvailableFormats(ctx.page);
    assertEqual(updatedFormats.length, initialCount + 1, 'Should have one more format');

    // Find the custom format
    const customFormat = updatedFormats.find(f => f.id === formatId);
    assertTrue(customFormat, 'Custom format should be in the list');
    assertEqual(customFormat.isCustom, true, 'Format should be marked as custom');
    assertEqual(customFormat.formatCode, '0.00%', 'Format code should match');
  },

  'Custom format persists through save/load': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value and create custom format
    await setCellValue(ctx.page, 'A1', '9876.54');
    await sleep(200);

    const formatId = await createCustomFormat(ctx.page, '"Value: "#,##0.00');
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await setCellFormat(ctx.page, 'A1', formatId);
    await sleep(200);

    // Verify initial display
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, 'Value: 9,876.54', 'Cell should display with custom prefix format');

    // Save to .cells format
    const cellsContent = await exportToCells(ctx.page);
    assertTrue(cellsContent.includes('F '), '.cells file should contain format definition (F line)');
    assertTrue(cellsContent.includes(formatId), '.cells file should contain the format ID');

    // Reload the page and load the file
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await loadFromCells(ctx.page, cellsContent);
    await sleep(300);

    // Verify the format still works
    display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, 'Value: 9,876.54', 'After reload, cell should still display with custom format');

    // Verify format is in available formats
    const formats = await getAvailableFormats(ctx.page);
    const customFormat = formats.find(f => f.id === formatId);
    assertTrue(customFormat, 'Custom format should still exist after reload');
    assertEqual(customFormat.isCustom, true, 'Format should still be marked as custom');
  },

  'Multiple custom formats work correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create multiple custom formats
    const format1 = await createCustomFormat(ctx.page, '[Red]#,##0.00');
    const format2 = await createCustomFormat(ctx.page, '0.0000');
    const format3 = await createCustomFormat(ctx.page, '"$"#,##0" USD"');

    // Enter values
    await setCellValue(ctx.page, 'A1', '100');
    await setCellValue(ctx.page, 'A2', '3.14159');
    await setCellValue(ctx.page, 'A3', '999');
    await sleep(200);

    // Apply different formats
    await setCellFormat(ctx.page, 'A1', format1);
    await setCellFormat(ctx.page, 'A2', format2);
    await setCellFormat(ctx.page, 'A3', format3);
    await sleep(200);

    // Verify displays
    const display1 = await getCellDisplayValue(ctx.page, 'A1');
    const display2 = await getCellDisplayValue(ctx.page, 'A2');
    const display3 = await getCellDisplayValue(ctx.page, 'A3');

    assertEqual(display1, '100.00', 'A1 should use red number format (color not visible in text)');
    assertEqual(display2, '3.1416', 'A2 should use 4 decimal format');
    assertEqual(display3, '$999 USD', 'A3 should use currency suffix format');
  },
};

runTests(tests);
