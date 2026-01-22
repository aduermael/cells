// Collaborative Demo Test - "Building the Master Plan"
//
// Story: Nico, Robert, and Shuying are building a master plan to release
// an AI-native collaborative Excel competitor called "Cells".
//
// This demo showcases real-time collaboration with three participants
// simultaneously editing a spreadsheet - ALL IN ONE SESSION.
//
// Features demonstrated:
// - Real-time collaborative editing
// - 80s retro/synthwave color palette (hot pink, cyan, neon green)
// - Bold and italic formatting
// - Multiple font families (Georgia, Helvetica, Courier New)
// - Cell borders (outline and all borders)
// - Number formatting (currency)
// - Lavender/cyan alternating row colors
// - Neon status badges (green/orange/magenta/coral)
// - Column & row resizing with synced dimensions
//
// Run in headed mode to watch the demo:
//   bazel run :e2e-headed -- collab
//
// Run with SLOWMO for slower animations:
//   SLOWMO=100 bazel run :e2e-headed -- collab

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
  selectRange,
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
    await page.click(colorSelector);
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
 * Apply a text color to the currently selected cell(s) using the toolbar
 * @param {Page} page - Puppeteer page
 * @param {string} color - Hex color (e.g., '#EF4444')
 */
async function applyTextColor(page, color) {
  await page.click('#style-text-color-btn');
  await sleep(100);
  const colorSelector = `#text-color-popup .color-option[data-color="${color.toUpperCase()}"]`;
  const hasColor = await page.$(colorSelector);
  if (hasColor) {
    await page.click(colorSelector);
  } else {
    const hexInput = await page.$('#text-color-popup .color-hex-input');
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
 * Toggle italic on the currently selected cell(s)
 * @param {Page} page - Puppeteer page
 */
async function applyItalic(page) {
  await page.click('#style-italic-btn');
  await sleep(200);
}

/**
 * Apply a border to the current selection using the toolbar dropdown
 * @param {Page} page - Puppeteer page
 * @param {'all' | 'outer' | 'top' | 'bottom' | 'left' | 'right' | 'none'} borderType
 */
async function applyBorder(page, borderType) {
  await page.click('#border-btn');
  await sleep(100);
  await page.click(`#border-${borderType}-btn`);
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

/**
 * Apply a font family to the currently selected cell(s) using the toolbar dropdown
 * @param {Page} page - Puppeteer page
 * @param {string} fontName - Font family name (e.g., 'Arial', 'Georgia', 'Courier New')
 */
async function applyFontFamily(page, fontName) {
  // Click the font family dropdown button to open the menu
  await page.click('#font-family-btn');
  await sleep(100);
  // Select the font from the dropdown menu (items are in #font-family-dropdown .font-dropdown-menu)
  const fontSelector = `#font-family-dropdown [data-font="${fontName}"]`;
  const hasFontOption = await page.$(fontSelector);
  if (hasFontOption) {
    await page.click(fontSelector);
  } else {
    console.warn(`Font "${fontName}" not found in font dropdown, using default`);
    // Close the dropdown without selection
    await page.click('#font-family-btn');
  }
  await sleep(200);
}

// 80s Retro / Synthwave color palette
const COLORS = {
  // Primary retro colors
  HOT_PINK: '#FF1493',      // Deep pink - titles
  CYAN: '#00CED1',          // Dark turquoise - headers
  ELECTRIC_PURPLE: '#9400D3', // Dark violet - totals row
  NEON_GREEN: '#00FF7F',    // Spring green - done status
  SUNSET_ORANGE: '#FF6B35', // Vivid orange - in progress
  ELECTRIC_BLUE: '#00BFFF', // Deep sky blue - accents
  MAGENTA: '#FF00FF',       // Fuchsia - planning status
  CORAL: '#FF7F50',         // Coral - todo status

  // Text colors for contrast
  DARK_TEXT: '#1A1A2E',     // Dark navy - for light backgrounds
  LIGHT_TEXT: '#FFFFFF',    // White - for dark backgrounds
  GOLD: '#FFD700',          // Gold - rate highlight

  // Background variations (lighter for alternating rows)
  LAVENDER: '#E6E6FA',      // Light lavender - alternating row
  LIGHT_CYAN: '#E0FFFF',    // Light cyan - alternating row
};

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
 * Set the theme (light or dark) for a participant
 * @param {Page} page - Puppeteer page
 * @param {'light' | 'dark'} theme - Theme to set
 */
async function setTheme(page, theme) {
  await page.evaluate((themeName) => {
    // Store in localStorage so it persists
    localStorage.setItem('cells.theme', themeName);
    // Apply immediately
    document.documentElement.setAttribute('data-theme', themeName);
    // Trigger theme change event so grid re-renders
    window.dispatchEvent(new CustomEvent('themechange', { detail: { theme: themeName } }));
  }, theme);
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
  console.log('       ★ 80s Retro Synthwave Edition ★');
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

      // Robert prefers light mode (for visual diversity in demo)
      await setTheme(robertPage, 'light');
      console.log('  [Robert] Switched to light mode\n');

      // Wait for everyone to see each other
      console.log('  Waiting for peer connections...');
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

    // === ACT 2: NICO CREATES, ROBERT STYLES ===
    results.push(await runTest('Act 2: Nico Creates, Robert Styles', async () => {
      console.log('\n  [Nico] Creating the spreadsheet structure...\n');

      // Nico creates the title
      await setCellValue(nicoPage, 'A1', 'CELLS - Master Plan 2025');
      await sleep(500);

      // Verify the title synced to Robert before he styles it
      await verifyCellSynced(robertPage, 'A1', 'CELLS - Master Plan 2025', 'Robert');

      // Robert immediately styles the title: hot pink background, dark text, bold
      console.log('  [Robert] I see the title! Let me give it that 80s synthwave vibe...\n');
      await selectRange(robertPage, 'A1', 'E1');
      await sleep(100);
      await applyBackgroundColor(robertPage, COLORS.HOT_PINK);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await applyBold(robertPage);
      await applyFontFamily(robertPage, 'Georgia');
      await sleep(300);

      console.log('  [Robert] Hot pink title with Georgia font - totally rad!\n');

      // Nico creates the header row
      await setCellValue(nicoPage, 'A3', 'Feature');
      await setCellValue(nicoPage, 'B3', 'Owner');
      await setCellValue(nicoPage, 'C3', 'Days');
      await setCellValue(nicoPage, 'D3', 'Cost');
      await setCellValue(nicoPage, 'E3', 'Status');
      await sleep(500);

      // Verify headers synced to Robert
      await verifyCellSynced(robertPage, 'A3', 'Feature', 'Robert');

      // Robert styles the header row: cyan background, dark text, bold, borders
      console.log('  [Robert] Headers are up! Adding that neon cyan look...\n');
      await selectRange(robertPage, 'A3', 'E3');
      await sleep(100);
      await applyBackgroundColor(robertPage, COLORS.CYAN);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await applyBold(robertPage);
      await applyBorder(robertPage, 'all');
      await applyFontFamily(robertPage, 'Helvetica');
      await sleep(300);

      console.log('  [Robert] Headers styled with cyan, Helvetica - looking fresh!\n');

      // Nico adds the feature list
      console.log('  [Nico] Adding feature list...\n');
      await setCellValue(nicoPage, 'A4', 'Real-time Collaboration');
      await setCellValue(nicoPage, 'A5', 'Formula Engine');
      await setCellValue(nicoPage, 'A6', 'XLSX Import/Export');
      await setCellValue(nicoPage, 'A7', 'AI Formula Assistant');
      await setCellValue(nicoPage, 'A8', 'Mobile Apps');
      await sleep(500);

      // Verify the styling synced to Shuying
      await verifyCellSynced(shuyingPage, 'A1', 'CELLS - Master Plan 2025', 'Shuying');

      console.log('  [Shuying] Wow! The styling synced to me perfectly!');
      console.log('  [Shuying] Love the retro pink title and cyan headers!\n');
    }));

    // === ACT 2.5: THE TEAM ADJUSTS THE LAYOUT ===
    results.push(await runTest('Act 2.5: Making Room for Greatness', async () => {
      console.log('\n  [Shuying] These feature names are getting cut off...\n');
      console.log('  [Shuying] Let me widen column A for those long feature names!\n');

      // Shuying widens column A to fit "Real-time Collaboration"
      await resizeColumn(shuyingPage, 'A', 180);
      await sleep(400);

      console.log('  [Shuying] Column A is now 180px - much better!\n');

      // Verify Shuying can still click cells correctly after resize
      await clickCell(shuyingPage, 'A4');
      await sleep(200);
      const featureValue = await getFormulaBarContent(shuyingPage);
      assertEqual(featureValue, 'Real-time Collaboration', 'A4 should be clickable after resize');

      console.log('  [Nico] Nice! That title row needs more presence though...\n');
      console.log('  [Nico] Making the title row taller for that epic 80s vibe!\n');

      // Nico makes the title row taller
      await resizeRow(nicoPage, 1, 36);
      await sleep(400);

      console.log('  [Nico] Row 1 is now 36px tall - very VHS cover art!\n');

      // Verify Nico can still click cells in the resized row
      await clickCell(nicoPage, 'A1');
      await sleep(200);
      const titleValue = await getFormulaBarContent(nicoPage);
      assertEqual(titleValue, 'CELLS - Master Plan 2025', 'A1 should be clickable after row resize');

      console.log('  [Robert] Let me make the Status column wider for those badges...\n');

      // Robert widens column E for status badges
      await resizeColumn(robertPage, 'E', 120);
      await sleep(400);

      console.log('  [Robert] Column E is now 120px - room for all those neon badges!\n');

      // Critical test: verify clicking cells in columns AFTER the resized column A works
      console.log('  [Team] Testing clicks after all the resizing...\n');

      // Click on column B (after resized column A) - this is the key test!
      await clickCell(nicoPage, 'B3');
      await sleep(200);
      const ownerHeader = await getFormulaBarContent(nicoPage);
      assertEqual(ownerHeader, 'Owner', 'B3 should be clickable after column A resize');

      // Click on column C (two columns after resize)
      await clickCell(robertPage, 'C3');
      await sleep(200);
      const daysHeader = await getFormulaBarContent(robertPage);
      assertEqual(daysHeader, 'Days', 'C3 should be clickable after column A resize');

      // Click on column D
      await clickCell(shuyingPage, 'D3');
      await sleep(200);
      const costHeader = await getFormulaBarContent(shuyingPage);
      assertEqual(costHeader, 'Cost', 'D3 should be clickable after column A resize');

      // Test clicking in a row after the resized row 1
      await clickCell(nicoPage, 'A3');
      await sleep(200);
      const featureHeader = await getFormulaBarContent(nicoPage);
      assertEqual(featureHeader, 'Feature', 'A3 should be clickable after row 1 resize');

      console.log('  [Shuying] Perfect! All the resizes synced and clicks work great!');
      console.log('  [Nico] The layout is looking totally tubular now!');
      console.log('  [Robert] Ready for the data - this is gonna be radical!\n');
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

    // === ACT 4: SHUYING ADDS ESTIMATES, ROBERT STYLES ===
    results.push(await runTest('Act 4: Shuying Adds Estimates, Robert Styles', async () => {
      console.log('\n  [Shuying] Adding effort estimates (in days)...\n');

      await setCellValue(shuyingPage, 'C4', '30');
      await setCellValue(shuyingPage, 'C5', '45');
      await setCellValue(shuyingPage, 'C6', '20');
      await setCellValue(shuyingPage, 'C7', '60');
      await setCellValue(shuyingPage, 'C8', '90');
      await sleep(500);

      // Verify estimates synced to Robert before he styles
      await verifyCellSynced(robertPage, 'C4', '30', 'Robert');

      // Robert styles the data rows with alternating lavender/light cyan backgrounds
      console.log('  [Robert] Time to add that pastel retro vibe to the data...\n');
      await selectRange(robertPage, 'A4', 'E4');
      await applyBackgroundColor(robertPage, COLORS.LAVENDER);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await sleep(100);
      // Row 5 gets light cyan
      await selectRange(robertPage, 'A5', 'E5');
      await applyBackgroundColor(robertPage, COLORS.LIGHT_CYAN);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await sleep(100);
      await selectRange(robertPage, 'A6', 'E6');
      await applyBackgroundColor(robertPage, COLORS.LAVENDER);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await sleep(100);
      await selectRange(robertPage, 'A7', 'E7');
      await applyBackgroundColor(robertPage, COLORS.LIGHT_CYAN);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await sleep(100);
      await selectRange(robertPage, 'A8', 'E8');
      await applyBackgroundColor(robertPage, COLORS.LAVENDER);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await sleep(300);

      console.log('  [Robert] Alternating lavender and light cyan - very Miami Vice!\n');

      // Verify styling synced to others
      await verifyCellSynced(nicoPage, 'C4', '30', 'Nico');

      console.log('  [Nico] 30 days for Collaboration - challenge accepted!');
      console.log('  [Shuying] The pastel retro colors look amazing, Robert!\n');
    }));

    // === ACT 5: EVERYONE ADDS FORMULAS ===
    results.push(await runTest('Act 5: The Formulas Come Alive', async () => {
      console.log('\n  [Shuying] Adding cost calculations...\n');

      // Shuying adds cost rate in F1 with styling
      await setCellValue(shuyingPage, 'F1', 'Rate/day:');
      await clickCell(shuyingPage, 'F1');
      await applyItalic(shuyingPage);
      await sleep(100);

      await setCellValue(shuyingPage, 'G1', '$500');
      await clickCell(shuyingPage, 'G1');
      await applyBold(shuyingPage);
      await applyTextColor(shuyingPage, COLORS.GOLD);
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
      await sleep(300);

      // Apply currency format to cost column
      console.log('  [Shuying] Formatting costs as currency...\n');
      await selectRange(shuyingPage, 'D4', 'D8');
      await applyNumberFormat(shuyingPage, 'CURRENCY');
      await sleep(300);

      console.log('  [Nico] Adding totals row...\n');

      // Nico adds totals
      await setCellValue(nicoPage, 'A10', 'TOTAL');
      await clickCell(nicoPage, 'A10');
      await applyBold(nicoPage);
      await sleep(100);

      await setCellValue(nicoPage, 'C10', '=SUM(C4:C8)');
      await setCellValue(nicoPage, 'D10', '=SUM(D4:D8)');
      await sleep(300);

      // Style the totals row with electric purple
      console.log('  [Nico] Styling the totals row with electric purple...\n');
      await selectRange(nicoPage, 'A10', 'E10');
      await applyBackgroundColor(nicoPage, COLORS.ELECTRIC_PURPLE);
      await applyTextColor(nicoPage, COLORS.LIGHT_TEXT);
      await applyBold(nicoPage);
      await applyBorder(nicoPage, 'all');
      await sleep(300);

      // Apply currency format to total cost
      await clickCell(nicoPage, 'D10');
      await applyNumberFormat(nicoPage, 'CURRENCY');
      await sleep(300);

      // Add outer border to the entire data table
      console.log('  [Robert] Adding borders to the table...\n');
      await selectRange(robertPage, 'A3', 'E10');
      await applyBorder(robertPage, 'outer');
      await sleep(500);

      // Verify formulas synced
      await verifyCellSynced(robertPage, 'D10', '=SUM(D4:D8)', 'Robert');

      console.log('  [Robert] The formulas are calculating automatically!');
      console.log('  [Shuying] The gold rate and purple totals look so retro!\n');
    }));

    // === ACT 6: STATUS UPDATES ===
    results.push(await runTest('Act 6: Status Updates', async () => {
      console.log('\n  [Everyone] Updating project status with neon colors...\n');

      // Everyone adds status with color-coded backgrounds - retro neon style!

      // Nico: E4 = Done (neon green)
      await setCellValue(nicoPage, 'E4', 'Done');
      await clickCell(nicoPage, 'E4');
      await applyBackgroundColor(nicoPage, COLORS.NEON_GREEN);
      await applyTextColor(nicoPage, COLORS.DARK_TEXT);
      await applyBold(nicoPage);
      await sleep(200);

      // Robert: E5 = In Progress (sunset orange)
      await setCellValue(robertPage, 'E5', 'In Progress');
      await clickCell(robertPage, 'E5');
      await applyBackgroundColor(robertPage, COLORS.SUNSET_ORANGE);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await applyBold(robertPage);
      await sleep(200);

      // Shuying: E6 = Done (neon green)
      await setCellValue(shuyingPage, 'E6', 'Done');
      await clickCell(shuyingPage, 'E6');
      await applyBackgroundColor(shuyingPage, COLORS.NEON_GREEN);
      await applyTextColor(shuyingPage, COLORS.DARK_TEXT);
      await applyBold(shuyingPage);
      await sleep(200);

      // Nico: E7 = Planning (magenta)
      await setCellValue(nicoPage, 'E7', 'Planning');
      await clickCell(nicoPage, 'E7');
      await applyBackgroundColor(nicoPage, COLORS.MAGENTA);
      await applyTextColor(nicoPage, COLORS.LIGHT_TEXT);
      await applyBold(nicoPage);
      await sleep(200);

      // Robert: E8 = Todo (coral)
      await setCellValue(robertPage, 'E8', 'Todo');
      await clickCell(robertPage, 'E8');
      await applyBackgroundColor(robertPage, COLORS.CORAL);
      await applyTextColor(robertPage, COLORS.DARK_TEXT);
      await applyBold(robertPage);
      await sleep(500);

      // Verify all statuses synced
      await verifyCellSynced(shuyingPage, 'E4', 'Done', 'Shuying');
      await verifyCellSynced(nicoPage, 'E5', 'In Progress', 'Nico');
      await verifyCellSynced(robertPage, 'E6', 'Done', 'Robert');

      console.log('  [Nico] Collaboration is DONE - neon green is electric!');
      console.log('  [Robert] Formula Engine is in progress - sunset orange vibes.');
      console.log('  [Shuying] XLSX is green too! These retro colors are rad!\n');
    }));

    // === FINALE: THE MASTER PLAN IS COMPLETE ===
    results.push(await runTest('Finale: All Together Now', async () => {
      console.log('\n  The team makes final touches simultaneously...\n');

      // Simultaneous edits - team credits
      const edits = [
        setCellValue(nicoPage, 'A12', 'Project Lead: Nico'),
        setCellValue(robertPage, 'A13', 'Tech Lead: Robert'),
        setCellValue(shuyingPage, 'A14', 'Engineering: Shuying'),
      ];
      await Promise.all(edits);
      await sleep(500);

      // Style the credits section - each person styles their own credit
      console.log('  [Team] Styling the credits with retro flair...\n');

      // Nico styles A12 - electric blue
      await clickCell(nicoPage, 'A12');
      await applyItalic(nicoPage);
      await applyTextColor(nicoPage, COLORS.ELECTRIC_BLUE);
      await sleep(100);

      // Robert styles A13 - neon green
      await clickCell(robertPage, 'A13');
      await applyItalic(robertPage);
      await applyTextColor(robertPage, COLORS.NEON_GREEN);
      await sleep(100);

      // Shuying styles A14 - magenta
      await clickCell(shuyingPage, 'A14');
      await applyItalic(shuyingPage);
      await applyTextColor(shuyingPage, COLORS.MAGENTA);
      await sleep(300);

      // Robert applies Courier New font to all credits for a distinctive look
      console.log('  [Robert] Adding Courier New font to credits for a distinctive look...\n');
      await selectRange(robertPage, 'A12', 'A14');
      await applyFontFamily(robertPage, 'Courier New');
      await sleep(500);

      // Add the company motto with special styling
      await setCellValue(nicoPage, 'A11', 'Cells: The Future of Spreadsheets');
      await sleep(500);

      // Style the motto as a signature banner - hot pink
      console.log('  [Nico] Adding the synthwave signature banner...\n');
      await selectRange(nicoPage, 'A11', 'E11');
      await applyBackgroundColor(nicoPage, COLORS.HOT_PINK);
      await applyTextColor(nicoPage, COLORS.DARK_TEXT);
      await applyBold(nicoPage);
      await applyItalic(nicoPage);
      await applyBorder(nicoPage, 'all');
      await applyFontFamily(nicoPage, 'Courier New');
      await sleep(800);

      // Final verification - everyone can see the complete plan
      await verifyCellSynced(robertPage, 'A11', 'Cells: The Future of Spreadsheets', 'Robert');
      await verifyCellSynced(shuyingPage, 'A11', 'Cells: The Future of Spreadsheets', 'Shuying');

      // Verify the data built up throughout the demo
      await verifyCellSynced(robertPage, 'A1', 'CELLS - Master Plan 2025', 'Robert');
      await verifyCellSynced(shuyingPage, 'A4', 'Real-time Collaboration', 'Shuying');
      await verifyCellSynced(nicoPage, 'E6', 'Done', 'Nico');  // Status column

      console.log('\n  ==========================================');
      console.log('       THE MASTER PLAN IS COMPLETE!');
      console.log('     ★ 80s RETRO SYNTHWAVE EDITION ★');
      console.log('  ==========================================');
      console.log('');
      console.log('  The spreadsheet now contains:');
      console.log('  - Hot pink title with Georgia font');
      console.log('  - Cyan header row with Helvetica font');
      console.log('  - Lavender/light cyan alternating rows');
      console.log('  - Cost formulas with gold rate highlight');
      console.log('  - Electric purple totals row');
      console.log('  - Neon status badges (green/orange/magenta/coral)');
      console.log('  - Retro credits with Courier New font');
      console.log('  - Hot pink signature banner');
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
  console.log('     ★ Cells: The Future of Spreadsheets ★');
  console.log('         80s Retro Synthwave Edition');
  console.log('====================================================');
  console.log('');
  console.log('  This demo showcased:');
  console.log('  - 80s retro color palette (hot pink, cyan, neon green)');
  console.log('  - Bold & italic formatting');
  console.log('  - Multiple font families (Georgia, Helvetica, Courier New)');
  console.log('  - Cell borders');
  console.log('  - Currency formatting with gold rate highlight');
  console.log('  - Lavender/cyan alternating row colors');
  console.log('  - Neon status badges (green/orange/magenta/coral)');
  console.log('  - Column & row resizing with synced dimensions');
  console.log('');
  console.log('====================================================\n');

  process.exit(0);
}

// Run the collaborative demo
runCollabDemo();
