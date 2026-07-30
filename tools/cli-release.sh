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
extra_toolchains=()
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  extra_toolchains=(
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_cmake_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_ninja_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_make_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_pkgconfig_toolchain
  )
fi
bazel build -c opt //apps/cli:cells "${extra_copt[@]}" "${extra_toolchains[@]}"

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells (optimized)"
