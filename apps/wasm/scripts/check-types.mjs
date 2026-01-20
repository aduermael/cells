#!/usr/bin/env node
/**
 * TypeScript type checking script.
 * Must be run via: bazel run :check-types or bazel run :check
 */

import './guard.mjs';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const appDir = join(__dirname, '..');

const tsc = spawn('npx', ['tsc', '--noEmit'], {
  cwd: appDir,
  stdio: 'inherit',
});

tsc.on('close', (code) => {
  process.exit(code ?? 0);
});
