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

// Import and run the unit test
const testPath = join(__dirname, '..', 'tests', 'unit', 'editing-session.test.mjs');
await import(testPath);
