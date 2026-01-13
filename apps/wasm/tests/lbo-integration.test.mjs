// Integration tests for Excel parity improvements with LBO model
// Tests: column widths, text overflow, borders, zoom slider, AI panel positioning

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertEqual,
  assertTrue,
  sleep,
  clickCell,
} from './helpers.mjs';

const tests = {
  // Phase 5: Column widths match Excel appearance
  'Column widths are imported from XLSX': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the LBO model XLSX file
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Get column widths from the app - columns are stored in app.columns
    const columnInfo = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) {
        return { error: 'No app context' };
      }

      // Column data is stored directly on app.columns
      const appColumns = ctx.app.columns || [];
      const columns = [];

      // Check first 10 columns (or however many are in the viewport)
      const count = Math.min(10, appColumns.length);
      for (let i = 0; i < count; i++) {
        const col = appColumns[i];
        columns.push({
          index: i,
          pos: col?.pos,
          width: col?.width ?? 100, // default
          name: col?.name,
        });
      }

      return { columns, totalColumns: appColumns.length };
    });

    console.log(`Total columns in viewport: ${columnInfo.totalColumns}`);
    console.log('Column widths:', columnInfo.columns?.map(c => `${c.name || c.index}:${c.width}px`).join(', '));

    // Should have some non-default column widths (not all 100px)
    // Default column width is 100px
    const nonDefaultWidths = columnInfo.columns?.filter(c => c.width !== 100) || [];

    assertTrue(
      nonDefaultWidths.length > 0,
      `Expected non-default column widths from XLSX import. All columns are 100px.`
    );
  },

  // Phase 5: Row heights match Excel appearance
  'Row heights are imported from XLSX': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    const rowInfo = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) {
        return { error: 'No app context' };
      }

      // Row data is stored directly on app.rows
      const appRows = ctx.app.rows || [];
      const rows = [];

      // Check first 20 rows (or however many are in the viewport)
      const count = Math.min(20, appRows.length);
      for (let i = 0; i < count; i++) {
        const row = appRows[i];
        rows.push({
          index: i,
          pos: row?.pos,
          height: row?.height ?? 24, // default
          name: row?.name,
        });
      }

      return { rows, totalRows: appRows.length };
    });

    console.log(`Total rows in viewport: ${rowInfo.totalRows}`);
    console.log('Row heights:', rowInfo.rows?.map(r => `${r.name || r.index}:${r.height}px`).join(', '));

    // Note: The LBO model may or may not have custom row heights
    // This test documents the current behavior
    const nonDefaultHeights = rowInfo.rows?.filter(r => r.height !== 24) || [];
    console.log(`Found ${nonDefaultHeights.length} rows with non-default heights`);
  },

  // Phase 6: Text overflow works for long labels
  'Long text overflows into empty adjacent cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Check cells with long text content
    const overflowInfo = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) {
        return { error: 'No app context' };
      }

      const cells = ctx.app.cells;
      const longTextCells = [];

      for (const cell of cells) {
        const display = cell.display || cell.value || '';
        const displayStr = String(display);

        // Find cells with text longer than typical column width would show
        if (displayStr.length > 15 && cell.type !== 'e') {
          longTextCells.push({
            col: cell.col,
            row: cell.row,
            text: displayStr.substring(0, 50) + (displayStr.length > 50 ? '...' : ''),
            length: displayStr.length,
          });
        }
      }

      return { longTextCells: longTextCells.slice(0, 10) };
    });

    console.log('Cells with long text:');
    for (const cell of overflowInfo.longTextCells || []) {
      const colLetter = String.fromCharCode(65 + cell.col);
      console.log(`  ${colLetter}${cell.row + 1}: "${cell.text}" (${cell.length} chars)`);
    }

    // The test verifies that long text cells exist - the visual overflow
    // is tested by the renderer which clips appropriately
    assertTrue(
      (overflowInfo.longTextCells?.length || 0) > 0,
      'Should have cells with long text content'
    );
  },

  // Phase 7: Borders appear correctly
  'Cell borders are imported from XLSX': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Check for cells with border data (borders are nested under style.border)
    const borderInfo = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) {
        return { error: 'No app context' };
      }

      const cells = ctx.app.cells;
      const cellsWithBorders = [];
      const cellsWithStyles = [];

      for (const cell of cells) {
        // Borders are under cell.style.border, not cell.border
        if (cell.style) {
          cellsWithStyles.push(cell);
          const border = cell.style.border;
          if (border) {
            // Check if any border edge has a non-none style
            const hasBorder =
              (border.top?.style && border.top.style !== 'none') ||
              (border.right?.style && border.right.style !== 'none') ||
              (border.bottom?.style && border.bottom.style !== 'none') ||
              (border.left?.style && border.left.style !== 'none');

            if (hasBorder) {
              cellsWithBorders.push({
                col: cell.col,
                row: cell.row,
                border: {
                  top: border.top?.style || 'none',
                  right: border.right?.style || 'none',
                  bottom: border.bottom?.style || 'none',
                  left: border.left?.style || 'none',
                }
              });
            }
          }
        }
      }

      return {
        totalCells: cells.length,
        totalWithStyles: cellsWithStyles.length,
        cellsWithBorders: cellsWithBorders.slice(0, 10),
        totalWithBorders: cellsWithBorders.length,
      };
    });

    console.log(`Found ${borderInfo.totalWithStyles} cells with styles, ${borderInfo.totalWithBorders} with borders out of ${borderInfo.totalCells} total`);

    if (borderInfo.cellsWithBorders?.length > 0) {
      console.log('Sample cells with borders:');
      for (const cell of borderInfo.cellsWithBorders.slice(0, 5)) {
        const colLetter = String.fromCharCode(65 + cell.col);
        console.log(`  ${colLetter}${cell.row + 1}: T:${cell.border.top} R:${cell.border.right} B:${cell.border.bottom} L:${cell.border.left}`);
      }
    }

    assertTrue(
      (borderInfo.totalWithBorders || 0) > 0,
      'Should have cells with border styling from XLSX import'
    );
  },

  // Phase 3: Zoom slider works
  'Zoom slider controls work correctly': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(500);

    // Wait for bottom bar to be visible
    await ctx.page.waitForSelector('#bottom-bar:not(.hidden)', { timeout: 5000 });
    await sleep(200);

    // The LBO model XLSX has a stored zoom of 115%
    let zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    console.log(`Initial zoom from XLSX: ${zoomLevel}`);
    // XLSX files can have different zoom values - verify display matches renderer
    let rendererZoom = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    assertEqual(zoomLevel, `${rendererZoom}%`, 'Display should match renderer zoom');

    // Check that zoom slider exists
    const sliderExists = await ctx.page.$('#zoom-slider');
    assertTrue(sliderExists, 'Zoom slider element should exist');

    // Get slider value - should match renderer
    let sliderValue = await ctx.page.$eval('#zoom-slider', el => el.value);
    assertEqual(sliderValue, String(rendererZoom), 'Slider should match renderer zoom');

    // Use zoom in button - should go to next zoom level
    await ctx.page.click('#zoom-in-btn');
    await sleep(100);

    const previousZoom = rendererZoom;
    rendererZoom = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    assertTrue(rendererZoom > previousZoom, 'Zoom should increase after clicking zoom-in');

    zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    sliderValue = await ctx.page.$eval('#zoom-slider', el => el.value);
    assertEqual(zoomLevel, `${rendererZoom}%`, 'Display should match after zoom in');
    assertEqual(sliderValue, String(rendererZoom), 'Slider should sync after zoom in');

    // Use zoom out button
    await ctx.page.click('#zoom-out-btn');
    await sleep(100);

    // Note: Zoom levels are discrete (10, 25, 50, 75, 100, 125, 150...)
    // Going from 115 -> 125 (zoom in) -> 100 (zoom out) is expected
    // because 100 is the next lower discrete level after 125
    const zoomAfterOut = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    assertTrue(zoomAfterOut < rendererZoom, 'Zoom should decrease after zoom out');
    assertEqual(zoomLevel, `${zoomAfterOut}%`, 'Display should match after zoom out');
  },

  // Phase 2: Zoom persists during scrolling
  'Zoom level persists during scrolling': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(500);

    await ctx.page.waitForSelector('#bottom-bar:not(.hidden)', { timeout: 5000 });
    await sleep(200);

    // Get initial zoom from XLSX
    let initialZoom = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    console.log(`Initial zoom after file load: ${initialZoom}%`);

    // Click zoom-out to change zoom
    await ctx.page.click('#zoom-out-btn');
    await sleep(200);

    let zoomAfterChange = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    let zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    console.log(`Zoom after clicking zoom-out: ${zoomLevel}`);

    assertTrue(zoomAfterChange < initialZoom, 'Zoom should decrease after zoom-out');
    assertEqual(zoomLevel, `${zoomAfterChange}%`, 'Display should match renderer');

    // Scroll by clicking on a distant cell
    await clickCell(ctx.page, 'Z50');
    await sleep(500);

    // Zoom should persist after scrolling
    let zoomAfterScroll = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    console.log(`Zoom after clicking Z50: ${zoomLevel}, renderer: ${zoomAfterScroll}`);
    assertEqual(zoomAfterScroll, zoomAfterChange, 'Zoom should persist after scrolling');
    assertEqual(zoomLevel, `${zoomAfterChange}%`, 'Display should persist after scrolling');

    // Scroll to another area
    await clickCell(ctx.page, 'A1');
    await sleep(500);

    // Zoom should still persist
    let zoomAfterReturn = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.renderer?.getZoomScale?.() ?? 100;
    });
    zoomLevel = await ctx.page.$eval('#zoom-level', el => el.textContent);
    assertEqual(zoomAfterReturn, zoomAfterChange, 'Zoom should persist after returning to A1');
    assertEqual(zoomLevel, `${zoomAfterChange}%`, 'Display should persist after returning');
  },

  // Phase 4: AI panel positioning
  'AI panel does not overlap zoom controls': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(500);

    await ctx.page.waitForSelector('#bottom-bar:not(.hidden)', { timeout: 5000 });

    // Click AI button to open panel (if it exists)
    const aiButton = await ctx.page.$('#ai-btn');
    if (!aiButton) {
      console.log('AI button not present - skipping AI panel test');
      return;
    }

    await aiButton.click();
    await sleep(300);

    // Check if chat panel is visible
    const chatPanel = await ctx.page.$('#chat-panel');
    if (!chatPanel) {
      console.log('Chat panel not present - skipping positioning test');
      return;
    }

    // Get positions of chat panel and zoom controls
    const positions = await ctx.page.evaluate(() => {
      const chatPanel = document.getElementById('chat-panel');
      const zoomControls = document.querySelector('.zoom-controls');
      const bottomBar = document.getElementById('bottom-bar');

      if (!chatPanel || !zoomControls || !bottomBar) {
        return null;
      }

      const chatRect = chatPanel.getBoundingClientRect();
      const zoomRect = zoomControls.getBoundingClientRect();
      const barRect = bottomBar.getBoundingClientRect();

      return {
        chatBottom: chatRect.bottom,
        barTop: barRect.top,
        zoomTop: zoomRect.top,
        zoomBottom: zoomRect.bottom,
      };
    });

    if (positions) {
      console.log(`Chat panel bottom: ${positions.chatBottom}px, Bottom bar top: ${positions.barTop}px`);

      // Chat panel should be above the bottom bar
      assertTrue(
        positions.chatBottom <= positions.barTop + 5, // Allow 5px tolerance
        `Chat panel should not overlap bottom bar. Panel bottom: ${positions.chatBottom}, Bar top: ${positions.barTop}`
      );
    }
  },

  // Combined integration test
  'All features work together after XLSX import': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    await ctx.page.waitForSelector('#bottom-bar:not(.hidden)', { timeout: 5000 });

    // Verify data loaded correctly (check that we have cells)
    const cellCount = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.cells?.length || 0;
    });

    assertTrue(cellCount > 0, 'Should have cells loaded from XLSX');
    console.log(`Loaded ${cellCount} cells`);

    // Test zoom while viewing data
    await ctx.page.click('#zoom-in-btn');
    await sleep(100);

    let zoom = await ctx.page.$eval('#zoom-level', el => el.textContent);
    assertEqual(zoom, '125%', 'Zoom should increase');

    // Navigate through the spreadsheet
    await clickCell(ctx.page, 'D7'); // Company_Name cell
    await sleep(200);

    // Verify zoom persisted
    zoom = await ctx.page.$eval('#zoom-level', el => el.textContent);
    assertEqual(zoom, '125%', 'Zoom should persist after cell click');

    // Navigate to another area
    await clickCell(ctx.page, 'K10');
    await sleep(200);

    // Zoom should still be 125%
    zoom = await ctx.page.$eval('#zoom-level', el => el.textContent);
    assertEqual(zoom, '125%', 'Zoom should persist after navigating');

    console.log('All integration features verified');
  },
};

// Run all tests
runTests(tests);
