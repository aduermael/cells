// Collab leave/rejoin and late-join content sync
// Verifies peers can come and go without breaking CRDT convergence.
//
// Run with:
//   bazel run :e2e -- collab-rejoin
//   HEADED=1 bazel run :e2e -- collab-rejoin

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

function generateRoomId() {
  const chars = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
  let id = '';
  for (let i = 0; i < 8; i++) {
    id += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return id;
}

async function joinRoom(page, baseUrl, roomId) {
  const url = `${baseUrl}/?room=${roomId}`;
  await page.goto(url);
  await waitForAppReady(page);
  const ready = await waitForCollabReady(page, 15000);
  if (!ready) {
    console.warn(`[joinRoom] Collab not ready for room ${roomId}, continuing anyway...`);
  }
}

async function runCollabRejoinTests() {
  let ctx;
  let page2;
  let context2;
  const results = [];

  console.log('\n=== Collab Leave/Rejoin Tests ===\n');

  try {
    ctx = await setup();
    context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // Late join: peer A has content before B joins; B must receive it
    results.push(await runTest('Late join receives existing cell content', async () => {
      const roomId = generateRoomId();
      console.log('[Test] Room:', roomId);

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await setCellValue(ctx.page, 'A1', 'BeforeJoin');
      await sleep(500);

      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'A1');
        await sleep(100);
        const v = await getFormulaBarContent(ctx.page);
        assertEqual(v, 'BeforeJoin', 'Peer 1 should have BeforeJoin');
      }, { retries: 5, initialDelay: 200 });

      await joinRoom(page2, ctx.baseUrl, roomId);
      const connected = await waitForPeerConnection(page2, 15000);
      assertTrue(connected, 'Peer 2 should connect to Peer 1');
      await waitForPeerConnection(ctx.page, 10000);
      await sleep(1500);

      await assertWithRetry(async () => {
        await clickCell(page2, 'A1');
        await sleep(200);
        const v = await getFormulaBarContent(page2);
        assertEqual(v, 'BeforeJoin', 'Late joiner should receive existing A1 content');
      }, { retries: 12, initialDelay: 500 });
    }));

    // Peer refresh must not force the other peer onto the first sheet.
    // No incoming network event should swap the active tab.
    results.push(await runTest('Peer refresh does not force other peer to first sheet', async () => {
      const roomId = generateRoomId();
      console.log('[Test] Room:', roomId);

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 15000);
      await waitForPeerConnection(page2, 15000);

      // Peer 1 adds a second sheet (synced via CRDT)
      await ctx.page.click('#add-sheet-btn');
      await sleep(500);

      await assertWithRetry(async () => {
        const count = await page2.evaluate(() =>
          document.querySelectorAll('.sheet-tab').length
        );
        assertTrue(count >= 2, `Peer 2 should receive second sheet, got ${count}`);
      }, { retries: 12, initialDelay: 400 });

      // Peer 2 switches to the second sheet
      await page2.evaluate(() => {
        const tabs = document.querySelectorAll('.sheet-tab');
        if (tabs[1]) tabs[1].click();
      });
      await sleep(400);

      await assertWithRetry(async () => {
        const active = await page2.evaluate(() => {
          const tabs = document.querySelectorAll('.sheet-tab');
          for (let i = 0; i < tabs.length; i++) {
            if (tabs[i].classList.contains('active')) return i;
          }
          return -1;
        });
        assertEqual(active, 1, 'Peer 2 should be on second sheet before refresh');
      }, { retries: 8, initialDelay: 300 });

      // Peer 1 "refreshes" — full page reload into the same room
      console.log('[Test] Peer 1 refreshing...');
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 15000);
      await waitForPeerConnection(page2, 15000);
      await sleep(2000);

      // Peer 2 must still be on the second sheet
      await assertWithRetry(async () => {
        const active = await page2.evaluate(() => {
          const tabs = document.querySelectorAll('.sheet-tab');
          for (let i = 0; i < tabs.length; i++) {
            if (tabs[i].classList.contains('active')) return i;
          }
          return -1;
        });
        assertEqual(active, 1, 'Peer 2 must stay on second sheet after peer 1 refreshes');
      }, { retries: 10, initialDelay: 400 });

      const tabs2 = await page2.evaluate(() =>
        document.querySelectorAll('.sheet-tab').length
      );
      assertTrue(tabs2 >= 2, 'Peer 2 should still have multiple sheets after refresh');
    }));

    // Leave + rejoin: B disconnects, A edits, B rejoins and converges
    results.push(await runTest('Leave and rejoin converges after offline edit', async () => {
      const roomId = generateRoomId();
      console.log('[Test] Room:', roomId);

      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 15000);
      await waitForPeerConnection(page2, 15000);

      await setCellValue(ctx.page, 'B1', 'Shared');
      await assertWithRetry(async () => {
        await clickCell(page2, 'B1');
        await sleep(200);
        const v = await getFormulaBarContent(page2);
        assertEqual(v, 'Shared', 'Both peers should share B1 before leave');
      }, { retries: 10, initialDelay: 400 });

      // Peer 2 leaves (navigate away from room)
      console.log('[Test] Peer 2 leaving room...');
      await page2.goto(ctx.baseUrl);
      await waitForAppReady(page2);
      await sleep(1000);

      // Peer 1 edits while peer 2 is gone
      await setCellValue(ctx.page, 'B1', 'AfterLeave');
      await sleep(300);
      await setCellValue(ctx.page, 'C1', 'NewWhileGone');
      await sleep(500);

      // Peer 2 rejoins same room
      console.log('[Test] Peer 2 rejoining room...');
      await joinRoom(page2, ctx.baseUrl, roomId);
      const reconnected = await waitForPeerConnection(page2, 15000);
      assertTrue(reconnected, 'Peer 2 should reconnect after rejoin');
      await waitForPeerConnection(ctx.page, 10000);
      await sleep(2000);

      await assertWithRetry(async () => {
        await clickCell(page2, 'B1');
        await sleep(200);
        const b1 = await getFormulaBarContent(page2);
        assertEqual(b1, 'AfterLeave', 'Rejoined peer should see edit made while offline');
      }, { retries: 12, initialDelay: 500 });

      await assertWithRetry(async () => {
        await clickCell(page2, 'C1');
        await sleep(200);
        const c1 = await getFormulaBarContent(page2);
        assertEqual(c1, 'NewWhileGone', 'Rejoined peer should see new cell from offline period');
      }, { retries: 12, initialDelay: 500 });

      // Bidirectional: rejoin peer edits, host should see it
      await setCellValue(page2, 'D1', 'FromRejoin');
      await assertWithRetry(async () => {
        await clickCell(ctx.page, 'D1');
        await sleep(200);
        const d1 = await getFormulaBarContent(ctx.page);
        assertEqual(d1, 'FromRejoin', 'Host should receive edit from rejoined peer');
      }, { retries: 12, initialDelay: 500 });
    }));

  } finally {
    if (page2) {
      await page2.close().catch(() => {});
    }
    if (context2) {
      await context2.close().catch(() => {});
    }
    if (ctx) {
      await ctx.close();
    }
  }

  console.log('\n=== Collab Leave/Rejoin Summary ===');
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

runCollabRejoinTests();
