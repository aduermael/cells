// Cursor behavior tests for Cells spreadsheet application
// Tests cursor persistence, formula reference insertion, and focus transitions

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  doubleClickCell,
  setCellValue,
  getFormulaBarContent,
  getCurrentCellRef,
  assertEqual,
  assertTrue,
  sleep,
  parseCellRef,
  getCanvasInfo,
  cellToPixel,
  selectRange,
} from './helpers.mjs';

/**
 * Get cursor position from the EditingSession (single source of truth)
 */
async function getSessionCursorPos(page) {
  return await page.evaluate(() => {
    // Access the editingSession global singleton
    if (window._editingSession) {
      const sel = window._editingSession.getSelection();
      return sel.start;
    }
    return null;
  });
}

/**
 * Get the cursor position from any active editor (cell-display or formula-display)
 */
async function getActiveCursorPos(page) {
  return await page.evaluate(() => {
    const active = document.activeElement;
    if (!active || (active.id !== 'cell-display' && active.id !== 'formula-display')) {
      return null;
    }
    const sel = window.getSelection();
    if (!sel || sel.rangeCount === 0) return null;
    const range = sel.getRangeAt(0);

    // Calculate position by walking through text nodes
    let pos = 0;
    const walker = document.createTreeWalker(active, NodeFilter.SHOW_TEXT, null);
    let node;
    while ((node = walker.nextNode())) {
      if (node === range.startContainer) {
        return pos + range.startOffset;
      }
      pos += node.textContent.length;
    }
    return pos;
  });
}

/**
 * Get which editor is currently focused
 */
async function getActiveEditor(page) {
  return await page.evaluate(() => {
    const active = document.activeElement;
    if (active?.id === 'cell-display') return 'cell';
    if (active?.id === 'formula-display') return 'formula';
    if (active?.id === 'grid') return 'canvas';
    return 'other';
  });
}

/**
 * Check if currently editing (either cell or formula bar is active)
 */
async function isEditing(page) {
  const editor = await getActiveEditor(page);
  return editor === 'cell' || editor === 'formula';
}

const tests = {
  // ===========================================================================
  // Phase 5a: Cursor Persistence Tests
  // ===========================================================================

  'Arrow keys move cursor within cell (not navigation) when not at boundary': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value
    await setCellValue(ctx.page, 'C3', 'ABCDEF');
    await sleep(100);

    // Double-click to edit (puts cursor at end by default)
    await doubleClickCell(ctx.page, 'C3');
    await sleep(200);

    // Type Home to go to beginning, then move right 3 times
    await ctx.page.keyboard.press('Home');
    await sleep(50);
    await ctx.page.keyboard.press('ArrowRight');
    await ctx.page.keyboard.press('ArrowRight');
    await ctx.page.keyboard.press('ArrowRight');
    await sleep(100);

    // Press left arrow - should move cursor left, not navigate cells
    await ctx.page.keyboard.press('ArrowLeft');
    await sleep(100);

    // Should still be editing same cell
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'C3', 'Should still be on C3');

    // Verify we're still editing
    const editing = await isEditing(ctx.page);
    assertTrue(editing, 'Should still be in editing mode');
  },

  'Arrow keys navigate cells when cursor at boundary': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'D3', 'Text');
    await setCellValue(ctx.page, 'E3', 'Next');
    await sleep(100);

    // Double-click to edit D3 (cursor at end)
    await doubleClickCell(ctx.page, 'D3');
    await sleep(200);

    // Press right arrow at end - should commit and navigate
    await ctx.page.keyboard.press('ArrowRight');
    await sleep(200);

    // Should be on E3 now
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'E3', 'Should have navigated to E3');
  },

  'Up/Down arrow keys navigate cells during editing': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'B2', 'Middle');
    await setCellValue(ctx.page, 'B1', 'Top');
    await setCellValue(ctx.page, 'B3', 'Bottom');
    await sleep(100);

    // Double-click to edit B2
    await doubleClickCell(ctx.page, 'B2');
    await sleep(200);

    // Press up arrow - should commit and navigate up
    await ctx.page.keyboard.press('ArrowUp');
    await sleep(200);

    // Should be on B1 now
    let cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B1', 'Should have navigated to B1');

    // Edit B1, then press down
    await doubleClickCell(ctx.page, 'B1');
    await sleep(200);
    await ctx.page.keyboard.press('ArrowDown');
    await sleep(200);

    // Should be on B2 now
    cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B2', 'Should have navigated to B2');
  },

  // ===========================================================================
  // Phase 5b: Formula Reference Insertion Tests
  // ===========================================================================

  'Click cell during formula editing inserts reference at cursor': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in B1 for reference
    await setCellValue(ctx.page, 'B1', '100');
    await sleep(100);

    // Start editing A1 with a formula
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await ctx.page.keyboard.type('=SUM(', { delay: 50 });
    await sleep(200);

    // Verify we're in formula mode
    let content = await getFormulaBarContent(ctx.page);
    assertTrue(content.startsWith('=SUM('), 'Should have formula starting with =SUM(');

    // Click on B1 to insert reference
    await clickCell(ctx.page, 'B1');
    await sleep(300);

    // Formula should have B1 inserted
    content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(B1', 'Formula should be =SUM(B1');
  },

  'Multiple reference insertions maintain correct cursor positions': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'D1', '10');
    await setCellValue(ctx.page, 'E1', '20');
    await sleep(100);

    // Start editing A1 with a formula
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await ctx.page.keyboard.type('=', { delay: 50 });
    await sleep(100);

    // Click D1 to insert first reference
    await clickCell(ctx.page, 'D1');
    await sleep(200);

    // Type the operator
    await ctx.page.keyboard.type('+', { delay: 50 });
    await sleep(100);

    // Click E1 to insert second reference
    await clickCell(ctx.page, 'E1');
    await sleep(200);

    // Formula should have both references
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=D1+E1', 'Formula should be =D1+E1');
  },

  'Reference insertion then typing places cursor correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value in nearby cell (avoid viewport issues with far cells)
    await setCellValue(ctx.page, 'C1', '50');
    await sleep(100);

    // Start formula in A2
    await clickCell(ctx.page, 'A2');
    await sleep(100);
    await ctx.page.keyboard.type('=', { delay: 50 });
    await sleep(200);

    // Click C1 to insert reference
    await clickCell(ctx.page, 'C1');
    await sleep(300);

    // Type after the reference (cursor should be after C1)
    await ctx.page.keyboard.type('*2', { delay: 50 });
    await sleep(100);

    // Verify formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=C1*2', 'Formula should be =C1*2 (cursor was after C1)');
  },

  'Formula with SUM function and multiple clicks': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values
    await setCellValue(ctx.page, 'A1', '1');
    await setCellValue(ctx.page, 'A2', '2');
    await setCellValue(ctx.page, 'A3', '3');
    await sleep(100);

    // Start editing B1 with a formula
    await clickCell(ctx.page, 'B1');
    await sleep(100);
    await ctx.page.keyboard.type('=SUM(', { delay: 50 });
    await sleep(200);

    // Click A1
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Type comma
    await ctx.page.keyboard.type(',', { delay: 50 });
    await sleep(100);

    // Click A3
    await clickCell(ctx.page, 'A3');
    await sleep(200);

    // Type closing paren
    await ctx.page.keyboard.type(')', { delay: 50 });
    await sleep(100);

    // Verify formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=SUM(A1,A3)', 'Formula should be =SUM(A1,A3)');
  },

  // ===========================================================================
  // Phase 5c: Focus Transitions Tests
  // ===========================================================================

  'Typing in formula bar updates value': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on cell to select it
    await clickCell(ctx.page, 'G1');
    await sleep(100);

    // Focus formula bar by clicking on it
    await ctx.page.evaluate(() => {
      document.getElementById('formula-display').focus();
    });
    await sleep(200);

    // Type text
    await ctx.page.keyboard.type('Hello', { delay: 50 });
    await sleep(100);

    // Press Enter to commit
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Verify the value was updated
    await clickCell(ctx.page, 'G1');
    await sleep(100);
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'Hello', 'Value should be Hello');
  },

  'Formula bar shows formula when cell selected': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter values and a formula
    await setCellValue(ctx.page, 'A1', '10');
    await setCellValue(ctx.page, 'B1', '=A1*2');
    await sleep(200);

    // Click on the formula cell
    await clickCell(ctx.page, 'B1');
    await sleep(200);

    // Formula bar should show the formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1*2', 'Formula bar should show =A1*2');
  },

  'Escape cancels edit and returns to canvas': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a value
    await setCellValue(ctx.page, 'A1', 'Original');
    await sleep(100);

    // Double-click to edit
    await doubleClickCell(ctx.page, 'A1');
    await sleep(200);

    // Type something different
    await ctx.page.keyboard.type('Changed', { delay: 50 });
    await sleep(100);

    // Press Escape to cancel
    await ctx.page.keyboard.press('Escape');
    await sleep(200);

    // Verify original value is preserved
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'Original', 'Original value should be preserved after Escape');
  },

  'Enter commits edit and moves down': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Type a value
    await ctx.page.keyboard.type('TestValue', { delay: 50 });
    await sleep(100);

    // Press Enter
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Should be on B3 now
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'B3', 'Should have moved to B3 after Enter');

    // Verify the value was saved
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'TestValue', 'Value should be saved');
  },

  'Tab commits edit and moves right': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on C3
    await clickCell(ctx.page, 'C3');
    await sleep(100);

    // Type a value
    await ctx.page.keyboard.type('TabTest', { delay: 50 });
    await sleep(100);

    // Press Tab
    await ctx.page.keyboard.press('Tab');
    await sleep(200);

    // Should be on D3 now
    const cellRef = await getCurrentCellRef(ctx.page);
    assertEqual(cellRef, 'D3', 'Should have moved to D3 after Tab');

    // Verify the value was saved
    await clickCell(ctx.page, 'C3');
    await sleep(100);
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, 'TabTest', 'Value should be saved');
  },

  // ===========================================================================
  // Regression Tests
  // ===========================================================================

  'Typing = keeps cursor at end (not reset to 0)': async (ctx) => {
    // Regression test: typing '=' triggered formula colorization which
    // set innerHTML and reset cursor to position 0
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on cell to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Type '=' to start a formula
    await ctx.page.keyboard.type('=', { delay: 50 });
    await sleep(200);

    // Get cursor position - should be at 1 (after the '=')
    const cursorPos = await getActiveCursorPos(ctx.page);
    assertEqual(cursorPos, 1, 'Cursor should be at position 1 after typing =');

    // Continue typing to verify cursor works correctly
    await ctx.page.keyboard.type('A1', { delay: 50 });
    await sleep(100);

    // Verify the formula is correct (not =A1 with cursor issues)
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, '=A1', 'Formula should be =A1');
  },

  'Typing formula maintains cursor position throughout': async (ctx) => {
    // Verify cursor stays correct while typing entire formula
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on cell to select it
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Type formula character by character, checking cursor
    const formula = '=SUM(A1)';
    for (let i = 0; i < formula.length; i++) {
      await ctx.page.keyboard.type(formula[i], { delay: 30 });
      await sleep(100);

      const cursorPos = await getActiveCursorPos(ctx.page);
      assertEqual(cursorPos, i + 1, `Cursor should be at position ${i + 1} after typing "${formula.slice(0, i + 1)}"`);
    }

    // Verify final formula
    const content = await getFormulaBarContent(ctx.page);
    assertEqual(content, formula, `Formula should be ${formula}`);
  },
};

// Run all tests
runTests(tests);
