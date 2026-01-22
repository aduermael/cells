#!/bin/bash
# Run E2E tests with browser visible (for debugging)
# Usage: bazel run :e2e-headed [-- <test>]
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
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
