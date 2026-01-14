// XLSX Export Round-Trip Test
// Validates that XLSX files can be exported and remain valid ZIP archives

import { runTests } from './harness.mjs';
import {
  waitForAppReady,
  loadTestFile,
  assertEqual,
  assertTrue,
  sleep,
} from './helpers.mjs';
import { writeFile, unlink, mkdir } from 'fs/promises';
import { execSync } from 'child_process';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, mkdirSync } from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const tmpDir = join(__dirname, '..', '..', '..', 'tmp');

// Ensure tmp directory exists
if (!existsSync(tmpDir)) {
  mkdirSync(tmpDir, { recursive: true });
}

const tests = {
  // Basic XLSX export validation
  'Export XLSX produces valid ZIP archive': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Create some test data
    await ctx.page.evaluate(() => {
      const canvas = document.getElementById('grid');
      canvas.focus();
    });
    await sleep(200);

    // Enter data in A1
    await ctx.page.keyboard.type('Test Value');
    await ctx.page.keyboard.press('Tab');
    await ctx.page.keyboard.type('123');
    await ctx.page.keyboard.press('Enter');
    await sleep(200);

    // Export via the data source API
    const exportResult = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) {
        return { error: 'No data source' };
      }

      try {
        const result = await ctx.app.dataSource.client.exportXLSX();
        if (result.error) {
          return { error: result.error };
        }

        // Convert ArrayBuffer to base64 for transfer
        const bytes = new Uint8Array(result.data);
        let binary = '';
        for (let i = 0; i < bytes.length; i++) {
          binary += String.fromCharCode(bytes[i]);
        }
        return {
          base64: btoa(binary),
          size: bytes.length,
          // Sample first and last bytes for debugging
          firstBytes: Array.from(bytes.slice(0, 20)),
          lastBytes: Array.from(bytes.slice(-20)),
        };
      } catch (err) {
        return { error: err.message };
      }
    });

    console.log('Export result:', {
      size: exportResult.size,
      error: exportResult.error,
      firstBytes: exportResult.firstBytes,
      lastBytes: exportResult.lastBytes,
    });

    assertTrue(!exportResult.error, `Export should succeed: ${exportResult.error}`);
    assertTrue(exportResult.size > 0, 'Export should produce data');

    // ZIP files start with PK (0x50 0x4B)
    assertEqual(exportResult.firstBytes[0], 0x50, 'Should start with P (0x50)');
    assertEqual(exportResult.firstBytes[1], 0x4B, 'Should start with K (0x4B)');

    // Write to temp file and validate with unzip
    const tmpPath = join(tmpDir, 'export-test-simple.xlsx');
    const buffer = Buffer.from(exportResult.base64, 'base64');
    await writeFile(tmpPath, buffer);

    console.log(`Wrote ${buffer.length} bytes to ${tmpPath}`);

    // Validate ZIP structure
    try {
      const output = execSync(`unzip -t "${tmpPath}" 2>&1`).toString();
      console.log('unzip -t output:', output);
      assertTrue(output.includes('No errors'), 'ZIP should be valid');
    } catch (err) {
      console.error('unzip error:', err.message);
      console.error('stderr:', err.stderr?.toString());

      // Show hex dump of problematic areas
      const hexHead = execSync(`xxd "${tmpPath}" | head -5`).toString();
      const hexTail = execSync(`xxd "${tmpPath}" | tail -5`).toString();
      console.log('Hex head:', hexHead);
      console.log('Hex tail:', hexTail);

      throw new Error(`ZIP validation failed: ${err.message}`);
    }

    // Cleanup
    await unlink(tmpPath);
  },

  // Round-trip test with LBO model
  'XLSX round-trip preserves valid ZIP structure': async (ctx) => {
    await ctx.page.goto(ctx.baseUrl);
    await waitForAppReady(ctx.page);

    // Load the LBO model
    await loadTestFile(ctx.page, 'xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx');
    await sleep(1500);

    // Verify file loaded
    const cellCount = await ctx.page.evaluate(() => {
      const ctx = window._appContext;
      return ctx?.app?.cells?.length || 0;
    });
    console.log(`Loaded ${cellCount} cells from LBO model`);
    assertTrue(cellCount > 0, 'Should have cells from XLSX');

    // Export the file
    const exportResult = await ctx.page.evaluate(async () => {
      const ctx = window._appContext;
      if (!ctx || !ctx.app || !ctx.app.dataSource) {
        return { error: 'No data source' };
      }

      try {
        const result = await ctx.app.dataSource.client.exportXLSX();
        if (result.error) {
          return { error: result.error };
        }

        // Convert ArrayBuffer to base64
        const bytes = new Uint8Array(result.data);
        let binary = '';
        for (let i = 0; i < bytes.length; i++) {
          binary += String.fromCharCode(bytes[i]);
        }

        // Check for 0xFD bytes (UTF-8 replacement character indicator)
        let fdCount = 0;
        for (let i = 0; i < bytes.length; i++) {
          if (bytes[i] === 0xFD) fdCount++;
        }

        return {
          base64: btoa(binary),
          size: bytes.length,
          fdCount,
          firstBytes: Array.from(bytes.slice(0, 20)),
          lastBytes: Array.from(bytes.slice(-20)),
        };
      } catch (err) {
        return { error: err.message };
      }
    });

    console.log('Export result:', {
      size: exportResult.size,
      fdCount: exportResult.fdCount,
      error: exportResult.error,
    });

    assertTrue(!exportResult.error, `Export should succeed: ${exportResult.error}`);

    // Check for corruption indicator
    if (exportResult.fdCount > 10) {
      console.warn(`WARNING: Found ${exportResult.fdCount} 0xFD bytes - possible UTF-8 corruption!`);
    }

    // ZIP signature check
    assertEqual(exportResult.firstBytes[0], 0x50, 'Should start with P');
    assertEqual(exportResult.firstBytes[1], 0x4B, 'Should start with K');

    // Write and validate
    const tmpPath = join(tmpDir, 'export-test-lbo.xlsx');
    const buffer = Buffer.from(exportResult.base64, 'base64');
    await writeFile(tmpPath, buffer);

    console.log(`Wrote ${buffer.length} bytes to ${tmpPath}`);

    try {
      const output = execSync(`unzip -t "${tmpPath}" 2>&1`).toString();
      console.log('unzip -t output:', output);
      assertTrue(output.includes('No errors'), 'ZIP should be valid');
    } catch (err) {
      console.error('ZIP validation failed!');

      // Detailed diagnostics
      const hexHead = execSync(`xxd "${tmpPath}" | head -10`).toString();
      const hexTail = execSync(`xxd "${tmpPath}" | tail -10`).toString();
      console.log('Hex head:\n', hexHead);
      console.log('Hex tail:\n', hexTail);

      // Try to show what's in the zip
      try {
        const zipList = execSync(`zipinfo "${tmpPath}" 2>&1`).toString();
        console.log('zipinfo:\n', zipList);
      } catch (e) {
        console.log('zipinfo failed:', e.message);
      }

      throw new Error(`ZIP validation failed: ${err.message}`);
    }

    // Cleanup
    await unlink(tmpPath);
  },
};

runTests(tests);
