// Formula Range Drag (Resize/Move) E2E Tests
// Tests Excel-like formula range manipulation: resizing by dragging corners,
// moving by dragging borders, preserving $ markers and sheet prefixes.
//
// Phase 7 of Formula Range Resize and Move plan

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  doubleClickCell,
  setCellValue,
  getFormulaBarContent,
  getCanvasInfo,
  cellToPixelFromRenderer,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Start editing a formula in a cell (by double-clicking to enter edit mode)
 * Waits for the cell editor to be active and formula highlights to be ready.
 */
async function startEditingFormula(page, cellRef) {
  await doubleClickCell(page, cellRef);
  await sleep(500); // Give time for formula highlights to populate
}

/**
 * Get the formula highlight bounds from the app context.
 * Returns an array of highlight objects with their pixel bounds.
 */
async function getFormulaHighlightBounds(page) {
  return await page.evaluate(() => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return [];

    const highlights = ctx.app.formulaHighlights || [];
    if (highlights.length === 0) return [];

    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;
    const DEFAULT_ROW_HEIGHT = 24;
    const zoomFactor = ctx.app.renderer?.getZoomFactor() ?? 1;
    const scrollX = ctx.app.scrollX ?? 0;
    const scrollY = ctx.app.scrollY ?? 0;

    return highlights.map((h, index) => {
      let bounds = null;

      if (h.type === 'cell' && h.col !== undefined && h.row !== undefined) {
        // Single cell
        let offsetX = 0;
        for (let i = 0; i < h.col; i++) {
          offsetX += ctx.app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
        }
        let offsetY = 0;
        for (let i = 0; i < h.row; i++) {
          offsetY += ctx.app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
        }
        const cellWidth = ctx.app.colWidths.get(h.col) ?? DEFAULT_COL_WIDTH;
        const cellHeight = ctx.app.rowHeights.get(h.row) ?? DEFAULT_ROW_HEIGHT;

        bounds = {
          x: Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) - Math.round(scrollX * zoomFactor),
          y: Math.round(HEADER_HEIGHT * zoomFactor) + Math.round(offsetY * zoomFactor) - Math.round(scrollY * zoomFactor),
          width: Math.round(cellWidth * zoomFactor),
          height: Math.round(cellHeight * zoomFactor),
        };
      } else if (
        h.type === 'range' &&
        h.startCol !== undefined &&
        h.startRow !== undefined &&
        h.endCol !== undefined &&
        h.endRow !== undefined
      ) {
        // Range
        const minCol = Math.min(h.startCol, h.endCol);
        const maxCol = Math.max(h.startCol, h.endCol);
        const minRow = Math.min(h.startRow, h.endRow);
        const maxRow = Math.max(h.startRow, h.endRow);

        let offsetX = 0;
        for (let i = 0; i < minCol; i++) {
          offsetX += ctx.app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
        }
        let offsetY = 0;
        for (let i = 0; i < minRow; i++) {
          offsetY += ctx.app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
        }

        let totalWidth = 0;
        for (let i = minCol; i <= maxCol; i++) {
          totalWidth += ctx.app.colWidths.get(i) ?? DEFAULT_COL_WIDTH;
        }
        let totalHeight = 0;
        for (let i = minRow; i <= maxRow; i++) {
          totalHeight += ctx.app.rowHeights.get(i) ?? DEFAULT_ROW_HEIGHT;
        }

        bounds = {
          x: Math.round(HEADER_WIDTH * zoomFactor) + Math.round(offsetX * zoomFactor) - Math.round(scrollX * zoomFactor),
          y: Math.round(HEADER_HEIGHT * zoomFactor) + Math.round(offsetY * zoomFactor) - Math.round(scrollY * zoomFactor),
          width: Math.round(totalWidth * zoomFactor),
          height: Math.round(totalHeight * zoomFactor),
        };
      }

      return {
        index,
        type: h.type,
        sourceStart: h.sourceStart,
        sourceEnd: h.sourceEnd,
        bounds,
      };
    });
  });
}

/**
 * Get the corner position (in canvas coordinates) for a formula highlight.
 * @param {object} bounds - The highlight bounds
 * @param {'nw'|'ne'|'sw'|'se'} corner - Which corner
 */
function getCornerPosition(bounds, corner) {
  const { x, y, width, height } = bounds;
  switch (corner) {
    case 'nw': return { x, y };
    case 'ne': return { x: x + width, y };
    case 'sw': return { x, y: y + height };
    case 'se': return { x: x + width, y: y + height };
    default: return { x, y };
  }
}

/**
 * Get the border center position for a formula highlight (for move operations).
 * @param {object} bounds - The highlight bounds
 * @param {'n'|'s'|'e'|'w'} border - Which border
 */
function getBorderCenterPosition(bounds, border) {
  const { x, y, width, height } = bounds;
  switch (border) {
    case 'n': return { x: x + width / 2, y };
    case 's': return { x: x + width / 2, y: y + height };
    case 'e': return { x: x + width, y: y + height / 2 };
    case 'w': return { x, y: y + height / 2 };
    default: return { x: x + width / 2, y };
  }
}

/**
 * Perform a drag operation on the canvas
 */
async function dragOnCanvas(page, startX, startY, endX, endY, steps = 10) {
  const canvasInfo = await getCanvasInfo(page);

  // Add canvas offset to get screen coordinates
  const screenStartX = canvasInfo.left + startX;
  const screenStartY = canvasInfo.top + startY;
  const screenEndX = canvasInfo.left + endX;
  const screenEndY = canvasInfo.top + endY;

  await page.mouse.move(screenStartX, screenStartY);
  await sleep(50);
  await page.mouse.down();
  await page.mouse.move(screenEndX, screenEndY, { steps });
  await page.mouse.up();
  await sleep(200);
}

/**
 * Press Escape to cancel the current operation
 */
async function pressEscape(page) {
  await page.keyboard.press('Escape');
  await sleep(100);
}

/**
 * Confirm edit with Enter
 */
async function confirmEdit(page) {
  await page.keyboard.press('Enter');
  await sleep(300);
}

/**
 * Get the formula text currently being edited (from cell editor or formula bar)
 */
async function getEditingFormulaText(page) {
  return await page.evaluate(() => {
    // Check cell editor first
    const cellDisplay = document.getElementById('cell-display');
    if (cellDisplay && cellDisplay.style.display !== 'none') {
      const text = cellDisplay.textContent || '';
      if (text) return text;
    }
    // Check formula bar
    const formulaDisplay = document.getElementById('formula-display');
    if (formulaDisplay) {
      return formulaDisplay.textContent || '';
    }
    return '';
  });
}

// ============================================================================
// Tests
// ============================================================================

const tests = {
  // --------------------------------------------------------------------------
  // Basic Functionality Tests
  // --------------------------------------------------------------------------

  'Formula editing shows formula highlights': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up a formula referencing A1
    await setCellValue(ctx.page, 'A1', '10');
    await sleep(200);
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Verify formula was saved
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    const formula = await getFormulaBarContent(ctx.page);
    assertEqual(formula, '=A1', 'Formula should be =A1');

    // Start editing B1
    await startEditingFormula(ctx.page, 'B1');

    // Get the formula highlight bounds
    const highlights = await getFormulaHighlightBounds(ctx.page);
    assertTrue(highlights.length > 0, 'Should have at least one formula highlight when editing');

    // Cancel editing
    await pressEscape(ctx.page);
  },

  // --------------------------------------------------------------------------
  // Resize Tests
  // --------------------------------------------------------------------------

  'Resize cell reference to range by dragging SE corner': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells with values
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await setCellValue(ctx.page, 'A3', '30');
    await sleep(200);

    // Set up formula
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Start editing B1
    await startEditingFormula(ctx.page, 'B1');

    // Get the formula highlight bounds for A1
    const highlights = await getFormulaHighlightBounds(ctx.page);
    assertTrue(highlights.length > 0, 'Should have at least one formula highlight');

    const h = highlights[0];
    assertTrue(h.bounds !== null, 'Highlight should have bounds');

    // Drag the SE corner down to A3 (adding 2 rows)
    const seCorner = getCornerPosition(h.bounds, 'se');

    // Calculate target: A3's bottom edge
    const a3Pos = await cellToPixelFromRenderer(ctx.page, 0, 2);
    // Target the bottom of A3
    const targetY = a3Pos.y + 12; // half cell height below center

    await dragOnCanvas(ctx.page, seCorner.x, seCorner.y, seCorner.x, targetY);

    // Check the formula text while editing
    const editingFormula = await getEditingFormulaText(ctx.page);
    assertTrue(
      editingFormula.includes('A1:A3') || editingFormula.includes('A1:A2'),
      `Formula should be expanded to a range, got: ${editingFormula}`
    );

    // Confirm the edit
    await confirmEdit(ctx.page);

    // Verify the formula was updated to a range
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    assertTrue(
      finalFormula.includes(':'),
      `Formula should contain a range reference, got: ${finalFormula}`
    );
  },

  'Resize preserves absolute reference markers ($)': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await sleep(200);

    // Set up formula with absolute references
    await setCellValue(ctx.page, 'B1', '=$A$1');
    await sleep(200);

    // Verify formula with $ markers
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    const initialFormula = await getFormulaBarContent(ctx.page);
    assertEqual(initialFormula, '=$A$1', 'Initial formula should be =$A$1');

    // Start editing B1
    await startEditingFormula(ctx.page, 'B1');

    // Get the formula highlight bounds
    const highlights = await getFormulaHighlightBounds(ctx.page);
    assertTrue(highlights.length > 0, 'Should have at least one formula highlight');

    const h = highlights[0];
    assertTrue(h.bounds !== null, 'Highlight should have bounds');

    // Drag SE corner down to expand to A2
    const seCorner = getCornerPosition(h.bounds, 'se');
    const a2Pos = await cellToPixelFromRenderer(ctx.page, 0, 1);
    const targetY = a2Pos.y + 12;

    await dragOnCanvas(ctx.page, seCorner.x, seCorner.y, seCorner.x, targetY);

    // Confirm the edit
    await confirmEdit(ctx.page);

    // Verify $ markers are preserved
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    assertTrue(
      finalFormula.includes('$'),
      `Formula should preserve $ markers, got: ${finalFormula}`
    );
  },

  // --------------------------------------------------------------------------
  // Move Tests
  // --------------------------------------------------------------------------

  'Move cell reference to new location by dragging border': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'B1', '20');
    await sleep(200);

    // Set up formula
    await setCellValue(ctx.page, 'C1', '=A1');
    await sleep(200);

    // Verify initial formula
    await clickCell(ctx.page, 'C1');
    await sleep(200);
    const initialFormula = await getFormulaBarContent(ctx.page);
    assertEqual(initialFormula, '=A1', 'Initial formula should be =A1');

    // Start editing C1
    await startEditingFormula(ctx.page, 'C1');

    // Get the formula highlight bounds for A1
    const highlights = await getFormulaHighlightBounds(ctx.page);
    assertTrue(highlights.length > 0, 'Should have at least one formula highlight');

    const h = highlights[0];
    assertTrue(h.bounds !== null, 'Highlight should have bounds');

    // Drag the east border to move from A1 to B1
    const eBorder = getBorderCenterPosition(h.bounds, 'e');
    const b1Pos = await cellToPixelFromRenderer(ctx.page, 1, 0);

    await dragOnCanvas(ctx.page, eBorder.x, eBorder.y, b1Pos.x, eBorder.y);

    // Confirm the edit
    await confirmEdit(ctx.page);

    // Verify the formula was updated
    await clickCell(ctx.page, 'C1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    assertEqual(finalFormula, '=B1', 'Formula should be updated to =B1');
  },

  // --------------------------------------------------------------------------
  // Cancel Tests
  // --------------------------------------------------------------------------

  'Escape during drag cancels and restores original formula': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set up cells
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'A2', '20');
    await sleep(200);

    // Set up formula
    await setCellValue(ctx.page, 'B1', '=A1');
    await sleep(200);

    // Start editing B1
    await startEditingFormula(ctx.page, 'B1');

    // Get the formula highlight bounds
    const highlights = await getFormulaHighlightBounds(ctx.page);
    assertTrue(highlights.length > 0, 'Should have at least one formula highlight');

    const h = highlights[0];
    assertTrue(h.bounds !== null, 'Highlight should have bounds');

    // Start dragging to resize
    const canvasInfo = await getCanvasInfo(ctx.page);
    const seCorner = getCornerPosition(h.bounds, 'se');
    const a2Pos = await cellToPixelFromRenderer(ctx.page, 0, 1);

    // Start the drag
    await ctx.page.mouse.move(canvasInfo.left + seCorner.x, canvasInfo.top + seCorner.y);
    await ctx.page.mouse.down();
    await ctx.page.mouse.move(canvasInfo.left + seCorner.x, canvasInfo.top + a2Pos.y + 12, { steps: 5 });

    // Press Escape to cancel the drag
    await pressEscape(ctx.page);
    await ctx.page.mouse.up();
    await sleep(200);

    // The formula should be restored while still editing
    const editingFormula = await getEditingFormulaText(ctx.page);
    assertEqual(editingFormula, '=A1', 'Formula should be restored to =A1 after Escape during drag');

    // Cancel editing
    await pressEscape(ctx.page);

    // Verify formula is unchanged
    await clickCell(ctx.page, 'B1');
    await sleep(200);
    const finalFormula = await getFormulaBarContent(ctx.page);
    assertEqual(finalFormula, '=A1', 'Formula should remain =A1 after canceling');
  },
};

// Run tests
runTests(tests);
