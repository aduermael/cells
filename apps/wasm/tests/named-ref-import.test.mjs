// Named Reference Dependency Import E2E Tests
// Tests that named references are correctly resolved in the dependency graph
// during XLSX import, ensuring no #REF! errors due to evaluation order issues.

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertEqual,
  assertTrue,
  sleep,
  clickCell,
  getCellDisplayValue,
  getFormulaBarContent,
} from './helpers.mjs';

const tests = {
  'XLSX import with named references has no #REF! errors': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the XLSX file that previously had #REF! errors due to named reference
    // dependencies not being tracked in the dependency graph
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000); // Give extra time for XLSX parsing and dependency resolution

    // Get all cells from the app and check for error types
    const errorInfo = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.cells) {
        return { error: 'No app context', cells: [] };
      }

      const cells = ctx.app.cells;
      const errorCells = [];

      for (const cell of cells) {
        // Check for error type cells (type === 'e')
        if (cell.type === 'e') {
          errorCells.push({
            col: cell.col,
            row: cell.row,
            display: cell.display || cell.value,
            formula: cell.formula,
          });
        }
        // Also check display value for error strings
        const display = (cell.display || cell.value || '').toString();
        if (display.startsWith('#') && display.endsWith('!')) {
          // Avoid duplicates - only add if not already in errorCells
          const exists = errorCells.some(e => e.col === cell.col && e.row === cell.row);
          if (!exists) {
            errorCells.push({
              col: cell.col,
              row: cell.row,
              display: display,
              formula: cell.formula,
            });
          }
        }
      }

      return {
        totalCells: cells.length,
        errorCells,
      };
    });

    // Filter to only #REF! errors (the specific issue we're fixing)
    const refErrors = errorInfo.errorCells.filter(e =>
      e.display && e.display.includes('#REF!')
    );

    // Log for debugging if there are errors
    if (refErrors.length > 0) {
      console.log('Found #REF! errors:');
      for (const err of refErrors.slice(0, 10)) { // Show first 10
        const colLetter = String.fromCharCode(65 + err.col);
        console.log(`  ${colLetter}${err.row + 1}: ${err.display} (formula: ${err.formula || 'N/A'})`);
      }
      if (refErrors.length > 10) {
        console.log(`  ... and ${refErrors.length - 10} more`);
      }
    }

    assertEqual(refErrors.length, 0,
      `Should have no #REF! errors after import. Found ${refErrors.length} errors.`);
  },

  'Cell K8 evaluates correctly on initial load': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the XLSX file
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Click on K8 to ensure viewport includes it
    await clickCell(ctx.page, 'K8');
    await sleep(200);

    // Get the display value of K8
    const k8Value = await getCellDisplayValue(ctx.page, 'K8');

    // K8 should NOT be a #REF! error
    assertTrue(
      k8Value !== '#REF!' && !k8Value?.includes('#REF!'),
      `K8 should not show #REF! error on initial load. Got: ${k8Value}`
    );

    // Verify K8 has some reasonable value (not empty, not an error)
    assertTrue(
      k8Value !== null && k8Value !== undefined && k8Value !== '',
      `K8 should have a computed value. Got: ${k8Value}`
    );

    // Log the value for verification
    console.log(`K8 value on initial load: ${k8Value}`);
  },

  'Named reference formulas evaluate without manual recalc': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the XLSX file
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1000);

    // Check multiple cells that likely use named references
    // These cells previously showed #REF! until manual recalculation
    const cellsToCheck = ['K8', 'L8', 'M8', 'K9', 'L9', 'M9'];

    for (const cellRef of cellsToCheck) {
      await clickCell(ctx.page, cellRef);
      await sleep(100);

      const value = await getCellDisplayValue(ctx.page, cellRef);
      const formula = await getFormulaBarContent(ctx.page);

      // Should not show #REF! error
      assertTrue(
        !value?.includes('#REF!'),
        `${cellRef} should not show #REF! error. Got: ${value} (formula: ${formula})`
      );
    }
  },
};

// Run all tests
runTests(tests);
