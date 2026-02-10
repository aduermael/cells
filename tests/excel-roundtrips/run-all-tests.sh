#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/data"

# Collect categories: from args or auto-discover
CATEGORIES=()
if [ $# -gt 0 ]; then
    CATEGORIES=("$@")
else
    for dir in "$DATA_DIR"/*/; do
        cat="$(basename "$dir")"
        # Only include categories that have both required files
        if [ -f "$dir/file.xlsx" ] && [ -f "$dir/file_no_cached_results.xlsx" ]; then
            CATEGORIES+=("$cat")
        fi
    done
fi

if [ ${#CATEGORIES[@]} -eq 0 ]; then
    echo "No test categories found." >&2
    exit 1
fi

# Run each category
PASS=0
FAIL=0
RESULTS=()

for cat in "${CATEGORIES[@]}"; do
    if "$SCRIPT_DIR/run-test.sh" "$cat" 2>&1; then
        RESULTS+=("PASS  $cat")
        PASS=$((PASS + 1))
    else
        RESULTS+=("FAIL  $cat")
        FAIL=$((FAIL + 1))
    fi
    echo ""
done

# Summary
echo "==============================="
echo "  RESULTS"
echo "==============================="
for result in "${RESULTS[@]}"; do
    echo "  $result"
done
echo "-------------------------------"
echo "  $PASS passed, $FAIL failed (${#CATEGORIES[@]} total)"
echo "==============================="

[ "$FAIL" -eq 0 ]
