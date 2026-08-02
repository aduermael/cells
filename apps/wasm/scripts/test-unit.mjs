#!/usr/bin/env node
/**
 * Unit test runner for TypeScript tests.
 * Must be run via: bazel run :check
 */

import './guard.mjs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Import and run unit tests
const unitDir = join(__dirname, '..', 'tests', 'unit');
for (const name of [
  'editing-session.test.mjs',
  'theme.test.mjs',
  'collab-menu-content.test.mjs',
]) {
  await import(join(unitDir, name));
}
