#!/bin/bash
# Build the headless engine (CLI only — no WASM/UI app).
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building headless CLI (no WASM/UI)..."
# shellcheck disable=SC2046
bazel build --config=headless //apps/cli:cells $(foreign_cc_toolchain_args)

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells (headless)"
