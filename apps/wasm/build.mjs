#!/usr/bin/env node
/**
 * esbuild script for bundling TypeScript sources
 *
 * Entry points:
 * - src/main.ts -> dist/main.js (browser application entry point)
 * - src/worker.ts -> dist/worker.js (web worker)
 *
 * Note: client.ts is bundled into main.js as a dependency, not as a separate
 * entry point. It contains the CellsClient class for worker communication.
 *
 * Usage:
 *   node build.mjs         # Production build
 *   node build.mjs --watch # Watch mode for development
 */

import * as esbuild from 'esbuild';

const isWatch = process.argv.includes('--watch');

/** @type {esbuild.BuildOptions} */
const commonOptions = {
  bundle: true,
  format: 'esm',
  target: 'es2020',
  sourcemap: true,
  minify: !isWatch,
  logLevel: 'info',
};

/** @type {esbuild.BuildOptions} */
const mainOptions = {
  ...commonOptions,
  entryPoints: ['src/main.ts'],
  outfile: 'dist/main.js',
};

/** @type {esbuild.BuildOptions} */
const workerOptions = {
  ...commonOptions,
  entryPoints: ['src/worker.ts'],
  outfile: 'dist/worker.js',
};

async function build() {
  try {
    if (isWatch) {
      // Watch mode - create contexts and watch
      const [mainCtx, workerCtx] = await Promise.all([
        esbuild.context(mainOptions),
        esbuild.context(workerOptions),
      ]);

      await Promise.all([mainCtx.watch(), workerCtx.watch()]);

      console.log('Watching for changes...');
    } else {
      // Production build
      await Promise.all([
        esbuild.build(mainOptions),
        esbuild.build(workerOptions),
      ]);

      console.log('Build complete!');
    }
  } catch (error) {
    console.error('Build failed:', error);
    process.exit(1);
  }
}

build();
