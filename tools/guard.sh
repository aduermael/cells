#!/bin/bash
# Bazel run guard - ensures scripts are only run via bazel commands.
#
# Usage: source this at the top of any script that should only run via bazel.
#
# The guard checks for BUILD_WORKSPACE_DIRECTORY environment variable,
# which is automatically set by Bazel when running via `bazel run :command`.
#
# After sourcing, these variables are available:
#   REPO_ROOT  - absolute path to the repository root
#   SCRIPT_DIR - absolute path to the tools/ directory

if [ -z "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    echo "" >&2
    echo "Error: This script must be run via bazel commands." >&2
    echo "" >&2
    echo "Available commands:" >&2
    echo "  bazel run :wasm-dist    # Build WASM distribution" >&2
    echo "  bazel run :wasm-debug   # Build WASM with debug symbols" >&2
    echo "  bazel run :wasm         # Build WASM (development)" >&2
    echo "  bazel run :serve        # Start development server" >&2
    echo "  bazel run :check        # Run all checks" >&2
    echo "  bazel run :test         # Run unit tests" >&2
    echo "  bazel run :e2e          # Run E2E tests" >&2
    echo "  bazel run :e2e-headed   # Run E2E tests with visible browser" >&2
    echo "  bazel run :check-types  # Run TypeScript type checking" >&2
    echo "  bazel run :lint         # Run linter" >&2
    echo "  bazel run :format       # Run formatter" >&2
    echo "  bazel run :xlsx-roundtrip # Run XLSX roundtrip tests" >&2
    echo "  bazel run :cli          # Build CLI (development)" >&2
    echo "  bazel run :cli-release  # Build CLI (release)" >&2
    echo "" >&2
    exit 1
fi

# Set up standard paths for use by scripts (export so child helpers inherit them)
export REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
export SCRIPT_DIR="$REPO_ROOT/tools"

# Append preinstalled foreign_cc toolchains when system cmake+ninja exist.
# Safe under `set -u` on macOS bash 3.2 (empty "${arr[@]}" is unbound there).
# Usage: bazel build //apps/cli:cells $(foreign_cc_toolchain_args)
# (unquoted command substitution is intentional — args have no spaces)
foreign_cc_toolchain_args() {
  if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    printf '%s ' \
      --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_cmake_toolchain \
      --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_ninja_toolchain \
      --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_make_toolchain \
      --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_pkgconfig_toolchain
  fi
}
