#!/bin/bash
# Build CLI binary (fast build, for development)
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building CLI..."
bazel build //apps/cli:cells

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells"
