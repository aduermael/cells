#!/bin/bash
# Run E2E tests with browser visible (for debugging)
# Usage: bazel run :e2e-headed [-- <test>]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/apps/wasm"

export HEADED=1
TARGET="${1:-}"

if [ -n "$TARGET" ]; then
    node "tests/${TARGET}.test.mjs"
else
    # Run all tests with browser visible (sequentially is better for headed mode)
    npm run test:parallel -- all
fi
