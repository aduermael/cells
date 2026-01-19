// Cross-Sheet Style Application Test
// Tests that setRangeStyleOnSheet() correctly applies styles to non-active sheets
// Part of Phase 3c of the Axis Flags and Styles plan

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Click a sheet tab by index (0-based)
 */
async function clickSheetTab(page, index) {
  await page.evaluate((idx) => {
    const tabs = document.querySelectorAll('.sheet-tab');
    if (tabs[idx]) {
      tabs[idx].click();
    }
  }, index);
  await sleep(300);
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
  // ============================================================================
  // Phase 3c: Cross-Sheet Style Application Test
  // ============================================================================
  // This test verifies that setRangeStyleOnSheet() can apply styles to a
  // non-active sheet without affecting the active sheet.
  // ============================================================================

  'setRangeStyleOnSheet applies style to non-active sheet': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#10B981'; // Green 500

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Now we're on Sheet2 (index 1). Switch back to Sheet1 (index 0)
    await clickSheetTab(ctx.page, 0);
    await sleep(200);

    // Apply style to B2:D4 on Sheet2 (index 1) while we're on Sheet1
    // This uses the setRangeStyleOnSheet API to target a specific sheet
    const result = await ctx.page.evaluate(async (style) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) {
        return { error: 'Context not available' };
      }
      // Apply style to Sheet2 (index 1), range B2:D4 (cols 1-3, rows 1-3)
      return await ctx.app.dataSource.setRangeStyleOnSheet(
        1,     // sheetIndex (Sheet2)
        1, 1,  // startCol, startRow (B2)
        3, 3,  // endCol, endRow (D4)
        style
      );
    }, { bgColor: testColor });

    console.log('setRangeStyleOnSheet result:', JSON.stringify(result));

    assertTrue(
      result.success === true,
      `setRangeStyleOnSheet should succeed, got: ${JSON.stringify(result)}`
    );

    // Verify that Sheet1 does NOT have the background color at B2
    // Click elsewhere to deselect any selection overlay
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const posB2Sheet1 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2Sheet1 = await getPixelColor(
      ctx.page,
      posB2Sheet1.x + posB2Sheet1.width / 2,
      posB2Sheet1.y + posB2Sheet1.height / 2
    );

    console.log(`Sheet1 B2 pixel color: r=${pixelB2Sheet1?.r}, g=${pixelB2Sheet1?.g}, b=${pixelB2Sheet1?.b}`);

    assertTrue(
      !isColorApproximately(pixelB2Sheet1, testColor, 30),
      `Sheet1 B2 should NOT have the green background color`
    );

    // Switch to Sheet2 and verify the style was applied
    await clickSheetTab(ctx.page, 1);
    await sleep(300);

    // Click elsewhere to deselect
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Check B2 on Sheet2 - should have the green background
    const posB2Sheet2 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2Sheet2 = await getPixelColor(
      ctx.page,
      posB2Sheet2.x + posB2Sheet2.width / 2,
      posB2Sheet2.y + posB2Sheet2.height / 2
    );

    console.log(`Sheet2 B2 pixel color: r=${pixelB2Sheet2?.r}, g=${pixelB2Sheet2?.g}, b=${pixelB2Sheet2?.b}`);

    assertTrue(
      isColorApproximately(pixelB2Sheet2, testColor, 30),
      `Sheet2 B2 should have the green background color (got r=${pixelB2Sheet2?.r}, g=${pixelB2Sheet2?.g}, b=${pixelB2Sheet2?.b})`
    );

    // Also check C3 and D4 on Sheet2
    const posC3 = await getCellPosition(ctx.page, 2, 2);
    const pixelC3 = await getPixelColor(
      ctx.page,
      posC3.x + posC3.width / 2,
      posC3.y + posC3.height / 2
    );

    assertTrue(
      isColorApproximately(pixelC3, testColor, 30),
      `Sheet2 C3 should have the green background color`
    );

    const posD4 = await getCellPosition(ctx.page, 3, 3);
    const pixelD4 = await getPixelColor(
      ctx.page,
      posD4.x + posD4.width / 2,
      posD4.y + posD4.height / 2
    );

    assertTrue(
      isColorApproximately(pixelD4, testColor, 30),
      `Sheet2 D4 should have the green background color`
    );

    // Verify style ranges are on Sheet2
    const styleRanges = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) return [];
      return ctx.app.styleRanges || [];
    });

    console.log('Style ranges on Sheet2:', JSON.stringify(styleRanges, null, 2));

    // Should have at least one style range covering B2:D4
    const hasRange = styleRanges.some(r =>
      r.startCol === 1 && r.startRow === 1 &&
      r.endCol === 3 && r.endRow === 3
    );

    assertTrue(
      hasRange,
      'Should have a style range covering B2:D4 on Sheet2'
    );
  },

  'setRangeStyleOnSheet does not affect active sheet': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const greenColor = '#10B981';
    const blueColor = '#3B82F6';

    // Add a second sheet
    await ctx.page.click('#add-sheet-btn');
    await sleep(300);

    // Go back to Sheet1
    await clickSheetTab(ctx.page, 0);
    await sleep(200);

    // First apply blue background to B2:D4 on Sheet1 using normal setRangeStyle
    await ctx.page.evaluate(async (style) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      // This uses setRangeStyle which operates on active sheet (Sheet1)
      await ctx.app.dataSource.setRangeStyle(1, 1, 3, 3, style);
    }, { bgColor: blueColor });
    await sleep(200);

    // Now apply green background to B2:D4 on Sheet2 using setRangeStyleOnSheet
    await ctx.page.evaluate(async (style) => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return;
      await ctx.app.dataSource.setRangeStyleOnSheet(1, 1, 1, 3, 3, style);
    }, { bgColor: greenColor });
    await sleep(200);

    // Verify Sheet1 still has BLUE (not affected by the Sheet2 operation)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const posB2Sheet1 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2Sheet1 = await getPixelColor(
      ctx.page,
      posB2Sheet1.x + posB2Sheet1.width / 2,
      posB2Sheet1.y + posB2Sheet1.height / 2
    );

    console.log(`Sheet1 B2 should be blue: r=${pixelB2Sheet1?.r}, g=${pixelB2Sheet1?.g}, b=${pixelB2Sheet1?.b}`);

    assertTrue(
      isColorApproximately(pixelB2Sheet1, blueColor, 30),
      `Sheet1 B2 should still have BLUE background (not affected by Sheet2 operation)`
    );

    // Switch to Sheet2 and verify it has GREEN
    await clickSheetTab(ctx.page, 1);
    await sleep(300);
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    const posB2Sheet2 = await getCellPosition(ctx.page, 1, 1);
    const pixelB2Sheet2 = await getPixelColor(
      ctx.page,
      posB2Sheet2.x + posB2Sheet2.width / 2,
      posB2Sheet2.y + posB2Sheet2.height / 2
    );

    console.log(`Sheet2 B2 should be green: r=${pixelB2Sheet2?.r}, g=${pixelB2Sheet2?.g}, b=${pixelB2Sheet2?.b}`);

    assertTrue(
      isColorApproximately(pixelB2Sheet2, greenColor, 30),
      `Sheet2 B2 should have GREEN background`
    );
  },
};

// Run all tests
runTests(tests);
