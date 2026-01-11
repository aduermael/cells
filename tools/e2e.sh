#!/bin/bash
# Run E2E tests (headless)
# Usage: bazel run :e2e [-- <test>]
#   <test> can be a single test: smoke, formula, editing, column-move, clipboard,
#          selection, script, collab, initial-sync, collab-demo
#          or omit to run all tests (default)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Auto-detect available cores
get_num_cores() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

JOBS=$(get_num_cores)
TARGET="${1:-}"

cd apps/wasm

if [ -n "$TARGET" ]; then
    # Run single test file
    echo "Running $TARGET test..."
    node "tests/${TARGET}.test.mjs"
else
    # Run all tests
    echo "Running all E2E tests with $JOBS parallel workers..."
    npm run test:parallel -- --concurrency "$JOBS" all
fi
