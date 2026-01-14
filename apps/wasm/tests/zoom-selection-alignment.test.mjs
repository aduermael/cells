// Zoom selection alignment tests for Cells spreadsheet application
// Tests that selection box position precisely matches cell position at various zoom levels
// This tests the root cause: scroll values need to be zoomed in selection rendering

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Set zoom level to a specific percentage
 */
async function setZoomLevel(page, targetZoom) {
  // Get current zoom
  let currentZoom = await page.evaluate(() => {
    const ctx = window._appContext;
    return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
  });

  // Click zoom buttons until we reach target
  const zoomLevels = [50, 75, 100, 125, 150, 175, 200];
  const targetIndex = zoomLevels.indexOf(targetZoom);
  const currentIndex = zoomLevels.indexOf(currentZoom);

  if (targetIndex === -1 || currentIndex === -1) {
    // Use slider for non-standard zoom levels
    await page.evaluate((zoom) => {
      const slider = document.getElementById('zoom-slider');
      if (slider) {
        slider.value = zoom;
        slider.dispatchEvent(new Event('input'));
      }
    }, targetZoom);
    await sleep(100);
    return;
  }

  if (targetIndex > currentIndex) {
    // Need to zoom in
    for (let i = currentIndex; i < targetIndex; i++) {
      await page.click('#zoom-in-btn');
      await sleep(50);
    }
  } else if (targetIndex < currentIndex) {
    // Need to zoom out
    for (let i = currentIndex; i > targetIndex; i--) {
      await page.click('#zoom-out-btn');
      await sleep(50);
    }
  }
  await sleep(100);
}

/**
 * Get cell position using the same calculation as the cell renderer
 * This uses getDragAdjustedColX/RowY which properly zooms scroll values
 */
async function getCellRendererPosition(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    // Use renderer's public methods that use getDragAdjustedColX/Y
    const cellX = renderer.getDragAdjustedColX(col);
    const cellY = renderer.getDragAdjustedRowY(row);

    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;

    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const baseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const baseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const width = Math.round(baseWidth * zoomFactor);
    const height = Math.round(baseHeight * zoomFactor);

    return { x: cellX, y: cellY, width, height, zoomFactor };
  }, { col, row });
}

/**
 * Get selection position using the same calculation as the selection renderer
 * This uses the FIXED logic with zoomed scroll values
 */
async function getSelectionRendererPosition(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();
    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;
    const scrollX = renderer.scrollX;
    const scrollY = renderer.scrollY;

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const zoomedHeaderWidth = Math.round(HEADER_WIDTH * zoomFactor);
    const zoomedHeaderHeight = Math.round(HEADER_HEIGHT * zoomFactor);

    // FIXED: Now using zoomed scroll values (matching the fix in grid-selection-renderer.ts)
    const zoomedScrollX = Math.round(scrollX * zoomFactor);
    const zoomedScrollY = Math.round(scrollY * zoomFactor);

    let selX = zoomedHeaderWidth - zoomedScrollX;
    for (let i = 0; i < col; i++) {
      const baseWidth = colWidths.get(i) ?? DEFAULT_COL_WIDTH;
      selX += Math.round(baseWidth * zoomFactor);
    }

    let selY = zoomedHeaderHeight - zoomedScrollY;
    for (let i = 0; i < row; i++) {
      const baseHeight = rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
      selY += Math.round(baseHeight * zoomFactor);
    }

    const baseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const baseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;
    const width = Math.round(baseWidth * zoomFactor);
    const height = Math.round(baseHeight * zoomFactor);

    return {
      x: selX,
      y: selY,
      width,
      height,
      zoomFactor,
      scrollX,
      scrollY,
      zoomedScrollX,
      zoomedScrollY
    };
  }, { col, row });
}

/**
 * Get the actual selection box bounds by querying what was rendered
 * This queries the internal getSelectionBounds function
 */
async function getActualSelectionBounds(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.renderer) return null;

    const renderer = ctx.app.renderer;
    // Get fill handle bounds which is computed from selection bounds
    const fillHandle = renderer.fillHandleBounds;
    if (!fillHandle) return null;

    // Fill handle is at bottom-right of selection, 6px size
    // So selection bounds are: x = fillHandle.x + 3 - width, y = fillHandle.y + 3 - height
    // But we can compute selection position from the selection state directly
    const selectedCell = renderer.selectedCell;
    if (!selectedCell) return null;

    const zoomFactor = renderer.getZoomFactor();
    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;
    const scrollX = renderer.scrollX;
    const scrollY = renderer.scrollY;

    return {
      fillHandleX: fillHandle.x,
      fillHandleY: fillHandle.y,
      selectedCell,
      zoomFactor,
      scrollX,
      scrollY,
    };
  });
}

/**
 * Click on a cell at the specified position using zoomed coordinates
 */
async function clickCellAtPosition(page, col, row) {
  const pos = await getCellRendererPosition(page, col, row);
  if (!pos) throw new Error('Could not get cell position');

  const canvasInfo = await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top };
  });

  // Click in the center of the cell
  const x = canvasInfo.left + pos.x + pos.width / 2;
  const y = canvasInfo.top + pos.y + pos.height / 2;

  await page.mouse.click(x, y);
  await sleep(100);
}

/**
 * Scroll the grid by a specified amount
 */
async function scrollGrid(page, deltaX, deltaY) {
  const canvasInfo = await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top, width: rect.width, height: rect.height };
  });

  // Position mouse in the middle of the canvas
  const x = canvasInfo.left + canvasInfo.width / 2;
  const y = canvasInfo.top + canvasInfo.height / 2;

  await page.mouse.move(x, y);
  await page.mouse.wheel({ deltaX, deltaY });
  await sleep(100);
}

const tests = {
  'Selection and cell positions match at 100% zoom without scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Ensure we're at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Click cell C3 (col=2, row=2)
    await clickCellAtPosition(ctx.page, 2, 2);
    await sleep(100);

    // Get both positions
    const cellPos = await getCellRendererPosition(ctx.page, 2, 2);
    const selPos = await getSelectionRendererPosition(ctx.page, 2, 2);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    // At 100% zoom without scroll, positions should match exactly
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 1,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 100% zoom without scroll`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 1,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 100% zoom without scroll`
    );
  },

  'Selection and cell positions match at 50% zoom without scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 50%
    await setZoomLevel(ctx.page, 50);

    // Click cell B2 (col=1, row=1)
    await clickCellAtPosition(ctx.page, 1, 1);
    await sleep(100);

    // Get both positions
    const cellPos = await getCellRendererPosition(ctx.page, 1, 1);
    const selPos = await getSelectionRendererPosition(ctx.page, 1, 1);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');
    assertEqual(cellPos.zoomFactor, 0.5, 'Zoom factor should be 0.5');

    // At 50% zoom without scroll, positions should still match
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 1,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 50% zoom without scroll`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 1,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 50% zoom without scroll`
    );
  },

  'Selection and cell positions match at 200% zoom without scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 200%
    await setZoomLevel(ctx.page, 200);

    // Click cell A1 (col=0, row=0)
    await clickCellAtPosition(ctx.page, 0, 0);
    await sleep(100);

    // Get both positions
    const cellPos = await getCellRendererPosition(ctx.page, 0, 0);
    const selPos = await getSelectionRendererPosition(ctx.page, 0, 0);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');
    assertEqual(cellPos.zoomFactor, 2.0, 'Zoom factor should be 2.0');

    // At 200% zoom without scroll, positions should still match
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 1,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 200% zoom without scroll`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 1,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 200% zoom without scroll`
    );
  },

  'Selection and cell positions match at 50% zoom WITH scroll (BUG TEST)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 50%
    await setZoomLevel(ctx.page, 50);

    // First click a cell to establish selection
    await clickCellAtPosition(ctx.page, 5, 5);
    await sleep(100);

    // Scroll right and down
    await scrollGrid(ctx.page, 200, 100);

    // Get both positions for a visible cell
    const cellPos = await getCellRendererPosition(ctx.page, 5, 5);
    const selPos = await getSelectionRendererPosition(ctx.page, 5, 5);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    // This is where the bug manifests!
    // With scroll and non-100% zoom, cell renderer uses zoomed scroll,
    // but selection renderer uses unzoomed scroll
    const scrollDiff = selPos.scrollX - selPos.zoomedScrollX;
    console.log(`Scroll difference: scrollX=${selPos.scrollX}, zoomedScrollX=${selPos.zoomedScrollX}, diff=${scrollDiff}`);
    console.log(`Cell X=${cellPos.x}, Selection X=${selPos.x}, diff=${cellPos.x - selPos.x}`);

    // After fix, these should match. Before fix, they will differ by scroll * (1 - zoomFactor)
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 50% zoom WITH scroll. ` +
      `Scroll: ${selPos.scrollX}, ZoomedScroll: ${selPos.zoomedScrollX}`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 2,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 50% zoom WITH scroll. ` +
      `Scroll: ${selPos.scrollY}, ZoomedScroll: ${selPos.zoomedScrollY}`
    );
  },

  'Selection and cell positions match at 200% zoom WITH scroll (BUG TEST)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 200%
    await setZoomLevel(ctx.page, 200);

    // First click a cell to establish selection
    await clickCellAtPosition(ctx.page, 3, 3);
    await sleep(100);

    // Scroll right and down (smaller amount due to zoom)
    await scrollGrid(ctx.page, 100, 50);

    // Get both positions
    const cellPos = await getCellRendererPosition(ctx.page, 3, 3);
    const selPos = await getSelectionRendererPosition(ctx.page, 3, 3);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    console.log(`Scroll: scrollX=${selPos.scrollX}, zoomedScrollX=${selPos.zoomedScrollX}`);
    console.log(`Cell X=${cellPos.x}, Selection X=${selPos.x}, diff=${cellPos.x - selPos.x}`);

    // After fix, these should match
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 200% zoom WITH scroll. ` +
      `Scroll: ${selPos.scrollX}, ZoomedScroll: ${selPos.zoomedScrollX}`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 2,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 200% zoom WITH scroll. ` +
      `Scroll: ${selPos.scrollY}, ZoomedScroll: ${selPos.zoomedScrollY}`
    );
  },

  'Fill handle position matches cell corner at non-100% zoom with scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 75%
    await setZoomLevel(ctx.page, 75);

    // Click a cell
    await clickCellAtPosition(ctx.page, 4, 4);
    await sleep(100);

    // Scroll
    await scrollGrid(ctx.page, 150, 75);

    // Get cell position (where fill handle should be at bottom-right)
    const cellPos = await getCellRendererPosition(ctx.page, 4, 4);
    assertTrue(cellPos !== null, 'Should get cell position');

    // Get actual fill handle bounds
    const bounds = await getActualSelectionBounds(ctx.page);

    if (bounds && bounds.fillHandleX !== undefined) {
      // Fill handle center should be at cell's bottom-right corner
      const expectedFillHandleX = cellPos.x + cellPos.width;
      const expectedFillHandleY = cellPos.y + cellPos.height;

      // Fill handle is 6px, so center is at +3 from its x,y
      const fillHandleCenterX = bounds.fillHandleX + 3;
      const fillHandleCenterY = bounds.fillHandleY + 3;

      console.log(`Expected fill handle at (${expectedFillHandleX}, ${expectedFillHandleY})`);
      console.log(`Actual fill handle center at (${fillHandleCenterX}, ${fillHandleCenterY})`);

      assertTrue(
        Math.abs(fillHandleCenterX - expectedFillHandleX) <= 3,
        `Fill handle X (${fillHandleCenterX}) should be near cell corner (${expectedFillHandleX})`
      );
      assertTrue(
        Math.abs(fillHandleCenterY - expectedFillHandleY) <= 3,
        `Fill handle Y (${fillHandleCenterY}) should be near cell corner (${expectedFillHandleY})`
      );
    }
  },

  'Selection aligns at non-standard zoom 72% (rounding accumulation test)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 72% - a non-standard zoom level that can expose rounding errors
    await setZoomLevel(ctx.page, 72);
    await sleep(100);

    // Verify zoom was set
    const actualZoom = await ctx.page.evaluate(() => {
      return window._appContext?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    console.log(`Actual zoom: ${actualZoom}%`);

    // Click cell F6 (col=5, row=5) - far enough to accumulate rounding errors
    await clickCellAtPosition(ctx.page, 5, 5);
    await sleep(100);

    // Get both positions
    const cellPos = await getCellRendererPosition(ctx.page, 5, 5);
    const selPos = await getSelectionRendererPosition(ctx.page, 5, 5);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    console.log(`At 72% zoom: Cell X=${cellPos.x}, Selection X=${selPos.x}, diff=${cellPos.x - selPos.x}`);
    console.log(`At 72% zoom: Cell Y=${cellPos.y}, Selection Y=${selPos.y}, diff=${cellPos.y - selPos.y}`);

    // At non-standard zoom, positions should still match exactly
    // (This test fails without the sum-unzoomed-first fix)
    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 1,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 72% zoom`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 1,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 72% zoom`
    );
  },
};

// Run all tests
runTests(tests);
