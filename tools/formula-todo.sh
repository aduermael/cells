#!/bin/bash
# Print one line per mog-derived formula case (PASS green / TODO+FAIL red)
# plus a supported vs not-implemented summary. Not part of :check / :test.
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"

rlocation() {
    local rel="$1"
    local base
    for base in \
        "${0}.runfiles/_main" \
        "${0}.runfiles/cells" \
        "${RUNFILES_DIR:-}/_main" \
        "${RUNFILES_DIR:-}/cells"; do
        if [ -n "$base" ] && [ -e "$base/$rel" ]; then
            echo "$base/$rel"
            return 0
        fi
    done
    return 1
}

REPORT="$(rlocation core/cells/formula_todo_report)" || {
    echo "Error: formula_todo_report not found in runfiles" >&2
    exit 1
}
CASES="$(rlocation testdata/formulas/mog_cases.tsv)" || {
    echo "Error: testdata/formulas/mog_cases.tsv not found in runfiles" >&2
    exit 1
}
exec "$REPORT" --cases "$CASES"
