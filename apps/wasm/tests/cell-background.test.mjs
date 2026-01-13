// Cell background rendering tests for Cells spreadsheet application
// Tests that cell backgrounds are drawn edge-to-edge without gaps

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
  // The popup has color buttons with data-color="#XXXXXX" attributes
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
 * @param {import('puppeteer').Page} page
 * @param {number} x - X coordinate on canvas
 * @param {number} y - Y coordinate on canvas
 * @returns {Promise<{r: number, g: number, b: number, a: number}>}
 */
async function getPixelColor(page, x, y) {
  return await page.evaluate(({ x, y }) => {
    const canvas = document.getElementById('grid');
    if (!canvas) return null;
    const ctx = canvas.getContext('2d');
    // Account for device pixel ratio
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
 * @param {import('puppeteer').Page} page
 * @param {number} col - Column index
 * @param {number} row - Row index
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
 * @param {{r: number, g: number, b: number}} pixel
 * @param {string} hexColor - Hex color string (e.g., '#FF0000')
 * @param {number} tolerance - Color tolerance (0-255)
 */
function isColorApproximately(pixel, hexColor, tolerance = 10) {
  if (!pixel) return false;

  // Parse hex color
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
  'Adjacent cells with same background have no gaps': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#3B82F6'; // Blue 500 (in palette)

    // Select cells A1:C1 (three adjacent cells in a row)
    await selectRange(ctx.page, 'A1', 'C1');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(200);

    // Click elsewhere to deselect (avoid selection overlay)
    await clickCell(ctx.page, 'E5');
    await sleep(100);

    // Get positions of the cells
    const posA1 = await getCellPosition(ctx.page, 0, 0);
    const posB1 = await getCellPosition(ctx.page, 1, 0);
    const posC1 = await getCellPosition(ctx.page, 2, 0);

    // Check pixels at the boundaries between cells
    // The boundary between A1 and B1 is at x = posA1.x + posA1.width
    // We check pixels just inside each cell (not on the grid line itself)
    const boundaryAB = posA1.x + posA1.width;
    const boundaryBC = posB1.x + posB1.width;
    const midY = posA1.y + posA1.height / 2;

    // Check pixels just inside each cell at the boundary
    // The grid line is 1px wide, so we check 2px inside each cell
    // If there were a gap, there would be white/default color between cells
    const pixelA1Right = await getPixelColor(ctx.page, boundaryAB - 2, midY);
    assertTrue(
      isColorApproximately(pixelA1Right, testColor, 30),
      `Pixel at right edge of A1 should be background color (got r=${pixelA1Right?.r}, g=${pixelA1Right?.g}, b=${pixelA1Right?.b})`
    );

    const pixelB1Left = await getPixelColor(ctx.page, boundaryAB + 2, midY);
    assertTrue(
      isColorApproximately(pixelB1Left, testColor, 30),
      `Pixel at left edge of B1 should be background color (got r=${pixelB1Left?.r}, g=${pixelB1Left?.g}, b=${pixelB1Left?.b})`
    );

    const pixelB1Right = await getPixelColor(ctx.page, boundaryBC - 2, midY);
    assertTrue(
      isColorApproximately(pixelB1Right, testColor, 30),
      `Pixel at right edge of B1 should be background color (got r=${pixelB1Right?.r}, g=${pixelB1Right?.g}, b=${pixelB1Right?.b})`
    );

    const pixelC1Left = await getPixelColor(ctx.page, boundaryBC + 2, midY);
    assertTrue(
      isColorApproximately(pixelC1Left, testColor, 30),
      `Pixel at left edge of C1 should be background color (got r=${pixelC1Left?.r}, g=${pixelC1Left?.g}, b=${pixelC1Left?.b})`
    );

    // Also verify the cell centers have the correct color
    const pixelCenterA1 = await getPixelColor(ctx.page, posA1.x + posA1.width / 2, midY);
    assertTrue(
      isColorApproximately(pixelCenterA1, testColor),
      'A1 center should have background color'
    );

    const pixelCenterB1 = await getPixelColor(ctx.page, posB1.x + posB1.width / 2, midY);
    assertTrue(
      isColorApproximately(pixelCenterB1, testColor),
      'B1 center should have background color'
    );
  },

  'Vertically adjacent cells with same background have no gaps': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#10B981'; // Green 500 (in palette)

    // Select cells A1:A3 (three adjacent cells in a column)
    await selectRange(ctx.page, 'A1', 'A3');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(200);

    // Click elsewhere to deselect (avoid selection overlay)
    await clickCell(ctx.page, 'E5');
    await sleep(100);

    // Get positions of the cells
    const posA1 = await getCellPosition(ctx.page, 0, 0);
    const posA2 = await getCellPosition(ctx.page, 0, 1);

    // Check pixels at the boundaries between cells
    const boundaryA1A2 = posA1.y + posA1.height;
    const boundaryA2A3 = posA2.y + posA2.height;
    const midX = posA1.x + posA1.width / 2;

    // Check pixels just inside each cell at the boundary
    // The grid line is 1px wide, so we check 2px inside each cell
    const pixelA1Bottom = await getPixelColor(ctx.page, midX, boundaryA1A2 - 2);
    assertTrue(
      isColorApproximately(pixelA1Bottom, testColor, 30),
      `Pixel at bottom edge of A1 should be background color (got r=${pixelA1Bottom?.r}, g=${pixelA1Bottom?.g}, b=${pixelA1Bottom?.b})`
    );

    const pixelA2Top = await getPixelColor(ctx.page, midX, boundaryA1A2 + 2);
    assertTrue(
      isColorApproximately(pixelA2Top, testColor, 30),
      `Pixel at top edge of A2 should be background color (got r=${pixelA2Top?.r}, g=${pixelA2Top?.g}, b=${pixelA2Top?.b})`
    );

    const pixelA2Bottom = await getPixelColor(ctx.page, midX, boundaryA2A3 - 2);
    assertTrue(
      isColorApproximately(pixelA2Bottom, testColor, 30),
      `Pixel at bottom edge of A2 should be background color (got r=${pixelA2Bottom?.r}, g=${pixelA2Bottom?.g}, b=${pixelA2Bottom?.b})`
    );

    const pixelA3Top = await getPixelColor(ctx.page, midX, boundaryA2A3 + 2);
    assertTrue(
      isColorApproximately(pixelA3Top, testColor, 30),
      `Pixel at top edge of A3 should be background color (got r=${pixelA3Top?.r}, g=${pixelA3Top?.g}, b=${pixelA3Top?.b})`
    );
  },

  'Grid of cells with background has no gaps at corners': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#FBBF24'; // Amber 400 (in palette)

    // Select cells A1:B2 (2x2 grid)
    await selectRange(ctx.page, 'A1', 'B2');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(200);

    // Click elsewhere to deselect (avoid selection overlay)
    await clickCell(ctx.page, 'E5');
    await sleep(100);

    // Get positions
    const posA1 = await getCellPosition(ctx.page, 0, 0);

    // Check pixels near the corner where all 4 cells meet
    // The corner itself has grid lines, so we check pixels just inside each quadrant
    const cornerX = posA1.x + posA1.width;
    const cornerY = posA1.y + posA1.height;

    // Check A1 (top-left quadrant) - 3px inside from corner
    const pixelA1 = await getPixelColor(ctx.page, cornerX - 3, cornerY - 3);
    assertTrue(
      isColorApproximately(pixelA1, testColor, 30),
      `A1 near corner should be background color (got r=${pixelA1?.r}, g=${pixelA1?.g}, b=${pixelA1?.b})`
    );

    // Check B1 (top-right quadrant)
    const pixelB1 = await getPixelColor(ctx.page, cornerX + 3, cornerY - 3);
    assertTrue(
      isColorApproximately(pixelB1, testColor, 30),
      `B1 near corner should be background color (got r=${pixelB1?.r}, g=${pixelB1?.g}, b=${pixelB1?.b})`
    );

    // Check A2 (bottom-left quadrant)
    const pixelA2 = await getPixelColor(ctx.page, cornerX - 3, cornerY + 3);
    assertTrue(
      isColorApproximately(pixelA2, testColor, 30),
      `A2 near corner should be background color (got r=${pixelA2?.r}, g=${pixelA2?.g}, b=${pixelA2?.b})`
    );

    // Check B2 (bottom-right quadrant)
    const pixelB2 = await getPixelColor(ctx.page, cornerX + 3, cornerY + 3);
    assertTrue(
      isColorApproximately(pixelB2, testColor, 30),
      `B2 near corner should be background color (got r=${pixelB2?.r}, g=${pixelB2?.g}, b=${pixelB2?.b})`
    );
  },

  'Cell background extends to cell edges': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testColor = '#EF4444'; // Red 500 (in palette)

    // Select just cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Apply background color
    await applyBackgroundColor(ctx.page, testColor);
    await sleep(200);

    // Click elsewhere to deselect (avoid selection overlay)
    await clickCell(ctx.page, 'E5');
    await sleep(100);

    // Get position of B2
    const posB2 = await getCellPosition(ctx.page, 1, 1);

    // Check pixels near all four edges of the cell (1-2 pixels inside from each edge)
    // Left edge
    const pixelLeftEdge = await getPixelColor(ctx.page, posB2.x + 2, posB2.y + posB2.height / 2);
    assertTrue(
      isColorApproximately(pixelLeftEdge, testColor),
      `Left edge should have background color (got r=${pixelLeftEdge?.r}, g=${pixelLeftEdge?.g}, b=${pixelLeftEdge?.b})`
    );

    // Right edge
    const pixelRightEdge = await getPixelColor(ctx.page, posB2.x + posB2.width - 2, posB2.y + posB2.height / 2);
    assertTrue(
      isColorApproximately(pixelRightEdge, testColor),
      `Right edge should have background color (got r=${pixelRightEdge?.r}, g=${pixelRightEdge?.g}, b=${pixelRightEdge?.b})`
    );

    // Top edge
    const pixelTopEdge = await getPixelColor(ctx.page, posB2.x + posB2.width / 2, posB2.y + 2);
    assertTrue(
      isColorApproximately(pixelTopEdge, testColor),
      `Top edge should have background color (got r=${pixelTopEdge?.r}, g=${pixelTopEdge?.g}, b=${pixelTopEdge?.b})`
    );

    // Bottom edge
    const pixelBottomEdge = await getPixelColor(ctx.page, posB2.x + posB2.width / 2, posB2.y + posB2.height - 2);
    assertTrue(
      isColorApproximately(pixelBottomEdge, testColor),
      `Bottom edge should have background color (got r=${pixelBottomEdge?.r}, g=${pixelBottomEdge?.g}, b=${pixelBottomEdge?.b})`
    );
  },
};

// Run all tests
runTests(tests);
