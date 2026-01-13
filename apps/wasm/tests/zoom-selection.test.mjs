// Zoom-aware selection tests for Cells spreadsheet application
// Tests that selection and cell editor positioning works correctly at various zoom levels

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the cell editor container position and dimensions
 */
async function getCellEditorBounds(page) {
  return await page.evaluate(() => {
    const container = document.getElementById('cell-editor-container');
    if (!container) return null;
    const style = container.style;
    return {
      left: parseFloat(style.left) || 0,
      top: parseFloat(style.top) || 0,
      width: parseFloat(style.width) || 0,
      height: parseFloat(style.height) || 0,
      display: style.display,
    };
  });
}

/**
 * Get the expected cell position from the renderer
 * This queries the app's internal state to get the actual expected position
 */
async function getExpectedCellPosition(page, col, row) {
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
    const width = Math.round(cellBaseWidth * zoomFactor);
    const height = Math.round(cellBaseHeight * zoomFactor);

    return { x, y, width, height, zoomFactor };
  }, { col, row });
}

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
 * Click a cell at a specific zoom level using zoomed coordinates
 */
async function clickCellAtZoom(page, col, row) {
  // Get zoom-aware position from the renderer
  const pos = await getExpectedCellPosition(page, col, row);
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

const tests = {
  'Cell editor aligns with cell at 100% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Ensure we're at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Click cell B2 (col=1, row=1) and enter edit mode
    await clickCellAtZoom(ctx.page, 1, 1);
    await sleep(100);

    // Use keyboard to enter edit mode (F2)
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    // Get cell editor bounds
    const bounds = await getCellEditorBounds(ctx.page);
    assertTrue(bounds !== null, 'Cell editor should exist');
    assertEqual(bounds.display, 'block', 'Cell editor should be visible');

    // Get expected position from renderer
    const expected = await getExpectedCellPosition(ctx.page, 1, 1);
    assertTrue(expected !== null, 'Should get expected position');

    // At 100% zoom, positions should match
    assertTrue(
      Math.abs(bounds.left - expected.x) <= 1,
      `Cell editor left should be ~${expected.x}, got ${bounds.left}`
    );
    assertTrue(
      Math.abs(bounds.top - expected.y) <= 1,
      `Cell editor top should be ~${expected.y}, got ${bounds.top}`
    );
    assertTrue(
      Math.abs(bounds.width - expected.width) <= 1,
      `Cell editor width should be ~${expected.width}, got ${bounds.width}`
    );
    assertTrue(
      Math.abs(bounds.height - expected.height) <= 1,
      `Cell editor height should be ~${expected.height}, got ${bounds.height}`
    );
  },

  'Cell editor aligns with cell at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 50%
    await setZoomLevel(ctx.page, 50);

    // At 50% zoom, clicking on A1 position
    await clickCellAtZoom(ctx.page, 0, 0);
    await sleep(100);

    // Use keyboard to enter edit mode (F2)
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    // Get cell editor bounds
    const bounds = await getCellEditorBounds(ctx.page);
    assertTrue(bounds !== null, 'Cell editor should exist');
    assertEqual(bounds.display, 'block', 'Cell editor should be visible');

    // Get expected position from renderer at 50% zoom - should be A1 (0,0)
    const expected = await getExpectedCellPosition(ctx.page, 0, 0);
    assertTrue(expected !== null, 'Should get expected position');
    assertEqual(expected.zoomFactor, 0.5, 'Zoom factor should be 0.5');

    // Verify cell editor is positioned correctly at 50% zoom
    // At 50% zoom: header = 25px, cell width = 50px, cell height = 12px
    assertTrue(
      Math.abs(bounds.left - expected.x) <= 1,
      `Cell editor left should be ~${expected.x}, got ${bounds.left}`
    );
    assertTrue(
      Math.abs(bounds.top - expected.y) <= 1,
      `Cell editor top should be ~${expected.y}, got ${bounds.top}`
    );
    assertTrue(
      Math.abs(bounds.width - expected.width) <= 1,
      `Cell editor width should be ~${expected.width}, got ${bounds.width}`
    );
    assertTrue(
      Math.abs(bounds.height - expected.height) <= 1,
      `Cell editor height should be ~${expected.height}, got ${bounds.height}`
    );
  },

  'Cell editor aligns with cell at 200% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 200%
    await setZoomLevel(ctx.page, 200);

    // Click cell B2 (col=1, row=1) using zoom-aware positioning
    // At 200% zoom: header = 100px, cell 0 = 200px, so B starts at 300px
    // Row header = 48px, row 0 = 48px, so row 1 starts at 96px
    await clickCellAtZoom(ctx.page, 1, 1);
    await sleep(100);

    // Use keyboard to enter edit mode (F2)
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    // Get cell editor bounds
    const bounds = await getCellEditorBounds(ctx.page);
    assertTrue(bounds !== null, 'Cell editor should exist');
    assertEqual(bounds.display, 'block', 'Cell editor should be visible');

    // Get expected position from renderer at 200% zoom
    const expected = await getExpectedCellPosition(ctx.page, 1, 1);
    assertTrue(expected !== null, 'Should get expected position');
    assertEqual(expected.zoomFactor, 2.0, 'Zoom factor should be 2.0');

    // Verify cell editor is positioned correctly at 200% zoom
    assertTrue(
      Math.abs(bounds.left - expected.x) <= 1,
      `Cell editor left should be ~${expected.x}, got ${bounds.left}`
    );
    assertTrue(
      Math.abs(bounds.top - expected.y) <= 1,
      `Cell editor top should be ~${expected.y}, got ${bounds.top}`
    );
    assertTrue(
      Math.abs(bounds.width - expected.width) <= 1,
      `Cell editor width should be ~${expected.width}, got ${bounds.width}`
    );
    assertTrue(
      Math.abs(bounds.height - expected.height) <= 1,
      `Cell editor height should be ~${expected.height}, got ${bounds.height}`
    );
  },

  'Cell editor width/height scales with zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Test at 100% first to get baseline
    await setZoomLevel(ctx.page, 100);
    await clickCellAtZoom(ctx.page, 0, 0);
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    const bounds100 = await getCellEditorBounds(ctx.page);
    assertTrue(bounds100 !== null, 'Cell editor should exist at 100%');

    // Press Escape to exit edit mode
    await ctx.page.keyboard.press('Escape');
    await sleep(100);

    // Test at 50%
    await setZoomLevel(ctx.page, 50);
    await clickCellAtZoom(ctx.page, 0, 0);
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    const bounds50 = await getCellEditorBounds(ctx.page);
    assertTrue(bounds50 !== null, 'Cell editor should exist at 50%');

    // Cell editor dimensions should be half at 50% zoom
    assertTrue(
      Math.abs(bounds50.width - bounds100.width / 2) <= 1,
      `Width at 50% should be half of 100%: expected ~${bounds100.width / 2}, got ${bounds50.width}`
    );
    assertTrue(
      Math.abs(bounds50.height - bounds100.height / 2) <= 1,
      `Height at 50% should be half of 100%: expected ~${bounds100.height / 2}, got ${bounds50.height}`
    );

    // Press Escape to exit edit mode
    await ctx.page.keyboard.press('Escape');
    await sleep(100);

    // Test at 200%
    await setZoomLevel(ctx.page, 200);
    await clickCellAtZoom(ctx.page, 0, 0);
    await ctx.page.keyboard.press('F2');
    await sleep(200);

    const bounds200 = await getCellEditorBounds(ctx.page);
    assertTrue(bounds200 !== null, 'Cell editor should exist at 200%');

    // Cell editor dimensions should be double at 200% zoom
    assertTrue(
      Math.abs(bounds200.width - bounds100.width * 2) <= 1,
      `Width at 200% should be double of 100%: expected ~${bounds100.width * 2}, got ${bounds200.width}`
    );
    assertTrue(
      Math.abs(bounds200.height - bounds100.height * 2) <= 1,
      `Height at 200% should be double of 100%: expected ~${bounds100.height * 2}, got ${bounds200.height}`
    );
  },

  'Cell editing works at non-100% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set zoom to 75%
    await setZoomLevel(ctx.page, 75);

    // Enter a value in A1 using zoom-aware click
    await clickCellAtZoom(ctx.page, 0, 0);
    await ctx.page.keyboard.type('ZoomTest', { delay: 50 });
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Click A1 and verify value
    await clickCellAtZoom(ctx.page, 0, 0);
    await sleep(100);

    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'ZoomTest', 'Value should be entered correctly at 75% zoom');

    // Now test at 150%
    await setZoomLevel(ctx.page, 150);

    // Enter another value in B2
    await clickCellAtZoom(ctx.page, 1, 1);
    await ctx.page.keyboard.type('HighZoom', { delay: 50 });
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Click B2 and verify value
    await clickCellAtZoom(ctx.page, 1, 1);
    await sleep(100);

    const content2 = await getFormulaBarContent(ctx.page);
    assertEqual(content2, 'HighZoom', 'Value should be entered correctly at 150% zoom');
  },
};

// Run all tests
runTests(tests);
