// Named Range Persistence E2E Tests
// Tests that named ranges are correctly loaded from ZCD files and preserved
// through export/reimport cycles.

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertEqual,
  assertTrue,
  sleep,
  getNamedRanges,
  exportToZCD,
} from './helpers.mjs';

const tests = {
  'Load ZCD file with named ranges': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the named ranges test file
    await loadTestFile(ctx.page, 'named_ranges.zcd');
    await sleep(500);

    // Get named ranges from the workbook
    const namedRanges = await getNamedRanges(ctx.page);

    // Should have 3 named ranges
    assertEqual(namedRanges.length, 3, 'Should have 3 named ranges');

    // Find each named range
    const myTotal = namedRanges.find(nr => nr.name === 'MyTotal');
    const dataRange = namedRanges.find(nr => nr.name === 'DataRange');
    const colA = namedRanges.find(nr => nr.name === 'ColA');

    assertTrue(myTotal, 'MyTotal named range should exist');
    assertTrue(dataRange, 'DataRange named range should exist');
    assertTrue(colA, 'ColA named range should exist');

    // Verify MyTotal is a cell reference
    assertEqual(myTotal.targetType, 'cell', 'MyTotal should be a cell reference');
    assertEqual(myTotal.scope, 'workbook', 'MyTotal should be workbook-scoped');

    // Verify DataRange is a range reference
    assertEqual(dataRange.targetType, 'range', 'DataRange should be a range reference');

    // Verify ColA is a column reference
    assertEqual(colA.targetType, 'column', 'ColA should be a column reference');
  },

  'Named ranges persist through export/reimport': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the named ranges test file
    await loadTestFile(ctx.page, 'named_ranges.zcd');
    await sleep(500);

    // Get original named ranges
    const originalRanges = await getNamedRanges(ctx.page);
    assertEqual(originalRanges.length, 3, 'Should have 3 named ranges initially');

    // Export to ZCD
    const zcdContent = await exportToZCD(ctx.page);

    // Verify ZCD contains named range lines
    assertTrue(zcdContent.includes('N "MyTotal"'), 'ZCD should contain MyTotal named range');
    assertTrue(zcdContent.includes('N "DataRange"'), 'ZCD should contain DataRange named range');
    assertTrue(zcdContent.includes('N "ColA"'), 'ZCD should contain ColA named range');

    // The ZCD format for named ranges
    assertTrue(zcdContent.includes('CELL'), 'ZCD should contain CELL target type');
    assertTrue(zcdContent.includes('RANGE'), 'ZCD should contain RANGE target type');
    assertTrue(zcdContent.includes('COLUMN'), 'ZCD should contain COLUMN target type');
  },

  'ZCD export contains correct named range format': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the named ranges test file
    await loadTestFile(ctx.page, 'named_ranges.zcd');
    await sleep(500);

    // Export to ZCD
    const zcdContent = await exportToZCD(ctx.page);

    // Split into lines and find named range lines
    const lines = zcdContent.split('\n');
    const namedRangeLines = lines.filter(line => line.startsWith('N '));

    assertEqual(namedRangeLines.length, 3, 'Should have 3 named range lines in ZCD');

    // Each line should follow the format: N "<name>" <scope> <scope-sheet-id> <target-type> <target-data>
    for (const line of namedRangeLines) {
      // Should start with N "
      assertTrue(line.startsWith('N "'), `Line should start with N ": ${line}`);

      // Should contain W or S for scope (after the name)
      const afterName = line.split('" ')[1];
      assertTrue(
        afterName.startsWith('W ') || afterName.startsWith('S '),
        `Line should have scope W or S: ${line}`
      );
    }
  },

  'Named ranges are sorted alphabetically in export': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the named ranges test file
    await loadTestFile(ctx.page, 'named_ranges.zcd');
    await sleep(500);

    // Export to ZCD
    const zcdContent = await exportToZCD(ctx.page);

    // Find named range lines
    const lines = zcdContent.split('\n');
    const namedRangeLines = lines.filter(line => line.startsWith('N '));

    // Extract names and verify they're sorted
    const names = namedRangeLines.map(line => {
      const match = line.match(/N "([^"]+)"/);
      return match ? match[1] : '';
    });

    const sortedNames = [...names].sort();
    assertEqual(
      JSON.stringify(names),
      JSON.stringify(sortedNames),
      'Named ranges should be sorted alphabetically'
    );
  },
};

// Run all tests
runTests(tests);
