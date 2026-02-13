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

# Create temp files
TMPXLSX="$(mktemp /tmp/cells-roundtrip-${CATEGORY}-XXXXXX.xlsx)"
TMPZCD="$(mktemp /tmp/cells-roundtrip-${CATEGORY}-XXXXXX.zcd)"
TMPXLSX2="$(mktemp /tmp/cells-roundtrip-${CATEGORY}-zcd-XXXXXX.xlsx)"
trap "rm -f '$TMPXLSX' '$TMPZCD' '$TMPXLSX2'" EXIT

FAILED=0

# --- Flow 1: Direct XLSX roundtrip ---
echo "=== XLSX roundtrip [$CATEGORY] ==="
echo "Running: cells --eval on $CATEGORY..."
"$CLI" -i "$NO_CACHE" --eval -y "$TMPXLSX"

echo "Comparing against Excel reference..."
if "$SCRIPT_DIR/compare.sh" "$ORIGINAL" "$TMPXLSX" --config "$SCRIPT_DIR/config.json"; then
    echo "PASS [$CATEGORY] XLSX roundtrip"
else
    echo "FAIL [$CATEGORY] XLSX roundtrip"
    FAILED=1
fi

# --- Flow 2: ZCD roundtrip ---
echo ""
echo "=== ZCD roundtrip [$CATEGORY] ==="
echo "Running: cells --eval → .zcd on $CATEGORY..."
"$CLI" -i "$NO_CACHE" --eval -y "$TMPZCD"

echo "Running: cells .zcd → .xlsx..."
"$CLI" -i "$TMPZCD" --eval -y "$TMPXLSX2"

echo "Comparing against Excel reference..."
if "$SCRIPT_DIR/compare.sh" "$ORIGINAL" "$TMPXLSX2" --config "$SCRIPT_DIR/config.json"; then
    echo "PASS [$CATEGORY] ZCD roundtrip"
else
    echo "FAIL [$CATEGORY] ZCD roundtrip"
    FAILED=1
fi

# --- Summary ---
echo ""
if [ $FAILED -eq 0 ]; then
    echo "PASS [$CATEGORY] (all flows)"
else
    echo "FAIL [$CATEGORY] (one or more flows failed)"
    exit 1
fi
