// Comprehensive zoom regression test suite for Cells spreadsheet application
// Tests all zoom-dependent rendering elements to prevent regressions

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

//=============================================================================
// Helper Functions
//=============================================================================

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

/**
 * Get cell renderer position using getDragAdjustedColX/Y
 */
async function getCellRendererPosition(page, col, row) {
  return await page.evaluate(({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;

    const renderer = ctx.app.renderer;
    const zoomFactor = renderer.getZoomFactor();

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
 * Get selection position using zoom-corrected calculations
 */
async function getSelectionPosition(page, col, row) {
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

    return { x: selX, y: selY, width, height, zoomFactor };
  }, { col, row });
}

/**
 * Get canvas position for mouse events
 */
async function getCanvasPosition(page) {
  return await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();
    return { left: rect.left, top: rect.top, width: rect.width, height: rect.height };
  });
}

/**
 * Click on a cell at the specified position using zoomed coordinates
 */
async function clickCell(page, col, row) {
  const pos = await getCellRendererPosition(page, col, row);
  if (!pos) throw new Error('Could not get cell position');

  const canvasPos = await getCanvasPosition(page);

  const x = canvasPos.left + pos.x + pos.width / 2;
  const y = canvasPos.top + pos.y + pos.height / 2;

  await page.mouse.click(x, y);
  await sleep(100);
}

/**
 * Scroll the grid by a specified amount
 */
async function scrollGrid(page, deltaX, deltaY) {
  const canvasPos = await getCanvasPosition(page);

  const x = canvasPos.left + canvasPos.width / 2;
  const y = canvasPos.top + canvasPos.height / 2;

  await page.mouse.move(x, y);
  await page.mouse.wheel({ deltaX, deltaY });
  await sleep(100);
}

/**
 * Get selected cell from app state
 */
async function getSelectedCell(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;
    return ctx.app.selectedCell;
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
    };
  });
}

/**
 * Start cell editing
 */
async function startCellEditing(page) {
  await page.evaluate(() => {
    const canvas = document.getElementById('grid');
    if (canvas) canvas.focus();
  });
  await sleep(50);
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

/**
 * Get resize preview position from app state
 */
async function getResizePreviewPosition(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return null;
    return {
      x: ctx.app.resizePreviewX,
      y: ctx.app.resizePreviewY,
      isResizing: ctx.app.isResizing(),
      isResizingRow: ctx.app.isResizingRow(),
    };
  });
}

/**
 * Get column boundary X position at current zoom
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

    let x = zoomedHeaderWidth;
    for (let i = 0; i <= col; i++) {
      const baseWidth = colWidths.get(i) ?? DEFAULT_COL_WIDTH;
      x += Math.round(baseWidth * zoomFactor);
    }

    return { x, zoomFactor };
  }, col);
}

/**
 * Get row boundary Y position at current zoom
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

    let y = zoomedHeaderHeight;
    for (let i = 0; i <= row; i++) {
      const baseHeight = rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
      y += Math.round(baseHeight * zoomFactor);
    }

    return { y, zoomFactor };
  }, row);
}

//=============================================================================
// Test Suite - Boundary Zoom Levels (8.7b)
//=============================================================================

const boundaryZoomTests = {
  'Header dimensions scale correctly at 25% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 25);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertTrue(
      Math.abs(dims.zoomFactor - 0.25) < 0.01,
      `Zoom factor should be 0.25, got ${dims.zoomFactor}`
    );
    assertEqual(dims.headerWidth, 13, 'Header width should be ~13px at 25% zoom');
    assertEqual(dims.headerHeight, 6, 'Header height should be ~6px at 25% zoom');
  },

  'Header dimensions scale correctly at 75% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 75);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 0.75, 'Zoom factor should be 0.75');
    assertEqual(dims.headerWidth, 38, 'Header width should be 38px at 75% zoom');
    assertEqual(dims.headerHeight, 18, 'Header height should be 18px at 75% zoom');
  },

  'Header dimensions scale correctly at 150% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 150);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 1.5, 'Zoom factor should be 1.5');
    assertEqual(dims.headerWidth, 75, 'Header width should be 75px at 150% zoom');
    assertEqual(dims.headerHeight, 36, 'Header height should be 36px at 150% zoom');
  },

  'Header dimensions scale correctly at 400% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 400);

    const dims = await getHeaderDimensions(ctx.page);
    assertTrue(dims !== null, 'Should get header dimensions');
    assertEqual(dims.zoomFactor, 4.0, 'Zoom factor should be 4.0');
    assertEqual(dims.headerWidth, 200, 'Header width should be 200px at 400% zoom');
    assertEqual(dims.headerHeight, 96, 'Header height should be 96px at 400% zoom');
  },

  'Cell selection works at 25% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // First select a cell at normal zoom
    await setZoomLevel(ctx.page, 100);
    await clickCell(ctx.page, 2, 2);

    const selectedBefore = await getSelectedCell(ctx.page);
    assertEqual(selectedBefore.col, 2, 'Selected col before zoom should be 2');
    assertEqual(selectedBefore.row, 2, 'Selected row before zoom should be 2');

    // Zoom to 25% and verify selection is preserved
    await setZoomLevel(ctx.page, 25);
    await sleep(100);

    const selectedAfter = await getSelectedCell(ctx.page);
    assertTrue(selectedAfter !== null, 'Should still have a selected cell at 25% zoom');
    assertEqual(selectedAfter.col, 2, 'Selected col should be preserved at 25% zoom');
    assertEqual(selectedAfter.row, 2, 'Selected row should be preserved at 25% zoom');

    // Verify zoom factor is correct
    const zoomFactor = await getZoomFactor(ctx.page);
    assertTrue(
      Math.abs(zoomFactor - 0.25) < 0.01,
      `Zoom factor should be 0.25, got ${zoomFactor}`
    );
  },

  'Cell selection works at 400% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 400);

    await clickCell(ctx.page, 0, 0);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 0, 'Selected cell column should be 0 (A)');
    assertEqual(selected.row, 0, 'Selected cell row should be 0 (1)');
  },
};

//=============================================================================
// Test Suite - Zoom + Scroll Combinations (8.7c)
//=============================================================================

const zoomScrollTests = {
  'Selection aligns with cell at 50% zoom after horizontal scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 50);
    await clickCell(ctx.page, 3, 3);
    await scrollGrid(ctx.page, 300, 0);

    const cellPos = await getCellRendererPosition(ctx.page, 3, 3);
    const selPos = await getSelectionPosition(ctx.page, 3, 3);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match after horizontal scroll at 50%`
    );
  },

  'Selection aligns with cell at 50% zoom after vertical scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 50);
    await clickCell(ctx.page, 2, 5);
    await scrollGrid(ctx.page, 0, 200);

    const cellPos = await getCellRendererPosition(ctx.page, 2, 5);
    const selPos = await getSelectionPosition(ctx.page, 2, 5);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 2,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match after vertical scroll at 50%`
    );
  },

  'Selection aligns with cell at 200% zoom after diagonal scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 200);
    await clickCell(ctx.page, 1, 2);
    await scrollGrid(ctx.page, 100, 100);

    const cellPos = await getCellRendererPosition(ctx.page, 1, 2);
    const selPos = await getSelectionPosition(ctx.page, 1, 2);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 200% with diagonal scroll`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 2,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 200% with diagonal scroll`
    );
  },

  'Selection aligns at 75% zoom after large scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 75);
    await clickCell(ctx.page, 5, 5);
    await scrollGrid(ctx.page, 500, 300);

    const cellPos = await getCellRendererPosition(ctx.page, 5, 5);
    const selPos = await getSelectionPosition(ctx.page, 5, 5);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Cell X (${cellPos.x}) and Selection X (${selPos.x}) should match at 75% after large scroll`
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 2,
      `Cell Y (${cellPos.y}) and Selection Y (${selPos.y}) should match at 75% after large scroll`
    );
  },

  'Cell editor position correct at 50% zoom with scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 50);
    await clickCell(ctx.page, 3, 3);
    await scrollGrid(ctx.page, 100, 50);
    await startCellEditing(ctx.page);

    try {
      const editorPos = await getCellEditorPosition(ctx.page);
      assertTrue(editorPos !== null, 'Cell editor should be visible');

      const cellPos = await getCellRendererPosition(ctx.page, 3, 3);
      assertTrue(cellPos !== null, 'Should get cell position');

      // Editor position should be close to cell position
      assertTrue(
        Math.abs(editorPos.left - cellPos.x) <= 5,
        `Editor left (${editorPos.left}) should match cell X (${cellPos.x}) at 50% with scroll`
      );
      assertTrue(
        Math.abs(editorPos.top - cellPos.y) <= 5,
        `Editor top (${editorPos.top}) should match cell Y (${cellPos.y}) at 50% with scroll`
      );
    } finally {
      await cancelCellEditing(ctx.page);
    }
  },

  'Cell editor position correct at 200% zoom with scroll': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 200);
    await clickCell(ctx.page, 1, 1);
    await scrollGrid(ctx.page, 50, 30);
    await startCellEditing(ctx.page);

    try {
      const editorPos = await getCellEditorPosition(ctx.page);
      assertTrue(editorPos !== null, 'Cell editor should be visible');

      const cellPos = await getCellRendererPosition(ctx.page, 1, 1);
      assertTrue(cellPos !== null, 'Should get cell position');

      assertTrue(
        Math.abs(editorPos.left - cellPos.x) <= 5,
        `Editor left (${editorPos.left}) should match cell X (${cellPos.x}) at 200% with scroll`
      );
      assertTrue(
        Math.abs(editorPos.top - cellPos.y) <= 5,
        `Editor top (${editorPos.top}) should match cell Y (${cellPos.y}) at 200% with scroll`
      );
    } finally {
      await cancelCellEditing(ctx.page);
    }
  },

  'Column resize indicator aligns at 75% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      await setZoomLevel(ctx.page, 75);
      await sleep(100);

      // Test resize on column B (index 1) which is always visible
      const boundary = await getColumnBoundaryX(ctx.page, 1);
      assertTrue(boundary !== null, 'Should get column boundary');

      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      const resizeHandleX = canvasPos.left + boundary.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(100);

      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should be in column resize mode');

      assertTrue(
        Math.abs(preview.x - boundary.x) <= 3,
        `Resize preview X (${preview.x}) should match column boundary (${boundary.x}) at 75% zoom`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },
};

//=============================================================================
// Test Suite - Zoom + Frozen Panes (8.7d)
//=============================================================================

const zoomFrozenTests = {
  'Selection works with frozen columns at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set frozen columns using the client's setFreezePanes method
    await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (ctx && ctx.client) {
        await ctx.client.setFreezePanes(2, 0);
      }
    });
    await sleep(200);

    await setZoomLevel(ctx.page, 50);

    // Click a cell in the non-frozen area
    await clickCell(ctx.page, 4, 2);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 4, 'Selected cell column should be 4 (E)');
    assertEqual(selected.row, 2, 'Selected cell row should be 2 (3)');
  },

  'Selection works with frozen rows at 200% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set frozen rows using the client's setFreezePanes method
    await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (ctx && ctx.client) {
        await ctx.client.setFreezePanes(0, 2);
      }
    });
    await sleep(200);

    await setZoomLevel(ctx.page, 200);

    // Click a cell in the non-frozen area
    await clickCell(ctx.page, 1, 4);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 1, 'Selected cell column should be 1 (B)');
    assertEqual(selected.row, 4, 'Selected cell row should be 4 (5)');
  },

  'Selection works in frozen area at 75% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set frozen columns and rows using the client's setFreezePanes method
    await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (ctx && ctx.client) {
        await ctx.client.setFreezePanes(2, 2);
      }
    });
    await sleep(200);

    await setZoomLevel(ctx.page, 75);

    // Click a cell in the frozen area
    await clickCell(ctx.page, 0, 0);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 0, 'Selected cell column should be 0 (A)');
    assertEqual(selected.row, 0, 'Selected cell row should be 0 (1)');
  },

  'Frozen pane with scroll at 50% zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set frozen columns using the client's setFreezePanes method
    await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (ctx && ctx.client) {
        await ctx.client.setFreezePanes(2, 0);
      }
    });
    await sleep(200);

    await setZoomLevel(ctx.page, 50);
    await scrollGrid(ctx.page, 200, 100);

    // Selection in frozen area should be unaffected by scroll
    await clickCell(ctx.page, 0, 0);

    const selected = await getSelectedCell(ctx.page);
    assertTrue(selected !== null, 'Should have a selected cell');
    assertEqual(selected.col, 0, 'Selected cell column should be 0 (A) in frozen area');
    assertEqual(selected.row, 0, 'Selected cell row should be 0 (1)');

    // Verify cell and selection alignment
    const cellPos = await getCellRendererPosition(ctx.page, 0, 0);
    const selPos = await getSelectionPosition(ctx.page, 0, 0);

    assertTrue(cellPos !== null, 'Should get cell position');
    assertTrue(selPos !== null, 'Should get selection position');

    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 2,
      `Frozen cell X (${cellPos.x}) and Selection X (${selPos.x}) should match`
    );
  },
};

//=============================================================================
// Test Suite - All Zoom-Dependent Rendering (8.7a)
//=============================================================================

const comprehensiveTests = {
  'All elements render correctly at 100% zoom baseline': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 100);

    // Verify zoom factor
    const zoomFactor = await getZoomFactor(ctx.page);
    assertEqual(zoomFactor, 1.0, 'Zoom factor should be 1.0');

    // Select a cell and verify alignment
    await clickCell(ctx.page, 2, 2);

    const cellPos = await getCellRendererPosition(ctx.page, 2, 2);
    const selPos = await getSelectionPosition(ctx.page, 2, 2);

    assertTrue(
      Math.abs(cellPos.x - selPos.x) <= 1,
      'Cell and selection X should match at 100% zoom'
    );
    assertTrue(
      Math.abs(cellPos.y - selPos.y) <= 1,
      'Cell and selection Y should match at 100% zoom'
    );
  },

  'Dynamic zoom change preserves selection cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 100);
    await clickCell(ctx.page, 3, 4);

    const before = await getSelectedCell(ctx.page);
    assertTrue(before !== null, 'Should have selection before zoom');
    assertEqual(before.col, 3, 'Selected col before zoom');
    assertEqual(before.row, 4, 'Selected row before zoom');

    // Change zoom multiple times
    await setZoomLevel(ctx.page, 50);
    await setZoomLevel(ctx.page, 200);
    await setZoomLevel(ctx.page, 75);

    const after = await getSelectedCell(ctx.page);
    assertTrue(after !== null, 'Should have selection after zoom changes');
    assertEqual(after.col, 3, 'Selected col should be unchanged');
    assertEqual(after.row, 4, 'Selected row should be unchanged');
  },

  'Resize indicator position is correct after zoom change': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    try {
      // Start at 100% zoom
      await setZoomLevel(ctx.page, 100);

      const boundary100 = await getColumnBoundaryX(ctx.page, 1);
      assertTrue(boundary100 !== null, 'Should get boundary at 100%');

      // Change zoom to 150%
      await setZoomLevel(ctx.page, 150);

      const boundary150 = await getColumnBoundaryX(ctx.page, 1);
      assertTrue(boundary150 !== null, 'Should get boundary at 150%');

      // Boundary should scale with zoom
      const expectedRatio = 1.5;
      const actualRatio = boundary150.x / boundary100.x;
      assertTrue(
        Math.abs(actualRatio - expectedRatio) < 0.1,
        `Boundary should scale by ${expectedRatio}, actual ratio: ${actualRatio}`
      );

      // Verify resize indicator at new zoom
      const canvasPos = await getCanvasPosition(ctx.page);
      const headers = await getHeaderDimensions(ctx.page);

      const resizeHandleX = canvasPos.left + boundary150.x - 2;
      const headerY = canvasPos.top + headers.headerHeight / 2;

      await ctx.page.mouse.move(resizeHandleX, headerY);
      await ctx.page.mouse.down();
      await sleep(100);

      const preview = await getResizePreviewPosition(ctx.page);
      assertTrue(preview !== null, 'Should get resize preview');
      assertTrue(preview.isResizing, 'Should be in resize mode');

      assertTrue(
        Math.abs(preview.x - boundary150.x) <= 3,
        `Preview X (${preview.x}) should match boundary (${boundary150.x}) at 150%`
      );
    } finally {
      await ctx.page.mouse.up();
    }
  },

  'Cell editor width matches cell width at various zoom levels': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    const testZoomLevels = [50, 100, 150, 200];

    for (const zoom of testZoomLevels) {
      await setZoomLevel(ctx.page, zoom);
      await clickCell(ctx.page, 1, 1);
      await startCellEditing(ctx.page);

      try {
        const editorPos = await getCellEditorPosition(ctx.page);
        assertTrue(editorPos !== null, `Editor should be visible at ${zoom}%`);

        const cellPos = await getCellRendererPosition(ctx.page, 1, 1);
        assertTrue(cellPos !== null, `Should get cell position at ${zoom}%`);

        assertTrue(
          Math.abs(editorPos.width - cellPos.width) <= 5,
          `Editor width (${editorPos.width}) should match cell width (${cellPos.width}) at ${zoom}%`
        );
      } finally {
        await cancelCellEditing(ctx.page);
      }
    }
  },

  'Selection size scales proportionally with zoom': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await setZoomLevel(ctx.page, 100);
    await clickCell(ctx.page, 2, 2);

    const sel100 = await getSelectionPosition(ctx.page, 2, 2);
    assertTrue(sel100 !== null, 'Should get selection at 100%');

    await setZoomLevel(ctx.page, 50);
    const sel50 = await getSelectionPosition(ctx.page, 2, 2);
    assertTrue(sel50 !== null, 'Should get selection at 50%');

    assertTrue(
      Math.abs(sel50.width - sel100.width * 0.5) <= 2,
      `Selection width at 50% (${sel50.width}) should be half of 100% (${sel100.width})`
    );
    assertTrue(
      Math.abs(sel50.height - sel100.height * 0.5) <= 2,
      `Selection height at 50% (${sel50.height}) should be half of 100% (${sel100.height})`
    );

    await setZoomLevel(ctx.page, 200);
    const sel200 = await getSelectionPosition(ctx.page, 2, 2);
    assertTrue(sel200 !== null, 'Should get selection at 200%');

    assertTrue(
      Math.abs(sel200.width - sel100.width * 2.0) <= 2,
      `Selection width at 200% (${sel200.width}) should be double of 100% (${sel100.width})`
    );
    assertTrue(
      Math.abs(sel200.height - sel100.height * 2.0) <= 2,
      `Selection height at 200% (${sel200.height}) should be double of 100% (${sel100.height})`
    );
  },
};

//=============================================================================
// Combine all tests
//=============================================================================

const tests = {
  ...boundaryZoomTests,
  ...zoomScrollTests,
  ...zoomFrozenTests,
  ...comprehensiveTests,
};

// Run all tests
runTests(tests);
