#!/bin/bash
# Build CLI binary (fast build, for development)
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building CLI..."
# Prefer preinstalled cmake/ninja when available (avoids foreign_cc prebuilt
# ninja requiring a newer glibc than some hosts provide).
# shellcheck disable=SC2046
bazel build //apps/cli:cells $(foreign_cc_toolchain_args)

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells"
