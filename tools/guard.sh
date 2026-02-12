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

# Set up standard paths for use by scripts
REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
SCRIPT_DIR="$REPO_ROOT/tools"
