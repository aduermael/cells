#!/usr/bin/env node
/**
 * Unit test runner for TypeScript/JS tests under apps/wasm/tests/unit/.
 * Must be run via: bazel run :test-js or bazel run :check
 *
 * Each suite runs as a child process so a suite's process.exit() cannot
 * skip the rest of the suite list.
 */

import './guard.mjs';
import { spawnSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const unitDir = join(__dirname, '..', 'tests', 'unit');

const suites = readdirSync(unitDir)
  .filter((name) => name.endsWith('.test.mjs'))
  .sort();

if (suites.length === 0) {
  console.error(`No unit test suites found in ${unitDir}`);
  process.exit(1);
}

console.log(`\n=== JS unit tests (${suites.length} suites) ===\n`);

let failed = 0;
for (const name of suites) {
  const path = join(unitDir, name);
  console.log(`--- ${name} ---`);
  const result = spawnSync(process.execPath, [path], {
    stdio: 'inherit',
    env: process.env,
  });
  if (result.error) {
    console.error(`Failed to spawn ${name}:`, result.error);
    failed++;
    continue;
  }
  if (result.status !== 0) {
    failed++;
  }
}

console.log('');
if (failed > 0) {
  console.error(`JS unit tests FAILED (${failed}/${suites.length} suite(s))`);
  process.exit(1);
}
console.log(`JS unit tests PASSED (${suites.length}/${suites.length} suites)`);
