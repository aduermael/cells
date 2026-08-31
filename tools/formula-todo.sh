#!/bin/bash
# Run the mog-derived formula TODO suite (opt-in; remaining cases fail).
# Not part of bazel run :check / :test.
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Running formula TODO suite (mog-derived; remaining cases fail)..."
# shellcheck disable=SC2046
bazel test //core/cells:formula_todo_strict_test \
  --test_output=all \
  --test_tag_filters=manual \
  --build_tests_only \
  $(foreign_cc_toolchain_args)
