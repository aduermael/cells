// Dynamic zoom change tests for Cells spreadsheet application
// Tests that components update correctly when zoom level changes dynamically

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
  await page.evaluate((zoom) => {
    const slider = document.getElementById('zoom-slider');
    if (slider) {
      slider.value = zoom;
      slider.dispatchEvent(new Event('input'));
    }
  }, targetZoom);
  await sleep(100);
}

/**
 * Get current zoom factor from renderer
 */
async function getZoomFactor(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    return ctx?.app?.renderer?.getZoomFactor?.() ?? 1.0;
  });
}

/**
 * Click on a cell using canvas coordinates
 */
async function clickCell(page, col, row) {
  const coords = await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const x = renderer.getDragAdjustedColX(col);
    const y = renderer.getDragAdjustedRowY(row);

    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;
    const zoomFactor = renderer.getZoomFactor();

    const width = Math.round((colWidths.get(col) ?? 100) * zoomFactor);
    const height = Math.round((rowHeights.get(row) ?? 24) * zoomFactor);

    return { x: x + width / 2, y: y + height / 2 };
  }, { col, row });

  if (!coords) throw new Error('Could not get cell coordinates');

  const canvasRect = await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top };
  });

  await page.mouse.click(canvasRect.left + coords.x, canvasRect.top + coords.y);
  await sleep(100);
}

/**
 * Get selection position from renderer state
 */
async function getSelectionBounds(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const selectedCell = renderer.selectedCell;
    if (!selectedCell) return null;

    const { col, row } = selectedCell;
    const zoomFactor = renderer.getZoomFactor();
    const colWidths = ctx.app.colWidths;
    const rowHeights = ctx.app.rowHeights;
    const scrollX = renderer.scrollX;
    const scrollY = renderer.scrollY;

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;

    const zoomedHeaderWidth = Math.round(HEADER_WIDTH * zoomFactor);
    const zoomedHeaderHeight = Math.round(HEADER_HEIGHT * zoomFactor);
    const zoomedScrollX = Math.round(scrollX * zoomFactor);
    const zoomedScrollY = Math.round(scrollY * zoomFactor);

    let x = zoomedHeaderWidth - zoomedScrollX;
    for (let i = 0; i < col; i++) {
      x += Math.round((colWidths.get(i) ?? 100) * zoomFactor);
    }

    let y = zoomedHeaderHeight - zoomedScrollY;
    for (let i = 0; i < row; i++) {
      y += Math.round((rowHeights.get(i) ?? 24) * zoomFactor);
    }

    const width = Math.round((colWidths.get(col) ?? 100) * zoomFactor);
    const height = Math.round((rowHeights.get(row) ?? 24) * zoomFactor);

    return { x, y, width, height, col, row, zoomFactor };
  });
}

/**
 * Get cell editor position from DOM
 */
async function getCellEditorPosition(page) {
  return await page.evaluate(() => {
    const container = document.getElementById('cell-editor-container');
    if (!container || container.style.display === 'none') {
      return null;
    }
    return {
      left: parseFloat(container.style.left) || 0,
      top: parseFloat(container.style.top) || 0,
      width: parseFloat(container.style.width) || 0,
      height: parseFloat(container.style.height) || 0,
      display: container.style.display,
    };
  });
}

/**
 * Check if cell editor is visible
 */
async function isCellEditorVisible(page) {
  return await page.evaluate(() => {
    const container = document.getElementById('cell-editor-container');
    return container && container.style.display !== 'none';
  });
}

/**
 * Start editing the selected cell
 */
async function startCellEditing(page) {
  // Ensure canvas has focus for keyboard events
  await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    if (canvas) canvas.focus();
  });
  await sleep(50);
  // Press F2 to start editing
  await page.keyboard.press('F2');
  await sleep(150);
}

/**
 * Cancel cell editing
 */
async function cancelCellEditing(page) {
  await page.keyboard.press('Escape');
  await sleep(100);
}

const tests = {
  'Selection updates when zoom changes from 100% to 50%': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Select cell C3
    await clickCell(ctx.page, 2, 2);

    // Get selection bounds at 100%
    const bounds100 = await getSelectionBounds(ctx.page);
    assertTrue(bounds100 !== null, 'Should have selection at 100%');
    assertEqual(bounds100.zoomFactor, 1.0, 'Zoom should be 100%');

    // Change zoom to 50%
    await setZoomLevel(ctx.page, 50);

    // Get selection bounds at 50%
    const bounds50 = await getSelectionBounds(ctx.page);
    assertTrue(bounds50 !== null, 'Should still have selection at 50%');
    assertEqual(bounds50.zoomFactor, 0.5, 'Zoom should be 50%');

    // Selection should be approximately half the size
    assertTrue(
      Math.abs(bounds50.width - bounds100.width * 0.5) <= 2,
      `Selection width at 50% (${bounds50.width}) should be ~half of 100% (${bounds100.width})`
    );
    assertTrue(
      Math.abs(bounds50.height - bounds100.height * 0.5) <= 2,
      `Selection height at 50% (${bounds50.height}) should be ~half of 100% (${bounds100.height})`
    );

    // Position should also be scaled
    assertTrue(
      Math.abs(bounds50.x - bounds100.x * 0.5) <= 5,
      `Selection X at 50% (${bounds50.x}) should be ~half of 100% (${bounds100.x})`
    );
    assertTrue(
      Math.abs(bounds50.y - bounds100.y * 0.5) <= 5,
      `Selection Y at 50% (${bounds50.y}) should be ~half of 100% (${bounds100.y})`
    );
  },

  'Selection updates when zoom changes from 100% to 200%': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Select cell B2
    await clickCell(ctx.page, 1, 1);

    // Get selection bounds at 100%
    const bounds100 = await getSelectionBounds(ctx.page);
    assertTrue(bounds100 !== null, 'Should have selection at 100%');

    // Change zoom to 200%
    await setZoomLevel(ctx.page, 200);

    // Get selection bounds at 200%
    const bounds200 = await getSelectionBounds(ctx.page);
    assertTrue(bounds200 !== null, 'Should still have selection at 200%');
    assertEqual(bounds200.zoomFactor, 2.0, 'Zoom should be 200%');

    // Selection should be approximately double the size
    assertTrue(
      Math.abs(bounds200.width - bounds100.width * 2.0) <= 2,
      `Selection width at 200% (${bounds200.width}) should be ~double of 100% (${bounds100.width})`
    );
    assertTrue(
      Math.abs(bounds200.height - bounds100.height * 2.0) <= 2,
      `Selection height at 200% (${bounds200.height}) should be ~double of 100% (${bounds100.height})`
    );
  },

  'Cell editor position updates when zoom changes while editing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Select cell B2 and start editing
    await clickCell(ctx.page, 1, 1);
    await startCellEditing(ctx.page);

    // Verify editor is visible
    assertTrue(await isCellEditorVisible(ctx.page), 'Cell editor should be visible');

    // Get editor position at 100%
    const pos100 = await getCellEditorPosition(ctx.page);
    assertTrue(pos100 !== null, 'Should have editor position at 100%');
    console.log(`Editor at 100%: left=${pos100.left}, top=${pos100.top}, width=${pos100.width}`);

    // Change zoom to 50% while editing
    await setZoomLevel(ctx.page, 50);
    await sleep(200); // Extra wait for repositioning

    // Get editor position at 50%
    const pos50 = await getCellEditorPosition(ctx.page);
    assertTrue(pos50 !== null, 'Should have editor position at 50%');
    console.log(`Editor at 50%: left=${pos50.left}, top=${pos50.top}, width=${pos50.width}`);

    // Editor should be repositioned to match zoomed cell position
    // Width should be approximately halved
    assertTrue(
      Math.abs(pos50.width - pos100.width * 0.5) <= 2,
      `Editor width at 50% (${pos50.width}) should be ~half of 100% (${pos100.width})`
    );

    // Position should also be scaled
    assertTrue(
      Math.abs(pos50.left - pos100.left * 0.5) <= 5,
      `Editor left at 50% (${pos50.left}) should be ~half of 100% (${pos100.left})`
    );
    assertTrue(
      Math.abs(pos50.top - pos100.top * 0.5) <= 5,
      `Editor top at 50% (${pos50.top}) should be ~half of 100% (${pos100.top})`
    );

    // Cleanup
    await cancelCellEditing(ctx.page);
  },

  'Cell editor position updates when zoom changes from 100% to 200% while editing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Select cell A1 and start editing
    await clickCell(ctx.page, 0, 0);
    await startCellEditing(ctx.page);

    // Get editor position at 100%
    const pos100 = await getCellEditorPosition(ctx.page);
    assertTrue(pos100 !== null, 'Should have editor position at 100%');

    // Change zoom to 200% while editing
    await setZoomLevel(ctx.page, 200);
    await sleep(200);

    // Get editor position at 200%
    const pos200 = await getCellEditorPosition(ctx.page);
    assertTrue(pos200 !== null, 'Should have editor position at 200%');

    // Width should be approximately doubled
    assertTrue(
      Math.abs(pos200.width - pos100.width * 2.0) <= 2,
      `Editor width at 200% (${pos200.width}) should be ~double of 100% (${pos100.width})`
    );

    // Cleanup
    await cancelCellEditing(ctx.page);
  },

  'Selection remains selected on same cell after zoom change': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Start at 100% zoom
    await setZoomLevel(ctx.page, 100);

    // Select cell D4 (col=3, row=3)
    await clickCell(ctx.page, 3, 3);

    // Get selection before zoom
    const before = await getSelectionBounds(ctx.page);
    assertTrue(before !== null, 'Should have selection');
    assertEqual(before.col, 3, 'Column should be D (3)');
    assertEqual(before.row, 3, 'Row should be 4 (3)');

    // Change zoom to 75%
    await setZoomLevel(ctx.page, 75);

    // Get selection after zoom
    const after = await getSelectionBounds(ctx.page);
    assertTrue(after !== null, 'Should still have selection');
    assertEqual(after.col, 3, 'Column should still be D (3)');
    assertEqual(after.row, 3, 'Row should still be 4 (3)');

    // Change zoom back to 100%
    await setZoomLevel(ctx.page, 100);

    // Selection should still be on same cell
    const final = await getSelectionBounds(ctx.page);
    assertEqual(final.col, 3, 'Column should still be D (3)');
    assertEqual(final.row, 3, 'Row should still be 4 (3)');
  },
};

// Run all tests
runTests(tests);
