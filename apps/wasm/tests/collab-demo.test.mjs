// Collaborative Demo Test - "Building the Master Plan"
//
// Story: Nico, Robert, and Shuying are building a master plan to release
// an AI-native collaborative Excel competitor called "Cells".
//
// This demo showcases real-time collaboration with three participants
// simultaneously editing a spreadsheet - ALL IN ONE SESSION.
//
// Run with HEADED=1 to watch the demo:
//   HEADED=1 npm run test:collab-demo
//
// Run with DEBUG=1 for verbose logging:
//   DEBUG=1 HEADED=1 npm run test:collab-demo
//
// Run with SLOWMO for slower animations:
//   HEADED=1 SLOWMO=100 npm run test:collab-demo

import { setup, runTest, CONFIG } from './harness.mjs';
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
 * Set the display name for a participant after page load
 */
async function setDisplayName(page, name) {
  await page.evaluate(async (displayName) => {
    // Set in sessionStorage for future loads
    try {
      sessionStorage.setItem('cells.displayName', displayName);
    } catch (e) {
      // Ignore if sessionStorage is not available
    }
    // Also set via the sync adapter if available
    if (window._syncAdapter?.setLocalName) {
      await window._syncAdapter.setLocalName(displayName);
    }
  }, name);
}

/**
 * Navigate to a specific collaboration room
 */
async function joinRoom(page, baseUrl, roomId, participantName) {
  const url = `${baseUrl}/?room=${roomId}`;
  console.log(`  [${participantName}] Joining room...`);
  await page.goto(url);
  await waitForAppReady(page);

  // Set the display name after page load
  await setDisplayName(page, participantName);

  const ready = await waitForCollabReady(page, 15000);
  if (ready) {
    console.log(`  [${participantName}] Connected!`);
  } else {
    console.warn(`  [${participantName}] Connection may not be fully ready, continuing...`);
  }
}

/**
 * Wait for all peers to be connected
 */
async function waitForAllPeers(pages, expectedPeerCount, timeout = 20000) {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    let allConnected = true;
    for (const page of pages) {
      const count = await page.evaluate(() => {
        return window._syncAdapter?.getConnectedPeerCount?.() ?? 0;
      });
      if (count < expectedPeerCount) {
        allConnected = false;
        break;
      }
    }
    if (allConnected) return true;
    await sleep(500);
  }
  return false;
}

/**
 * Resize a column by simulating a drag on the column border
 * @param {Page} page - Puppeteer page
 * @param {string} colLetter - Column letter (A, B, C, etc.)
 * @param {number} newWidth - New width in pixels
 */
async function resizeColumn(page, colLetter, newWidth) {
  const colIndex = colLetter.toUpperCase().charCodeAt(0) - 65;

  // Get canvas position and current column right edge
  const info = await page.evaluate(({ colIndex }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;

    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();

    // Calculate current column right edge
    let rightEdge = HEADER_WIDTH;
    for (let i = 0; i <= colIndex; i++) {
      // Try to get actual width from grid event handler state
      const w = window._appContext?.eventManager?.colWidths?.get(i) || DEFAULT_COL_WIDTH;
      rightEdge += w;
    }

    // Get current width
    const currentWidth = window._appContext?.eventManager?.colWidths?.get(colIndex) || DEFAULT_COL_WIDTH;

    return {
      canvasLeft: rect.left,
      canvasTop: rect.top,
      rightEdge,
      currentWidth,
      headerHeight: HEADER_HEIGHT
    };
  }, { colIndex });

  // Position to start drag: right edge of column, in header area
  const startX = info.canvasLeft + info.rightEdge;
  const startY = info.canvasTop + info.headerHeight / 2;

  // Calculate how much to drag
  const dragDelta = newWidth - info.currentWidth;
  const endX = startX + dragDelta;

  // Simulate the drag
  await page.mouse.move(startX, startY);
  await page.mouse.down();
  await page.mouse.move(endX, startY, { steps: 10 });
  await page.mouse.up();

  await sleep(300);
}

/**
 * Drag a column to a new position
 * @param {Page} page - Puppeteer page
 * @param {string} sourceCol - Source column letter (e.g., "B")
 * @param {string} targetCol - Target column letter to drop before (e.g., "A")
 */
async function dragColumn(page, sourceCol, targetCol) {
  const sourceColIndex = sourceCol.toUpperCase().charCodeAt(0) - 65;
  const targetColIndex = targetCol.toUpperCase().charCodeAt(0) - 65;

  // Query actual column positions from the engine
  const positions = await page.evaluate(({ sourceIdx, targetIdx }) => {
    const HEADER_WIDTH = 50;
    const HEADER_HEIGHT = 24;
    const DEFAULT_COL_WIDTH = 100;

    const canvas = document.getElementById('grid');
    const rect = canvas.getBoundingClientRect();

    // Try to get actual column widths from viewport data
    let sourceX = HEADER_WIDTH;
    let targetX = HEADER_WIDTH;

    // Calculate positions based on viewport column data if available
    const viewportData = window._appContext?.app?.lastViewportData;
    if (viewportData?.columns) {
      // Sum up widths to get positions
      for (let i = 0; i < Math.max(sourceIdx, targetIdx) + 1; i++) {
        const colWidth = viewportData.columns[i]?.width || DEFAULT_COL_WIDTH;
        if (i < sourceIdx) sourceX += colWidth;
        if (i === sourceIdx) sourceX += colWidth / 2;
        if (i < targetIdx) targetX += colWidth;
        if (i === targetIdx) targetX += colWidth / 2;
      }
    } else {
      // Fallback to default widths
      sourceX += sourceIdx * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
      targetX += targetIdx * DEFAULT_COL_WIDTH + DEFAULT_COL_WIDTH / 2;
    }

    return {
      left: rect.left,
      top: rect.top,
      sourceX,
      targetX,
      headerHeight: HEADER_HEIGHT
    };
  }, { sourceIdx: sourceColIndex, targetIdx: targetColIndex });

  const sourceX = positions.left + positions.sourceX;
  const targetX = positions.left + positions.targetX;
  const y = positions.top + positions.headerHeight / 2;

  await page.mouse.move(sourceX, y);
  await page.mouse.down();
  await page.mouse.move(sourceX + 10, y, { steps: 5 });
  await page.mouse.move(targetX, y, { steps: 10 });
  await page.mouse.up();
  await sleep(400);
}

/**
 * Verify a value synced to a peer
 */
async function verifyCellSynced(page, cellRef, expectedValue, peerName) {
  await assertWithRetry(async () => {
    await clickCell(page, cellRef);
    await sleep(200);
    const content = await getFormulaBarContent(page);
    assertEqual(content, expectedValue, `${cellRef} should sync to ${peerName}`);
  }, { retries: 5, initialDelay: 500 });
}

async function runCollabDemo() {
  let ctx;
  let nicoPage, robertPage, shuyingPage;
  let context2, context3;
  const results = [];

  console.log('\n====================================================');
  console.log('       CELLS - Collaborative Demo');
  console.log('       "Building the Master Plan"');
  console.log('====================================================\n');
  console.log('Story: Nico, Robert, and Shuying are building a master');
  console.log('plan to release an AI-native collaborative spreadsheet.\n');

  // Generate ONE room ID for the entire demo
  const ROOM_ID = generateRoomId();
  console.log(`Room ID: ${ROOM_ID}\n`);

  try {
    // Setup first browser context (Nico)
    ctx = await setup();
    nicoPage = ctx.page;

    // Create second and third browser contexts (Robert and Shuying)
    context2 = await ctx.browser.createBrowserContext();
    robertPage = await context2.newPage();

    context3 = await ctx.browser.createBrowserContext();
    shuyingPage = await context3.newPage();

    // === ACT 1: THE TEAM ASSEMBLES ===
    results.push(await runTest('Act 1: The Team Assembles', async () => {
      console.log('\n  The team joins the collaboration room...\n');

      // All three participants join the SAME room
      await joinRoom(nicoPage, ctx.baseUrl, ROOM_ID, 'Nico');
      await joinRoom(robertPage, ctx.baseUrl, ROOM_ID, 'Robert');
      await joinRoom(shuyingPage, ctx.baseUrl, ROOM_ID, 'Shuying');

      // Wait for everyone to see each other
      console.log('\n  Waiting for peer connections...');
      await waitForAllPeers([nicoPage, robertPage, shuyingPage], 2, 25000);

      // Verify all canvases are loaded
      const canvas1 = await nicoPage.$('#grid');
      const canvas2 = await robertPage.$('#grid');
      const canvas3 = await shuyingPage.$('#grid');

      assertTrue(canvas1, 'Nico should have canvas');
      assertTrue(canvas2, 'Robert should have canvas');
      assertTrue(canvas3, 'Shuying should have canvas');

      console.log('  All team members connected!\n');
    }));

    // === ACT 2: NICO CREATES THE STRUCTURE ===
    results.push(await runTest('Act 2: Nico Creates the Structure', async () => {
      console.log('\n  [Nico] Creating the spreadsheet structure...\n');

      // Nico creates the title
      await setCellValue(nicoPage, 'A1', 'CELLS - Master Plan 2025');
      await sleep(300);

      // Nico creates the header row
      await setCellValue(nicoPage, 'A3', 'Feature');
      await setCellValue(nicoPage, 'B3', 'Owner');
      await setCellValue(nicoPage, 'C3', 'Days');
      await setCellValue(nicoPage, 'D3', 'Cost');
      await setCellValue(nicoPage, 'E3', 'Status');
      await sleep(300);

      // Nico adds the feature list
      console.log('  [Nico] Adding feature list...\n');
      await setCellValue(nicoPage, 'A4', 'Real-time Collaboration');
      await setCellValue(nicoPage, 'A5', 'Formula Engine');
      await setCellValue(nicoPage, 'A6', 'XLSX Import/Export');
      await setCellValue(nicoPage, 'A7', 'AI Formula Assistant');
      await setCellValue(nicoPage, 'A8', 'Mobile Apps');
      await sleep(500);

      // Verify the title synced to Robert and Shuying
      await verifyCellSynced(robertPage, 'A1', 'CELLS - Master Plan 2025', 'Robert');
      await verifyCellSynced(shuyingPage, 'A1', 'CELLS - Master Plan 2025', 'Shuying');

      console.log('  [Robert] I can see the structure! Great start Nico.');
      console.log('  [Shuying] Looking good! Let me see the features...\n');
    }));

    // === ACT 3: ROBERT ASSIGNS OWNERS ===
    results.push(await runTest('Act 3: Robert Assigns Owners', async () => {
      console.log('\n  [Robert] Assigning owners to features...\n');

      await setCellValue(robertPage, 'B4', 'Nico');
      await setCellValue(robertPage, 'B5', 'Shuying');
      await setCellValue(robertPage, 'B6', 'Robert');
      await setCellValue(robertPage, 'B7', 'Shuying');
      await setCellValue(robertPage, 'B8', 'Nico');
      await sleep(500);

      // Verify synced
      await verifyCellSynced(nicoPage, 'B4', 'Nico', 'Nico');
      await verifyCellSynced(shuyingPage, 'B5', 'Shuying', 'Shuying');

      console.log('  [Nico] I see my name on Collaboration - perfect!');
      console.log('  [Shuying] Formula Engine is mine!\n');
    }));

    // === ACT 4: SHUYING ADDS ESTIMATES ===
    results.push(await runTest('Act 4: Shuying Adds Estimates', async () => {
      console.log('\n  [Shuying] Adding effort estimates (in days)...\n');

      await setCellValue(shuyingPage, 'C4', '30');
      await setCellValue(shuyingPage, 'C5', '45');
      await setCellValue(shuyingPage, 'C6', '20');
      await setCellValue(shuyingPage, 'C7', '60');
      await setCellValue(shuyingPage, 'C8', '90');
      await sleep(500);

      // Verify synced
      await verifyCellSynced(nicoPage, 'C4', '30', 'Nico');
      await verifyCellSynced(robertPage, 'C5', '45', 'Robert');

      console.log('  [Nico] 30 days for Collaboration - challenge accepted!');
      console.log('  [Robert] 20 days for XLSX - I can do that.\n');
    }));

    // === ACT 5: EVERYONE ADDS FORMULAS ===
    results.push(await runTest('Act 5: The Formulas Come Alive', async () => {
      console.log('\n  [Shuying] Adding cost calculations...\n');

      // Shuying adds cost rate in F1
      await setCellValue(shuyingPage, 'F1', 'Rate/day:');
      await setCellValue(shuyingPage, 'G1', '500');
      await sleep(300);

      // Shuying adds cost formulas
      await setCellValue(shuyingPage, 'D4', '=C4*$G$1');
      await sleep(200);
      await setCellValue(shuyingPage, 'D5', '=C5*$G$1');
      await sleep(200);
      await setCellValue(shuyingPage, 'D6', '=C6*$G$1');
      await sleep(200);
      await setCellValue(shuyingPage, 'D7', '=C7*$G$1');
      await sleep(200);
      await setCellValue(shuyingPage, 'D8', '=C8*$G$1');
      await sleep(500);

      console.log('  [Nico] Adding totals row...\n');

      // Nico adds totals
      await setCellValue(nicoPage, 'A10', 'TOTAL');
      await setCellValue(nicoPage, 'C10', '=SUM(C4:C8)');
      await setCellValue(nicoPage, 'D10', '=SUM(D4:D8)');
      await sleep(500);

      // Verify formulas synced
      await verifyCellSynced(robertPage, 'D10', '=SUM(D4:D8)', 'Robert');

      console.log('  [Robert] The formulas are calculating automatically!');
      console.log('  [Shuying] Total cost is showing - this is powerful!\n');
    }));

    // === ACT 6: STATUS UPDATES ===
    results.push(await runTest('Act 6: Status Updates', async () => {
      console.log('\n  [Everyone] Updating project status...\n');

      // Everyone adds status (different cells to avoid conflicts)
      await setCellValue(nicoPage, 'E4', 'Done');
      await sleep(100);
      await setCellValue(robertPage, 'E5', 'In Progress');
      await sleep(100);
      await setCellValue(shuyingPage, 'E6', 'Done');
      await sleep(100);
      await setCellValue(nicoPage, 'E7', 'Planning');
      await sleep(100);
      await setCellValue(robertPage, 'E8', 'Todo');
      await sleep(500);

      // Verify all statuses synced
      await verifyCellSynced(shuyingPage, 'E4', 'Done', 'Shuying');
      await verifyCellSynced(nicoPage, 'E5', 'In Progress', 'Nico');
      await verifyCellSynced(robertPage, 'E6', 'Done', 'Robert');

      console.log('  [Nico] Collaboration is DONE!');
      console.log('  [Robert] Formula Engine is in progress.');
      console.log('  [Shuying] XLSX is done too!\n');
    }));

    // === FINALE: THE MASTER PLAN IS COMPLETE ===
    results.push(await runTest('Finale: All Together Now', async () => {
      console.log('\n  The team makes final touches simultaneously...\n');

      // Simultaneous edits
      const edits = [
        setCellValue(nicoPage, 'A12', 'Project Lead: Nico'),
        setCellValue(robertPage, 'A13', 'Tech Lead: Robert'),
        setCellValue(shuyingPage, 'A14', 'Engineering: Shuying'),
      ];
      await Promise.all(edits);
      await sleep(500);

      // Add the company motto
      await setCellValue(nicoPage, 'A16', 'Cells: The Future of Spreadsheets');
      await sleep(500);

      // Final verification - everyone can see the complete plan
      await verifyCellSynced(robertPage, 'A16', 'Cells: The Future of Spreadsheets', 'Robert');
      await verifyCellSynced(shuyingPage, 'A16', 'Cells: The Future of Spreadsheets', 'Shuying');

      // Verify the data built up throughout the demo
      await verifyCellSynced(robertPage, 'A1', 'CELLS - Master Plan 2025', 'Robert');
      await verifyCellSynced(shuyingPage, 'A4', 'Real-time Collaboration', 'Shuying');
      await verifyCellSynced(nicoPage, 'E6', 'Done', 'Nico');  // Status column

      console.log('\n  ==========================================');
      console.log('       THE MASTER PLAN IS COMPLETE!');
      console.log('  ==========================================');
      console.log('');
      console.log('  The spreadsheet now contains:');
      console.log('  - Title: CELLS - Master Plan 2025');
      console.log('  - 5 Features with owners and estimates');
      console.log('  - Cost formulas calculating from a rate');
      console.log('  - SUM totals for days and cost');
      console.log('  - Status for each feature');
      console.log('  - Team credits');
      console.log('');
      console.log('  All built collaboratively in real-time!');
      console.log('  ==========================================\n');
    }));

  } finally {
    // Cleanup
    if (shuyingPage) await shuyingPage.close().catch(() => {});
    if (robertPage) await robertPage.close().catch(() => {});
    if (ctx) await ctx.close();
  }

  // Print summary
  console.log('\n====================================================');
  console.log('               Demo Test Summary');
  console.log('====================================================');
  const passed = results.filter(r => r.passed).length;
  const failed = results.filter(r => !r.passed).length;
  console.log(`Passed: ${passed}/${results.length}`);
  console.log(`Failed: ${failed}`);

  if (failed > 0) {
    console.log('\nFailed acts:');
    for (const r of results.filter(r => !r.passed)) {
      console.log(`  - ${r.name}: ${r.error}`);
    }
    process.exit(1);
  }

  console.log('\n====================================================');
  console.log('         Cells: The Future of Spreadsheets');
  console.log('====================================================\n');

  process.exit(0);
}

// Run the collaborative demo
runCollabDemo();
