#!/usr/bin/env node
/**
 * esbuild script for bundling TypeScript sources
 *
 * Entry points:
 * - src/client.ts -> dist/client.js (main application)
 * - src/worker.ts -> dist/worker.js (web worker)
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
const clientOptions = {
  ...commonOptions,
  entryPoints: ['src/client.ts'],
  outfile: 'dist/client.js',
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
      const [clientCtx, workerCtx] = await Promise.all([
        esbuild.context(clientOptions),
        esbuild.context(workerOptions),
      ]);

      await Promise.all([clientCtx.watch(), workerCtx.watch()]);

      console.log('Watching for changes...');
    } else {
      // Production build
      await Promise.all([
        esbuild.build(clientOptions),
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
