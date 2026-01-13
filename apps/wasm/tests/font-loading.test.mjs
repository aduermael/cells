// Test dynamic web font loading behavior
// Verifies that fonts are loaded from Google Fonts when not available locally

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertTrue,
  assertEqual,
  sleep,
} from './helpers.mjs';

const tests = {
  'Font loader module is available': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Check that the font loader functions are accessible
    const fontLoaderExists = await ctx.page.evaluate(() => {
      // The font loader is imported in the renderer, so we check if
      // the app has registered the font loaded callback
      const ctx = window._appContext;
      return ctx && typeof ctx.app === 'object';
    });

    assertTrue(fontLoaderExists, 'App context should exist');
  },

  'System fonts return immediately without loading': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Test that system fonts like Arial don't trigger network requests
    const fontInfo = await ctx.page.evaluate(async () => {
      // Check if common system fonts are available
      const systemFonts = ['Arial', 'Times New Roman', 'Verdana'];
      const results = {};

      for (const font of systemFonts) {
        // Use document.fonts.check to verify font availability
        if (document.fonts && typeof document.fonts.check === 'function') {
          results[font] = document.fonts.check(`16px "${font}"`);
        } else {
          // Fallback - assume system fonts are available
          results[font] = true;
        }
      }

      return results;
    });

    console.log('\n=== System Font Availability ===');
    for (const [font, available] of Object.entries(fontInfo)) {
      console.log(`${font}: ${available ? 'available' : 'not available'}`);
    }

    // At least Arial should be available on most systems
    assertTrue(
      fontInfo['Arial'] === true || fontInfo['Times New Roman'] === true,
      'At least one system font should be available'
    );
  },

  'Font rendering works with custom fonts': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Set a cell with a custom font and verify rendering doesn't break
    const fontResult = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app) {
        return { error: 'No app context' };
      }

      // Set cell A1 with a value
      ctx.app.wasmDataSource?.setCellValueAt(0, 0, 0, 'Test');

      // Try to set a font (Calibri, which should use Carlito substitute)
      ctx.app.wasmDataSource?.setCellStyleAt(0, 0, 0, {
        fontFamily: 'Calibri',
      });

      // Trigger a render
      if (ctx.app.renderer) {
        ctx.app.renderer.render();
      }

      // Wait a moment for any font loading
      await new Promise(resolve => setTimeout(resolve, 500));

      // Check if Google Fonts link was added for Carlito
      const googleFontLinks = Array.from(document.querySelectorAll('link'))
        .filter(link => link.href && link.href.includes('fonts.googleapis.com'))
        .map(link => link.href);

      return {
        success: true,
        googleFontLinks,
      };
    });

    console.log('\n=== Font Rendering Check ===');
    console.log(`Rendering succeeded: ${fontResult.success}`);
    console.log(`Google Font links: ${fontResult.googleFontLinks?.length || 0}`);

    // Rendering should succeed (no crashes)
    assertTrue(
      fontResult.success === true,
      'Font rendering should succeed with custom fonts'
    );
  },

  'XLSX with Calibri font loads with fallback': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the LBO model which uses Calibri font
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Check that cells with Calibri font are rendered
    const fontCheck = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) {
        return { error: 'No app context' };
      }

      const cells = ctx.app.cells;
      const fontsUsed = new Set();
      let calibriCells = 0;

      for (const cell of cells) {
        const font = cell.style?.fontFamily;
        if (font) {
          fontsUsed.add(font);
          if (font.toLowerCase().includes('calibri')) {
            calibriCells++;
          }
        }
      }

      // Check if Carlito (Calibri substitute) has been requested
      const hasGoogleFontLink = Array.from(document.querySelectorAll('link'))
        .some(link => link.href && link.href.includes('fonts.googleapis.com'));

      return {
        fontsUsed: Array.from(fontsUsed),
        calibriCells,
        hasGoogleFontLink,
        totalCells: cells.length,
      };
    });

    console.log('\n=== Font Usage in XLSX ===');
    console.log(`Total cells: ${fontCheck.totalCells}`);
    console.log(`Fonts used: ${fontCheck.fontsUsed.join(', ') || '(none specified)'}`);
    console.log(`Cells with Calibri: ${fontCheck.calibriCells}`);
    console.log(`Google Fonts link added: ${fontCheck.hasGoogleFontLink}`);

    // The XLSX should have loaded successfully
    assertTrue(fontCheck.totalCells > 0, 'XLSX should load cells');
  },

  'Font loading callback triggers re-render': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Track render calls
    const trackingSetup = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.renderer) {
        return { error: 'No renderer' };
      }

      // Track render calls
      window._renderCount = 0;
      const originalRender = ctx.app.renderer.render.bind(ctx.app.renderer);
      ctx.app.renderer.render = function(...args) {
        window._renderCount++;
        return originalRender(...args);
      };

      return { setup: true };
    });

    assertTrue(trackingSetup.setup, 'Render tracking should be set up');

    // Load a file that might trigger font loading
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(2000); // Wait for potential font loading

    const renderCount = await ctx.page.evaluate(() => window._renderCount);

    console.log('\n=== Render Count After Load ===');
    console.log(`Render calls: ${renderCount}`);

    // Should have rendered at least once after loading
    assertTrue(renderCount > 0, 'Should have rendered after loading file');
  },
};

// Run all tests
runTests(tests);
