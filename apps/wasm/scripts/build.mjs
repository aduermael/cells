#!/usr/bin/env node
/**
 * Build script for bundling TypeScript sources with esbuild.
 * Must be run via: bazel run :wasm-dist
 *
 * Entry points:
 * - src/main.ts -> ../../dist/wasm/main.js (browser application entry point)
 * - src/worker.ts -> ../../dist/wasm/worker.js (web worker)
 *
 * Stamps __CELLS_VERSION__ the same way as tools/wasm-ts-build.sh:
 * CELLS_VERSION env → nearest git semver tag → 0.0.1 default
 * (via scripts/release/common.sh cells_resolve_product_version).
 */

import './guard.mjs';
import * as esbuild from 'esbuild';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(__dirname, '../../..');

/** Resolve product version using the shared shell helper (single source of truth). */
function resolveProductVersion() {
  const script = `
set -euo pipefail
# shellcheck source=scripts/release/common.sh
. "${repoRoot}/scripts/release/common.sh"
export REPO_ROOT="${repoRoot}"
cells_resolve_product_version
`;
  const out = execFileSync('sh', ['-c', script], {
    encoding: 'utf8',
    env: process.env,
  });
  return out.trim();
}

const productVersion = resolveProductVersion();
console.log(`Stamping frontend CELLS_VERSION=${productVersion}`);

/** @type {esbuild.BuildOptions} */
const commonOptions = {
  bundle: true,
  format: 'esm',
  target: 'es2020',
  sourcemap: true,
  minify: true,
  logLevel: 'info',
  define: {
    __CELLS_VERSION__: JSON.stringify(productVersion),
  },
};

/** @type {esbuild.BuildOptions} */
const mainOptions = {
  ...commonOptions,
  entryPoints: ['src/main.ts'],
  outfile: '../../dist/wasm/main.js',
};

/** @type {esbuild.BuildOptions} */
const workerOptions = {
  ...commonOptions,
  entryPoints: ['src/worker.ts'],
  outfile: '../../dist/wasm/worker.js',
};

async function build() {
  try {
    await Promise.all([
      esbuild.build(mainOptions),
      esbuild.build(workerOptions),
    ]);

    console.log('Build complete!');
  } catch (error) {
    console.error('Build failed:', error);
    process.exit(1);
  }
}

build();
