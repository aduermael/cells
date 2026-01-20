#!/usr/bin/env node
/**
 * Build script for bundling TypeScript sources with esbuild.
 * Must be run via: bazel run :wasm-dist
 *
 * Entry points:
 * - src/main.ts -> ../../dist/wasm/main.js (browser application entry point)
 * - src/worker.ts -> ../../dist/wasm/worker.js (web worker)
 */

import './guard.mjs';
import * as esbuild from 'esbuild';

/** @type {esbuild.BuildOptions} */
const commonOptions = {
  bundle: true,
  format: 'esm',
  target: 'es2020',
  sourcemap: true,
  minify: true,
  logLevel: 'info',
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
