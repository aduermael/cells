#!/bin/bash
# Build CLI binary with optimizations (for production/release)
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building CLI (optimized)..."
bazel build -c opt //apps/cli:cells

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells (optimized)"
