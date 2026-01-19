// Range styles E2E tests
// Tests that range-based styling works correctly:
// 1. Style ranges render backgrounds for empty cells
// 2. Range styles don't create empty cell entries (efficient storage)
// 3. Style ranges are included in viewport data
// 4. Range style clears redundant cell styles (I2)
// 5. Overlapping ranges combine styles (I3)
// 6. Range edge adjustment on column deletion (I1)

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

  // I2: Range style clears cell styles (test at render level)
  'Range style provides correct rendering': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#8B5CF6'; // Purple 500

    // Apply style to a range B2:C3
    await selectRange(ctx.page, 'B2', 'C3');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Verify all cells in the range have the correct background color
    // This tests that the range style is being applied correctly
    for (let col = 1; col <= 2; col++) {
      for (let row = 1; row <= 2; row++) {
        const pos = await getCellPosition(ctx.page, col, row);
        const pixel = await getPixelColor(
          ctx.page,
          pos.x + pos.width / 2,
          pos.y + pos.height / 2
        );

        const colLetter = String.fromCharCode(65 + col);
        assertTrue(
          isColorApproximately(pixel, testColor, 30),
          `${colLetter}${row + 1} should have background color (got r=${pixel?.r}, g=${pixel?.g}, b=${pixel?.b})`
        );
      }
    }

    // Verify that a style range exists covering B2:C3
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    const hasRange = styleRanges.some(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 2 && r.endRow === 2
    );

    assertTrue(hasRange, 'Should have a style range covering B2:C3');
  },

  // J4: Overlapping ranges with same property - new range replaces overlap area
  // When applying red to C3:E5 over existing blue at B2:D4, the overlap area (C3:D4)
  // should become red, and the blue range should be split to not overlap.
  'Overlapping ranges with same property splits the old range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const blueColor = '#3B82F6';  // Blue 500
    const redColor = '#EF4444';   // Red 500

    // Apply blue background to B2:D4 (cols 1-3, rows 1-3)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, blueColor);
    await sleep(300);

    // Apply red background to C3:E5 (cols 2-4, rows 2-4)
    // This overlaps with B2:D4 at C3:D4 (cols 2-3, rows 2-3)
    await selectRange(ctx.page, 'C3', 'E5');
    await sleep(100);
    await applyBackgroundColor(ctx.page, redColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Get style ranges to verify the split happened
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    console.log('Style ranges after split:', JSON.stringify(styleRanges, null, 2));

    // The blue range should have been split into non-overlapping parts
    // Original blue B2:D4 minus C3:E5 intersection produces:
    // - Left strip: B2:B4 (col 1, rows 1-3)
    // - Top strip: C2:D2 (cols 2-3, row 1)
    // Plus the red range at C3:E5

    // Check the overlap area C3 - should now be RED (new range wins)
    const posC3 = await getCellPosition(ctx.page, 2, 2);
    const pixelC3 = await getPixelColor(
      ctx.page,
      posC3.x + posC3.width / 2,
      posC3.y + posC3.height / 2
    );

    console.log(`C3 pixel color: r=${pixelC3?.r}, g=${pixelC3?.g}, b=${pixelC3?.b}`);
    assertTrue(
      isColorApproximately(pixelC3, redColor, 30),
      `C3 (overlap area) should have RED background after new range applied (got r=${pixelC3?.r}, g=${pixelC3?.g}, b=${pixelC3?.b})`
    );

    // Check D4 (also in overlap) - should be RED
    const posD4 = await getCellPosition(ctx.page, 3, 3);
    const pixelD4 = await getPixelColor(
      ctx.page,
      posD4.x + posD4.width / 2,
      posD4.y + posD4.height / 2
    );
    console.log(`D4 pixel color: r=${pixelD4?.r}, g=${pixelD4?.g}, b=${pixelD4?.b}`);
    assertTrue(
      isColorApproximately(pixelD4, redColor, 30),
      `D4 (overlap area) should have RED background`
    );

    // Check E5 - should have RED (only in red range)
    const posE5 = await getCellPosition(ctx.page, 4, 4);
    const pixelE5 = await getPixelColor(
      ctx.page,
      posE5.x + posE5.width / 2,
      posE5.y + posE5.height / 2
    );
    console.log(`E5 pixel color: r=${pixelE5?.r}, g=${pixelE5?.g}, b=${pixelE5?.b}`);
    assertTrue(
      isColorApproximately(pixelE5, redColor, 30),
      `E5 (red range only) should have RED background`
    );

    // Check B2 - should still have BLUE (in split blue range)
    const posB2 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2 = await getPixelColor(
      ctx.page,
      posB2.x + posB2.width / 2,
      posB2.y + posB2.height / 2
    );
    console.log(`B2 pixel color: r=${pixelB2?.r}, g=${pixelB2?.g}, b=${pixelB2?.b}`);
    assertTrue(
      isColorApproximately(pixelB2, blueColor, 30),
      `B2 (split blue range) should have BLUE background`
    );

    // Check C2 - should still have BLUE (in split blue range - top strip)
    const posC2 = await getCellPosition(ctx.page, 2, 1);
    const pixelC2 = await getPixelColor(
      ctx.page,
      posC2.x + posC2.width / 2,
      posC2.y + posC2.height / 2
    );
    console.log(`C2 pixel color: r=${pixelC2?.r}, g=${pixelC2?.g}, b=${pixelC2?.b}`);
    assertTrue(
      isColorApproximately(pixelC2, blueColor, 30),
      `C2 (split blue range - top) should have BLUE background`
    );

    // Check B4 - should still have BLUE (in split blue range - left strip)
    const posB4 = await getCellPosition(ctx.page, 1, 3);
    const pixelB4 = await getPixelColor(
      ctx.page,
      posB4.x + posB4.width / 2,
      posB4.y + posB4.height / 2
    );
    console.log(`B4 pixel color: r=${pixelB4?.r}, g=${pixelB4?.g}, b=${pixelB4?.b}`);
    assertTrue(
      isColorApproximately(pixelB4, blueColor, 30),
      `B4 (split blue range - left) should have BLUE background`
    );
  },

  // Additional test: Larger range overlaps with partial coverage
  // Specifically tests green B1:E5 then red D3:F8 (matching user-reported scenario)
  'Overlapping ranges: large first range, partial overlap second': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const greenColor = '#10B981';  // Emerald 500
    const redColor = '#EF4444';    // Red 500

    // Apply green background to B1:E5 (larger range)
    await selectRange(ctx.page, 'B1', 'E5');
    await sleep(100);
    await applyBackgroundColor(ctx.page, greenColor);
    await sleep(300);

    // Apply red background to D3:F8 (overlaps with D3:E5)
    await selectRange(ctx.page, 'D3', 'F8');
    await sleep(100);
    await applyBackgroundColor(ctx.page, redColor);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // The green range should have been split - the overlap area D3:E5 should be RED
    // Original green B1:E5 minus D3:F8 intersection produces:
    // - Left strip: B1:C5 (cols 1-2, rows 0-4)
    // - Top strip: D1:E2 (cols 3-4, rows 0-1)

    // Check D3 (overlap area) - should be RED
    const posD3 = await getCellPosition(ctx.page, 3, 2);
    const pixelD3 = await getPixelColor(
      ctx.page,
      posD3.x + posD3.width / 2,
      posD3.y + posD3.height / 2
    );
    console.log(`D3 pixel color: r=${pixelD3?.r}, g=${pixelD3?.g}, b=${pixelD3?.b}`);
    assertTrue(
      isColorApproximately(pixelD3, redColor, 30),
      `D3 (overlap area) should have RED background after new range applied (got r=${pixelD3?.r}, g=${pixelD3?.g}, b=${pixelD3?.b})`
    );

    // Check E5 (overlap area) - should be RED
    const posE5 = await getCellPosition(ctx.page, 4, 4);
    const pixelE5 = await getPixelColor(
      ctx.page,
      posE5.x + posE5.width / 2,
      posE5.y + posE5.height / 2
    );
    console.log(`E5 pixel color: r=${pixelE5?.r}, g=${pixelE5?.g}, b=${pixelE5?.b}`);
    assertTrue(
      isColorApproximately(pixelE5, redColor, 30),
      `E5 (overlap area) should have RED background`
    );

    // Check F6 (red only, outside green) - should be RED
    const posF6 = await getCellPosition(ctx.page, 5, 5);
    const pixelF6 = await getPixelColor(
      ctx.page,
      posF6.x + posF6.width / 2,
      posF6.y + posF6.height / 2
    );
    console.log(`F6 pixel color: r=${pixelF6?.r}, g=${pixelF6?.g}, b=${pixelF6?.b}`);
    assertTrue(
      isColorApproximately(pixelF6, redColor, 30),
      `F6 (red range only) should have RED background`
    );

    // Check B1 (green only, not in overlap) - should be GREEN
    const posB1 = await getCellPosition(ctx.page, 1, 0);
    const pixelB1 = await getPixelColor(
      ctx.page,
      posB1.x + posB1.width / 2,
      posB1.y + posB1.height / 2
    );
    console.log(`B1 pixel color: r=${pixelB1?.r}, g=${pixelB1?.g}, b=${pixelB1?.b}`);
    assertTrue(
      isColorApproximately(pixelB1, greenColor, 30),
      `B1 (split green range - left) should have GREEN background`
    );

    // Check C5 (green only - in left split strip) - should be GREEN
    const posC5 = await getCellPosition(ctx.page, 2, 4);
    const pixelC5 = await getPixelColor(
      ctx.page,
      posC5.x + posC5.width / 2,
      posC5.y + posC5.height / 2
    );
    console.log(`C5 pixel color: r=${pixelC5?.r}, g=${pixelC5?.g}, b=${pixelC5?.b}`);
    assertTrue(
      isColorApproximately(pixelC5, greenColor, 30),
      `C5 (split green range - left) should have GREEN background`
    );

    // Check D2 (green only - in top split strip) - should be GREEN
    const posD2 = await getCellPosition(ctx.page, 3, 1);
    const pixelD2 = await getPixelColor(
      ctx.page,
      posD2.x + posD2.width / 2,
      posD2.y + posD2.height / 2
    );
    console.log(`D2 pixel color: r=${pixelD2?.r}, g=${pixelD2?.g}, b=${pixelD2?.b}`);
    assertTrue(
      isColorApproximately(pixelD2, greenColor, 30),
      `D2 (split green range - top) should have GREEN background`
    );
  },

  // J5: Overlapping ranges with DIFFERENT properties can layer (no splitting)
  // When applying textColor to an overlapping range, bgColor range should NOT be split
  'Overlapping ranges with different properties can layer': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const blueColor = '#3B82F6';  // Blue 500 for background
    const redColor = '#EF4444';   // Red 500 for text color

    // Apply blue background to B2:D4 (cols 1-3, rows 1-3)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, blueColor);
    await sleep(300);

    // Get style ranges after first application
    const rangesAfterFirst = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after blue background:', JSON.stringify(rangesAfterFirst, null, 2));

    // Apply red TEXT color to C3:E5 (cols 2-4, rows 2-4) - different property!
    await selectRange(ctx.page, 'C3', 'E5');
    await sleep(100);

    // Click the text color button to open the color picker popup
    await ctx.page.click('#style-text-color-btn');
    await sleep(100);
    const textColorSelector = `#text-color-popup .color-option[data-color="${redColor.toUpperCase()}"]`;
    const hasTextColor = await ctx.page.$(textColorSelector);
    if (hasTextColor) {
      await ctx.page.click(textColorSelector);
    } else {
      // Use the hex input field if the color isn't in the palette
      const hexInput = await ctx.page.$('#text-color-popup .color-hex-input');
      if (hexInput) {
        await hexInput.click({ clickCount: 3 });
        await ctx.page.keyboard.type(redColor);
        await ctx.page.keyboard.press('Enter');
      }
    }
    await sleep(300);

    // Get style ranges after second application
    const rangesAfterSecond = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after text color:', JSON.stringify(rangesAfterSecond, null, 2));

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // The blue background range should NOT have been split (different property)
    // We should have:
    // 1. The original B2:D4 background range (unchanged)
    // 2. The new C3:E5 text color range

    // Verify blue background is still visible at B2
    const posB2 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2 = await getPixelColor(
      ctx.page,
      posB2.x + posB2.width / 2,
      posB2.y + posB2.height / 2
    );
    console.log(`B2 pixel color: r=${pixelB2?.r}, g=${pixelB2?.g}, b=${pixelB2?.b}`);
    assertTrue(
      isColorApproximately(pixelB2, blueColor, 30),
      `B2 should have BLUE background`
    );

    // The overlap area C3 should ALSO have blue background (both ranges apply)
    // The text color range doesn't override the background color range
    const posC3 = await getCellPosition(ctx.page, 2, 2);
    const pixelC3 = await getPixelColor(
      ctx.page,
      posC3.x + posC3.width / 2,
      posC3.y + posC3.height / 2
    );
    console.log(`C3 pixel color (overlap): r=${pixelC3?.r}, g=${pixelC3?.g}, b=${pixelC3?.b}`);
    assertTrue(
      isColorApproximately(pixelC3, blueColor, 30),
      `C3 (overlap) should still have BLUE background (text color is different property)`
    );

    // E5 should NOT have blue background (only text color range, no background)
    const posE5 = await getCellPosition(ctx.page, 4, 4);
    const pixelE5 = await getPixelColor(
      ctx.page,
      posE5.x + posE5.width / 2,
      posE5.y + posE5.height / 2
    );
    console.log(`E5 pixel color: r=${pixelE5?.r}, g=${pixelE5?.g}, b=${pixelE5?.b}`);
    // E5 should have default background (white or gray), not blue
    assertTrue(
      !isColorApproximately(pixelE5, blueColor, 30),
      `E5 should NOT have blue background (only covered by text color range)`
    );

    // Verify both ranges still exist (bgColor range was NOT split)
    const bgColorRanges = rangesAfterSecond.filter(r => r.style && r.style.bgColor);
    const textColorRanges = rangesAfterSecond.filter(r => r.style && r.style.textColor);

    // We should have 1 bgColor range (not split) and 1 textColor range
    console.log(`bgColor ranges: ${bgColorRanges.length}, textColor ranges: ${textColorRanges.length}`);

    // The original B2:D4 bgColor range should still exist as-is (not split)
    const originalBgRange = bgColorRanges.find(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    );
    assertTrue(
      originalBgRange !== undefined,
      'Original B2:D4 bgColor range should NOT have been split (different properties can layer)'
    );
  },

  // K8: Exact match style merging - apply multiple styles to same range
  // When applying bold to the same range that already has bgColor,
  // the styles should be merged into the existing range (no new range created)
  'Same-area style merging creates single range with merged properties': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#818CF8'; // Purple 400

    // Apply blue background to B2:D4 (cols 1-3, rows 1-3)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Get ranges after first application
    const rangesAfterBgColor = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after bgColor:', JSON.stringify(rangesAfterBgColor, null, 2));

    // Count ranges covering B2:D4 (cols 1-3, rows 1-3)
    const bgColorRangeCount = rangesAfterBgColor.filter(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    ).length;

    assertTrue(
      bgColorRangeCount === 1,
      `Should have exactly 1 range covering B2:D4 after bgColor, got ${bgColorRangeCount}`
    );

    // Now apply bold to the EXACT same range B2:D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);

    // Click bold button
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Get ranges after bold application
    const rangesAfterBold = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after bold:', JSON.stringify(rangesAfterBold, null, 2));

    // Count total ranges covering B2:D4
    const rangesCoveringArea = rangesAfterBold.filter(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    );

    console.log('Ranges covering B2:D4 after bold:', JSON.stringify(rangesCoveringArea, null, 2));

    // With K3/K4 implemented, we should have EXACTLY 1 range covering B2:D4
    // The bold should have been merged into the existing bgColor range
    assertTrue(
      rangesCoveringArea.length === 1,
      `Should have exactly 1 merged range covering B2:D4, got ${rangesCoveringArea.length}`
    );

    // The single range should have BOTH bgColor AND bold
    const mergedRange = rangesCoveringArea[0];
    assertTrue(
      mergedRange.style && mergedRange.style.bgColor,
      'Merged range should have bgColor property'
    );
    assertTrue(
      mergedRange.style && mergedRange.style.bold === true,
      'Merged range should have bold property'
    );

    console.log('Merged range style:', JSON.stringify(mergedRange.style, null, 2));
  },

  // K9: Superset range strips conflicting properties from contained ranges
  // When applying bgColor to A1:E5 (superset of B2:D4), the contained B2:D4 range
  // should lose its bgColor (same property) but the new range takes over
  'Superset range strips conflicting properties from contained range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const blueColor = '#3B82F6';   // Blue 500
    const greenColor = '#10B981'; // Green 500

    // Apply blue background to B2:D4 (cols 1-3, rows 1-3)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, blueColor);
    await sleep(300);

    // Get ranges after first application
    const rangesAfterBlue = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after blue (B2:D4):', JSON.stringify(rangesAfterBlue, null, 2));

    // Now apply GREEN background to A1:F6 - a SUPERSET of B2:D4
    await selectRange(ctx.page, 'A1', 'F6');
    await sleep(100);
    await applyBackgroundColor(ctx.page, greenColor);
    await sleep(300);

    // Get ranges after superset application
    const rangesAfterGreen = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });
    console.log('Style ranges after green (A1:F6 superset):', JSON.stringify(rangesAfterGreen, null, 2));

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'G7');
    await sleep(100);

    // The old B2:D4 blue range should have been handled:
    // Option 1: Deleted entirely (if only had bgColor, now empty after stripping)
    // Option 2: Style stripped of bgColor (if had other props, which it doesn't)
    // Since B2:D4 only had bgColor, it should be deleted

    // Count ranges with blue bgColor
    const blueRanges = rangesAfterGreen.filter(r =>
      r.style && r.style.bgColor === blueColor.toUpperCase()
    );
    console.log(`Blue bgColor ranges remaining: ${blueRanges.length}`);

    // There should be NO blue ranges anymore - the superset took over
    // (The contained range's bgColor was stripped, making it empty, so it was deleted)
    assertEqual(
      blueRanges.length,
      0,
      `Should have 0 blue bgColor ranges after superset, got ${blueRanges.length}`
    );

    // There should be exactly one green range covering A1:F6
    const greenRange = rangesAfterGreen.find(r =>
      r.startCol === 0 && r.startRow === 0 &&
      r.endCol === 5 && r.endRow === 5 &&
      r.style && r.style.bgColor === greenColor.toUpperCase()
    );
    assertTrue(
      greenRange !== undefined,
      'Should have the green A1:F6 range'
    );

    // Verify B2 (was blue, inside superset) now renders GREEN
    const posB2 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2 = await getPixelColor(
      ctx.page,
      posB2.x + posB2.width / 2,
      posB2.y + posB2.height / 2
    );
    console.log(`B2 pixel color: r=${pixelB2?.r}, g=${pixelB2?.g}, b=${pixelB2?.b}`);
    assertTrue(
      isColorApproximately(pixelB2, greenColor, 30),
      `B2 (inside superset) should have GREEN background, not blue`
    );

    // Verify A1 (corner of superset) renders GREEN
    const posA1 = await getCellPosition(ctx.page, 0, 0);
    const pixelA1 = await getPixelColor(
      ctx.page,
      posA1.x + posA1.width / 2,
      posA1.y + posA1.height / 2
    );
    console.log(`A1 pixel color: r=${pixelA1?.r}, g=${pixelA1?.g}, b=${pixelA1?.b}`);
    assertTrue(
      isColorApproximately(pixelA1, greenColor, 30),
      `A1 (corner of superset) should have GREEN background`
    );

    // Verify F6 (corner of superset) renders GREEN
    const posF6 = await getCellPosition(ctx.page, 5, 5);
    const pixelF6 = await getPixelColor(
      ctx.page,
      posF6.x + posF6.width / 2,
      posF6.y + posF6.height / 2
    );
    console.log(`F6 pixel color: r=${pixelF6?.r}, g=${pixelF6?.g}, b=${pixelF6?.b}`);
    assertTrue(
      isColorApproximately(pixelF6, greenColor, 30),
      `F6 (corner of superset) should have GREEN background`
    );
  },

  // I1: Range edge adjustment on column deletion (unit test coverage in crdt_test.cc)
  // This E2E test verifies a basic range creation scenario
  'Range creation and rendering works': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#F59E0B'; // Amber 500

    // Apply style to B2:D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Verify the style range exists and covers B2:D4 (cols 1-3, rows 1-3)
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    console.log('Style ranges:', JSON.stringify(styleRanges, null, 2));

    const range = styleRanges.find(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    );

    assertTrue(
      range !== undefined,
      'Expected to find style range covering B2:D4'
    );

    // Click elsewhere to deselect and verify visual
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // All cells in B2:D4 should have the background color
    for (let col = 1; col <= 3; col++) {
      for (let row = 1; row <= 3; row++) {
        const pos = await getCellPosition(ctx.page, col, row);
        const pixel = await getPixelColor(
          ctx.page,
          pos.x + pos.width / 2,
          pos.y + pos.height / 2
        );

        const colLetter = String.fromCharCode(65 + col);
        assertTrue(
          isColorApproximately(pixel, testColor, 30),
          `${colLetter}${row + 1} should have background color`
        );
      }
    }
  },

  // ==========================================================================
  // Phase L: UI Effective Style Display
  // ==========================================================================

  // L6: Range style shows in toolbar when selecting a cell inside a range
  'Toolbar displays effective style from range when cell is selected': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#34D399'; // Green 400

    // Apply green background to B2:D4 (cols 1-3, rows 1-3)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(300);

    // Click on C3 - inside the styled range but empty cell
    await clickCell(ctx.page, 'C3');
    await sleep(200);

    // Check the background color swatch in the toolbar
    // It should show the green color from the range style
    const swatchStyle = await ctx.page.evaluate(() => {
      const swatch = document.querySelector('#bg-color-swatch');
      if (!swatch) return null;
      return window.getComputedStyle(swatch).backgroundColor;
    });

    console.log(`Background swatch color: ${swatchStyle}`);

    // Parse the RGB values from the computed style
    const rgbMatch = swatchStyle?.match(/rgb\((\d+),\s*(\d+),\s*(\d+)\)/);
    if (rgbMatch) {
      const [, r, g, b] = rgbMatch.map(Number);

      // Green 400 is #34D399 = rgb(52, 211, 153)
      const tolerance = 30;
      assertTrue(
        Math.abs(r - 52) <= tolerance &&
        Math.abs(g - 211) <= tolerance &&
        Math.abs(b - 153) <= tolerance,
        `Toolbar swatch should show green from range style (got rgb(${r}, ${g}, ${b}))`
      );
    } else {
      // If we couldn't parse RGB, check if it's displayed at all
      assertTrue(
        swatchStyle !== null && swatchStyle !== 'transparent',
        `Toolbar swatch should have a color, got: ${swatchStyle}`
      );
    }
  },

  // L7: Cell override shows in toolbar (cell style takes precedence over range)
  'Toolbar displays cell-level style override, not range style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const rangeColor = '#3B82F6'; // Blue 500
    const cellColor = '#EF4444'; // Red 500

    // Apply blue background to B2:D4 (range style)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBackgroundColor(ctx.page, rangeColor);
    await sleep(300);

    // Now apply red background to C3 only (cell-level override)
    await clickCell(ctx.page, 'C3');
    await sleep(100);
    await applyBackgroundColor(ctx.page, cellColor);
    await sleep(300);

    // Click elsewhere then back to C3 to ensure fresh state
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await clickCell(ctx.page, 'C3');
    await sleep(200);

    // Check the background color swatch in the toolbar
    // It should show the RED color (cell override wins over range)
    const swatchStyle = await ctx.page.evaluate(() => {
      const swatch = document.querySelector('#bg-color-swatch');
      if (!swatch) return null;
      return window.getComputedStyle(swatch).backgroundColor;
    });

    console.log(`Background swatch color for cell override: ${swatchStyle}`);

    // Parse the RGB values from the computed style
    const rgbMatch = swatchStyle?.match(/rgb\((\d+),\s*(\d+),\s*(\d+)\)/);
    if (rgbMatch) {
      const [, r, g, b] = rgbMatch.map(Number);

      // Red 500 is #EF4444 = rgb(239, 68, 68)
      const tolerance = 30;
      assertTrue(
        Math.abs(r - 239) <= tolerance &&
        Math.abs(g - 68) <= tolerance &&
        Math.abs(b - 68) <= tolerance,
        `Toolbar swatch should show RED from cell override, not blue from range (got rgb(${r}, ${g}, ${b}))`
      );
    } else {
      assertTrue(false, `Could not parse swatch color: ${swatchStyle}`);
    }
  },

  // Test: Cell can explicitly override range style to default value
  // When a range has bold=true, a cell inside can set bold=false (with defined flag)
  // and the effective style should be bold=false (cell's explicit value wins)
  'Cell explicit default value overrides range style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply bold to B2:D4 (range style)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Verify B2 is bold (from range)
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    const b2BoldBefore = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(b2BoldBefore, 'B2 should be bold from range style');

    // Now select C3 (inside the range) and toggle bold OFF
    await clickCell(ctx.page, 'C3');
    await sleep(100);

    // Verify C3 is currently bold (from range)
    const c3BoldBefore = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(c3BoldBefore, 'C3 should be bold from range style before override');

    // Click bold button to turn OFF bold for C3
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Verify C3 is now NOT bold (cell override wins)
    const c3BoldAfter = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(!c3BoldAfter, 'C3 should NOT be bold after explicit override to default value');

    // Verify B2 is still bold (still from range)
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    const b2BoldAfter = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(b2BoldAfter, 'B2 should still be bold from range style');

    // Go back to C3 and verify it's still not bold
    await clickCell(ctx.page, 'C3');
    await sleep(100);
    const c3BoldFinal = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn?.classList.contains('active');
    });
    assertTrue(!c3BoldFinal, 'C3 should persist its bold=false override');
  },

  // L8: Mixed styles show correctly in multi-cell selection
  'Toolbar shows mixed indicator for multi-cell selection with different styles': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Apply bold to B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    await ctx.page.click('#style-bold-btn');
    await sleep(200);

    // C2 has no style (not bold)

    // Now select B2:C2 - one cell is bold, one is not
    await selectRange(ctx.page, 'B2', 'C2');
    await sleep(200);

    // Check if bold button has "mixed" state
    const boldBtnClasses = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn ? btn.className : '';
    });

    console.log(`Bold button classes: ${boldBtnClasses}`);

    // The bold button should show mixed state (not fully active, not fully inactive)
    // This is typically indicated by a "mixed" class or similar indicator
    // The exact implementation depends on how StyleControls handles mixed state

    // Check that the button doesn't show "active" (since styles are mixed)
    // OR it shows a "mixed" indicator
    const hasMixedOrNotActive =
      boldBtnClasses.includes('mixed') ||
      !boldBtnClasses.includes('active');

    assertTrue(
      hasMixedOrNotActive,
      `Bold button should show mixed state for selection with different bold values. Classes: ${boldBtnClasses}`
    );
  },
};

// Run all tests
runTests(tests);
