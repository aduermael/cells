#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CLI="$REPO_ROOT/dist/cli/cells"

if [ $# -ne 1 ]; then
    echo "Usage: $0 <category>" >&2
    echo "Example: $0 math-basic" >&2
    exit 1
fi

CATEGORY="$1"
DATA_DIR="$SCRIPT_DIR/data/$CATEGORY"
ORIGINAL="$DATA_DIR/file.xlsx"
NO_CACHE="$DATA_DIR/file_no_cached_results.xlsx"

# Verify test files exist
if [ ! -f "$ORIGINAL" ]; then
    echo "FAIL [$CATEGORY]: Missing $ORIGINAL" >&2
    exit 1
fi
if [ ! -f "$NO_CACHE" ]; then
    echo "FAIL [$CATEGORY]: Missing $NO_CACHE" >&2
    exit 1
fi

# Build CLI if needed
if [ ! -f "$CLI" ]; then
    echo "CLI not found at $CLI, building..." >&2
    (cd "$REPO_ROOT" && bazel build //apps/cli:cells && mkdir -p dist/cli && cp bazel-bin/apps/cli/cells dist/cli/cells)
fi

# Create temp file for evaluated output
TMPFILE="$(mktemp /tmp/cells-roundtrip-${CATEGORY}-XXXXXX.xlsx)"
trap "rm -f '$TMPFILE'" EXIT

# Evaluate formulas
echo "Running: cells --eval on $CATEGORY..."
"$CLI" -i "$NO_CACHE" --eval -y "$TMPFILE"

# Compare against original
echo "Comparing against Excel reference..."
"$SCRIPT_DIR/compare.sh" "$ORIGINAL" "$TMPFILE" --config "$SCRIPT_DIR/config.json"
RESULT=$?

if [ $RESULT -eq 0 ]; then
    echo "PASS [$CATEGORY]"
else
    echo "FAIL [$CATEGORY]"
    exit 1
fi
