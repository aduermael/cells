// Style Late Join Test
// Tests that when peer 2 joins AFTER peer 1 has already applied styles,
// peer 2 receives and displays those styles correctly.
//
// This is Bug #4 from the collab demo improvements plan:
// "Existing styles not loaded when joining a room"
//
// Run with HEADED=1 for visible browser:
//   HEADED=1 bazel run :e2e -- style-late-join
//
// Run standalone:
//   bazel run :e2e -- style-late-join

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
} from './helpers.mjs';

/**
 * Apply a background color to the currently selected cell(s) using the toolbar
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
 */
async function applyBold(page) {
  await page.click('#style-bold-btn');
  await sleep(200);
}

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
 * Get sync debug info from the WASM engine
 */
async function getSyncDebugInfo(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app?.dataSource?._client) return null;

    try {
      // Get sync state
      const syncState = await ctx.app.dataSource._client.getSyncState();
      return {
        syncState,
        oplogSize: syncState.oplogSize,
        peerCount: syncState.peerCount,
        state: syncState.state,
      };
    } catch (e) {
      console.error('[getSyncDebugInfo] Error:', e);
      return null;
    }
  });
}

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

const COLORS = {
  BLUE_500: '#3B82F6',
  GREEN_500: '#10B981',
  RED_500: '#EF4444',
};

async function runStyleLateJoinTests() {
  let ctx;
  let page2;
  let context2;
  const results = [];

  console.log('\n=== Style Late Join Tests ===\n');
  console.log('Testing Bug #4: Existing styles should be visible when joining a room\n');

  try {
    // Setup first browser context
    ctx = await setup();

    // Create second browser context
    context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Test 1: Late-joining peer receives background colors
    results.push(await runTest('Late-joining peer receives background color applied before joining', async () => {
      const roomId = generateRoomId();
      console.log(`[Test] Room ID: ${roomId}`);

      // Step 1: Peer 1 joins the room FIRST
      console.log('[Test] Peer 1 joining room...');
      await joinRoom(ctx.page, ctx.baseUrl, roomId);

      // Step 2: Peer 1 sets a value and applies background color (BEFORE peer 2 joins)
      console.log('[Test] Peer 1 applying style before peer 2 joins...');
      await setCellValue(ctx.page, 'A1', 'Blue');
      await sleep(200);
      await clickCell(ctx.page, 'A1');
      await applyBackgroundColor(ctx.page, COLORS.BLUE_500);
      await sleep(500);

      // Verify peer 1 has the style
      const peer1Style = await getCellStyle(ctx.page, 'A1');
      console.log('[Test] Peer 1 A1 style:', JSON.stringify(peer1Style, null, 2));
      assertTrue(peer1Style !== null, 'Peer 1 should have style for A1');
      assertEqual(peer1Style?.bgColor?.toUpperCase(), COLORS.BLUE_500.toUpperCase(), 'Peer 1 A1 should have blue bg');

      // Debug: Check peer 1 oplog size
      const peer1Debug = await getSyncDebugInfo(ctx.page);
      console.log('[Test] Peer 1 sync debug info:', JSON.stringify(peer1Debug, null, 2));

      // Step 3: NOW peer 2 joins (AFTER styles were applied)
      console.log('[Test] Peer 2 joining room (AFTER peer 1 applied styles)...');
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peer connection
      const peer1Connected = await waitForPeerConnection(ctx.page, 10000);
      const peer2Connected = await waitForPeerConnection(page2, 10000);
      console.log(`[Test] Peer 1 connected: ${peer1Connected}, Peer 2 connected: ${peer2Connected}`);
      assertTrue(peer2Connected, 'Peer 2 should connect to Peer 1');

      // Give time for initial sync to complete
      await sleep(2000);

      // Debug: Check peer 2 oplog size after sync
      const peer2DebugAfterSync = await getSyncDebugInfo(page2);
      console.log('[Test] Peer 2 sync debug info after sync:', JSON.stringify(peer2DebugAfterSync, null, 2));

      // Step 4: Verify peer 2 received the value
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, 'Blue', 'Peer 2 should receive the cell value');
      }, { retries: 10, initialDelay: 500 });

      // Step 5: Verify peer 2 received the STYLE
      await assertWithRetry(async () => {
        const peer2Style = await getCellStyle(page2, 'A1');
        console.log('[Test] Peer 2 A1 style:', JSON.stringify(peer2Style, null, 2));
        assertTrue(peer2Style !== null, 'Peer 2 should have style info for A1');
        assertEqual(
          peer2Style?.bgColor?.toUpperCase(),
          COLORS.BLUE_500.toUpperCase(),
          'Peer 2 A1 should have blue background (late-join style sync)'
        );
      }, { retries: 10, initialDelay: 500 });
    }));

    // Test 2: Late-joining peer receives multiple styles
    results.push(await runTest('Late-joining peer receives multiple styled cells', async () => {
      const roomId = generateRoomId();
      console.log(`[Test] Room ID: ${roomId}`);

      // Peer 1 joins first
      await joinRoom(ctx.page, ctx.baseUrl, roomId);

      // Peer 1 applies multiple styles BEFORE peer 2 joins
      await setCellValue(ctx.page, 'B1', 'Bold');
      await clickCell(ctx.page, 'B1');
      await applyBold(ctx.page);
      await sleep(200);

      await setCellValue(ctx.page, 'C1', 'Green');
      await clickCell(ctx.page, 'C1');
      await applyBackgroundColor(ctx.page, COLORS.GREEN_500);
      await sleep(200);

      await setCellValue(ctx.page, 'D1', 'Red');
      await clickCell(ctx.page, 'D1');
      await applyBackgroundColor(ctx.page, COLORS.RED_500);
      await sleep(500);

      // Verify peer 1 has all styles
      const p1StyleB1 = await getCellStyle(ctx.page, 'B1');
      const p1StyleC1 = await getCellStyle(ctx.page, 'C1');
      const p1StyleD1 = await getCellStyle(ctx.page, 'D1');
      console.log('[Test] Peer 1 styles:', { B1: p1StyleB1, C1: p1StyleC1, D1: p1StyleD1 });

      // NOW peer 2 joins
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);
      await sleep(2000);

      // Verify peer 2 received all values
      await assertWithRetry(async () => {
        await clickCell(page2, 'B1');
        const b1 = await getFormulaBarContent(page2);
        assertEqual(b1, 'Bold', 'Peer 2 should receive B1 value');
      }, { retries: 5, initialDelay: 500 });

      await assertWithRetry(async () => {
        await clickCell(page2, 'C1');
        const c1 = await getFormulaBarContent(page2);
        assertEqual(c1, 'Green', 'Peer 2 should receive C1 value');
      }, { retries: 5, initialDelay: 500 });

      await assertWithRetry(async () => {
        await clickCell(page2, 'D1');
        const d1 = await getFormulaBarContent(page2);
        assertEqual(d1, 'Red', 'Peer 2 should receive D1 value');
      }, { retries: 5, initialDelay: 500 });

      // Verify peer 2 received all STYLES
      await assertWithRetry(async () => {
        const p2StyleB1 = await getCellStyle(page2, 'B1');
        console.log('[Test] Peer 2 B1 style:', JSON.stringify(p2StyleB1, null, 2));
        assertEqual(p2StyleB1?.bold, true, 'Peer 2 B1 should be bold (late-join)');
      }, { retries: 10, initialDelay: 500 });

      await assertWithRetry(async () => {
        const p2StyleC1 = await getCellStyle(page2, 'C1');
        console.log('[Test] Peer 2 C1 style:', JSON.stringify(p2StyleC1, null, 2));
        assertEqual(p2StyleC1?.bgColor?.toUpperCase(), COLORS.GREEN_500.toUpperCase(), 'Peer 2 C1 should have green bg (late-join)');
      }, { retries: 10, initialDelay: 500 });

      await assertWithRetry(async () => {
        const p2StyleD1 = await getCellStyle(page2, 'D1');
        console.log('[Test] Peer 2 D1 style:', JSON.stringify(p2StyleD1, null, 2));
        assertEqual(p2StyleD1?.bgColor?.toUpperCase(), COLORS.RED_500.toUpperCase(), 'Peer 2 D1 should have red bg (late-join)');
      }, { retries: 10, initialDelay: 500 });
    }));

  } finally {
    // Cleanup
    if (page2) {
      await page2.close().catch(() => {});
    }
    if (ctx) {
      await ctx.close();
    }
  }

  // Print summary
  console.log('\n=== Style Late Join Test Summary ===');
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

// Run the style late join tests
runStyleLateJoinTests();
