// Zoom-aware resize indicator tests for Cells spreadsheet application
// Tests that column/row resize indicators align correctly with cell boundaries at various zoom levels

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
 * Get the expected column boundary X position (right edge of column col) at current zoom
 */
async function getColumnBoundaryX(page, col) {
  return await page.evaluate((col) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();
    const colWidths = ctx.app.colWidths;

    const HEADER_WIDTH = 50;
    const DEFAULT_COL_WIDTH = 100;

    const zoomedHeaderWidth = Math.round(HEADER_WIDTH * zoomFactor);

    // Sum up columns 0 through col (inclusive) to get right boundary
    let x = zoomedHeaderWidth;
    for (let i = 0; i <= col; i++) {
      const baseWidth = colWidths.get(i) ?? DEFAULT_COL_WIDTH;
      x += Math.round(baseWidth * zoomFactor);
    }

    return { x, zoomFactor };
  }, col);
}

/**
 * Get the expected row boundary Y position (bottom edge of row) at current zoom
 */
async function getRowBoundaryY(page, row) {
  return await page.evaluate((row) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();
    const rowHeights = ctx.app.rowHeights;

    const HEADER_HEIGHT = 24;
    const DEFAULT_ROW_HEIGHT = 24;

    const zoomedHeaderHeight = Math.round(HEADER_HEIGHT * zoomFactor);

    // Sum up rows 0 through row (inclusive) to get bottom boundary
    let y = zoomedHeaderHeight;
    for (let i = 0; i <= row; i++) {
      const baseHeight = rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
      y += Math.round(baseHeight * zoomFactor);
    }

    return { y, zoomFactor };
  }, row);
}

/**
 * Get the current resize preview position from the app state
 */
async function getResizePreviewPosition(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const app = ctx.app;
    // Check app state directly (isResizing() uses the UI state machine)
    return {
      x: app.resizePreviewX,
      y: app.resizePreviewY,
      isResizing: app.isResizing(),
      isResizingRow: app.isResizingRow(),
    };
  });
}

/**
 * Get canvas position for mouse events
 */
async function getCanvasPosition(page) {
  return await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top };
  });
}

/**
 * Get header dimensions at current zoom
 */
async function getHeaderDimensions(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;

    return {
      zoomFactor,
      headerWidth: Math.round(HEADER_WIDTH * zoomFactor),
      headerHeight: Math.round(HEADER_HEIGHT * zoomFactor),
    };
  });
}

const tests = {
  'Column resize indicator aligns with column boundary at 100% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Ensure we're at 100% zoom
      await setZoomLevel(ctx.page, 100);
      await sleep(100);

      // Get expected column A boundary (right edge of column 0)
      const boundary = await getColumnBoundaryX(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get column boundary');
      assertEqual(boundary.zoomFactor, 1.0, 'Zoom factor should be 1.0');

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the header row, near the right edge of column A
      // The resize handle is typically within a few pixels of the column boundary
      const resizeHandleX = canvasPos.left + boundary.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      // Start column resize by pressing mouse at the column boundary
      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(100);

      // Check the resize preview position
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should be in column resize mode');

      // The resize preview X should match the column boundary (within 2px tolerance)
      assertTrue(
        Math.abs(preview.x - boundary.x) <= 2,
        `Resize preview X (${preview.x}) should match column boundary (${boundary.x}) at 100% zoom`
      );
    } finally {
      // Always release mouse
      await ctx.page.mouse.up();
    }
  },

  'Column resize indicator aligns with column boundary at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Set zoom to 50%
      await setZoomLevel(ctx.page, 50);
      await sleep(100);

      // Get expected column A boundary at 50% zoom
      const boundary = await getColumnBoundaryX(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get column boundary');
      assertEqual(boundary.zoomFactor, 0.5, 'Zoom factor should be 0.5');

      // At 50% zoom: header = 25px, column A = 50px, so boundary = 75px
      const expectedX = 25 + 50; // headerWidth + colWidth at 50%
      assertTrue(
        Math.abs(boundary.x - expectedX) <= 1,
        `Column boundary at 50% zoom should be ~${expectedX}, got ${boundary.x}`
      );

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the header row, near the right edge of column A
      const resizeHandleX = canvasPos.left + boundary.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      // Start column resize
      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(100);

      // Check the resize preview position
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should be in column resize mode');

      // The resize preview X should match the column boundary at zoomed position
      assertTrue(
        Math.abs(preview.x - boundary.x) <= 2,
        `Resize preview X (${preview.x}) should match column boundary (${boundary.x}) at 50% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },

  'Column resize indicator aligns with column boundary at 200% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Set zoom to 200%
      await setZoomLevel(ctx.page, 200);
      await sleep(100);

      // Get expected column A boundary at 200% zoom
      const boundary = await getColumnBoundaryX(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get column boundary');
      assertEqual(boundary.zoomFactor, 2.0, 'Zoom factor should be 2.0');

      // At 200% zoom: header = 100px, column A = 200px, so boundary = 300px
      const expectedX = 100 + 200; // headerWidth + colWidth at 200%
      assertTrue(
        Math.abs(boundary.x - expectedX) <= 1,
        `Column boundary at 200% zoom should be ~${expectedX}, got ${boundary.x}`
      );

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the header row, near the right edge of column A
      const resizeHandleX = canvasPos.left + boundary.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      // Start column resize
      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(100);

      // Check the resize preview position
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should be in column resize mode');

      // The resize preview X should match the column boundary at zoomed position
      assertTrue(
        Math.abs(preview.x - boundary.x) <= 2,
        `Resize preview X (${preview.x}) should match column boundary (${boundary.x}) at 200% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },

  'Row resize indicator aligns with row boundary at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Set zoom to 50%
      await setZoomLevel(ctx.page, 50);
      await sleep(100);

      // Get expected row 0 boundary at 50% zoom
      const boundary = await getRowBoundaryY(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get row boundary');
      assertEqual(boundary.zoomFactor, 0.5, 'Zoom factor should be 0.5');

      // At 50% zoom: header = 12px, row 0 = 12px, so boundary = 24px
      const expectedY = 12 + 12; // headerHeight + rowHeight at 50%
      assertTrue(
        Math.abs(boundary.y - expectedY) <= 1,
        `Row boundary at 50% zoom should be ~${expectedY}, got ${boundary.y}`
      );

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the row header area, near the bottom edge of row 0
      const headerX = canvasPos.left + headers.headerWidth / 2;
      const resizeHandleY = canvasPos.top + boundary.y - 2;

      // Start row resize
      await ctx.page.mouse.move(headerX, resizeHandleY);
      await ctx.page.mouse.down();
      await sleep(100);

      // Check the resize preview position
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizingRow, 'Should be in row resize mode');

      // The resize preview Y should match the row boundary at zoomed position
      assertTrue(
        Math.abs(preview.y - boundary.y) <= 2,
        `Resize preview Y (${preview.y}) should match row boundary (${boundary.y}) at 50% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },

  'Row resize indicator aligns with row boundary at 200% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Set zoom to 200%
      await setZoomLevel(ctx.page, 200);
      await sleep(100);

      // Get expected row 0 boundary at 200% zoom
      const boundary = await getRowBoundaryY(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get row boundary');
      assertEqual(boundary.zoomFactor, 2.0, 'Zoom factor should be 2.0');

      // At 200% zoom: header = 48px, row 0 = 48px, so boundary = 96px
      const expectedY = 48 + 48; // headerHeight + rowHeight at 200%
      assertTrue(
        Math.abs(boundary.y - expectedY) <= 1,
        `Row boundary at 200% zoom should be ~${expectedY}, got ${boundary.y}`
      );

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the row header area, near the bottom edge of row 0
      const headerX = canvasPos.left + headers.headerWidth / 2;
      const resizeHandleY = canvasPos.top + boundary.y - 2;

      // Start row resize
      await ctx.page.mouse.move(headerX, resizeHandleY);
      await ctx.page.mouse.down();
      await sleep(100);

      // Check the resize preview position
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizingRow, 'Should be in row resize mode');

      // The resize preview Y should match the row boundary at zoomed position
      assertTrue(
        Math.abs(preview.y - boundary.y) <= 2,
        `Resize preview Y (${preview.y}) should match row boundary (${boundary.y}) at 200% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },

  'Resize preview updates correctly during drag at non-100% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Set zoom to 150%
      await setZoomLevel(ctx.page, 150);
      await sleep(100);

      // Get initial column A boundary
      const boundary = await getColumnBoundaryX(ctx.page, 0);
      assertTrue(boundary !== null, 'Should get column boundary');

      // Get canvas position
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      // Position in the header row, near the right edge of column A
      const resizeHandleX = canvasPos.left + boundary.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      // Start column resize
      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(50);

      // Drag 50 screen pixels to the right
      await ctx.page.mouse.move(resizeHandleX + 50, headerY);
      await sleep(50);

      // Check the resize preview position after dragging
      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should still be in column resize mode');

      // The resize preview should have moved approximately 50 pixels
      // (the exact calculation depends on zoom handling)
      const expectedX = boundary.x + 50;
      assertTrue(
        Math.abs(preview.x - expectedX) <= 5,
        `Resize preview X (${preview.x}) should be approximately ${expectedX} after dragging at 150% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },
};

// Run all tests
runTests(tests);
