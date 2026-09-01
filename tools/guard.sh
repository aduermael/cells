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
    echo "  bazel run :test         # Run C++ unit tests" >&2
    echo "  bazel run :test-js      # Run JS/TS unit tests" >&2
    echo "  bazel run :release-test # Run release/install tests" >&2
    echo "  bazel run :e2e          # Run E2E tests" >&2
    echo "  bazel run :e2e-headed   # Run E2E tests with visible browser" >&2
    echo "  bazel run :check-types  # Run TypeScript type checking" >&2
    echo "  bazel run :lint         # Run linter" >&2
    echo "  bazel run :format       # Run formatter" >&2
    echo "  bazel run :xlsx-roundtrip # Run XLSX roundtrip tests" >&2
    echo "  bazel run :cli          # Build CLI (development)" >&2
    echo "  bazel run :cli-release  # Build CLI (release)" >&2
    echo "  bazel run :cli-headless # Build CLI only (no WASM/UI)" >&2
    echo "  bazel run :cli-no-collab # Headless, no ledger/connectivity" >&2
    echo "  bazel run :cli-headless-no-collab # Same as :cli-no-collab" >&2
    echo "  bazel run :formula-todo # Mog-derived formula TODO suite" >&2
    echo "" >&2
    exit 1
fi

# Set up standard paths for use by scripts (export so child helpers inherit them)
export REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
export SCRIPT_DIR="$REPO_ROOT/tools"

# Historical no-op: libdatachannel is pure Bazel (no rules_foreign_cc).
# Kept so existing call sites `$(foreign_cc_toolchain_args)` stay valid.
# Safe under `set -u` on macOS bash 3.2 (empty expansion is unbound there).
foreign_cc_toolchain_args() {
  :
}

# Linux C++ links use GNU gold (see .bazelrc build:linux). Fail with install
# instructions instead of gcc's "collect2: cannot find 'ld'".
require_linux_linker() {
    case "$(uname -s 2>/dev/null || echo unknown)" in
        Linux) ;;
        *) return 0 ;;
    esac
    if command -v ld.gold >/dev/null 2>&1 || [ -x /usr/bin/ld.gold ]; then
        return 0
    fi
    echo "" >&2
    echo "Error: GNU gold linker (ld.gold) not found." >&2
    echo "" >&2
    echo "Linux C++ builds are pinned to GNU gold (-fuse-ld=gold) because:" >&2
    echo "  - GNU ld.bfd does not support Bazel's -Wl,--start-lib" >&2
    echo "  - LLVM lld is often picked from a non-sandbox PATH (e.g. Swift)," >&2
    echo "    which then fails as: collect2: fatal error: cannot find 'ld'" >&2
    echo "" >&2
    echo "Install GNU gold, then retry:" >&2
    echo "  Debian/Ubuntu:  sudo apt install binutils" >&2
    echo "                  (if still missing: sudo apt install binutils-gold)" >&2
    echo "  Fedora:         sudo dnf install binutils-gold" >&2
    echo "  Arch:           sudo pacman -S binutils" >&2
    echo "" >&2
    exit 1
}
require_linux_linker
