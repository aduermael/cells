// Dropdown Frame tests for Cells spreadsheet application
// Tests the DropdownFrame component behavior: mutual exclusivity, outside click, Escape key

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  clickCell,
  createNewWorkbook,
  assertTrue,
  sleep,
} from './helpers.mjs';

/**
 * Check if an element is visible (not hidden)
 * @param {import('puppeteer').Page} page
 * @param {string} selector - CSS selector for the element
 * @returns {Promise<boolean>}
 */
async function isElementVisible(page, selector) {
  return await page.evaluate((selector) => {
    const element = document.querySelector(selector);
    if (!element) return false;
    return !element.classList.contains('hidden') &&
           window.getComputedStyle(element).display !== 'none';
  }, selector);
}

/**
 * Create test dropdowns using DropdownFrame
 * Returns cleanup function to remove the test elements
 * @param {import('puppeteer').Page} page
 * @returns {Promise<void>}
 */
async function setupTestDropdowns(page) {
  await page.evaluate(() => {
    // Create two test buttons and dropdown contents
    const container = document.createElement('div');
    container.id = 'test-dropdown-container';
    container.style.cssText = 'position: fixed; top: 50px; left: 50px; z-index: 9999;';

    const btn1 = document.createElement('button');
    btn1.id = 'test-dropdown-btn-1';
    btn1.textContent = 'Dropdown 1';
    btn1.style.marginRight = '10px';

    const btn2 = document.createElement('button');
    btn2.id = 'test-dropdown-btn-2';
    btn2.textContent = 'Dropdown 2';

    const menu1 = document.createElement('div');
    menu1.id = 'test-dropdown-menu-1';
    menu1.textContent = 'Menu 1 Content';
    menu1.style.cssText = 'padding: 20px; background: white;';

    const menu2 = document.createElement('div');
    menu2.id = 'test-dropdown-menu-2';
    menu2.textContent = 'Menu 2 Content';
    menu2.style.cssText = 'padding: 20px; background: white;';

    container.appendChild(btn1);
    container.appendChild(btn2);
    document.body.appendChild(container);
    document.body.appendChild(menu1);
    document.body.appendChild(menu2);

    // Import and use DropdownFrame
    // Note: The DropdownFrame is bundled into the app, so we access it through the app's modules
    const { DropdownFrame } = window._appModules || {};

    if (DropdownFrame) {
      const frame1 = new DropdownFrame({
        anchor: btn1,
        content: menu1,
        menuId: 'testMenu1',
      });

      const frame2 = new DropdownFrame({
        anchor: btn2,
        content: menu2,
        menuId: 'testMenu2',
      });

      // Store references for tests to access
      window._testDropdownFrame1 = frame1;
      window._testDropdownFrame2 = frame2;

      // Setup click handlers
      btn1.addEventListener('click', (e) => {
        e.stopPropagation();
        frame1.toggle();
      });
      btn2.addEventListener('click', (e) => {
        e.stopPropagation();
        frame2.toggle();
      });
    }
  });
}

/**
 * Cleanup test dropdowns
 * @param {import('puppeteer').Page} page
 */
async function cleanupTestDropdowns(page) {
  await page.evaluate(() => {
    // Destroy the frames
    if (window._testDropdownFrame1) {
      window._testDropdownFrame1.destroy();
      delete window._testDropdownFrame1;
    }
    if (window._testDropdownFrame2) {
      window._testDropdownFrame2.destroy();
      delete window._testDropdownFrame2;
    }

    // Remove DOM elements
    const container = document.getElementById('test-dropdown-container');
    if (container) container.remove();
    const menu1 = document.getElementById('test-dropdown-menu-1');
    if (menu1) menu1.remove();
    const menu2 = document.getElementById('test-dropdown-menu-2');
    if (menu2) menu2.remove();
  });
}

/**
 * Check if DropdownFrame module is available
 * @param {import('puppeteer').Page} page
 * @returns {Promise<boolean>}
 */
async function hasDropdownFrameModule(page) {
  return await page.evaluate(() => {
    return !!(window._appModules && window._appModules.DropdownFrame);
  });
}

const tests = {
  'Opening one DropdownFrame closes others (MenuStateManager integration)': async (ctx) => {
    await ctx.page.setViewport({ width: 800, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    // Check if DropdownFrame module is exposed for testing
    const hasModule = await hasDropdownFrameModule(ctx.page);
    if (!hasModule) {
      // Skip this test if module isn't exposed - we'll test via real UI components
      console.log('DropdownFrame module not exposed for testing, testing via existing dropdowns');

      // Test with existing font family and font size dropdowns (both use MenuStateManager)
      await clickCell(ctx.page, 'A1');
      await sleep(100);

      // Open font family dropdown
      await ctx.page.click('#font-family-btn');
      await sleep(200);

      // Verify font family dropdown is open
      const fontFamilyOpen = await ctx.page.evaluate(() => {
        const dropdown = document.getElementById('font-family-dropdown');
        return dropdown && dropdown.classList.contains('open');
      });
      assertTrue(fontFamilyOpen, 'Font family dropdown should be open');

      // Now open font size dropdown - should close font family
      await ctx.page.click('#font-size-btn');
      await sleep(200);

      // Verify font family dropdown is now closed
      const fontFamilyClosed = await ctx.page.evaluate(() => {
        const dropdown = document.getElementById('font-family-dropdown');
        return dropdown && !dropdown.classList.contains('open');
      });
      assertTrue(fontFamilyClosed, 'Font family dropdown should be closed after opening font size');

      // Verify font size dropdown is open
      const fontSizeOpen = await ctx.page.evaluate(() => {
        const dropdown = document.getElementById('font-size-dropdown');
        return dropdown && dropdown.classList.contains('open');
      });
      assertTrue(fontSizeOpen, 'Font size dropdown should be open');

      return;
    }

    // If module is exposed, use programmatic test
    await setupTestDropdowns(ctx.page);

    try {
      // Open first dropdown
      await ctx.page.click('#test-dropdown-btn-1');
      await sleep(100);

      const menu1Visible = await isElementVisible(ctx.page, '#test-dropdown-menu-1');
      assertTrue(menu1Visible, 'First dropdown menu should be visible');

      // Open second dropdown - should close first
      await ctx.page.click('#test-dropdown-btn-2');
      await sleep(100);

      const menu1Hidden = !await isElementVisible(ctx.page, '#test-dropdown-menu-1');
      const menu2Visible = await isElementVisible(ctx.page, '#test-dropdown-menu-2');

      assertTrue(menu1Hidden, 'First dropdown should be closed when second is opened');
      assertTrue(menu2Visible, 'Second dropdown should be visible');
    } finally {
      await cleanupTestDropdowns(ctx.page);
    }
  },

  'Outside click closes dropdown': async (ctx) => {
    await ctx.page.setViewport({ width: 800, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font family dropdown
    await ctx.page.click('#font-family-btn');
    await sleep(200);

    // Verify it's open
    const isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(isOpen, 'Font family dropdown should be open');

    // Click outside (on the canvas)
    const canvasEl = await ctx.page.$('#grid');
    const box = await canvasEl.boundingBox();
    await ctx.page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    await sleep(200);

    // Verify it's closed
    const isClosed = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && !dropdown.classList.contains('open');
    });
    assertTrue(isClosed, 'Font family dropdown should be closed after outside click');
  },

  'Escape key closes dropdown': async (ctx) => {
    await ctx.page.setViewport({ width: 800, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font family dropdown
    await ctx.page.click('#font-family-btn');
    await sleep(200);

    // Verify it's open
    const isOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(isOpen, 'Font family dropdown should be open');

    // Press Escape
    await ctx.page.keyboard.press('Escape');
    await sleep(200);

    // Verify it's closed
    const isClosed = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && !dropdown.classList.contains('open');
    });
    assertTrue(isClosed, 'Font family dropdown should be closed after Escape key');
  },

  'Opening color picker closes font dropdown': async (ctx) => {
    await ctx.page.setViewport({ width: 800, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font family dropdown
    await ctx.page.click('#font-family-btn');
    await sleep(200);

    // Verify font family is open
    const fontFamilyOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(fontFamilyOpen, 'Font family dropdown should be open');

    // Open background color popup
    await ctx.page.click('#style-bg-color-btn');
    await sleep(200);

    // Verify font family is closed
    const fontFamilyClosed = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-family-dropdown');
      return dropdown && !dropdown.classList.contains('open');
    });
    assertTrue(fontFamilyClosed, 'Font family dropdown should be closed after opening color picker');

    // Verify color picker is open
    const colorPickerOpen = await ctx.page.evaluate(() => {
      const wrapper = document.getElementById('bg-color-wrapper');
      return wrapper && wrapper.classList.contains('open');
    });
    assertTrue(colorPickerOpen, 'Background color picker should be open');
  },

  'Opening border dropdown closes other toolbar dropdowns': async (ctx) => {
    await ctx.page.setViewport({ width: 800, height: 600 });
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);
    await createNewWorkbook(ctx.page);

    await clickCell(ctx.page, 'A1');
    await sleep(100);

    // Open font size dropdown
    await ctx.page.click('#font-size-btn');
    await sleep(200);

    // Verify font size is open
    const fontSizeOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-size-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(fontSizeOpen, 'Font size dropdown should be open');

    // Open border dropdown
    await ctx.page.click('#border-btn');
    await sleep(200);

    // Verify font size is closed
    const fontSizeClosed = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('font-size-dropdown');
      return dropdown && !dropdown.classList.contains('open');
    });
    assertTrue(fontSizeClosed, 'Font size dropdown should be closed after opening border dropdown');

    // Verify border dropdown is open
    const borderOpen = await ctx.page.evaluate(() => {
      const dropdown = document.getElementById('border-dropdown');
      return dropdown && dropdown.classList.contains('open');
    });
    assertTrue(borderOpen, 'Border dropdown should be open');
  },
};

runTests(tests, 'Dropdown Frame');
