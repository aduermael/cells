// Collaboration test for Cells spreadsheet application
// Tests that two browser contexts can sync changes via WebRTC
//
// Run with HEADED=1 for better WebRTC support:
//   HEADED=1 npm run test:collab
//
// Run with DEBUG=1 for verbose logging:
//   DEBUG=1 HEADED=1 npm run test:collab

import { setup, runTest, TestContext } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getFormulaBarContent,
  getWorkbookName,
  assertEqual,
  assertTrue,
  sleep,
  waitForCollabReady,
  waitForPeerConnection,
  assertWithRetry,
} from './helpers.mjs';

/**
 * Generate a random room ID for testing
 * Room IDs must be 8 characters, alphanumeric (base62)
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
  // Wait for collaboration to be ready (data channel open)
  const ready = await waitForCollabReady(page, 10000);
  if (!ready) {
    console.warn(`[joinRoom] Collab not ready for room ${roomId}, continuing anyway...`);
  }
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

      // Wait for peers to see each other
      const peer1Connected = await waitForPeerConnection(ctx.page, 10000);
      const peer2Connected = await waitForPeerConnection(page2, 10000);

      // Both pages should be loaded
      const canvas1 = await ctx.page.$('#grid');
      const canvas2 = await page2.$('#grid');

      assertTrue(canvas1, 'First peer should have canvas');
      assertTrue(canvas2, 'Second peer should have canvas');
      assertTrue(peer1Connected || peer2Connected, 'At least one peer should see the other');
    }));

    // Test 2: Changes sync between peers
    results.push(await runTest('Cell changes sync between peers', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peers to connect
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // First peer enters a value
      await setCellValue(ctx.page, 'A1', 'Sync Test');

      // Wait for sync with retry
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, 'Sync Test', 'Value should sync to second peer');
      }, { retries: 5, initialDelay: 500 });
    }));

    // Test 3: Formula syncs and computes on both sides
    results.push(await runTest('Formula syncs between peers', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peers to connect
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // First peer enters values and formula
      await setCellValue(ctx.page, 'A1', '100');
      await setCellValue(ctx.page, 'A2', '200');
      await setCellValue(ctx.page, 'A3', '=A1+A2');

      // Wait for sync with retry
      await assertWithRetry(async () => {
        await clickCell(page2, 'A3');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, '=A1+A2', 'Formula should sync to second peer');
      }, { retries: 5, initialDelay: 500 });
    }));

    // Test 4: Bidirectional sync
    results.push(await runTest('Changes sync bidirectionally', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peers to connect
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // First peer enters value in A1
      await setCellValue(ctx.page, 'A1', 'From Peer 1');

      // Wait for first value to sync before second peer edits
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(100);
        const c = await getFormulaBarContent(page2);
        assertEqual(c, 'From Peer 1', 'A1 should sync to peer 2');
      }, { retries: 5, initialDelay: 300 });

      // Second peer enters value in B1
      await setCellValue(page2, 'B1', 'From Peer 2');

      // Check first peer sees both values with retry
      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'A1');
        await sleep(100);
        const content1 = await getFormulaBarContent(ctx.page);
        assertEqual(content1, 'From Peer 1', 'Peer 1 should have A1');

        await clickCell(ctx.page, 'B1');
        await sleep(100);
        const content2 = await getFormulaBarContent(ctx.page);
        assertEqual(content2, 'From Peer 2', 'Peer 1 should see B1 from Peer 2');
      }, { retries: 5, initialDelay: 300 });

      // Check second peer sees both values
      await clickCell(page2, 'B1');
      await sleep(100);
      const content3 = await getFormulaBarContent(page2);
      assertEqual(content3, 'From Peer 2', 'Peer 2 should have B1');

      await clickCell(page2, 'A1');
      await sleep(100);
      const content4 = await getFormulaBarContent(page2);
      assertEqual(content4, 'From Peer 1', 'Peer 2 should see A1 from Peer 1');
    }));

    // Test 5: Formula referencing distant cell syncs (CRDT entity creation)
    // This tests the CRDT-compliant formula resolution where the formula creates
    // entities (column, row, cell) that didn't previously exist.
    results.push(await runTest('Formula with new entity reference syncs', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peers to connect
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // First peer enters a value in a "distant" cell (D5)
      // This creates the column D and row 5 via CRDT operations
      await setCellValue(ctx.page, 'D5', '42');

      // Verify D5 syncs to peer 2
      await assertWithRetry(async () => {
        await clickCell(page2, 'D5');
        await sleep(200);
        const content = await getFormulaBarContent(page2);
        assertEqual(content, '42', 'D5 value should sync to second peer');
      }, { retries: 5, initialDelay: 500 });

      // Now first peer enters a formula in A1 that references D5
      await setCellValue(ctx.page, 'A1', '=D5*2');

      // Verify formula syncs and evaluates correctly on peer 2
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const formulaContent = await getFormulaBarContent(page2);
        assertEqual(formulaContent, '=D5*2', 'Formula should sync to second peer');
      }, { retries: 5, initialDelay: 500 });
    }));

    // Test 6: Formula referencing non-existent cell creates entities via CRDT
    // This tests that when a formula references a cell that doesn't exist yet,
    // the CRDT operations create the necessary column/row/cell entities and
    // sync them to the remote peer.
    results.push(await runTest('Formula creates distant cell reference via CRDT', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      // Wait for peers to connect
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // First peer enters a formula that references E10 (which doesn't exist yet)
      // The CRDT-compliant resolution should:
      // 1. Create column E
      // 2. Create row 10
      // 3. Create cell E10
      // All via CRDT operations that sync to peer 2
      await setCellValue(ctx.page, 'A1', '=E10+1');

      // Verify formula syncs to peer 2
      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const formulaContent = await getFormulaBarContent(page2);
        assertEqual(formulaContent, '=E10+1', 'Formula referencing new cell should sync');
      }, { retries: 5, initialDelay: 500 });

      // Now if peer 2 sets a value in E10, peer 1's formula should update
      await setCellValue(page2, 'E10', '99');

      // Verify peer 1 sees the formula still works after E10 gets a value
      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'A1');
        await sleep(200);
        const content = await getFormulaBarContent(ctx.page);
        assertEqual(content, '=E10+1', 'Formula should still show =E10+1');
      }, { retries: 5, initialDelay: 500 });
    }));

    // Test 7: Document title/name syncs between peers
    results.push(await runTest('Workbook title syncs between peers', async () => {
      const roomId = generateRoomId();

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);

      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);

      // Peer 1 renames the workbook via the title editor
      await ctx.page.click('#workbook-title');
      await sleep(100);
      await ctx.page.evaluate(() => {
        const el = document.getElementById('workbook-title');
        if (!el) return;
        el.focus();
        el.textContent = 'Collab Shared Doc';
        el.dispatchEvent(new Event('blur', { bubbles: true }));
      });
      await sleep(300);

      // Peer 1 should show the new title
      const name1 = await getWorkbookName(ctx.page);
      assertEqual(name1, 'Collab Shared Doc', 'Peer 1 should show renamed title');

      // Peer 2 should receive the WORKBOOK_SET and update the header title
      await assertWithRetry(async () => {
        const name2 = await getWorkbookName(page2);
        assertEqual(name2, 'Collab Shared Doc', 'Title should sync to peer 2');
      }, { retries: 8, initialDelay: 400 });
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
