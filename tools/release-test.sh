#!/bin/bash
# Run release packaging / install helper tests.
# Usage: bazel run :release-test
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

exec "$REPO_ROOT/scripts/release/release_test.sh"
