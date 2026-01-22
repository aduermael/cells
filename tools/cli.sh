#!/bin/bash
# Build CLI binary (fast build, for development)
# Output: dist/cli/cells
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/guard.sh"
cd "$REPO_ROOT"

echo "Building CLI..."
bazel build //apps/cli:cells

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells"
