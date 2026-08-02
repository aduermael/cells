#!/usr/bin/env node
/**
 * Optional Node-based TS bundle (npm run build / local). Prefer:
 *   bazel run :wasm-dist   # tools/wasm-ts-build.sh
 *
 * Stamps CELLS_VERSION the same way as wasm-ts-build.sh: resolve via
 * scripts/release/common.sh (env → nearest git semver tag → default), then
 * inject with esbuild define __CELLS_VERSION__.
 *
 * Entry points:
 * - src/main.ts -> ../../dist/wasm/main.js (browser application entry point)
 * - src/worker.ts -> ../../dist/wasm/worker.js (web worker)
 */

import './guard.mjs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as esbuild from 'esbuild';

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(__dirname, '../../..');

/**
 * Resolve product version via the shared shell helper (single source of truth).
 * @returns {string} bare semver (no leading v)
 */
function resolveProductVersion() {
  const script = [
    'set -e',
    `. "${repoRoot}/scripts/release/common.sh"`,
    'cells_resolve_product_version',
  ].join('\n');
  const result = spawnSync('sh', ['-c', script], {
    encoding: 'utf8',
    env: {
      ...process.env,
      REPO_ROOT: repoRoot,
      CELLS_VERSION_GIT_DIR: repoRoot,
    },
  });
  if (result.status !== 0) {
    const err = (result.stderr || result.stdout || '').trim();
    console.warn(
      `Warning: cells_resolve_product_version failed${err ? `: ${err}` : ''}; using 0.0.1`,
    );
    return '0.0.1';
  }
  const version = (result.stdout || '').trim().split('\n').pop() || '';
  if (!version) {
    console.warn('Warning: empty product version from resolver; using 0.0.1');
    return '0.0.1';
  }
  return version;
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
  // JSON.stringify produces a quoted string literal for esbuild --define.
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
