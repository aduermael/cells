#!/bin/bash
# Run Office.js (QuickJS) unit tests
# Usage: bazel run :officejs [-- <gtest filter fragment>]
#   omit arg: all Office.js tests
#   e.g. bazel run :officejs -- WriteValues
#        bazel run :officejs -- ExcelRun
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

FILTER="${1:-}"

# shellcheck disable=SC2046
if [ -n "$FILTER" ]; then
    echo "Running Office.js test filter: *${FILTER}*"
    bazel test //core/cells:officejs_test \
        --test_arg="--gtest_filter=*${FILTER}*" \
        --test_output=all \
        $(foreign_cc_toolchain_args)
else
    echo "Running all Office.js tests..."
    bazel test //core/cells:officejs_test \
        --test_output=all \
        $(foreign_cc_toolchain_args)
fi
