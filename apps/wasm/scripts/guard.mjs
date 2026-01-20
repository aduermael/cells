#!/usr/bin/env node
/**
 * Bazel run guard - ensures scripts are only run via bazel commands.
 *
 * Usage: import this at the top of any script that should only run via bazel.
 *
 * The guard checks for BAZEL_RUN=1 environment variable, which is set by
 * the shell scripts in tools/ when invoked via `bazel run :command`.
 */

if (process.env.BAZEL_RUN !== '1') {
  console.error('');
  console.error('Error: This script must be run via bazel commands.');
  console.error('');
  console.error('Available commands:');
  console.error('  bazel run :wasm-dist    # Build WASM distribution');
  console.error('  bazel run :check        # Run all checks (unit tests, lint, type-check, e2e)');
  console.error('  bazel run :e2e          # Run E2E tests');
  console.error('  bazel run :e2e-headed   # Run E2E tests with visible browser');
  console.error('  bazel run :check-types  # Run TypeScript type checking');
  console.error('  bazel run :lint         # Run linter');
  console.error('  bazel run :format       # Run formatter');
  console.error('');
  process.exit(1);
}
