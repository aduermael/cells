// Alignment UI tests
// Tests that alignment buttons reflect the correct state:
// - No explicit alignment: no button active (GENERAL alignment)
// - Explicit alignment: corresponding button active

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  setCellValue,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Get the active state of alignment buttons
 */
async function getAlignmentButtonStates(page) {
  return await page.evaluate(() => {
    const leftBtn = document.querySelector('#align-left-btn');
    const centerBtn = document.querySelector('#align-center-btn');
    const rightBtn = document.querySelector('#align-right-btn');
    const topBtn = document.querySelector('#valign-top-btn');
    const middleBtn = document.querySelector('#valign-middle-btn');
    const bottomBtn = document.querySelector('#valign-bottom-btn');
    return {
      hAlign: {
        left: leftBtn?.classList.contains('active') ?? false,
        center: centerBtn?.classList.contains('active') ?? false,
        right: rightBtn?.classList.contains('active') ?? false,
      },
      vAlign: {
        top: topBtn?.classList.contains('active') ?? false,
        middle: middleBtn?.classList.contains('active') ?? false,
        bottom: bottomBtn?.classList.contains('active') ?? false,
      },
    };
  });
}

/**
 * Click an alignment button
 */
async function clickAlignButton(page, alignType) {
  const selectors = {
    left: '#align-left-btn',
    center: '#align-center-btn',
    right: '#align-right-btn',
    top: '#valign-top-btn',
    middle: '#valign-middle-btn',
    bottom: '#valign-bottom-btn',
  };
  await page.click(selectors[alignType]);
  await sleep(200);
}

const tests = {
  'Empty cell with no style: no alignment button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Click on an empty cell (A1)
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states for empty cell:', JSON.stringify(states, null, 2));

    // No horizontal alignment button should be active
    assertTrue(
      !states.hAlign.left && !states.hAlign.center && !states.hAlign.right,
      'No horizontal alignment button should be active for empty cell'
    );

    // No vertical alignment button should be active
    assertTrue(
      !states.vAlign.top && !states.vAlign.middle && !states.vAlign.bottom,
      'No vertical alignment button should be active for empty cell'
    );
  },

  'Number cell with no explicit alignment: no alignment button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number in A1
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states for number cell:', JSON.stringify(states, null, 2));

    // Numbers render right-aligned via GENERAL alignment, but no button should be active
    // because no explicit alignment has been set
    assertTrue(
      !states.hAlign.left && !states.hAlign.center && !states.hAlign.right,
      'No horizontal alignment button should be active for number with GENERAL alignment'
    );
  },

  'Text cell with no explicit alignment: no alignment button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text in A1
    await setCellValue(ctx.page, 'A1', 'Hello');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states for text cell:', JSON.stringify(states, null, 2));

    // Text renders left-aligned via GENERAL alignment, but no button should be active
    // because no explicit alignment has been set
    assertTrue(
      !states.hAlign.left && !states.hAlign.center && !states.hAlign.right,
      'No horizontal alignment button should be active for text with GENERAL alignment'
    );
  },

  'Cell with explicit left alignment: left button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number in A1
    await setCellValue(ctx.page, 'A1', '42');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Apply left alignment
    await clickAlignButton(ctx.page, 'left');

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states after explicit left alignment:', JSON.stringify(states, null, 2));

    // Left button should now be active
    assertTrue(states.hAlign.left, 'Left alignment button should be active');
    assertTrue(!states.hAlign.center, 'Center alignment button should not be active');
    assertTrue(!states.hAlign.right, 'Right alignment button should not be active');
  },

  'Cell with explicit center alignment: center button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text in A1
    await setCellValue(ctx.page, 'A1', 'Hello');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Apply center alignment
    await clickAlignButton(ctx.page, 'center');

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states after explicit center alignment:', JSON.stringify(states, null, 2));

    // Center button should now be active
    assertTrue(!states.hAlign.left, 'Left alignment button should not be active');
    assertTrue(states.hAlign.center, 'Center alignment button should be active');
    assertTrue(!states.hAlign.right, 'Right alignment button should not be active');
  },

  'Cell with explicit right alignment: right button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text in A1
    await setCellValue(ctx.page, 'A1', 'Hello');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Apply right alignment
    await clickAlignButton(ctx.page, 'right');

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states after explicit right alignment:', JSON.stringify(states, null, 2));

    // Right button should now be active
    assertTrue(!states.hAlign.left, 'Left alignment button should not be active');
    assertTrue(!states.hAlign.center, 'Center alignment button should not be active');
    assertTrue(states.hAlign.right, 'Right alignment button should be active');
  },

  'Cell with explicit vertical alignment: corresponding button active': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter text in A1
    await setCellValue(ctx.page, 'A1', 'Hello');
    await sleep(100);

    // Click on A1 to select it
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Apply top vertical alignment
    await clickAlignButton(ctx.page, 'top');

    // Get alignment button states
    const states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states after explicit top alignment:', JSON.stringify(states, null, 2));

    // Top button should now be active
    assertTrue(states.vAlign.top, 'Top alignment button should be active');
    assertTrue(!states.vAlign.middle, 'Middle alignment button should not be active');
    assertTrue(!states.vAlign.bottom, 'Bottom alignment button should not be active');
  },

  'Alignment persists after navigating away and back': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Enter a number in A1
    await setCellValue(ctx.page, 'A1', '100');
    await sleep(100);

    // Click on A1 and apply center alignment
    await clickCell(ctx.page, 'A1');
    await sleep(100);
    await clickAlignButton(ctx.page, 'center');

    // Navigate to another cell
    await clickCell(ctx.page, 'B2');
    await sleep(100);

    // Check that no alignment is active on B2 (empty cell)
    let states = await getAlignmentButtonStates(ctx.page);
    assertTrue(
      !states.hAlign.left && !states.hAlign.center && !states.hAlign.right,
      'No alignment button should be active for empty B2'
    );

    // Navigate back to A1
    await clickCell(ctx.page, 'A1');
    await sleep(200);

    // Check that center alignment is still active
    states = await getAlignmentButtonStates(ctx.page);
    console.log('Button states after returning to A1:', JSON.stringify(states, null, 2));

    assertTrue(states.hAlign.center, 'Center alignment should persist after navigating back');
  },
};

// Run all tests
runTests(tests);
