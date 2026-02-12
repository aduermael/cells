#!/bin/bash
# Run XLSX roundtrip tests against Excel reference files
#
# Usage:
#   bazel run :xlsx-roundtrip   # Run all enabled roundtrip tests
#
# Tests evaluate formulas via CLI, then compare output against Excel-computed
# reference files. Only enabled categories are tested.
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

# Categories that are known to pass (add new ones here as they're fixed)
ENABLED_CATEGORIES=(
    math-basic
)

# Build CLI
echo "Building CLI..."
bazel build //apps/cli:cells
mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

# Run each category
PASSED=0
FAILED=0
FAILED_NAMES=()

for category in "${ENABLED_CATEGORIES[@]}"; do
    echo ""
    echo "--- $category ---"
    if tests/excel-roundtrips/run-test.sh "$category"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        FAILED_NAMES+=("$category")
    fi
done

# Summary
echo ""
TOTAL=$((PASSED + FAILED))
if [ $FAILED -eq 0 ]; then
    echo "XLSX roundtrip: $PASSED/$TOTAL passed"
else
    echo "XLSX roundtrip: $PASSED/$TOTAL passed, $FAILED failed: ${FAILED_NAMES[*]}"
    exit 1
fi
