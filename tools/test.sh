#!/bin/bash
# Run unit tests (C++)
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

# When system cmake/ninja exist, prefer preinstalled foreign_cc toolchains.
# Avoids rules_foreign_cc's prebuilt ninja requiring a newer glibc than the host
# (e.g. Debian bookworm / GLIBC 2.36). Harmless on newer hosts (GHA, etc.).
extra_toolchains=()
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  extra_toolchains=(
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_cmake_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_ninja_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_make_toolchain
    --extra_toolchains=@rules_foreign_cc//toolchains:preinstalled_pkgconfig_toolchain
  )
fi

# --test_output=errors prints logs for failures only (keeps CI actionable).
# Include //apps/cli:sync_args_test (release arg parsing). Do not use
# //apps/cli/... yet — converter_test still has pre-existing data-path failures.
bazel test //core/... //apps/cli:sync_args_test \
  --test_output=errors \
  "${extra_toolchains[@]}"
