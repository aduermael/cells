#!/bin/bash
# Generate Excel goldens via Windows COM (cells-verify excel-save).
#
# Usage:
#   bazel run :excel-verify -- excel-save testdata/xlsx/simple.xlsx golden.xlsx
#   bazel run :excel-verify -- version
#
# excel-save requires Windows + Microsoft Excel. Off Windows it errors.
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"

# Check Go version (require 1.22+; same floor as tools/serve.sh)
go version | awk '{print $3}' | sed 's/go//' | awk -F. '{if ($1 < 1 || ($1 == 1 && $2 < 22)) {
    print "Error: Go 1.22+ required (you have: " $0 ")";
    print "  Install: brew install go";
    exit 1
}}'

# Build in the module, then exec from the caller's cwd so relative paths work.
mkdir -p "$REPO_ROOT/dist/excel-verify"
(
    cd "$REPO_ROOT/tools/excel-verify"
    go build -o "$REPO_ROOT/dist/excel-verify/cells-verify" ./cmd/cells-verify
)
cd "${BUILD_WORKING_DIRECTORY:-$REPO_ROOT}"
exec "$REPO_ROOT/dist/excel-verify/cells-verify" "$@"
