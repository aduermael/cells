#!/bin/bash
# Run unit tests (C++)
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

# --test_output=errors prints logs for failures only (keeps CI actionable).
bazel test //core/... --test_output=errors
