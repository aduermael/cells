#!/bin/bash
# Run unit tests (C++)
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

# --test_output=errors prints logs for failures only (keeps CI actionable).
# Include //apps/cli:sync_args_test (release arg parsing) and output_spill_test
# (agent-friendly large stdout). Do not use //apps/cli/... yet — converter_test
# still has pre-existing data-path failures.
# foreign_cc_toolchain_args is a no-op (libdatachannel is pure Bazel).
# shellcheck disable=SC2046
bazel test //core/... //apps/cli:sync_args_test //apps/cli:output_spill_test \
  --test_output=errors \
  $(foreign_cc_toolchain_args)
