#!/usr/bin/env node
// Parallel E2E test runner for Cells
// Runs multiple test files concurrently with unique ports

import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import { dirname, join, basename } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Test collection definitions
// Note: All tests must pass. There is no "stable" subset - if a test is flaky or broken, fix it or remove it.
const COLLECTIONS = {
  all: [
    'smoke.test.mjs',
    'formula.test.mjs',
    'editing.test.mjs',
    'column-move.test.mjs',
    'clipboard.test.mjs',
    'selection.test.mjs',
    'named-ranges.test.mjs',
    'named-ref-import.test.mjs',
    'collab.test.mjs',
    'initial-sync.test.mjs',
    'collab-demo.test.mjs',
    'lbo-integration.test.mjs',
    'zoom-selection.test.mjs',
    'zoom-headers.test.mjs',
  ],
  collab: [
    'collab.test.mjs',
    'initial-sync.test.mjs',
    'collab-demo.test.mjs',
  ],
};

const BASE_PORT = 9000;
const DEFAULT_CONCURRENCY = 10;

/**
 * Parse command line arguments
 */
function parseArgs() {
  const args = process.argv.slice(2);
  let concurrency = DEFAULT_CONCURRENCY;
  let collection = 'all';

  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--concurrency' || args[i] === '-c') {
      concurrency = parseInt(args[i + 1], 10);
      i++;
    } else if (args[i] === '--help' || args[i] === '-h') {
      printUsage();
      process.exit(0);
    } else if (!args[i].startsWith('-')) {
      collection = args[i];
    }
  }

  if (!COLLECTIONS[collection]) {
    console.error(`Unknown collection: ${collection}`);
    console.error(`Available: ${Object.keys(COLLECTIONS).join(', ')}`);
    process.exit(1);
  }

  return { concurrency, collection };
}

function printUsage() {
  console.log(`
Usage: node run-parallel.mjs [options] [collection]

Collections:
  all      All test files (default)
  collab   Collaboration tests: collab, initial-sync, collab-demo

Options:
  -c, --concurrency N   Maximum concurrent tests (default: ${DEFAULT_CONCURRENCY})
  -h, --help            Show this help message

Examples:
  node run-parallel.mjs                  # Run all tests
  node run-parallel.mjs -c 5 collab      # Run collab tests with max 5 concurrent
`);
}

/**
 * Run a single test file and collect results
 */
function runTestFile(testFile, port) {
  return new Promise((resolve) => {
    const testPath = join(__dirname, testFile);
    const testName = basename(testFile, '.test.mjs');
    const startTime = Date.now();
    let stdout = '';
    let stderr = '';

    const proc = spawn('node', [testPath], {
      env: {
        ...process.env,
        TEST_PORT: port.toString(),
      },
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    proc.stdout.on('data', (data) => {
      stdout += data.toString();
    });

    proc.stderr.on('data', (data) => {
      stderr += data.toString();
    });

    proc.on('close', (code) => {
      const elapsed = Date.now() - startTime;
      const result = parseTestOutput(stdout, testName);
      resolve({
        name: testName,
        file: testFile,
        port,
        elapsed,
        exitCode: code,
        passed: result.passed,
        failed: result.failed,
        total: result.total,
        failedTests: result.failedTests,
        stdout,
        stderr,
      });
    });

    proc.on('error', (err) => {
      const elapsed = Date.now() - startTime;
      resolve({
        name: testName,
        file: testFile,
        port,
        elapsed,
        exitCode: 1,
        passed: 0,
        failed: 1,
        total: 1,
        failedTests: [{ name: 'spawn error', error: err.message }],
        stdout,
        stderr,
      });
    });
  });
}

/**
 * Parse test output to extract results
 */
function parseTestOutput(output, testName) {
  const result = {
    passed: 0,
    failed: 0,
    total: 0,
    failedTests: [],
  };

  // Look for "Passed: N" and "Failed: M" in output
  const passedMatch = output.match(/Passed:\s*(\d+)/);
  const failedMatch = output.match(/Failed:\s*(\d+)/);

  if (passedMatch) {
    result.passed = parseInt(passedMatch[1], 10);
  }
  if (failedMatch) {
    result.failed = parseInt(failedMatch[1], 10);
  }
  result.total = result.passed + result.failed;

  // Extract failed test names and errors
  const failedSection = output.match(/Failed tests:\s*\n([\s\S]*?)(?:\n\n|$)/);
  if (failedSection) {
    const failedLines = failedSection[1].split('\n');
    for (const line of failedLines) {
      const match = line.match(/^\s*-\s*(.+?):\s*(.+)$/);
      if (match) {
        result.failedTests.push({ name: match[1], error: match[2] });
      }
    }
  }

  // Also look for individual FAIL lines if no summary found
  if (result.total === 0) {
    const passLines = (output.match(/^PASS/gm) || []).length;
    const failLines = (output.match(/^FAIL/gm) || []).length;
    if (passLines > 0 || failLines > 0) {
      result.passed = passLines;
      result.failed = failLines;
      result.total = passLines + failLines;
    }
  }

  return result;
}

/**
 * Run tests with concurrency limit
 */
async function runTestsWithConcurrency(testFiles, concurrency) {
  const results = [];
  const running = new Map();
  let nextPort = BASE_PORT;

  for (let i = 0; i < testFiles.length; i++) {
    const testFile = testFiles[i];
    const port = nextPort++;

    // Wait if at concurrency limit
    while (running.size >= concurrency) {
      const completed = await Promise.race(running.values());
      results.push(completed);
      running.delete(completed.file);
    }

    // Start new test
    const promise = runTestFile(testFile, port);
    running.set(testFile, promise);
  }

  // Wait for remaining tests
  for (const promise of running.values()) {
    results.push(await promise);
  }

  return results;
}

/**
 * Print the final report
 */
function printReport(results, totalElapsed) {
  const line = '═'.repeat(65);
  const thinLine = '─'.repeat(65);

  console.log(`\n${line}`);
  console.log('                     E2E TEST RESULTS');
  console.log(line);
  console.log('');

  let totalPassed = 0;
  let totalFailed = 0;

  for (const r of results) {
    const icon = r.failed === 0 && r.exitCode === 0 ? '✓' : '✗';
    const elapsed = (r.elapsed / 1000).toFixed(1);
    const status = `${r.passed}/${r.total} passed`;
    const name = r.name.padEnd(14);
    console.log(` ${icon} ${name} ${status.padEnd(16)} (${elapsed}s)`);

    if (r.failedTests.length > 0) {
      for (const ft of r.failedTests) {
        console.log(`   └─ ${ft.name}: ${ft.error}`);
      }
    }

    totalPassed += r.passed;
    totalFailed += r.failed;
  }

  console.log('');
  console.log(thinLine);

  const totalTests = totalPassed + totalFailed;
  const failedSuffix = totalFailed > 0 ? ` (${totalFailed} failed)` : '';
  const duration = (totalElapsed / 1000).toFixed(1);
  console.log(` TOTAL: ${totalPassed}/${totalTests} passed${failedSuffix}    Duration: ${duration}s`);
  console.log(thinLine);
  console.log('');

  return totalFailed > 0;
}

/**
 * Main entry point
 */
async function main() {
  const { concurrency, collection } = parseArgs();
  const testFiles = COLLECTIONS[collection];

  console.log(`Running ${collection} tests (${testFiles.length} files, concurrency: ${concurrency})`);
  console.log('');

  const startTime = Date.now();
  const results = await runTestsWithConcurrency(testFiles, concurrency);
  const totalElapsed = Date.now() - startTime;

  const hasFailures = printReport(results, totalElapsed);

  // Show verbose output for failed tests
  if (hasFailures && process.env.DEBUG) {
    console.log('\n=== Failed Test Output ===\n');
    for (const r of results) {
      if (r.failed > 0 || r.exitCode !== 0) {
        console.log(`\n--- ${r.name} ---`);
        console.log(r.stdout);
        if (r.stderr) {
          console.log('STDERR:');
          console.log(r.stderr);
        }
      }
    }
  }

  process.exit(hasFailures ? 1 : 0);
}

main().catch((err) => {
  console.error('Fatal error:', err);
  process.exit(1);
});
