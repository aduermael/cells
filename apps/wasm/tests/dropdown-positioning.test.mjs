// Dropdown positioning tests for Cells spreadsheet application
// Tests that dropdowns stay within viewport bounds

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  createNewWorkbook,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Check if an element is within viewport bounds with minimum padding
 * @param {import('puppeteer').Page} page
 * @param {string} selector - CSS selector for the element
 * @param {number} padding - Minimum padding from edges in pixels
 * @returns {Promise<{withinBounds: boolean, details: object}>}
 */
async function checkElementWithinViewport(page, selector, padding = 8) {
  return await page.evaluate(({ selector, padding }) => {
    const element = document.querySelector(selector);
    if (!element) {
      return { withinBounds: false, details: { error: 'Element not found' } };
    }

    const rect = element.getBoundingClientRect();
    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;

    const withinLeft = rect.left >= padding;
    const withinRight = rect.right <= viewportWidth - padding;
    const withinTop = rect.top >= padding;
    const withinBottom = rect.bottom <= viewportHeight - padding;

    return {
      withinBounds: withinLeft && withinRight && withinTop && withinBottom,
      details: {
        rect: { left: rect.left, right: rect.right, top: rect.top, bottom: rect.bottom },
        viewport: { width: viewportWidth, height: viewportHeight },
        padding,
        withinLeft,
        withinRight,
        withinTop,
        withinBottom,
      },
    };
  }, { selector, padding });
}

/**
 * Get dropdown menu bounding rect
 * @param {import('puppeteer').Page} page
 * @param {string} selector
 * @returns {Promise<DOMRect|null>}
 */
async function getElementRect(page, selector) {
  return await page.evaluate((selector) => {
    const element = document.querySelector(selector);
    if (!element) return null;
    const rect = element.getBoundingClientRect();
    return { left: rect.left, right: rect.right, top: rect.top, bottom: rect.bottom, width: rect.width, height: rect.height };
  }, selector);
}

const tests = {
  'Border dropdown stays within viewport bounds when opened near right edge': async (ctx) => {
    // Use a viewport that can fit the border dropdown (which is quite tall with style and color options)
    await ctx.page.setViewport({ width: 600, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    // Click cell to ensure we have a valid selection
    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open border dropdown
    await ctx.page.click('#border-btn');
    await sleep(200);

    // Check that dropdown is within viewport
    const result = await checkElementWithinViewport(ctx.page, '#border-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Border dropdown should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Format dropdown stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown
    await ctx.page.click('#format-dropdown-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#format-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Format dropdown should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Currency dropdown stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // First apply a currency format so the dropdown is active
    await ctx.page.click('#format-dropdown-btn');
    await sleep(100);

    // Select currency format
    const currencyItem = await ctx.page.$('[data-format-category="CURRENCY"]');
    if (currencyItem) {
      await currencyItem.click();
      await sleep(200);
    }

    // Now open the currency dropdown
    await ctx.page.click('#currency-dropdown-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#currency-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Currency dropdown should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Background color popup stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open background color popup (button ID is style-bg-color-btn, popup ID is bg-color-popup)
    await ctx.page.click('#style-bg-color-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#bg-color-popup');
    assertTrue(result.withinBounds,
      `Background color popup should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Text color popup stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open text color popup (button ID is style-text-color-btn, popup ID is text-color-popup)
    await ctx.page.click('#style-text-color-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#text-color-popup');
    assertTrue(result.withinBounds,
      `Text color popup should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Font family dropdown stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font family dropdown
    await ctx.page.click('#font-family-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#font-family-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Font family dropdown should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Font size dropdown stays within viewport bounds': async (ctx) => {
    await ctx.page.setViewport({ width: 600, height: 400 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font size dropdown
    await ctx.page.click('#font-size-btn');
    await sleep(200);

    const result = await checkElementWithinViewport(ctx.page, '#font-size-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Font size dropdown should stay within viewport. Details: ${JSON.stringify(result.details)}`);
  },

  'Dropdown repositions when viewport is narrower than normal': async (ctx) => {
    // Use a narrow viewport to test horizontal repositioning
    // The format dropdown is shorter than the border dropdown, so it fits vertically
    await ctx.page.setViewport({ width: 500, height: 500 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open format dropdown (shorter than border dropdown)
    await ctx.page.click('#format-dropdown-btn');
    await sleep(200);

    // Check that dropdown is visible and within viewport (may be repositioned)
    const result = await checkElementWithinViewport(ctx.page, '#format-dropdown .dropdown-menu');
    assertTrue(result.withinBounds,
      `Format dropdown should reposition to stay within narrow viewport. Details: ${JSON.stringify(result.details)}`);
  },
};

runTests(tests, 'Dropdown Positioning');
