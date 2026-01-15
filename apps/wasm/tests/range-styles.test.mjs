// Range styles E2E tests
// Tests that range-based styling works correctly:
// 1. Style ranges render backgrounds for empty cells
// 2. Range styles don't create empty cell entries (efficient storage)
// 3. Style ranges are included in viewport data

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  selectRange,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Apply a background color to the currently selected cell(s) using the toolbar
 * @param {import('puppeteer').Page} page
 * @param {string} color - Hex color (e.g., '#3B82F6')
 */
async function applyBackgroundColor(page, color) {
  // Click the background color button to open the color picker popup
  await page.click('#style-bg-color-btn');
  await sleep(100);

  // Click the color option with the matching data-color attribute
  const colorSelector = `#bg-color-popup .color-option[data-color="${color.toUpperCase()}"]`;
  const hasColor = await page.$(colorSelector);

  if (hasColor) {
    await page.click(colorSelector);
  } else {
    // Use the hex input field if the color isn't in the palette
    const hexInput = await page.$('#bg-color-popup .color-hex-input');
    if (hexInput) {
      await hexInput.click({ clickCount: 3 }); // Select all
      await page.keyboard.type(color);
      await page.keyboard.press('Enter');
    }
  }
  await sleep(200);
}

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

const tests = {
  'Range style creates styleRange entry in viewport data': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#3B82F6'; // Blue 500

    // Select a 3x3 range of empty cells (B2:D4)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Get style ranges from the app context
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    console.log('Style ranges:', JSON.stringify(styleRanges, null, 2));

    // Should have exactly one style range
    assertTrue(
      styleRanges.length >= 1,
      `Expected at least 1 style range, got ${styleRanges.length}`
    );

    // Find the range that covers B2:D4 (cols 1-3, rows 1-3)
    const range = styleRanges.find(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    );

    assertTrue(
      range !== undefined,
      'Expected to find style range covering B2:D4 (cols 1-3, rows 1-3)'
    );

    // Check that the style has the correct background color
    assertTrue(
      range.style && range.style.bgColor,
      'Style range should have bgColor property'
    );
    assertEqual(
      range.style.bgColor.toUpperCase(),
      testColor.toUpperCase(),
      'Style range bgColor should match applied color'
    );
  },

  'Range style renders background for empty cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#10B981'; // Green 500

    // Select a 2x2 range of empty cells (C3:D4)
    await selectRange(ctx.page, 'C3', 'D4');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click elsewhere to deselect (avoid selection overlay)
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check pixel colors in the center of each cell in the range
    // C3 is at col=2, row=2
    const posC3 = await getCellPosition(ctx.page, 2, 2);
    const posD3 = await getCellPosition(ctx.page, 3, 2);
    const posC4 = await getCellPosition(ctx.page, 2, 3);
    const posD4 = await getCellPosition(ctx.page, 3, 3);

    // Check center of C3
    const pixelC3 = await getPixelColor(
      ctx.page,
      posC3.x + posC3.width / 2,
      posC3.y + posC3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelC3, testColor, 30),
      `C3 center should have background color (got r=${pixelC3?.r}, g=${pixelC3?.g}, b=${pixelC3?.b})`
    );

    // Check center of D3
    const pixelD3 = await getPixelColor(
      ctx.page,
      posD3.x + posD3.width / 2,
      posD3.y + posD3.height / 2
    );
    assertTrue(
      isColorApproximately(pixelD3, testColor, 30),
      `D3 center should have background color (got r=${pixelD3?.r}, g=${pixelD3?.g}, b=${pixelD3?.b})`
    );

    // Check center of C4
    const pixelC4 = await getPixelColor(
      ctx.page,
      posC4.x + posC4.width / 2,
      posC4.y + posC4.height / 2
    );
    assertTrue(
      isColorApproximately(pixelC4, testColor, 30),
      `C4 center should have background color (got r=${pixelC4?.r}, g=${pixelC4?.g}, b=${pixelC4?.b})`
    );

    // Check center of D4
    const pixelD4 = await getPixelColor(
      ctx.page,
      posD4.x + posD4.width / 2,
      posD4.y + posD4.height / 2
    );
    assertTrue(
      isColorApproximately(pixelD4, testColor, 30),
      `D4 center should have background color (got r=${pixelD4?.r}, g=${pixelD4?.g}, b=${pixelD4?.b})`
    );
  },

  'Range style does not create empty cell entries': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#FBBF24'; // Amber 400

    // Get initial cell count
    const initialCellCount = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) return 0;
      return ctx.app.cells.length;
    });

    console.log(`Initial cell count: ${initialCellCount}`);

    // Select a 3x3 range of empty cells (E5:G7) - definitely empty area
    await selectRange(ctx.page, 'E5', 'G7');
    await sleep(100);

    // Apply background color using Range system
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Get final cell count
    const finalCellCount = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) return 0;
      return ctx.app.cells.length;
    });

    console.log(`Final cell count: ${finalCellCount}`);

    // Cell count should not increase for the 9 cells in the range
    // (some cells might be created for the anchor cell due to selection, but not all 9)
    const cellsAdded = finalCellCount - initialCellCount;
    console.log(`Cells added: ${cellsAdded}`);

    // The key insight: with Range-based styling, we should NOT create 9 empty cells
    // At most we might have 1 cell for the selection anchor
    assertTrue(
      cellsAdded <= 1,
      `Range style should not create empty cell entries. Expected <= 1 new cells, got ${cellsAdded}`
    );

    // But we SHOULD have a style range
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    assertTrue(
      styleRanges.length >= 1,
      `Expected at least 1 style range, got ${styleRanges.length}`
    );

    // Verify the range covers E5:G7 (cols 4-6, rows 4-6)
    const range = styleRanges.find(r =>
      r.startCol === 4 && r.startRow === 4 &&
      r.endCol === 6 && r.endRow === 6
    );

    assertTrue(
      range !== undefined,
      'Expected to find style range covering E5:G7'
    );
  },

  'Range style background extends across full range area': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#EF4444'; // Red 500

    // Select a horizontal range (B2:E2)
    await selectRange(ctx.page, 'B2', 'E2');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Check that all cells in the range have the background
    // B2, C2, D2, E2 are cols 1-4, row 1
    for (let col = 1; col <= 4; col++) {
      const pos = await getCellPosition(ctx.page, col, 1);
      const pixel = await getPixelColor(
        ctx.page,
        pos.x + pos.width / 2,
        pos.y + pos.height / 2
      );

      const colLetter = String.fromCharCode(65 + col);
      assertTrue(
        isColorApproximately(pixel, testColor, 30),
        `${colLetter}2 should have background color (got r=${pixel?.r}, g=${pixel?.g}, b=${pixel?.b})`
      );
    }

    // Check that cells outside the range do NOT have the background
    // Check A2 (col 0) and F2 (col 5)
    const posA2 = await getCellPosition(ctx.page, 0, 1);
    const pixelA2 = await getPixelColor(
      ctx.page,
      posA2.x + posA2.width / 2,
      posA2.y + posA2.height / 2
    );
    assertTrue(
      !isColorApproximately(pixelA2, testColor, 30),
      `A2 should NOT have the background color`
    );

    const posF2 = await getCellPosition(ctx.page, 5, 1);
    const pixelF2 = await getPixelColor(
      ctx.page,
      posF2.x + posF2.width / 2,
      posF2.y + posF2.height / 2
    );
    assertTrue(
      !isColorApproximately(pixelF2, testColor, 30),
      `F2 should NOT have the background color`
    );
  },
};

// Run all tests
runTests(tests);
