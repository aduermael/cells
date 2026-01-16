// Border UI controls tests for Cells spreadsheet application
// Tests the border dropdown and border application functionality

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  selectRange,
  createNewWorkbook,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the effective style for a cell at a given position
 * @param {import('puppeteer').Page} page
 * @param {number} col
 * @param {number} row
 * @returns {Promise<object>}
 */
async function getCellStyle(page, col, row) {
  return await page.evaluate(async ({ col, row }) => {
    const ctx = window._appContext;
    if (!ctx || !ctx.app || !ctx.app.dataSource) {
      return null;
    }
    try {
      const style = await ctx.app.dataSource.client.getEffectiveCellStyle(col, row);
      return style;
    } catch (e) {
      console.error('Error getting cell style:', e);
      return null;
    }
  }, { col, row });
}

/**
 * Apply a border to the current selection using the toolbar dropdown
 * @param {import('puppeteer').Page} page
 * @param {'all' | 'outer' | 'top' | 'bottom' | 'left' | 'right' | 'none'} borderType
 */
async function applyBorder(page, borderType) {
  // Click the border button to open the dropdown
  await page.click('#border-btn');
  await sleep(100);

  // Click the appropriate border option
  const buttonId = `#border-${borderType}-btn`;
  await page.click(buttonId);
  await sleep(200);
}

const tests = {
  'Border button and dropdown exist in toolbar': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Check border button exists
    const borderBtn = await ctx.page.$('#border-btn');
    assertTrue(borderBtn !== null, 'Border button should exist in toolbar');

    // Open dropdown
    await borderBtn.click();
    await sleep(100);

    // Check dropdown options exist
    const borderAllBtn = await ctx.page.$('#border-all-btn');
    const borderOuterBtn = await ctx.page.$('#border-outer-btn');
    const borderTopBtn = await ctx.page.$('#border-top-btn');
    const borderBottomBtn = await ctx.page.$('#border-bottom-btn');
    const borderLeftBtn = await ctx.page.$('#border-left-btn');
    const borderRightBtn = await ctx.page.$('#border-right-btn');
    const borderNoneBtn = await ctx.page.$('#border-none-btn');

    assertTrue(borderAllBtn !== null, 'All Borders button should exist');
    assertTrue(borderOuterBtn !== null, 'Outline button should exist');
    assertTrue(borderTopBtn !== null, 'Top Border button should exist');
    assertTrue(borderBottomBtn !== null, 'Bottom Border button should exist');
    assertTrue(borderLeftBtn !== null, 'Left Border button should exist');
    assertTrue(borderRightBtn !== null, 'Right Border button should exist');
    assertTrue(borderNoneBtn !== null, 'No Border button should exist');
  },

  'Border dropdown closes when clicking outside': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Open the border dropdown
    await ctx.page.click('#border-btn');
    await sleep(100);

    // Check that dropdown is open (has 'open' class)
    let isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('border-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(isOpen, 'Dropdown should be open after clicking button');

    // Click on the canvas (outside the dropdown)
    await clickCell(ctx.page, 'E5');
    await sleep(200);

    // Check that dropdown is closed
    isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('border-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(!isOpen, 'Dropdown should be closed after clicking outside');
  },

  'Apply all borders to single cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Apply all borders
    await applyBorder(ctx.page, 'all');
    await sleep(300);

    // Check that the cell has borders
    const style = await getCellStyle(ctx.page, 1, 1); // B2 = col 1, row 1
    console.log('Cell B2 style:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'Cell style should be returned');
    assertTrue(style.border !== undefined, 'Cell should have border property');
    assertTrue(style.border.top !== undefined, 'Cell should have top border');
    assertTrue(style.border.bottom !== undefined, 'Cell should have bottom border');
    assertTrue(style.border.left !== undefined, 'Cell should have left border');
    assertTrue(style.border.right !== undefined, 'Cell should have right border');
  },

  'Apply top border to single cell': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell C3
    await clickCell(ctx.page, 'C3');
    await sleep(100);

    // Apply top border
    await applyBorder(ctx.page, 'top');
    await sleep(300);

    // Check that the cell has top border
    const style = await getCellStyle(ctx.page, 2, 2); // C3 = col 2, row 2
    console.log('Cell C3 style:', JSON.stringify(style, null, 2));

    assertTrue(style !== null, 'Cell style should be returned');
    assertTrue(style.border !== undefined, 'Cell should have border property');
    assertTrue(style.border.top !== undefined && style.border.top.style === 'thin',
      'Cell should have thin top border');
  },

  'Apply borders to range selection': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select range A1:C3
    await selectRange(ctx.page, 'A1', 'C3');
    await sleep(100);

    // Apply outline border
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // Check corner cells have correct borders
    // A1 should have top and left borders
    const styleA1 = await getCellStyle(ctx.page, 0, 0);
    console.log('Cell A1 style:', JSON.stringify(styleA1, null, 2));

    // C3 should have bottom and right borders
    const styleC3 = await getCellStyle(ctx.page, 2, 2);
    console.log('Cell C3 style:', JSON.stringify(styleC3, null, 2));

    // B2 (center cell) should have no borders from outline operation
    const styleB2 = await getCellStyle(ctx.page, 1, 1);
    console.log('Cell B2 style:', JSON.stringify(styleB2, null, 2));

    assertTrue(styleA1 !== null, 'A1 style should be returned');
    assertTrue(styleC3 !== null, 'C3 style should be returned');
  },

  'Remove borders with No Border option': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Select cell B2
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // First apply all borders
    await applyBorder(ctx.page, 'all');
    await sleep(300);

    // Verify borders were applied
    let style = await getCellStyle(ctx.page, 1, 1);
    assertTrue(style.border !== undefined && style.border.top !== undefined,
      'Cell should have borders after applying all borders');

    // Now remove borders
    await applyBorder(ctx.page, 'none');
    await sleep(300);

    // Verify borders were removed
    style = await getCellStyle(ctx.page, 1, 1);
    console.log('Cell B2 style after removing borders:', JSON.stringify(style, null, 2));

    // After removing, the border property should either not exist or have no style
    const hasBorders = style.border &&
      (style.border.top?.style === 'thin' ||
       style.border.bottom?.style === 'thin' ||
       style.border.left?.style === 'thin' ||
       style.border.right?.style === 'thin');
    assertTrue(!hasBorders, 'Cell should have no borders after applying No Border');
  },
  // Border + Bold bug test: After applying OUTLINE border to a range, bold should still work
  // Bug: when setting an OUTLINE border for a range, then clicking "bold" for the same range,
  // the bold button becomes disabled (shows mixed state) and not all cells get bold styling.
  // Root cause: Outline border only applies borders to edge cells, creating mixed styles,
  // which causes getEffectiveStyleForRange to return mixed=true for border property,
  // incorrectly affecting the bold button state.
  'Outline border then bold on same range applies bold to all cells': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Step 1: Select range B2:D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);

    // Step 2: Apply OUTLINE border (not "all borders") - this only styles edge cells
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // Verify outline borders were applied to edge cells
    const styleB2 = await getCellStyle(ctx.page, 1, 1); // B2 - top-left corner
    console.log('B2 style after outline border:', JSON.stringify(styleB2, null, 2));
    assertTrue(
      styleB2 !== null && styleB2.border !== undefined,
      'B2 (corner) should have border after applying outline'
    );

    // Interior cell C3 should NOT have borders (outline only applies to edges)
    const styleC3Interior = await getCellStyle(ctx.page, 2, 2); // C3 - interior cell
    console.log('C3 (interior) style after outline border:', JSON.stringify(styleC3Interior, null, 2));

    // Bug scenario: DON'T re-select, immediately check bold button and click it
    // (keeping same selection from border operation)

    // Check bold button state - it should NOT be disabled/mixed just because
    // border property is mixed (bold is a DIFFERENT property)
    const boldBtnStateBefore = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      if (!btn) return null;
      return {
        disabled: btn.disabled,
        className: btn.className,
      };
    });
    console.log('Bold button state before click:', JSON.stringify(boldBtnStateBefore, null, 2));

    // The bold button should be clickable (not disabled)
    assertTrue(
      boldBtnStateBefore !== null && boldBtnStateBefore.disabled === false,
      `Bold button should NOT be disabled when range is selected (got disabled=${boldBtnStateBefore?.disabled})`
    );

    // Click bold button
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Step 4: Verify ALL cells in B2:D4 now have bold styling
    // Check each cell in the 3x3 range (cols 1-3, rows 1-3)
    const cellsToCheck = [
      { col: 1, row: 1, name: 'B2' },
      { col: 2, row: 1, name: 'C2' },
      { col: 3, row: 1, name: 'D2' },
      { col: 1, row: 2, name: 'B3' },
      { col: 2, row: 2, name: 'C3' },
      { col: 3, row: 2, name: 'D3' },
      { col: 1, row: 3, name: 'B4' },
      { col: 2, row: 3, name: 'C4' },
      { col: 3, row: 3, name: 'D4' },
    ];

    let boldCount = 0;
    for (const cell of cellsToCheck) {
      const style = await getCellStyle(ctx.page, cell.col, cell.row);
      console.log(`${cell.name} style:`, JSON.stringify(style, null, 2));
      if (style && style.bold === true) {
        boldCount++;
      }
    }

    console.log(`Bold cells: ${boldCount} / ${cellsToCheck.length}`);
    assertEqual(
      boldCount,
      cellsToCheck.length,
      `All ${cellsToCheck.length} cells should be bold, but only ${boldCount} are bold`
    );

    // Step 5: Verify edge cell B2 still has border (styles should coexist)
    const styleAfterBold = await getCellStyle(ctx.page, 1, 1); // B2
    console.log('B2 style after bold:', JSON.stringify(styleAfterBold, null, 2));
    assertTrue(
      styleAfterBold !== null && styleAfterBold.border !== undefined,
      'B2 should still have border after applying bold (different properties should layer)'
    );
    assertTrue(
      styleAfterBold !== null && styleAfterBold.bold === true,
      'B2 should have bold after applying bold'
    );
  },

  // Test that edge cells with cell-level borders render bold text when range has bold
  'Edge cell with border renders bold text from range style': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Step 1: Apply outline border to B2:D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // Step 2: Apply bold to the same range
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Step 3: Enter text in B2 (edge cell with border)
    await clickCell(ctx.page, 'B2');
    await sleep(100);
    await ctx.page.keyboard.type('Test');
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Step 4: Check that B2's viewport data includes bold=true
    // The viewport data is what the renderer uses
    const viewportData = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) {
        return null;
      }
      // Get viewport data for the visible area using queryViewport
      const viewportResult = await ctx.app.dataSource.client.queryViewport(0, 0, 10, 10);
      return viewportResult;
    });

    // Find B2 in the viewport data
    const b2Cell = viewportData?.cells?.find(c => c.col === 1 && c.row === 1);
    console.log('B2 viewport cell:', JSON.stringify(b2Cell, null, 2));

    assertTrue(
      b2Cell !== null && b2Cell !== undefined,
      'B2 should be in viewport data'
    );
    assertTrue(
      b2Cell.style !== undefined && b2Cell.style.bold === true,
      `B2 should have bold=true in viewport style (got: ${JSON.stringify(b2Cell?.style)})`
    );

    // Verify border is also present
    assertTrue(
      b2Cell.style.border !== undefined,
      'B2 should also have border in viewport style'
    );
  },

  // Test toggling bold OFF after outline border + bold applied
  'Toggle bold off removes bold from all cells in range': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Step 1: Apply outline border to B2:D4
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // Step 2: Apply bold to the same range
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Verify bold was applied
    const styleAfterBold = await getCellStyle(ctx.page, 1, 1); // B2
    console.log('B2 after bold ON:', JSON.stringify(styleAfterBold, null, 2));
    assertTrue(
      styleAfterBold !== null && styleAfterBold.bold === true,
      'B2 should have bold=true after applying bold'
    );

    // Step 3: Toggle bold OFF by clicking bold button again
    // Need to ensure the range is still selected
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);

    // Verify bold button is active (showing current state)
    const boldBtnBeforeToggle = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      return btn ? btn.classList.contains('active') : null;
    });
    console.log('Bold button active before toggle:', boldBtnBeforeToggle);

    // Check the effective style for the range before toggle
    const effectiveStyleBefore = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return null;
      return await ctx.app.dataSource.client.getEffectiveStyleForRange(1, 1, 3, 3);
    });
    console.log('Effective style for range before toggle:', JSON.stringify(effectiveStyleBefore, null, 2));

    // Click bold to toggle it OFF (via UI button)
    console.log('Clicking bold button to toggle OFF...');
    await ctx.page.click('#style-bold-btn');
    await sleep(300);

    // Check the effective style for the range after toggle
    const effectiveStyleAfter = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) return null;
      return await ctx.app.dataSource.client.getEffectiveStyleForRange(1, 1, 3, 3);
    });
    console.log('Effective style for range after toggle:', JSON.stringify(effectiveStyleAfter, null, 2));

    // Step 4: Verify ALL cells no longer have bold
    const cellsToCheck = [
      { col: 1, row: 1, name: 'B2' },
      { col: 2, row: 1, name: 'C2' },
      { col: 3, row: 1, name: 'D2' },
      { col: 1, row: 2, name: 'B3' },
      { col: 2, row: 2, name: 'C3' },
      { col: 3, row: 2, name: 'D3' },
      { col: 1, row: 3, name: 'B4' },
      { col: 2, row: 3, name: 'C4' },
      { col: 3, row: 3, name: 'D4' },
    ];

    let nonBoldCount = 0;
    for (const cell of cellsToCheck) {
      const style = await getCellStyle(ctx.page, cell.col, cell.row);
      console.log(`${cell.name} after toggle OFF:`, JSON.stringify(style, null, 2));
      if (!style || style.bold !== true) {
        nonBoldCount++;
      }
    }

    console.log(`Non-bold cells: ${nonBoldCount} / ${cellsToCheck.length}`);
    assertEqual(
      nonBoldCount,
      cellsToCheck.length,
      `All ${cellsToCheck.length} cells should NOT be bold after toggle, but ${cellsToCheck.length - nonBoldCount} still are`
    );

    // Verify borders are still present on B2
    const styleAfterToggle = await getCellStyle(ctx.page, 1, 1); // B2
    assertTrue(
      styleAfterToggle !== null && styleAfterToggle.border !== undefined,
      'B2 should still have border after toggling bold off'
    );
  },

  // Additional test: Bold button state after OUTLINE border application
  'Bold button not disabled after outline border application': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);
    await sleep(200);

    // Apply OUTLINE border to B2:D4 (creates mixed border styles - edge vs interior)
    await selectRange(ctx.page, 'B2', 'D4');
    await sleep(100);
    await applyBorder(ctx.page, 'outer');
    await sleep(300);

    // WITHOUT re-selecting, check bold button state
    // The selection should still be B2:D4
    const boldBtnState = await ctx.page.evaluate(() => {
      const btn = document.querySelector('#style-bold-btn');
      if (!btn) return null;
      return {
        disabled: btn.disabled,
        className: btn.className,
        ariaDisabled: btn.getAttribute('aria-disabled'),
      };
    });

    console.log('Bold button state after outline border:', JSON.stringify(boldBtnState, null, 2));

    assertTrue(
      boldBtnState !== null,
      'Bold button should exist'
    );
    // Bold button should NOT be disabled just because border is mixed
    // Bold is a different property and all cells have bold=false (consistent)
    assertTrue(
      boldBtnState.disabled === false,
      `Bold button should not be disabled after outline border (disabled=${boldBtnState.disabled})`
    );
    // Bold button should NOT show "mixed" class - bold is uniformly false
    assertTrue(
      !boldBtnState.className.includes('mixed'),
      `Bold button should not show mixed state after outline border (class=${boldBtnState.className})`
    );
  },
};

// Run all tests
runTests(tests);
