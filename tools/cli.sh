#!/bin/bash
# Build CLI binary (fast build, for development)
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building CLI..."
# Prefer preinstalled cmake/ninja when available (avoids foreign_cc prebuilt
# ninja requiring a newer glibc than some hosts provide).
extra_toolchains=()
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  extra_toolchains=(
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_cmake_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_ninja_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_make_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_pkgconfig_toolchain
  )
fi
bazel build //apps/cli:cells "${extra_toolchains[@]}"

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells"
