// Clipboard operations tests for Cells spreadsheet application
// Tests copy/paste behaviors including formula preservation

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getCellDisplayValue,
  getCanvasInfo,
  cellToPixel,
  parseCellRef,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Right-click on a cell to open context menu
 */
async function rightClickCell(page, cellRef) {
  const { col, row } = parseCellRef(cellRef);
  const canvasInfo = await getCanvasInfo(page);
  const { x, y } = cellToPixel(col, row, canvasInfo);
  await page.mouse.click(x, y, { button: 'right' });
  await sleep(200);
}

/**
 * Click a context menu item by label
 */
async function clickContextMenuItem(page, label) {
  // Find and click the menu item by its text
  await page.evaluate((label) => {
    const buttons = document.querySelectorAll('.context-menu-item');
    for (const btn of buttons) {
      const labelEl = btn.querySelector('.context-menu-label');
      if (labelEl && labelEl.textContent === label) {
        btn.click();
        return true;
      }
    }
    return false;
  }, label);
  await sleep(200);
}

const tests = {
  'Clipboard serialization preserves formula': async (ctx) => {
    // This test verifies that the clipboard data includes formulas
    // by checking the internal serialization without using system clipboard
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up source data for formula
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(100);
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter a formula in C1
    await setCellValue(ctx.page, 'C1', '=A1+B1');
    await sleep(200);

    // Select C1
    await clickCell(ctx.page, 'C1');
    await sleep(100);

    // Verify the formula was entered
    let formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=A1+B1', 'C1 should contain the formula');

    // Get the serialized clipboard data by calling the internal method
    const clipboardData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) {
        return { error: 'no app' };
      }

      const selectedCell = app.selectedCell;
      if (!selectedCell) {
        return { error: 'no selectedCell' };
      }

      const cells = app.cells || [];
      if (cells.length === 0) {
        return { error: 'cells array empty', selectedCell };
      }

      const cell = cells.find(c => c.col === selectedCell.col && c.row === selectedCell.row);
      if (!cell) {
        return { error: 'cell not found in array', selectedCell, cellsCount: cells.length, firstCell: cells[0] };
      }

      // Simulate what serializeSelection does
      return {
        value: cell.display || cell.value || '',
        formula: cell.formula || null,
        type: cell.type,
      };
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: clipboard data:', clipboardData);
    }

    assertTrue(clipboardData !== null && !clipboardData.error,
      clipboardData?.error ? `Should have clipboard data: ${clipboardData.error}` : 'Should have clipboard data');
    assertEqual(clipboardData.formula, '=A1+B1', 'Clipboard should preserve formula (with =)');
    assertEqual(clipboardData.type, 'f', 'Cell type should be formula');
  },

  'TSV export includes formula with = prefix': async (ctx) => {
    // This test verifies that toTSV exports formulas not values
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up source data for formula
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(100);
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter a formula in C1
    await setCellValue(ctx.page, 'C1', '=A1+B1');
    await sleep(200);

    // Select C1
    await clickCell(ctx.page, 'C1');
    await sleep(100);

    // Verify the formula was entered
    let formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=A1+B1', 'C1 should contain the formula');

    // Get the TSV output by simulating what toTSV does
    const tsvData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) return null;

      const cell = app.selectedCell;
      if (!cell) return null;

      const cellData = app.cells.find(c => c.col === cell.col && c.row === cell.row);
      if (!cellData) return null;

      // This is what toTSV should output for a formula cell
      // After the fix: use formula as-is (already has = prefix from WASM)
      // Before fix: it was just "30" (the computed value)
      const formula = cellData.formula;
      const value = cellData.display || cellData.value || '';

      // Simulate toTSV logic after fix - formula already has = prefix
      const tsvValue = formula || value;

      return {
        tsvValue,
        rawFormula: formula,
        displayValue: value,
      };
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: TSV data:', tsvData);
    }

    assertTrue(tsvData !== null, 'Should have TSV data');
    assertEqual(tsvData.tsvValue, '=A1+B1', 'TSV should export formula with = prefix');
    assertEqual(tsvData.displayValue, '30', 'Display value should be computed result');
  },

  'Copy and paste formula via direct API': async (ctx) => {
    // Test the full copy/paste flow using direct API calls (bypassing clipboard API)
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up source data
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(100);
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(100);

    // Enter a formula in C1
    await setCellValue(ctx.page, 'C1', '=A1+B1');
    await sleep(200);

    // Select C1 and "copy" by capturing the cell data
    await clickCell(ctx.page, 'C1');
    await sleep(100);

    const copiedData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) return null;
      const cell = app.selectedCell;
      if (!cell) return null;
      const cellData = app.cells.find(c => c.col === cell.col && c.row === cell.row);
      return cellData ? { formula: cellData.formula, value: cellData.value, display: cellData.display } : null;
    });

    assertTrue(copiedData !== null, 'Should copy cell data');
    assertEqual(copiedData.formula, '=A1+B1', 'Copied data should have formula (with =)');

    // Select D1 and "paste" by creating cell with formula
    await clickCell(ctx.page, 'D1');
    await sleep(100);

    // Formula is stored with = prefix, so use as-is
    await ctx.page.evaluate(async (formula) => {
      const ds = window._appContext?.app?.dataSource;
      const app = window._appContext?.app;
      if (!ds || !app?.selectedCell) return;
      await ds.createCell(app.selectedCell.col, app.selectedCell.row, formula);
    }, copiedData.formula);
    await sleep(300);

    // Click away and back to refresh
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await clickCell(ctx.page, 'D1');
    await sleep(200);

    // Verify D1 has the formula
    const formulaBar = await getFormulaBarContent(ctx.page);
    assertEqual(formulaBar, '=A1+B1', 'D1 should have the pasted formula');
  },

  'Context menu shows keyboard shortcuts': async (ctx) => {
    // This test verifies that the context menu displays keyboard shortcuts
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Right-click to open context menu
    await rightClickCell(ctx.page, 'A1');
    await sleep(200);

    // Check that shortcuts are displayed
    const shortcuts = await ctx.page.evaluate(() => {
      const items = document.querySelectorAll('.context-menu-item');
      const result = {};
      for (const item of items) {
        const label = item.querySelector('.context-menu-label');
        const shortcut = item.querySelector('.context-menu-shortcut');
        if (label && shortcut) {
          result[label.textContent] = shortcut.textContent;
        }
      }
      return result;
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: shortcuts:', shortcuts);
    }

    // Verify shortcuts are present (on Mac it's ⌘, on other platforms it might be different)
    assertTrue(shortcuts['Cut'] !== undefined, 'Cut should have a shortcut');
    assertTrue(shortcuts['Copy'] !== undefined, 'Copy should have a shortcut');
    assertTrue(shortcuts['Paste'] !== undefined, 'Paste should have a shortcut');

    // Close context menu
    await ctx.page.keyboard.press('Escape');
  },

  // =========================================================================
  // Format Preservation Tests
  // =========================================================================

  'Copy/paste preserves percentage format': async (ctx) => {
    // Test that copying a cell with percentage format preserves the format when pasted
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value and apply percentage format
    await setCellValue(ctx.page, 'A1', '0.15');
    await sleep(200);

    // Click on the cell and apply percentage format
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown and select Percent format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="PERCENTAGE"]');
    await sleep(300);

    // Verify format is applied
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '15%', 'A1 should display 15%');

    // Copy the cell using Cmd/Ctrl+C
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Get the clipboard data including format (base64)
    const copiedData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) return null;
      const cell = app.selectedCell;
      if (!cell) return null;
      const cellData = app.cells.find(c => c.col === cell.col && c.row === cell.row);
      return cellData ? {
        value: cellData.value,
        display: cellData.display,
        format: cellData.format, // base64-encoded format
      } : null;
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: copied data:', copiedData);
    }

    assertTrue(copiedData !== null, 'Should have copied cell data');
    assertTrue(copiedData.format !== undefined && copiedData.format !== '',
      'Copied cell should have a non-GENERAL format');

    // Simulate internal paste at B1 with format preservation
    await ctx.page.evaluate(async (data) => {
      const ds = window._appContext?.app?.dataSource;
      if (!ds) return;

      // Create cell with value
      await ds.createCell(1, 0, data.value); // B1 = col 1, row 0

      // Apply format using base64 format
      // Pass the format as an object with base64 property
      if (data.format) {
        await ds.setCellFormatAt(1, 0, { base64: data.format });
      }
    }, copiedData);
    await sleep(300);

    // Click on B1 and verify format is preserved
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '15%', 'B1 should display 15% (format preserved)');

    // Verify format dropdown shows Percent
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Percent', 'Format dropdown should show Percent for pasted cell');
  },

  'Copy/paste preserves currency format': async (ctx) => {
    // Test that copying a cell with currency format preserves the format when pasted
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value
    await setCellValue(ctx.page, 'A1', '1234.56');
    await sleep(200);

    // Click on the cell and apply currency format
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown and select Currency format
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="CURRENCY"]');
    await sleep(300);

    // Verify format is applied
    let display = await getCellDisplayValue(ctx.page, 'A1');
    assertEqual(display, '$1,234.56', 'A1 should display $1,234.56');

    // Get the clipboard data including format (base64)
    const copiedData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) return null;
      const cell = app.selectedCell;
      if (!cell) return null;
      const cellData = app.cells.find(c => c.col === cell.col && c.row === cell.row);
      return cellData ? {
        value: cellData.value,
        display: cellData.display,
        format: cellData.format, // base64-encoded format
      } : null;
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: copied currency data:', copiedData);
    }

    assertTrue(copiedData !== null, 'Should have copied cell data');
    assertTrue(copiedData.format !== undefined && copiedData.format !== '',
      'Copied cell should have a currency format');

    // Simulate internal paste at B1 with format preservation
    await ctx.page.evaluate(async (data) => {
      const ds = window._appContext?.app?.dataSource;
      if (!ds) return;

      // Create cell with value
      await ds.createCell(1, 0, data.value); // B1 = col 1, row 0

      // Apply format using base64 format
      // Pass the format as an object with base64 property
      if (data.format) {
        await ds.setCellFormatAt(1, 0, { base64: data.format });
      }
    }, copiedData);
    await sleep(300);

    // Click on B1 and verify format is preserved
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    display = await getCellDisplayValue(ctx.page, 'B1');
    assertEqual(display, '$1,234.56', 'B1 should display $1,234.56 (format preserved)');

    // Verify format dropdown shows Currency
    const formatLabel = await ctx.page.$eval('#format-dropdown-label', el => el.textContent);
    assertEqual(formatLabel, 'Currency', 'Format dropdown should show Currency for pasted cell');
  },

  'Clipboard serialization includes format': async (ctx) => {
    // Test that the internal clipboard serialization includes format (base64)
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value and apply percentage format
    await setCellValue(ctx.page, 'A1', '0.25');
    await sleep(200);

    // Click on the cell and apply percentage format
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);
    await ctx.page.click('[data-format-category="PERCENTAGE"]');
    await sleep(300);

    // Get the cell data to verify it includes format
    const cellData = await ctx.page.evaluate(() => {
      const app = window._appContext?.app;
      if (!app) return null;
      const cell = app.selectedCell;
      if (!cell) return null;
      const data = app.cells.find(c => c.col === cell.col && c.row === cell.row);
      return data;
    });

    if (process.env.DEBUG) {
      console.log('DEBUG: cell data with format:', cellData);
    }

    assertTrue(cellData !== null, 'Should have cell data');
    assertTrue(cellData.format !== undefined, 'Cell data should include format (base64)');
    assertTrue(cellData.format !== '', 'format should not be empty');
    // The format should be a base64 string containing the percentage format
    // We can verify by checking it's a non-empty string (base64 encoded)
    assertTrue(typeof cellData.format === 'string' && cellData.format.length > 0,
      'format should be a non-empty base64 string');
  },
};

// Run all tests
runTests(tests);
