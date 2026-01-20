#!/bin/bash
# Run E2E tests with browser visible (for debugging)
# Usage: bazel run :e2e-headed [-- <test>]
set -euo pipefail

# Use Bazel's workspace directory if available (when run via bazel run)
# Otherwise fall back to script location (when run directly)
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
else
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$REPO_ROOT/apps/wasm"

export BAZEL_RUN=1
export HEADED=1
TARGET="${1:-}"

if [ -n "$TARGET" ]; then
    node "tests/${TARGET}.test.mjs"
else
    # Run all tests with browser visible (sequentially is better for headed mode)
    node scripts/test-parallel.mjs all
fi
