// Custom format tests for Cells spreadsheet application
// Tests custom format creation, persistence, and collaboration sync
//
// Run with: npm run test -- tests/custom-format.test.mjs
// Run with DEBUG=1 for verbose logging

import { setup, runTest, TestContext } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  getCellDisplayValue,
  assertEqual,
  assertTrue,
  sleep,
  waitForCollabReady,
  waitForPeerConnection,
  assertWithRetry,
} from './helpers.mjs';

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

/**
 * Create a custom format via the client API and return the format ID
 */
async function createCustomFormat(page, formatCode) {
  return await page.evaluate(async (code) => {
    const ctx = window._appContext;
    if (!ctx) {
      throw new Error('_appContext is undefined');
    }
    if (!ctx.app) {
      throw new Error('_appContext.app is undefined');
    }
    if (!ctx.app.dataSource) {
      throw new Error('_appContext.app.dataSource is undefined');
    }

    // Debug: check what's available on dataSource
    const ds = ctx.app.dataSource;
    console.log('[DEBUG] dataSource keys:', Object.keys(ds));
    console.log('[DEBUG] dataSource.client:', ds.client);
    console.log('[DEBUG] dataSource._client:', ds._client);

    // Try accessing via _client (private property)
    const client = ds.client || ds._client;
    if (!client) {
      throw new Error('No client found on dataSource. Keys: ' + Object.keys(ds).join(', '));
    }

    console.log('[DEBUG] client keys:', Object.keys(client));
    console.log('[DEBUG] client.createCustomFormat:', typeof client.createCustomFormat);

    if (typeof client.createCustomFormat !== 'function') {
      throw new Error('createCustomFormat not a function. Client methods: ' + Object.getOwnPropertyNames(Object.getPrototypeOf(client)).join(', '));
    }

    const result = await client.createCustomFormat(code);
    if (result.error) {
      throw new Error(result.error);
    }
    return result.formatId;
  }, formatCode);
}

/**
 * Apply a format to a cell by reference (e.g., "A1")
 */
async function setCellFormat(page, cellRef, formatId) {
  return await page.evaluate(async ({ cellRef, formatId }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      throw new Error('App context not available');
    }
    // Parse cell reference to get col/row
    const match = cellRef.match(/^([A-Z]+)(\d+)$/i);
    if (!match) throw new Error('Invalid cell reference');
    const colLetter = match[1].toUpperCase();
    const row = parseInt(match[2], 10) - 1; // 0-indexed
    let col = 0;
    for (let i = 0; i < colLetter.length; i++) {
      col = col * 26 + (colLetter.charCodeAt(i) - 64);
    }
    col -= 1; // 0-indexed

    const result = await ctx.app.dataSource.client.setCellFormatAt(col, row, formatId);
    return result.success;
  }, { cellRef, formatId });
}

/**
 * Get the list of available formats (including custom)
 */
async function getAvailableFormats(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      throw new Error('App context not available');
    }
    return await ctx.app.dataSource.client.getAvailableFormats();
  });
}

/**
 * Save workbook to .cells format string
 */
async function exportToCells(page) {
  return await page.evaluate(async () => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      throw new Error('App context not available');
    }
    const result = await ctx.app.dataSource.client.exportCells();
    // Convert ArrayBuffer to string
    const decoder = new TextDecoder();
    return decoder.decode(result.data);
  });
}

/**
 * Load workbook from .cells format string
 */
async function loadFromCells(page, content) {
  return await page.evaluate(async (content) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      throw new Error('App context not available');
    }
    const result = await ctx.app.dataSource.client.loadCells(content);
    return result.success;
  }, content);
}

async function runCustomFormatTests() {
  let ctx;
  let page2;
  const results = [];

  try {
    ctx = await setup();

    // ========================================================================
    // Test 1: Create custom format and apply to cell
    // ========================================================================
    results.push(await runTest('Create custom format and apply to cell', async () => {
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Enter a number
      await setCellValue(ctx.page, 'A1', '1234.5678');
      await sleep(200);

      // Create a custom format with 3 decimal places
      const formatId = await createCustomFormat(ctx.page, '#,##0.000');
      assertTrue(formatId, 'Should return a format ID');
      assertTrue(formatId.length === 8, 'Format ID should be 8 characters');

      // Apply the format to the cell
      await clickCell(ctx.page, 'A1');
      await sleep(100);
      const success = await setCellFormat(ctx.page, 'A1', formatId);
      assertTrue(success, 'setCellFormat should succeed');
      await sleep(200);

      // Verify the display value uses the custom format
      const display = await getCellDisplayValue(ctx.page, 'A1');
      assertEqual(display, '1,234.568', 'Cell should display with custom format (3 decimals, thousands separator)');
    }));

    // ========================================================================
    // Test 2: Custom format appears in available formats list
    // ========================================================================
    results.push(await runTest('Custom format appears in available formats', async () => {
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Get initial formats
      const initialFormats = await getAvailableFormats(ctx.page);
      const initialCount = initialFormats.length;

      // Create a custom format
      const formatId = await createCustomFormat(ctx.page, '0.00%');

      // Get formats again
      const updatedFormats = await getAvailableFormats(ctx.page);
      assertEqual(updatedFormats.length, initialCount + 1, 'Should have one more format');

      // Find the custom format
      const customFormat = updatedFormats.find(f => f.id === formatId);
      assertTrue(customFormat, 'Custom format should be in the list');
      assertEqual(customFormat.isCustom, true, 'Format should be marked as custom');
      assertEqual(customFormat.formatCode, '0.00%', 'Format code should match');
    }));

    // ========================================================================
    // Test 3: Custom format persists through save/load
    // ========================================================================
    results.push(await runTest('Custom format persists through save/load', async () => {
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Enter a value and create custom format
      await setCellValue(ctx.page, 'A1', '9876.54');
      await sleep(200);

      const formatId = await createCustomFormat(ctx.page, '"Value: "#,##0.00');
      await clickCell(ctx.page, 'A1');
      await sleep(100);
      await setCellFormat(ctx.page, 'A1', formatId);
      await sleep(200);

      // Verify initial display
      let display = await getCellDisplayValue(ctx.page, 'A1');
      assertEqual(display, 'Value: 9,876.54', 'Cell should display with custom prefix format');

      // Save to .cells format
      const cellsContent = await exportToCells(ctx.page);
      assertTrue(cellsContent.includes('F '), '.cells file should contain format definition (F line)');
      assertTrue(cellsContent.includes(formatId), '.cells file should contain the format ID');

      // Reload the page and load the file
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);
      await loadFromCells(ctx.page, cellsContent);
      await sleep(300);

      // Verify the format still works
      display = await getCellDisplayValue(ctx.page, 'A1');
      assertEqual(display, 'Value: 9,876.54', 'After reload, cell should still display with custom format');

      // Verify format is in available formats
      const formats = await getAvailableFormats(ctx.page);
      const customFormat = formats.find(f => f.id === formatId);
      assertTrue(customFormat, 'Custom format should still exist after reload');
      assertEqual(customFormat.isCustom, true, 'Format should still be marked as custom');
    }));

    // ========================================================================
    // Test 4: Multiple custom formats
    // ========================================================================
    results.push(await runTest('Multiple custom formats work correctly', async () => {
      await ctx.page.goto(ctx.baseUrl);
      await waitForAppReady(ctx.page);

      // Create multiple custom formats
      const format1 = await createCustomFormat(ctx.page, '[Red]#,##0.00');
      const format2 = await createCustomFormat(ctx.page, '0.0000');
      const format3 = await createCustomFormat(ctx.page, '"$"#,##0" USD"');

      // Enter values
      await setCellValue(ctx.page, 'A1', '100');
      await setCellValue(ctx.page, 'A2', '3.14159');
      await setCellValue(ctx.page, 'A3', '999');
      await sleep(200);

      // Apply different formats
      await setCellFormat(ctx.page, 'A1', format1);
      await setCellFormat(ctx.page, 'A2', format2);
      await setCellFormat(ctx.page, 'A3', format3);
      await sleep(200);

      // Verify displays
      const display1 = await getCellDisplayValue(ctx.page, 'A1');
      const display2 = await getCellDisplayValue(ctx.page, 'A2');
      const display3 = await getCellDisplayValue(ctx.page, 'A3');

      assertEqual(display1, '100.00', 'A1 should use red number format (color not visible in text)');
      assertEqual(display2, '3.1416', 'A2 should use 4 decimal format');
      assertEqual(display3, '$999 USD', 'A3 should use currency suffix format');
    }));

    // ========================================================================
    // Collaboration Tests (require two browser contexts)
    // ========================================================================

    // Create second page for collab tests
    const context2 = await ctx.browser.createBrowserContext();
    page2 = await context2.newPage();

    // ========================================================================
    // Test 5: Peer joins room with existing custom formats
    // ========================================================================
    results.push(await runTest('Peer joins room with existing custom formats', async () => {
      const roomId = generateRoomId();

      // First peer joins and creates custom format
      await joinRoom(ctx.page, ctx.baseUrl, roomId);

      // Create custom format and apply to cell
      await setCellValue(ctx.page, 'A1', '42.5');
      await sleep(200);
      const formatId = await createCustomFormat(ctx.page, '"Result: "0.00');
      await setCellFormat(ctx.page, 'A1', formatId);
      await sleep(500);

      // Verify first peer sees the format
      let display1 = await getCellDisplayValue(ctx.page, 'A1');
      assertEqual(display1, 'Result: 42.50', 'First peer should see custom format');

      // Second peer joins
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(page2, 10000);
      await sleep(1000); // Wait for sync

      // Verify second peer sees the formatted value
      await assertWithRetry(async () => {
        const display2 = await getCellDisplayValue(page2, 'A1');
        assertEqual(display2, 'Result: 42.50', 'Second peer should see custom format after joining');
      }, 5000, 500);

      // Verify second peer has the format in available formats
      const formats = await getAvailableFormats(page2);
      const customFormat = formats.find(f => f.id === formatId);
      assertTrue(customFormat, 'Second peer should have the custom format in available formats');
    }));

    // ========================================================================
    // Test 6: Custom format created live syncs to other peer
    // ========================================================================
    results.push(await runTest('Custom format created live syncs to peer', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);
      await sleep(500);

      // First peer enters a value
      await setCellValue(ctx.page, 'B1', '1000');
      await sleep(500);

      // Wait for value to sync
      await assertWithRetry(async () => {
        const display = await getCellDisplayValue(page2, 'B1');
        assertEqual(display, '1000', 'Value should sync to second peer');
      }, 5000, 500);

      // First peer creates custom format and applies it
      const formatId = await createCustomFormat(ctx.page, '#,##0" units"');
      await setCellFormat(ctx.page, 'B1', formatId);
      await sleep(500);

      // Verify first peer sees it
      let display1 = await getCellDisplayValue(ctx.page, 'B1');
      assertEqual(display1, '1,000 units', 'First peer should see new format');

      // Verify second peer receives the format and sees updated display
      await assertWithRetry(async () => {
        const display2 = await getCellDisplayValue(page2, 'B1');
        assertEqual(display2, '1,000 units', 'Second peer should see format after live sync');
      }, 5000, 500);
    }));

    // ========================================================================
    // Test 7: Both peers can use same custom format
    // ========================================================================
    results.push(await runTest('Both peers can use same custom format', async () => {
      const roomId = generateRoomId();

      // Both peers join
      await joinRoom(ctx.page, ctx.baseUrl, roomId);
      await joinRoom(page2, ctx.baseUrl, roomId);
      await waitForPeerConnection(ctx.page, 10000);
      await waitForPeerConnection(page2, 10000);
      await sleep(500);

      // First peer creates a custom format (0.0% multiplies by 100)
      const formatId = await createCustomFormat(ctx.page, '0.0%');
      await sleep(500);

      // First peer enters value and applies format
      await setCellValue(ctx.page, 'A1', '0.75');
      await setCellFormat(ctx.page, 'A1', formatId);
      await sleep(500);

      // Second peer enters a different value
      await setCellValue(page2, 'A2', '0.25');
      await sleep(500);

      // Wait for sync
      await sleep(500);

      // Second peer applies the SAME format (by ID) to their cell
      await page2.evaluate(async ({ formatId }) => {
        const ctx = window._appContext;
        if (!ctx || !ctx.app || !ctx.app.dataSource) {
          throw new Error('App context not available on peer 2');
        }
        // A2 = column 0, row 1
        await ctx.app.dataSource.client.setCellFormatAt(0, 1, formatId);
      }, { formatId });
      await sleep(500);

      // Verify both cells use the format correctly on both peers
      await assertWithRetry(async () => {
        const display1_p1 = await getCellDisplayValue(ctx.page, 'A1');
        const display2_p1 = await getCellDisplayValue(ctx.page, 'A2');
        assertEqual(display1_p1, '75.0%', 'Peer 1: A1 should show 75.0%');
        assertEqual(display2_p1, '25.0%', 'Peer 1: A2 should show 25.0%');
      }, 5000, 500);

      await assertWithRetry(async () => {
        const display1_p2 = await getCellDisplayValue(page2, 'A1');
        const display2_p2 = await getCellDisplayValue(page2, 'A2');
        assertEqual(display1_p2, '75.0%', 'Peer 2: A1 should show 75.0%');
        assertEqual(display2_p2, '25.0%', 'Peer 2: A2 should show 25.0%');
      }, 5000, 500);
    }));

    // Print results
    console.log('\n========================================');
    console.log('Custom Format Test Results:');
    console.log('========================================');

    let passed = 0;
    let failed = 0;
    for (const result of results) {
      const status = result.passed ? '\x1b[32mPASS\x1b[0m' : '\x1b[31mFAIL\x1b[0m';
      console.log(`${status}: ${result.name}`);
      if (!result.passed) {
        console.log(`  Error: ${result.error}`);
        failed++;
      } else {
        passed++;
      }
    }

    console.log('----------------------------------------');
    console.log(`Total: ${passed} passed, ${failed} failed`);
    console.log('========================================\n');

    // Cleanup
    if (page2) await page2.close();
    if (ctx) await ctx.close();

    process.exit(failed > 0 ? 1 : 0);

  } catch (error) {
    console.error('Test setup failed:', error);
    if (page2) await page2.close();
    if (ctx) await ctx.close();
    process.exit(1);
  }
}

// Run tests
runCustomFormatTests();
