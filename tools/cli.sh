#!/bin/bash
# Build CLI binary (fast build, for development)
# Output: dist/cli/cells
set -euo pipefail

# Use Bazel's workspace directory if available (when run via bazel run)
# Otherwise fall back to script location (when run directly)
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
else
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$REPO_ROOT"

echo "Building CLI..."
bazel build //apps/cli:cells

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells"
