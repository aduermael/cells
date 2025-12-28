// Collaboration test for Cells spreadsheet application
// Tests that two browser contexts can sync changes via WebRTC
//
// NOTE: These tests are experimental and may not pass in headless Chrome.
// WebRTC peer connections between browser contexts in the same Chrome instance
// have limitations. For reliable collaboration testing, consider:
// - Using separate browser processes (not contexts)
// - Running with headed Chrome (headless: false)
// - Using a real network loopback setup

import { setup, runTest, TestContext } from './harness.mjs';
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
 * Generate a random room ID for testing
 */
function generateRoomId() {
  return 'test-' + Math.random().toString(36).substring(2, 10);
}

/**
 * Navigate to a specific collaboration room
 */
async function joinRoom(page, baseUrl, roomId) {
  const url = `${baseUrl}/?room=${roomId}`;
  await page.goto(url);
  await waitForAppReady(page);
  // Give extra time for WebRTC connection setup
  await sleep(2000);
}

/**
 * Check if collaboration UI shows connected peers
 */
async function getCollabStatus(page) {
  return await page.evaluate(() => {
    const container = document.getElementById('collab-ui-container');
    if (!container) return null;
    return container.textContent || container.innerText;
  });
}

/**
 * Wait for peer connection (checks for peer indicator in UI)
 */
async function waitForPeerConnection(page, timeout = 10000) {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    const status = await getCollabStatus(page);
    // Look for indicators that we're connected to at least one peer
    if (status && (status.includes('2') || status.includes('peer'))) {
      return true;
    }
    await sleep(500);
  }
  return false;
}

async function runCollabTests() {
  let ctx;
  let page2;
  const results = [];

  try {
    // Setup first browser context
    ctx = await setup();

    // Create second page in the same browser
    const context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Test 1: Two peers can join the same room
    results.push(await runTest('Two peers can join same room', async () => {
      const roomId = generateRoomId();

      // First peer joins
      await joinRoom(ctx.page, ctx.baseUrl, roomId);

      // Second peer joins
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for connection
      await sleep(2000);

      // Both pages should be loaded
      const canvas1 = await ctx.page.$('#grid');
      const canvas2 = await page2.$('#grid');

      assertTrue(canvas1, 'First peer should have canvas');
      assertTrue(canvas2, 'Second peer should have canvas');
    }));

    // Test 2: Changes sync between peers
    // NOTE: This test may fail in headless Chrome due to WebRTC limitations
    results.push(await runTest('Cell changes sync between peers', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for WebRTC connection and data channel establishment
      await sleep(5000);

      // First peer enters a value
      await setCellValue(ctx.page, 'A1', 'Sync Test');

      // Wait for sync (CRDT operation broadcast and apply)
      await sleep(3000);

      // Second peer checks the value
      await clickCell(page2, 'A1');
      await sleep(500);

      const content = await getFormulaBarContent(page2);
      assertEqual(content, 'Sync Test', 'Value should sync to second peer');
    }));

    // Test 3: Formula syncs and computes on both sides
    results.push(await runTest('Formula syncs between peers', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for WebRTC connection and data channel establishment
      await sleep(5000);

      // First peer enters values and formula
      await setCellValue(ctx.page, 'A1', '100');
      await setCellValue(ctx.page, 'A2', '200');
      await setCellValue(ctx.page, 'A3', '=A1+A2');

      // Wait for sync (CRDT operation broadcast and apply)
      await sleep(3000);

      // Second peer checks the formula
      await clickCell(page2, 'A3');
      await sleep(500);

      const content = await getFormulaBarContent(page2);
      assertEqual(content, '=A1+A2', 'Formula should sync to second peer');
    }));

    // Test 4: Bidirectional sync
    results.push(await runTest('Changes sync bidirectionally', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for WebRTC connection and data channel establishment
      await sleep(5000);

      // First peer enters value in A1
      await setCellValue(ctx.page, 'A1', 'From Peer 1');
      await sleep(3000);

      // Second peer enters value in B1
      await setCellValue(page2, 'B1', 'From Peer 2');
      await sleep(3000);

      // Check first peer sees both values
      await clickCell(ctx.page, 'A1');
      await sleep(200);
      let content1 = await getFormulaBarContent(ctx.page);
      assertEqual(content1, 'From Peer 1', 'Peer 1 should have A1');

      await clickCell(ctx.page, 'B1');
      await sleep(200);
      content1 = await getFormulaBarContent(ctx.page);
      assertEqual(content1, 'From Peer 2', 'Peer 1 should see B1 from Peer 2');

      // Check second peer sees both values
      await clickCell(page2, 'B1');
      await sleep(200);
      let content2 = await getFormulaBarContent(page2);
      assertEqual(content2, 'From Peer 2', 'Peer 2 should have B1');

      await clickCell(page2, 'A1');
      await sleep(200);
      content2 = await getFormulaBarContent(page2);
      assertEqual(content2, 'From Peer 1', 'Peer 2 should see A1 from Peer 1');
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
  console.log('\n=== Collaboration Test Summary ===');
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

// Run collaboration tests
runCollabTests();
