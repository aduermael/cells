#!/bin/bash
# Build CLI binary with optimizations (for production/release)
# Output: dist/cli/cells
# Optional: CELLS_VERSION=x.y.z stamps --version output
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

extra_copt=()
if [ -n "${CELLS_VERSION:-}" ]; then
  extra_copt=(--copt="-DCELLS_VERSION=\"${CELLS_VERSION}\"")
fi

echo "Building CLI (optimized)${CELLS_VERSION:+ version=$CELLS_VERSION}..."
bazel build -c opt //apps/cli:cells "${extra_copt[@]}"

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells (optimized)"
