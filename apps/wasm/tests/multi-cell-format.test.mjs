// Multi-cell format/style tests for Cells spreadsheet application
// Tests that format and style changes apply to all cells in a selection

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  selectRange,
  setCellValue,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the current format dropdown label text
 */
async function getFormatDropdownLabel(page) {
  return page.evaluate(() => {
    const label = document.querySelector('#format-dropdown-label');
    return label ? label.textContent : null;
  });
}

/**
 * Click a style button (bold, italic, underline)
 */
async function clickStyleButton(page, style) {
  const selectors = {
    bold: '#style-bold-btn',
    italic: '#style-italic-btn',
    underline: '#style-underline-btn',
  };
  await page.click(selectors[style]);
  await sleep(200);
}

/**
 * Check if a style button is active
 */
async function isStyleButtonActive(page, style) {
  const selectors = {
    bold: '#style-bold-btn',
    italic: '#style-italic-btn',
    underline: '#style-underline-btn',
  };
  return page.evaluate((selector) => {
    const btn = document.querySelector(selector);
    return btn ? btn.classList.contains('active') : false;
  }, selectors[style]);
}

/**
 * Check if a style button shows mixed state
 */
async function isStyleButtonMixed(page, style) {
  const selectors = {
    bold: '#style-bold-btn',
    italic: '#style-italic-btn',
    underline: '#style-underline-btn',
  };
  return page.evaluate((selector) => {
    const btn = document.querySelector(selector);
    return btn ? btn.classList.contains('mixed') : false;
  }, selectors[style]);
}

/**
 * Get effective cell style from data source (resolves cell > range > column > row hierarchy)
 */
async function getCellStyle(page, cellRef) {
  return page.evaluate(async (ref) => {
    const col = ref.charCodeAt(0) - 'A'.charCodeAt(0);
    const row = parseInt(ref.slice(1)) - 1;
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource?.client) return null;
    return ctx.app.dataSource.client.getEffectiveCellStyle(col, row);
  }, cellRef);
}

/**
 * Set cell format using the data source directly
 * Now accepts format properties object instead of legacy format IDs
 */
async function setCellFormat(page, cellRef, formatProps) {
  return page.evaluate(async ({ ref, formatProps }) => {
    const col = ref.charCodeAt(0) - 'A'.charCodeAt(0);
    const row = parseInt(ref.slice(1)) - 1;
    const ctx = window._appContext;
    if (!ctx?.app?.dataSource) return false;
    await ctx.app.dataSource.setCellFormatAt(col, row, formatProps);
    return true;
  }, { ref: cellRef, formatProps });
}

const tests = {
  // ============================================================================
  // Multi-cell style tests (via UI - these work reliably)
  // ============================================================================

  'Apply bold to range applies to all cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up some values
    await setCellValue(ctx.page, 'A1', 'Text 1');
    await setCellValue(ctx.page, 'A2', 'Text 2');
    await sleep(100);

    // Select the range
    await selectRange(ctx.page, 'A1', 'A2');
    await sleep(100);

    // Apply bold
    await clickStyleButton(ctx.page, 'bold');
    await sleep(300);

    // Verify both cells have bold style
    const style1 = await getCellStyle(ctx.page, 'A1');
    const style2 = await getCellStyle(ctx.page, 'A2');

    assertTrue(style1?.bold === true, `A1 should be bold, got: ${JSON.stringify(style1)}`);
    assertTrue(style2?.bold === true, `A2 should be bold, got: ${JSON.stringify(style2)}`);
  },

  'Toggle bold off for entire range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up values and make them bold individually
    await setCellValue(ctx.page, 'B1', 'Bold 1');
    await clickCell(ctx.page, 'B1');
    await clickStyleButton(ctx.page, 'bold');

    await setCellValue(ctx.page, 'B2', 'Bold 2');
    await clickCell(ctx.page, 'B2');
    await clickStyleButton(ctx.page, 'bold');
    await sleep(100);

    // Now select range and toggle bold off
    await selectRange(ctx.page, 'B1', 'B2');
    await sleep(100);
    await clickStyleButton(ctx.page, 'bold');
    await sleep(300);

    // Verify both cells are no longer bold
    const style1 = await getCellStyle(ctx.page, 'B1');
    const style2 = await getCellStyle(ctx.page, 'B2');

    assertTrue(!style1?.bold, `B1 should not be bold, got: ${JSON.stringify(style1)}`);
    assertTrue(!style2?.bold, `B2 should not be bold, got: ${JSON.stringify(style2)}`);
  },

  'Uniform bold shows active state': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells both bold
    await setCellValue(ctx.page, 'C1', 'Bold 1');
    await clickCell(ctx.page, 'C1');
    await clickStyleButton(ctx.page, 'bold');

    await setCellValue(ctx.page, 'C2', 'Bold 2');
    await clickCell(ctx.page, 'C2');
    await clickStyleButton(ctx.page, 'bold');
    await sleep(100);

    // Select range with uniform bold
    await selectRange(ctx.page, 'C1', 'C2');
    await sleep(200);

    // Check the bold button shows active (not mixed)
    const isMixed = await isStyleButtonMixed(ctx.page, 'bold');
    const isActive = await isStyleButtonActive(ctx.page, 'bold');

    assertTrue(!isMixed, 'Bold button should not show mixed state when uniform');
    assertTrue(isActive, 'Bold button should be active when all cells are bold');
  },

  'Mixed bold state shows mixed indicator': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells with different bold states
    await setCellValue(ctx.page, 'D1', 'Bold');
    await clickCell(ctx.page, 'D1');
    await sleep(100);
    await clickStyleButton(ctx.page, 'bold');
    await sleep(100);

    await setCellValue(ctx.page, 'D2', 'Not bold');
    await clickCell(ctx.page, 'D2');
    // D2 is not bold (default), but we need to ensure it has a style entry
    await sleep(100);

    // Select range with mixed bold
    await selectRange(ctx.page, 'D1', 'D2');
    await sleep(300); // Wait longer for style controls to update

    // Check the bold button shows mixed state
    const isMixed = await isStyleButtonMixed(ctx.page, 'bold');
    const isActive = await isStyleButtonActive(ctx.page, 'bold');

    assertTrue(isMixed, 'Bold button should show mixed state for mixed selection');
    assertTrue(!isActive, 'Bold button should not be active when mixed');
  },

  // ============================================================================
  // Multi-cell format tests (via data source - more reliable than UI)
  // ============================================================================

  'Mixed format via data source shows Multiple in dropdown': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells with values
    await setCellValue(ctx.page, 'E1', '100');
    await setCellValue(ctx.page, 'E2', '200');
    await sleep(200);

    // Apply different formats via data source (content-addressed format properties)
    await setCellFormat(ctx.page, 'E1', { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await setCellFormat(ctx.page, 'E2', { category: 'PERCENTAGE', decimals: 0 });
    await sleep(100);

    // Re-fetch viewport to get updated cell data with formats
    await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (ctx?.app?.fetchViewportNow) await ctx.app.fetchViewportNow();
    });
    await sleep(200);

    // Select range with mixed formats
    await selectRange(ctx.page, 'E1', 'E2');
    await sleep(300);

    // Check the format dropdown shows "Multiple"
    const label = await getFormatDropdownLabel(ctx.page);
    assertEqual(label, 'Multiple', `Format dropdown should show "Multiple" for mixed formats, got: ${label}`);
  },

  'Uniform format shows correct label': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells with same format via data source
    await setCellValue(ctx.page, 'F1', '100');
    await setCellValue(ctx.page, 'F2', '200');
    await sleep(200);

    // Apply same format to both (content-addressed format properties)
    await setCellFormat(ctx.page, 'F1', { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await setCellFormat(ctx.page, 'F2', { category: 'CURRENCY', decimals: 2, separator: true, currency: '$' });
    await sleep(200);

    // Select range with uniform format
    await selectRange(ctx.page, 'F1', 'F2');
    await sleep(200);

    // Check the format dropdown shows "Currency" (not "Multiple")
    const label = await getFormatDropdownLabel(ctx.page);
    assertEqual(label, 'Currency', `Format dropdown should show "Currency" for uniform format, got: ${label}`);
  },

  // ============================================================================
  // Style merging tests (ensure styles are properly merged, not replaced)
  // ============================================================================

  'Bold and italic can be applied together': async (ctx) => {
    // Regression test: previously, applying bold would reset italic and vice versa
    // This test verifies that style properties are properly merged, not replaced
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setCellValue(ctx.page, 'G1', 'Bold and Italic');
    await clickCell(ctx.page, 'G1');
    await sleep(100);

    // Apply bold first
    await clickStyleButton(ctx.page, 'bold');
    await sleep(200);

    // Then apply italic - should NOT reset bold
    await clickStyleButton(ctx.page, 'italic');
    await sleep(200);

    // Verify both styles are active
    const style = await getCellStyle(ctx.page, 'G1');
    assertTrue(style?.bold === true, `Cell should be bold, got: ${JSON.stringify(style)}`);
    assertTrue(style?.italic === true, `Cell should be italic, got: ${JSON.stringify(style)}`);
  },
};

// Run all tests
runTests(tests);
