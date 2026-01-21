// Local Style Application Tests
// Tests that styling operations work correctly on a single peer (no collaboration)
// This helps isolate issues where styles don't appear on the originating peer.
//
// Run with HEADED=1 for visible browser:
//   HEADED=1 bazel run :e2e -- local-style
//
// Run standalone:
//   bazel run :e2e -- local-style

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Apply a background color to the currently selected cell(s) using the toolbar
 * @param {Page} page - Puppeteer page
 * @param {string} color - Hex color (e.g., '#3B82F6')
 */
async function applyBackgroundColor(page, color) {
  await page.click('#style-bg-color-btn');
  await sleep(100);

  const colorSelector = `#bg-color-popup .color-option[data-color="${color.toUpperCase()}"]`;
  const hasColor = await page.$(colorSelector);

  if (hasColor) {
    // Click via JavaScript to ensure event handlers fire
    await page.$eval(colorSelector, el => el.click());
  } else {
    // Fall back to hex input for colors not in palette
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
 * Toggle bold on the currently selected cell(s)
 */
async function applyBold(page) {
  await page.click('#style-bold-btn');
  await sleep(200);
}

/**
 * Get the effective style of a cell by querying the WASM engine directly.
 */
async function getCellStyle(page, cellRef) {
  const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
  if (!match) throw new Error(`Invalid cell reference: ${cellRef}`);

  const colStr = match[1].toUpperCase();
  const row = parseInt(match[2], 10) - 1;
  let col = 0;
  for (let i = 0; i < colStr.length; i++) {
    col = col * 26 + (colStr.charCodeAt(i) - 64);
  }
  col -= 1;

  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app?.dataSource) return null;

    try {
      const style = await ctx.app.dataSource.getEffectiveCellStyle(col, row);
      return style;
    } catch (e) {
      console.error('[getCellStyle] Error:', e);
      return null;
    }
  }, { col, row });
}

// Color palette constants
const COLORS = {
  BLUE_500: '#3B82F6',
  GREEN_500: '#10B981',
  RED_500: '#EF4444',
  AMBER_400: '#FBBF24',
};

const tests = {
  // Bug #3: Style changes don't appear on the originating peer
  // This test verifies that a single peer applying a style can see it locally
  'Single peer can apply and see background color locally': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in A1
    await setCellValue(ctx.page, 'A1', 'Test');
    await sleep(200);

    // Click A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Apply blue background color
    await applyBackgroundColor(ctx.page, COLORS.BLUE_500);
    await sleep(500);

    // Verify the style is applied by querying the WASM engine
    const style = await getCellStyle(ctx.page, 'A1');

    console.log('A1 style after applying blue background:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'A1 should have style info');
    assertEqual(
      style?.bgColor?.toUpperCase(),
      COLORS.BLUE_500.toUpperCase(),
      'A1 bgColor should be blue'
    );
  },

  'Single peer can apply and see bold locally': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in B1
    await setCellValue(ctx.page, 'B1', 'Bold');
    await sleep(200);

    // Click B1 to select it
    await clickCell(ctx.page, 'B1');
    await sleep(100);

    // Apply bold
    await applyBold(ctx.page);
    await sleep(500);

    // Verify the style is applied
    const style = await getCellStyle(ctx.page, 'B1');

    console.log('B1 style after applying bold:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'B1 should have style info');
    assertEqual(style?.bold, true, 'B1 should be bold');
  },

  'Style persists after clicking away and back': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in C1
    await setCellValue(ctx.page, 'C1', 'Persist');
    await sleep(200);

    // Apply green background color
    await clickCell(ctx.page, 'C1');
    await sleep(100);
    await applyBackgroundColor(ctx.page, COLORS.GREEN_500);
    await sleep(300);

    // Click away to deselect
    await clickCell(ctx.page, 'E5');
    await sleep(300);

    // Click back to C1
    await clickCell(ctx.page, 'C1');
    await sleep(300);

    // Verify the style is still applied
    const style = await getCellStyle(ctx.page, 'C1');

    console.log('C1 style after clicking away and back:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'C1 should have style info');
    assertEqual(
      style?.bgColor?.toUpperCase(),
      COLORS.GREEN_500.toUpperCase(),
      'C1 bgColor should still be green after clicking away and back'
    );
  },

  'Empty cell can have background color applied': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click D1 (empty cell) to select it
    await clickCell(ctx.page, 'D1');
    await sleep(100);

    // Apply red background color to empty cell
    await applyBackgroundColor(ctx.page, COLORS.RED_500);
    await sleep(500);

    // Verify the style is applied to empty cell
    const style = await getCellStyle(ctx.page, 'D1');

    console.log('D1 (empty) style after applying red background:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'D1 should have style info even when empty');
    assertEqual(
      style?.bgColor?.toUpperCase(),
      COLORS.RED_500.toUpperCase(),
      'D1 bgColor should be red'
    );
  },

  'Multiple styles can be applied to same cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in E1
    await setCellValue(ctx.page, 'E1', 'Multi');
    await sleep(200);

    // Apply amber background
    await clickCell(ctx.page, 'E1');
    await sleep(100);
    await applyBackgroundColor(ctx.page, COLORS.AMBER_400);
    await sleep(200);

    // Apply bold
    await applyBold(ctx.page);
    await sleep(500);

    // Verify both styles are applied
    const style = await getCellStyle(ctx.page, 'E1');

    console.log('E1 style after applying amber background and bold:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'E1 should have style info');
    assertEqual(
      style?.bgColor?.toUpperCase(),
      COLORS.AMBER_400.toUpperCase(),
      'E1 bgColor should be amber'
    );
    assertEqual(style?.bold, true, 'E1 should be bold');
  },
};

// Run all tests
runTests(tests);
