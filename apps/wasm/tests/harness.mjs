// Test harness for Cells e2e tests
// Starts local dev server and Lightpanda browser for automated testing

import { spawn, execSync } from 'child_process';
import { lightpanda } from '@lightpanda/browser';
import puppeteer from 'puppeteer-core';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { existsSync } from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, '..', '..', '..');

// Configuration
const CONFIG = {
  serverPort: 8082, // Use different port from dev server to avoid conflicts
  lightpandaPort: 9222,
  lightpandaHost: '127.0.0.1',
  distDir: join(projectRoot, 'dist'),
  timeout: 30000,
};

/**
 * Test context with browser, page, and cleanup function
 */
export class TestContext {
  constructor(browser, page, serverProc, lightpandaProc) {
    this.browser = browser;
    this.page = page;
    this.serverProc = serverProc;
    this.lightpandaProc = lightpandaProc;
    this.baseUrl = `http://localhost:${CONFIG.serverPort}`;
  }

  async close() {
    if (this.page) {
      await this.page.close().catch(() => {});
    }
    if (this.browser) {
      await this.browser.disconnect().catch(() => {});
    }
    if (this.lightpandaProc) {
      this.lightpandaProc.stdout?.destroy();
      this.lightpandaProc.stderr?.destroy();
      this.lightpandaProc.kill();
    }
    if (this.serverProc) {
      this.serverProc.kill();
    }
  }
}

/**
 * Wait for server to be ready
 */
async function waitForServer(port, maxAttempts = 30) {
  const url = `http://localhost:${port}/`;
  for (let i = 0; i < maxAttempts; i++) {
    try {
      const response = await fetch(url);
      if (response.ok) {
        return true;
      }
    } catch (e) {
      // Server not ready yet
    }
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  throw new Error(`Server not ready after ${maxAttempts} attempts`);
}

/**
 * Start the Go dev server
 */
async function startServer() {
  // Check if dist directory exists
  if (!existsSync(CONFIG.distDir)) {
    throw new Error(`dist/ directory not found. Run 'make wasm-dist' first.`);
  }

  const serverProc = spawn('go', [
    'run', '.',
    '-port', CONFIG.serverPort.toString(),
    '-dir', CONFIG.distDir,
    '-enable-collab'
  ], {
    cwd: join(projectRoot, 'tools', 'serve'),
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  // Log server output for debugging
  serverProc.stdout.on('data', (data) => {
    if (process.env.DEBUG) {
      console.log(`[Server] ${data.toString().trim()}`);
    }
  });
  serverProc.stderr.on('data', (data) => {
    console.error(`[Server Error] ${data.toString().trim()}`);
  });

  await waitForServer(CONFIG.serverPort);
  return serverProc;
}

/**
 * Start Lightpanda browser and connect Puppeteer
 */
async function startBrowser() {
  const lpdopts = {
    host: CONFIG.lightpandaHost,
    port: CONFIG.lightpandaPort,
  };

  const proc = await lightpanda.serve(lpdopts);

  const browser = await puppeteer.connect({
    browserWSEndpoint: `ws://${CONFIG.lightpandaHost}:${CONFIG.lightpandaPort}`,
  });

  return { browser, proc };
}

/**
 * Setup test environment
 * @returns {Promise<TestContext>}
 */
export async function setup() {
  console.log('Starting test server...');
  const serverProc = await startServer();
  console.log(`Server running on port ${CONFIG.serverPort}`);

  console.log('Starting Lightpanda browser...');
  const { browser, proc: lightpandaProc } = await startBrowser();
  console.log('Browser ready');

  const context = await browser.createBrowserContext();
  const page = await context.newPage();

  return new TestContext(browser, page, serverProc, lightpandaProc);
}

/**
 * Run a single test
 */
export async function runTest(name, fn) {
  console.log(`\n--- ${name} ---`);
  const start = Date.now();
  try {
    await fn();
    const elapsed = Date.now() - start;
    console.log(`PASS (${elapsed}ms)`);
    return { name, passed: true, elapsed };
  } catch (error) {
    const elapsed = Date.now() - start;
    console.error(`FAIL: ${error.message}`);
    return { name, passed: false, elapsed, error: error.message };
  }
}

/**
 * Run all tests with setup/teardown
 */
export async function runTests(tests) {
  let ctx;
  const results = [];

  try {
    ctx = await setup();

    for (const [name, fn] of Object.entries(tests)) {
      const result = await runTest(name, () => fn(ctx));
      results.push(result);
    }
  } finally {
    if (ctx) {
      await ctx.close();
    }
  }

  // Print summary
  console.log('\n=== Test Summary ===');
  const passed = results.filter(r => r.passed).length;
  const failed = results.filter(r => !r.passed).length;
  console.log(`Passed: ${passed}`);
  console.log(`Failed: ${failed}`);

  if (failed > 0) {
    console.log('\nFailed tests:');
    for (const r of results.filter(r => !r.passed)) {
      console.log(`  - ${r.name}: ${r.error}`);
    }
    process.exit(1);
  }

  process.exit(0);
}

export { CONFIG };
