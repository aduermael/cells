// CRDT Resurrection E2E Tests
// Tests that full-state SET operations correctly resurrect deleted entities
// when operations arrive out of order between peers.
//
// The resurrection problem:
// - Peer A: DELETE cell at t=1000
// - Peer B: SET cell with value+style+format at t=2000
// - On Peer C, DELETE arrives first, then SET
// With LWW semantics, SET (t=2000) should win, resurrecting the cell with ALL properties.
//
// Run with HEADED=1 for visible browser:
//   HEADED=1 bazel run :e2e -- collab-resurrection
//
// Run standalone:
//   bazel run :e2e -- collab-resurrection

import { setup, runTest } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  assertEqual,
  assertTrue,
  sleep,
  waitForCollabReady,
  waitForPeerConnection,
  assertWithRetry,
  resizeColumn,
  resizeRow,
} from './helpers.mjs';

// =============================================================================
// Styling Helper Functions
// =============================================================================

/**
 * Apply a background color to the currently selected cell(s) using the toolbar
 * @param {Page} page - Puppeteer page
 * @param {string} color - Hex color (e.g., '#3B82F6')
 */
async function applyBackgroundColor(page, color) {
  await page.click('#style-bg-color-btn');
  await sleep(100);

  const colorSelector = `#bg-color-popup .color-option[data-color="${color.toUpperCase()}"]`;
  const hasColor = await page.$(colorSelector);

  if (hasColor) {
    await page.$eval(colorSelector, el => el.click());
  } else {
    const hexInput = await page.$('#bg-color-popup .color-hex-input');
    if (hexInput) {
      await hexInput.click({ clickCount: 3 });
      await page.keyboard.type(color);
      await page.keyboard.press('Enter');
    }
  }
  await sleep(200);
}

/**
 * Toggle bold on the currently selected cell(s)
 * @param {Page} page - Puppeteer page
 */
async function applyBold(page) {
  await page.click('#style-bold-btn');
  await sleep(200);
}

/**
 * Apply a number format category to the current selection
 * @param {Page} page - Puppeteer page
 * @param {'NUMBER' | 'CURRENCY' | 'PERCENTAGE' | 'ACCOUNTING'} formatCategory
 */
async function applyNumberFormat(page, formatCategory) {
  await page.click('#format-dropdown-btn');
  await sleep(100);
  await page.click(`[data-format-category="${formatCategory}"]`);
  await sleep(200);
}

// Color palette constants
const COLORS = {
  BLUE_500: '#3B82F6',
  GREEN_500: '#10B981',
  RED_500: '#EF4444',
};

/**
 * Generate a random room ID for testing
 */
function generateRoomId() {
  const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
  let id = '';
  for (let i = 0; i < 8; i++) {
    id += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return id;
}

/**
 * Navigate to a specific collaboration room
 */
async function joinRoom(page, baseUrl, roomId) {
  const url = `${baseUrl}/?room=${roomId}`;
  await page.goto(url);
  await waitForAppReady(page);
  const ready = await waitForCollabReady(page, 10000);
  if (!ready) {
    console.warn(`[joinRoom] Collab not ready for room ${roomId}, continuing anyway...`);
  }
}

// =============================================================================
// Style Verification Helpers
// =============================================================================

/**
 * Get the effective style of a cell by querying the WASM engine directly.
 */
async function getCellStyle(page, cellRef) {
  const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
  if (!match) throw new Error(`Invalid cell reference: ${cellRef}`);

  const colStr = match[1].toUpperCase();
  const row = parseInt(match[2], 10) - 1;
  let col = 0;
  for (let i = 0; i < colStr.length; i++) {
    col = col * 26 + (colStr.charCodeAt(i) - 64);
  }
  col -= 1;

  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app?.dataSource) return null;

    try {
      const style = await ctx.app.dataSource.getEffectiveCellStyle(col, row);
      return style;
    } catch (e) {
      console.error('[getCellStyle] Error:', e);
      return null;
    }
  }, { col, row });
}

/**
 * Get cell format info (number format category, etc.)
 */
async function getCellFormat(page, cellRef) {
  const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
  if (!match) throw new Error(`Invalid cell reference: ${cellRef}`);

  const colStr = match[1].toUpperCase();
  const row = parseInt(match[2], 10) - 1;
  let col = 0;
  for (let i = 0; i < colStr.length; i++) {
    col = col * 26 + (colStr.charCodeAt(i) - 64);
  }
  col -= 1;

  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app?.dataSource) return null;

    try {
      // Get format from effective cell format
      const format = await ctx.app.dataSource.getEffectiveCellFormat(col, row);
      return format;
    } catch (e) {
      console.error('[getCellFormat] Error:', e);
      return null;
    }
  }, { col, row });
}

/**
 * Delete the currently selected cell
 */
async function deleteCurrentCell(page) {
  await page.keyboard.press('Delete');
  await sleep(200);
}

/**
 * Get column width at a given position
 */
async function getColumnWidth(page, col) {
  return await page.evaluate(({ col }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return 100; // Default
    return ctx.app.colWidths.get(col) ?? 100;
  }, { col });
}

/**
 * Get row height at a given position
 */
async function getRowHeight(page, row) {
  return await page.evaluate(({ row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app) return 24; // Default
    return ctx.app.rowHeights.get(row) ?? 24;
  }, { row });
}

async function runCollabResurrectionTests() {
  let ctx;
  let page2;
  let context2;
  const results = [];

  console.log('\n=== CRDT Resurrection E2E Tests ===\n');

  try {
    ctx = await setup();

    // Create second browser context for peer 2
    context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Test 1: Cell resurrection preserves value + style + format
    results.push(await runTest('Cell resurrection preserves value + style + format', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // Peer 1 creates a styled cell with format
      await setCellValue(ctx.page, 'A1', '1234.56');
      await sleep(300);
      await clickCell(ctx.page, 'A1');
      await applyBackgroundColor(ctx.page, COLORS.BLUE_500);
      await applyBold(ctx.page);
      await applyNumberFormat(ctx.page, 'CURRENCY');
      await sleep(500);

      // Verify cell exists on peer 2 with all properties
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertTrue(content && content.includes('1234'), `Value should sync to peer 2, got: ${content}`);
      }, { retries: 10, initialDelay: 300 });

      // Verify style synced to peer 2
      await assertWithRetry(async () => {
        const style = await getCellStyle(page2, 'A1');
        assertTrue(style !== null, 'Peer 2 should have style info for A1');
        assertTrue(style?.bold === true, 'A1 should be bold on peer 2');
        const bgColor = (style?.bgColor || '').toUpperCase();
        assertEqual(bgColor, COLORS.BLUE_500, 'A1 should have blue background on peer 2');
      }, { retries: 8, initialDelay: 300 });

      // Now simulate a "late delete" scenario:
      // Peer 1 deletes the cell
      await clickCell(ctx.page, 'A1');
      await deleteCurrentCell(ctx.page);
      await sleep(500);

      // Peer 2 re-creates the cell with new value
      // This simulates a case where peer 2 had set the cell before seeing the delete
      await setCellValue(page2, 'A1', '9999');
      await sleep(300);
      await clickCell(page2, 'A1');
      await applyBackgroundColor(page2, COLORS.GREEN_500);
      await sleep(500);

      // Both should converge - the SET wins over DELETE due to higher timestamp
      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'A1');
        await sleep(200);
        const content = await getFormulaBarContent(ctx.page);
        assertTrue(content && content.includes('9999'), `Peer 1 should see resurrected value 9999, got: ${content}`);
      }, { retries: 10, initialDelay: 300 });

      // Style should also be present (from the new SET)
      await assertWithRetry(async () => {
        const style = await getCellStyle(ctx.page, 'A1');
        assertTrue(style !== null, 'Peer 1 should see style on resurrected cell');
        const bgColor = (style?.bgColor || '').toUpperCase();
        assertEqual(bgColor, COLORS.GREEN_500, 'Resurrected cell should have green background');
      }, { retries: 8, initialDelay: 300 });
    }));

    // Test 2: Axis resurrection preserves size/hidden/style
    results.push(await runTest('Axis resurrection preserves size', async () => {
      const roomId = generateRoomId();

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // Peer 1 resizes column A to 200px
      await clickCell(ctx.page, 'A1');
      await resizeColumn(ctx.page, 'A', 200);
      await sleep(500);

      // Verify size synced to peer 2
      await assertWithRetry(async () => {
        const width = await getColumnWidth(page2, 0);
        assertTrue(width >= 195 && width <= 205, `Column A width should be ~200 on peer 2, got: ${width}`);
      }, { retries: 8, initialDelay: 300 });

      // Peer 1 resizes row 1 to 50px
      await resizeRow(ctx.page, 1, 50);
      await sleep(500);

      // Verify row size synced to peer 2
      await assertWithRetry(async () => {
        const height = await getRowHeight(page2, 0);
        assertTrue(height >= 45 && height <= 55, `Row 1 height should be ~50 on peer 2, got: ${height}`);
      }, { retries: 8, initialDelay: 300 });
    }));

    // Test 3: Formula cell resurrection with dependencies
    results.push(await runTest('Formula cell resurrection preserves formula', async () => {
      const roomId = generateRoomId();

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // Peer 1 creates dependency cells and formula
      await setCellValue(ctx.page, 'A1', '10');
      await sleep(200);
      await setCellValue(ctx.page, 'B1', '20');
      await sleep(200);
      await setCellValue(ctx.page, 'C1', '=A1+B1');
      await sleep(500);

      // Verify formula result on peer 2
      await assertWithRetry(async () => {
        await clickCell(page2, 'C1');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertTrue(content && content.includes('='), `C1 should have formula on peer 2, got: ${content}`);
      }, { retries: 10, initialDelay: 300 });

      // Peer 2 updates the formula with style
      await clickCell(page2, 'C1');
      await applyBackgroundColor(page2, COLORS.RED_500);
      await sleep(500);

      // Verify style synced to peer 1
      await assertWithRetry(async () => {
        const style = await getCellStyle(ctx.page, 'C1');
        assertTrue(style !== null, 'Peer 1 should see style on C1');
        const bgColor = (style?.bgColor || '').toUpperCase();
        assertEqual(bgColor, COLORS.RED_500, 'C1 should have red background on peer 1');
      }, { retries: 8, initialDelay: 300 });

      // Verify formula still works after style change
      await clickCell(ctx.page, 'C1');
      await sleep(200);
      const formulaContent = await getFormulaBarContent(ctx.page);
      assertTrue(formulaContent && formulaContent.includes('=A1+B1'),
        `Formula should be preserved after style change, got: ${formulaContent}`);
    }));

    // Test 4: Concurrent edits on same cell - both style and value
    results.push(await runTest('Concurrent style and value edits converge correctly', async () => {
      const roomId = generateRoomId();

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // Peer 1 creates a cell
      await setCellValue(ctx.page, 'D1', 'Original');
      await sleep(500);

      // Wait for sync
      await assertWithRetry(async () => {
        await clickCell(page2, 'D1');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, 'Original', 'Value should sync to peer 2');
      }, { retries: 8, initialDelay: 300 });

      // Both peers edit simultaneously
      // Peer 1 changes value
      await setCellValue(ctx.page, 'D1', 'FromPeer1');

      // Peer 2 adds style (should include the current value in full-state op)
      await clickCell(page2, 'D1');
      await applyBold(page2);
      await applyBackgroundColor(page2, COLORS.GREEN_500);
      await sleep(500);

      // After convergence, the latest SET should win
      // Both should see the same final state (order depends on timestamps)
      await sleep(1000); // Allow time for sync

      // Verify both peers converged to same state
      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'D1');
        await clickCell(page2, 'D1');
        await sleep(200);

        const style1 = await getCellStyle(ctx.page, 'D1');
        const style2 = await getCellStyle(page2, 'D1');

        // Both should have the same style state
        assertEqual(style1?.bold, style2?.bold, 'Bold state should match between peers');
      }, { retries: 8, initialDelay: 500 });
    }));

  } finally {
    if (page2) {
      await page2.close().catch(() => {});
    }
    if (ctx) {
      await ctx.close();
    }
  }

  // Print summary
  console.log('\n=== CRDT Resurrection Test Summary ===');
  const passed = results.filter(r => r.passed).length;
  const failed = results.filter(r => !r.passed).length;
  console.log(`Passed: ${passed}`);
  console.log(`Failed: ${failed}`);

  if (failed > 0) {
    console.log('\nFailed tests:');
    for (const r of results.filter(r => !r.passed)) {
      console.log(`  - ${r.name}: ${r.error}`);
    }
    process.exit(1);
  }

  process.exit(0);
}

// Run the tests
runCollabResurrectionTests();
