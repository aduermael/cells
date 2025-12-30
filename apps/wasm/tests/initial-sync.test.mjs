// Initial sync test for Cells spreadsheet application
// Tests that when peer 1 has loaded a file and starts collaboration,
// peer 2 can join and receive the full document state.
//
// Run with:
//   HEADED=1 npm run test:initial-sync

import { setup, runTest } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  getFormulaBarContent,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
  sleep,
  waitForCollabReady,
  waitForPeerConnection,
  assertWithRetry,
  loadTestFile,
} from './helpers.mjs';

/**
 * Start collaboration by clicking the Copy Link button
 * This triggers the _onInitializeRequest callback and creates a room
 * @param {import('puppeteer').Page} page
 * @returns {Promise<string>} - The room URL
 */
async function startCollaborationAndGetUrl(page) {
  // Click the collaborate button to open the panel
  const collabBtn = await page.$('.collab-collaborate-btn');
  if (!collabBtn) {
    throw new Error('Collaborate button not found');
  }
  await collabBtn.click();
  await sleep(300);

  // Click "Copy Link" button to start collaboration
  const copyLinkBtn = await page.$('#collab-copy-link-btn');
  if (!copyLinkBtn) {
    throw new Error('Copy Link button not found');
  }
  await copyLinkBtn.click();

  // Wait for collaboration to initialize and room to be created
  await waitForCollabReady(page, 15000);

  // Wait for the share link to be populated (with retry)
  let roomUrl = null;
  for (let i = 0; i < 20; i++) {
    roomUrl = await page.evaluate(() => {
      const shareInput = document.querySelector('#collab-share-link');
      if (shareInput && shareInput.value && shareInput.value.includes('room=')) {
        return shareInput.value;
      }
      // Also try the URL directly
      const url = new URL(window.location.href);
      const roomId = url.searchParams.get('room');
      if (roomId) {
        return window.location.href;
      }
      return null;
    });
    if (roomUrl) break;
    await sleep(200);
  }

  if (!roomUrl) {
    throw new Error('Could not get room URL');
  }

  return roomUrl;
}

/**
 * Get oplog size from the sync adapter
 * @param {import('puppeteer').Page} page
 * @returns {Promise<number>}
 */
async function getOpLogSize(page) {
  return await page.evaluate(() => {
    if (window._syncAdapter?._client) {
      // Access the WASM client directly
      return window._syncAdapter._client.getOpLogSize?.() ?? -1;
    }
    return -1;
  });
}

async function runInitialSyncTests() {
  let ctx;
  let page2;
  const results = [];

  try {
    // Setup first browser context
    ctx = await setup();

    // Create second browser context
    const context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Test: Peer 2 receives full state when joining room where Peer 1 has loaded a file
    results.push(await runTest('New peer receives full document state from existing peer', async () => {
      // Step 1: Navigate to app first, then load a test file
      console.log('[Test] Peer 1: Navigating to app...');
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Step 2: Peer 1 loads a test file
      console.log('[Test] Peer 1: Loading test file...');
      await loadTestFile(ctx.page, 'unicode.zcd');
      await sleep(500);

      // Verify file loaded on peer 1
      await clickCell(ctx.page, 'A1');
      await sleep(200);
      const peer1A1Before = await getFormulaBarContent(ctx.page);
      console.log('[Test] Peer 1 A1 value:', peer1A1Before);
      assertTrue(peer1A1Before && peer1A1Before.length > 0, 'Peer 1 should have content in A1 after loading file');

      // Step 3: Peer 1 starts collaboration and gets room URL
      console.log('[Test] Peer 1: Starting collaboration...');
      const roomUrl = await startCollaborationAndGetUrl(ctx.page);
      console.log('[Test] Room URL:', roomUrl);

      // Wait for peer 1 to be fully ready
      await waitForCollabReady(ctx.page, 10000);

      // Check peer 1 oplog size
      const peer1OpLogSize = await getOpLogSize(ctx.page);
      console.log('[Test] Peer 1 oplog size:', peer1OpLogSize);
      assertTrue(peer1OpLogSize > 0, 'Peer 1 should have operations in oplog after starting collaboration');

      // Step 3: Peer 2 navigates to room URL (without loading any file first)
      console.log('[Test] Peer 2: Joining room via URL...');
      await page2.goto(roomUrl);
      await waitForAppReady(page2);

      // Wait for collaboration to be ready on peer 2
      const collabReady = await waitForCollabReady(page2, 15000);
      console.log('[Test] Peer 2 collab ready:', collabReady);

      // Wait for peer connection
      const peer2Connected = await waitForPeerConnection(page2, 15000);
      console.log('[Test] Peer 2 connected to peer:', peer2Connected);
      assertTrue(peer2Connected, 'Peer 2 should connect to Peer 1');

      // Also wait for peer 1 to see peer 2
      const peer1Connected = await waitForPeerConnection(ctx.page, 10000);
      console.log('[Test] Peer 1 connected to peer:', peer1Connected);

      // Step 4: Wait for sync to complete and verify peer 2 has the data
      console.log('[Test] Waiting for sync to complete...');
      await sleep(2000); // Give time for sync-response to be processed

      // Check peer 2 oplog size
      const peer2OpLogSize = await getOpLogSize(page2);
      console.log('[Test] Peer 2 oplog size:', peer2OpLogSize);

      // Verify peer 2 has the content
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const peer2A1 = await getFormulaBarContent(page2);
        console.log('[Test] Peer 2 A1 value:', peer2A1);
        assertEqual(peer2A1, peer1A1Before, 'Peer 2 should have same A1 value as Peer 1');
      }, { retries: 10, initialDelay: 500 });
    }));

    // Test 2: Verify the content is actually visible (rendered)
    results.push(await runTest('Synced content is visible on peer 2 canvas', async () => {
      // This test continues from the previous state
      // Check if peer 2 can see the display value (not just formula bar)

      const displayValue = await getCellDisplayValue(page2, 'A1');
      console.log('[Test] Peer 2 A1 display value:', displayValue);
      assertTrue(displayValue && displayValue.length > 0, 'Peer 2 should display value in A1');
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
  console.log('\n=== Initial Sync Test Summary ===');
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
runInitialSyncTests();
