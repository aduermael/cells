#!/bin/bash
# Run TypeScript/JS unit tests (no browser).
# Usage: bazel run :test-js
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT/apps/wasm"

export BAZEL_RUN=1
node scripts/test-unit.mjs
