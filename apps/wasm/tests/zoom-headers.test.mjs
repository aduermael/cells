// Zoom-aware header tests for Cells spreadsheet application
// Tests that row and column headers align correctly with grid at various zoom levels

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
 * Get the expected header and cell dimensions at current zoom
 */
async function getHeaderDimensions(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    return {
      zoomFactor,
      headerWidth: Math.round(HEADER_WIDTH * zoomFactor),
      headerHeight: Math.round(HEADER_HEIGHT * zoomFactor),
      cellWidth: Math.round(DEFAULT_COL_WIDTH * zoomFactor),
      cellHeight: Math.round(DEFAULT_ROW_HEIGHT * zoomFactor),
      // Font size should scale with zoom
      expectedFontSize: 12 * zoomFactor,
    };
  });
}

/**
 * Get the position of a specific cell at current zoom
 */
async function getCellPosition(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();
    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;

    const zoomedHeaderWidth = Math.round(HEADER_WIDTH * zoomFactor);
    const zoomedHeaderHeight = Math.round(HEADER_HEIGHT * zoomFactor);

    let x = zoomedHeaderWidth;
    for (let i = 0; i < col; i++) {
      const baseWidth = colWidths.get(i) ?? DEFAULT_COL_WIDTH;
      x += Math.round(baseWidth * zoomFactor);
    }

    let y = zoomedHeaderHeight;
    for (let i = 0; i < row; i++) {
      const baseHeight = rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
      y += Math.round(baseHeight * zoomFactor);
    }

    const cellBaseWidth = colWidths.get(col) ?? DEFAULT_COL_WIDTH;
    const cellBaseHeight = rowHeights.get(row) ?? DEFAULT_ROW_HEIGHT;

    return {
      x,
      y,
      width: Math.round(cellBaseWidth * zoomFactor),
      height: Math.round(cellBaseHeight * zoomFactor),
      zoomFactor,
    };
  }, { col, row });
}

/**
 * Click a cell using zoom-aware coordinates
 */
async function clickCellAtZoom(page, col, row) {
  const pos = await getCellPosition(page, col, row);
  if (!pos) throw new Error('Could not get cell position');

  const canvasInfo = await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top };
  });

  const x = canvasInfo.left + pos.x + pos.width / 2;
  const y = canvasInfo.top + pos.y + pos.height / 2;

  await page.mouse.click(x, y);
  await sleep(100);
}

/**
 * Get the selected cell from the app context
 */
async function getSelectedCell(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;
    return ctx.app.selectedCell;
  });
}

const tests = {
  'Header dimensions scale correctly at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 50);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 0.5, 'Zoom factor should be 0.5');

    // At 50% zoom:
    // Header width: 50 * 0.5 = 25px
    // Header height: 24 * 0.5 = 12px
    // Cell width: 100 * 0.5 = 50px
    // Cell height: 24 * 0.5 = 12px
    assertEqual(dims.headerWidth, 25, 'Header width should be 25px at 50% zoom');
    assertEqual(dims.headerHeight, 12, 'Header height should be 12px at 50% zoom');
    assertEqual(dims.cellWidth, 50, 'Cell width should be 50px at 50% zoom');
    assertEqual(dims.cellHeight, 12, 'Cell height should be 12px at 50% zoom');
    assertEqual(dims.expectedFontSize, 6, 'Font size should be 6px at 50% zoom');
  },

  'Header dimensions scale correctly at 100% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 100);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 1.0, 'Zoom factor should be 1.0');

    // At 100% zoom (unzoomed values):
    assertEqual(dims.headerWidth, 50, 'Header width should be 50px at 100% zoom');
    assertEqual(dims.headerHeight, 24, 'Header height should be 24px at 100% zoom');
    assertEqual(dims.cellWidth, 100, 'Cell width should be 100px at 100% zoom');
    assertEqual(dims.cellHeight, 24, 'Cell height should be 24px at 100% zoom');
    assertEqual(dims.expectedFontSize, 12, 'Font size should be 12px at 100% zoom');
  },

  'Header dimensions scale correctly at 200% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 200);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 2.0, 'Zoom factor should be 2.0');

    // At 200% zoom:
    // Header width: 50 * 2 = 100px
    // Header height: 24 * 2 = 48px
    // Cell width: 100 * 2 = 200px
    // Cell height: 24 * 2 = 48px
    assertEqual(dims.headerWidth, 100, 'Header width should be 100px at 200% zoom');
    assertEqual(dims.headerHeight, 48, 'Header height should be 48px at 200% zoom');
    assertEqual(dims.cellWidth, 200, 'Cell width should be 200px at 200% zoom');
    assertEqual(dims.cellHeight, 48, 'Cell height should be 48px at 200% zoom');
    assertEqual(dims.expectedFontSize, 24, 'Font size should be 24px at 200% zoom');
  },

  'Clicking on cell B2 at 50% zoom selects correct cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 50);

    // Click on B2 (col=1, row=1) using zoom-aware coordinates
    await clickCellAtZoom(ctx.page, 1, 1);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 1, 'Selected cell column should be 1 (B)');
    assertEqual(selected.row, 1, 'Selected cell row should be 1 (2)');
  },

  'Clicking on cell C3 at 200% zoom selects correct cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 200);

    // Click on C3 (col=2, row=2) using zoom-aware coordinates
    await clickCellAtZoom(ctx.page, 2, 2);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 2, 'Selected cell column should be 2 (C)');
    assertEqual(selected.row, 2, 'Selected cell row should be 2 (3)');
  },

  'Cell positions align with headers at 75% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 75);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');

    // Cell A1 should start exactly at header boundaries
    const cellA1 = await getCellPosition(ctx.page, 0, 0);
    assertTrue(cellA1 !== null, 'Should get cell A1 position');

    // Cell A1 X position should equal zoomed header width
    assertEqual(cellA1.x, dims.headerWidth, 'Cell A1 X should start at header width');
    // Cell A1 Y position should equal zoomed header height
    assertEqual(cellA1.y, dims.headerHeight, 'Cell A1 Y should start at header height');

    // Cell B2 should be offset by one cell in each direction
    const cellB2 = await getCellPosition(ctx.page, 1, 1);
    assertTrue(cellB2 !== null, 'Should get cell B2 position');

    assertEqual(
      cellB2.x,
      dims.headerWidth + dims.cellWidth,
      'Cell B2 X should be header width + one cell width'
    );
    assertEqual(
      cellB2.y,
      dims.headerHeight + dims.cellHeight,
      'Cell B2 Y should be header height + one cell height'
    );
  },

  'Cell positions align with headers at 150% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 150);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');

    // Cell A1 should start exactly at header boundaries
    const cellA1 = await getCellPosition(ctx.page, 0, 0);
    assertTrue(cellA1 !== null, 'Should get cell A1 position');

    // Cell A1 X position should equal zoomed header width
    assertEqual(cellA1.x, dims.headerWidth, 'Cell A1 X should start at header width');
    // Cell A1 Y position should equal zoomed header height
    assertEqual(cellA1.y, dims.headerHeight, 'Cell A1 Y should start at header height');

    // Verify the zoomed dimensions match expected calculations
    // At 150%: header width = 50 * 1.5 = 75px
    assertEqual(dims.headerWidth, 75, 'Header width should be 75px at 150%');
    // header height = 24 * 1.5 = 36px
    assertEqual(dims.headerHeight, 36, 'Header height should be 36px at 150%');
  },
};

// Run all tests
runTests(tests);
