// Style Before Collaboration Test
// Tests that styles applied BEFORE enabling collaboration are synced to late-joining peers.
// This tests the bootstrapOpLog() path which generates CRDT operations from existing workbook state.
//
// Run with HEADED=1 for visible browser:
//   HEADED=1 bazel run :e2e -- style-before-collab
//
// Run standalone:
//   bazel run :e2e -- style-before-collab

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
      const syncState = await ctx.app.dataSource._client.getSyncState();
      return {
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
 * Enable collaboration and join a room WITHOUT reloading the page
 * This simulates clicking the "Collaborate" button
 */
async function enableCollaborationInPlace(page, roomId) {
  // Initialize collaboration and join room via JavaScript, without page reload
  const result = await page.evaluate(async (roomId) => {
    const ctx = window._appContext;
    const app = ctx?.app;

    // Need to initialize collaboration first if not already done
    if (!app?.collaborationInitialized) {
      // Trigger collaboration initialization via the collabUI button callback
      if (app?.collabUI?._onInitializeRequest) {
        await app.collabUI._onInitializeRequest();
      } else {
        return { error: 'No collab UI initializer' };
      }
    }

    // Now join the room
    if (!app?.roomManager) {
      return { error: 'No room manager after initialization' };
    }

    try {
      await app.roomManager.joinRoom(roomId);
      return { success: true };
    } catch (e) {
      return { error: e.message };
    }
  }, roomId);

  if (result.error) {
    console.error('[enableCollaborationInPlace] Error:', result.error);
  }

  // Wait for collaboration to be ready
  const ready = await waitForCollabReady(page, 10000);
  if (!ready) {
    console.warn(`[enableCollaborationInPlace] Collab not ready for room ${roomId}, continuing anyway...`);
  }
}

/**
 * Join an existing collaboration room by navigating to URL
 */
async function joinRoomByUrl(page, baseUrl, roomId) {
  const url = `${baseUrl}/?room=${roomId}`;
  await page.goto(url);
  await waitForAppReady(page);
  const ready = await waitForCollabReady(page, 10000);
  if (!ready) {
    console.warn(`[joinRoomByUrl] Collab not ready for room ${roomId}, continuing anyway...`);
  }
}

const COLORS = {
  GREEN_500: '#10B981',
  BLUE_500: '#3B82F6',
};

async function runStyleBeforeCollabTests() {
  let ctx;
  let page2;
  let context2;
  const results = [];

  console.log('\n=== Style Before Collaboration Tests ===\n');
  console.log('Testing: Styles applied before clicking "Collaborate" should sync to late-joining peers\n');

  try {
    // Setup first browser context
    ctx = await setup();

    // Create second browser context
    context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Test 1: Style applied to empty cell before collaboration
    results.push(await runTest('Empty styled cell syncs when collaboration is enabled after styling', async () => {
      const roomId = generateRoomId();
      console.log(`[Test] Room ID: ${roomId}`);

      // Step 1: Peer 1 opens app WITHOUT collaboration (no room URL)
      console.log('[Test] Peer 1 opening app without collaboration...');
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Step 2: Peer 1 applies style to B2 (empty cell) - BEFORE collaboration
      console.log('[Test] Peer 1 styling B2 (empty) with green background BEFORE collaboration...');
      await clickCell(ctx.page, 'B2');
      await applyBackgroundColor(ctx.page, COLORS.GREEN_500);
      await sleep(300);

      // Verify peer 1 has the style locally
      const peer1StyleBefore = await getCellStyle(ctx.page, 'B2');
      console.log('[Test] Peer 1 B2 style before collab:', JSON.stringify(peer1StyleBefore, null, 2));
      assertEqual(peer1StyleBefore?.bgColor?.toUpperCase(), COLORS.GREEN_500.toUpperCase(), 'Peer 1 B2 should have green bg locally');

      // Step 3: Peer 1 adds value to C2 - BEFORE collaboration
      console.log('[Test] Peer 1 setting C2 value "FOO" BEFORE collaboration...');
      await setCellValue(ctx.page, 'C2', 'FOO');
      await sleep(200);

      // Step 4: NOW enable collaboration IN PLACE (without reloading page)
      console.log('[Test] Peer 1 enabling collaboration IN PLACE (simulating Collaborate button)...');
      await enableCollaborationInPlace(ctx.page, roomId);

      // Debug: Check peer 1 oplog after enabling collaboration
      const peer1DebugAfterCollab = await getSyncDebugInfo(ctx.page);
      console.log('[Test] Peer 1 sync debug after enabling collab:', JSON.stringify(peer1DebugAfterCollab, null, 2));

      // Verify peer 1 still has the style after enabling collaboration
      const peer1StyleAfterCollab = await getCellStyle(ctx.page, 'B2');
      console.log('[Test] Peer 1 B2 style after collab:', JSON.stringify(peer1StyleAfterCollab, null, 2));
      assertEqual(peer1StyleAfterCollab?.bgColor?.toUpperCase(), COLORS.GREEN_500.toUpperCase(), 'Peer 1 B2 should still have green bg after enabling collab');

      // Step 5: Peer 2 joins the room (by navigating to room URL)
      console.log('[Test] Peer 2 joining room via URL...');
      await joinRoomByUrl(page2, ctx.baseUrl, roomId);

      // Wait for peer connection
      const peer1Connected = await waitForPeerConnection(ctx.page, 10000);
      const peer2Connected = await waitForPeerConnection(page2, 10000);
      console.log(`[Test] Peer 1 connected: ${peer1Connected}, Peer 2 connected: ${peer2Connected}`);
      assertTrue(peer2Connected, 'Peer 2 should connect to Peer 1');

      // Give time for initial sync
      await sleep(2000);

      // Debug: Check peer 2 oplog after sync
      const peer2DebugAfterSync = await getSyncDebugInfo(page2);
      console.log('[Test] Peer 2 sync debug after sync:', JSON.stringify(peer2DebugAfterSync, null, 2));

      // Step 6: Verify peer 2 received the value
      await assertWithRetry(async () => {
        await clickCell(page2, 'C2');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, 'FOO', 'Peer 2 should receive C2 value');
      }, { retries: 5, initialDelay: 500 });

      // Step 7: Verify peer 2 received the STYLE on the empty cell
      await assertWithRetry(async () => {
        const peer2Style = await getCellStyle(page2, 'B2');
        console.log('[Test] Peer 2 B2 style:', JSON.stringify(peer2Style, null, 2));
        assertTrue(peer2Style !== null, 'Peer 2 should have style info for B2');
        assertEqual(
          peer2Style?.bgColor?.toUpperCase(),
          COLORS.GREEN_500.toUpperCase(),
          'Peer 2 B2 should have green background (bootstrapped from pre-collab styling)'
        );
      }, { retries: 10, initialDelay: 500 });
    }));

    // Test 2: Multiple styled cells before collaboration
    results.push(await runTest('Multiple styled cells sync when collaboration is enabled after styling', async () => {
      const roomId = generateRoomId();
      console.log(`[Test] Room ID: ${roomId}`);

      // Peer 1 opens app without collaboration
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Peer 1 styles multiple cells BEFORE collaboration
      await setCellValue(ctx.page, 'A1', 'Title');
      await clickCell(ctx.page, 'A1');
      await applyBackgroundColor(ctx.page, COLORS.BLUE_500);
      await sleep(200);

      await clickCell(ctx.page, 'B1');  // Empty cell
      await applyBackgroundColor(ctx.page, COLORS.GREEN_500);
      await sleep(200);

      await setCellValue(ctx.page, 'C1', 'Data');
      await clickCell(ctx.page, 'C1');
      await applyBackgroundColor(ctx.page, COLORS.GREEN_500);
      await sleep(200);

      // Enable collaboration IN PLACE (without reloading page)
      await enableCollaborationInPlace(ctx.page, roomId);

      // Peer 2 joins via URL
      await joinRoomByUrl(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);
      await sleep(2000);

      // Verify peer 2 received all styles
      await assertWithRetry(async () => {
        const p2A1 = await getCellStyle(page2, 'A1');
        console.log('[Test] Peer 2 A1 style:', JSON.stringify(p2A1, null, 2));
        assertEqual(p2A1?.bgColor?.toUpperCase(), COLORS.BLUE_500.toUpperCase(), 'Peer 2 A1 should have blue bg');
      }, { retries: 10, initialDelay: 500 });

      await assertWithRetry(async () => {
        const p2B1 = await getCellStyle(page2, 'B1');
        console.log('[Test] Peer 2 B1 style:', JSON.stringify(p2B1, null, 2));
        assertEqual(p2B1?.bgColor?.toUpperCase(), COLORS.GREEN_500.toUpperCase(), 'Peer 2 B1 (empty) should have green bg');
      }, { retries: 10, initialDelay: 500 });

      await assertWithRetry(async () => {
        const p2C1 = await getCellStyle(page2, 'C1');
        console.log('[Test] Peer 2 C1 style:', JSON.stringify(p2C1, null, 2));
        assertEqual(p2C1?.bgColor?.toUpperCase(), COLORS.GREEN_500.toUpperCase(), 'Peer 2 C1 should have green bg');
      }, { retries: 10, initialDelay: 500 });

      // Verify values too
      await clickCell(page2, 'A1');
      const a1Val = await getFormulaBarContent(page2);
      assertEqual(a1Val, 'Title', 'Peer 2 A1 value');

      await clickCell(page2, 'C1');
      const c1Val = await getFormulaBarContent(page2);
      assertEqual(c1Val, 'Data', 'Peer 2 C1 value');
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
  console.log('\n=== Style Before Collaboration Test Summary ===');
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
runStyleBeforeCollabTests();
