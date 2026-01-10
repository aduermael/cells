#!/bin/bash
# LOC Evolution Tracker - collects historical LOC data using CLOC
# Usage: ./scripts/loc-tracker.sh [--full-rebuild]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
STATS_DIR="$PROJECT_ROOT/stats"
HISTORY_FILE="$STATS_DIR/loc-history.json"

# Parse arguments
FULL_REBUILD=false
for arg in "$@"; do
    case $arg in
        --full-rebuild)
            FULL_REBUILD=true
            shift
            ;;
    esac
done

# Ensure stats directory exists
mkdir -p "$STATS_DIR"

# Check for CLOC
if ! command -v cloc &> /dev/null; then
    echo "Error: cloc not installed. Install with: brew install cloc" >&2
    exit 1
fi

# Check for jq
if ! command -v jq &> /dev/null; then
    echo "Error: jq not installed. Install with: brew install jq" >&2
    exit 1
fi

cd "$PROJECT_ROOT"

# Initialize history file if it doesn't exist or if full rebuild requested
if [ ! -f "$HISTORY_FILE" ] || [ "$FULL_REBUILD" = true ]; then
    echo '{"generated": "", "history": []}' > "$HISTORY_FILE"
fi

# Get the last processed date
LAST_DATE=""
if [ -f "$HISTORY_FILE" ]; then
    LAST_DATE=$(jq -r '.history[-1].date // empty' "$HISTORY_FILE" 2>/dev/null || echo "")
fi

# Get list of unique dates with commits (format: YYYY-MM-DD)
# For each date, we want the LAST commit of that day
get_dates_to_process() {
    if [ -n "$LAST_DATE" ] && [ "$FULL_REBUILD" = false ]; then
        # Get dates after LAST_DATE
        git log --format="%ci" --reverse | cut -d' ' -f1 | sort -u | awk -v last="$LAST_DATE" '$0 > last'
    else
        # Get all dates
        git log --format="%ci" --reverse | cut -d' ' -f1 | sort -u
    fi
}

DATES_TO_PROCESS=$(get_dates_to_process)

if [ -z "$DATES_TO_PROCESS" ]; then
    echo "No new dates to process." >&2
    exit 0
fi

DATE_COUNT=$(echo "$DATES_TO_PROCESS" | wc -l | tr -d ' ')
echo "Processing $DATE_COUNT date(s)..." >&2

# Temporary directory for worktrees
WORKTREE_BASE="/tmp/cells-loc-tracker-$$"
mkdir -p "$WORKTREE_BASE"
trap "rm -rf '$WORKTREE_BASE'" EXIT

# Function to get last commit of a given date
get_last_commit_of_date() {
    local date="$1"
    git log --format="%H" --until="${date}T23:59:59" --since="${date}T00:00:00" -1
}

# Function to run CLOC and extract totals
run_cloc_product() {
    local path="$1"
    cloc --json \
         --exclude-dir=bazel-bin,bazel-out,bazel-cells,bazel-testlogs,node_modules,dist,.cache,external,third_party \
         --not-match-f='_test\.(cc|cpp|go)|\.test\.(mjs|js|ts)|\.spec\.(mjs|js|ts)|/tests/' \
         --exclude-ext=md \
         --quiet \
         "$path" 2>/dev/null | jq -r '.SUM.code // 0'
}

run_cloc_docs() {
    local path="$1"
    cloc --json \
         --exclude-dir=bazel-bin,bazel-out,bazel-cells,bazel-testlogs,node_modules,dist,.cache,external,third_party \
         --include-ext=md \
         --quiet \
         "$path" 2>/dev/null | jq -r '.SUM.code // 0'
}

run_cloc_test() {
    local path="$1"
    # First, count files in /tests/ directories
    local tests_dir_count=0
    if [ -d "$path/apps/wasm/tests" ]; then
        tests_dir_count=$(cloc --json \
             --exclude-dir=node_modules \
             --quiet \
             "$path/apps/wasm/tests" 2>/dev/null | jq -r '.SUM.code // 0')
    fi

    # Then count _test.* and *.test.* files elsewhere
    local test_file_count=$(cloc --json \
         --exclude-dir=bazel-bin,bazel-out,bazel-cells,bazel-testlogs,node_modules,dist,.cache,external,third_party,tests \
         --match-f='_test\.(cc|cpp|go)|\.test\.(mjs|js|ts)|\.spec\.(mjs|js|ts)' \
         --quiet \
         "$path" 2>/dev/null | jq -r '.SUM.code // 0')

    echo $((tests_dir_count + test_file_count))
}

# Process each date
CURRENT=0
for DATE in $DATES_TO_PROCESS; do
    CURRENT=$((CURRENT + 1))
    COMMIT=$(get_last_commit_of_date "$DATE")

    if [ -z "$COMMIT" ]; then
        echo "  [$CURRENT/$DATE_COUNT] Skipping $DATE (no commit found)" >&2
        continue
    fi

    SHORT_COMMIT=$(echo "$COMMIT" | cut -c1-7)
    echo "  [$CURRENT/$DATE_COUNT] Processing $DATE ($SHORT_COMMIT)..." >&2

    WORKTREE_PATH="$WORKTREE_BASE/$DATE"

    # Create worktree for this commit
    git worktree add --detach "$WORKTREE_PATH" "$COMMIT" 2>/dev/null || {
        echo "    Warning: Could not create worktree for $COMMIT, skipping" >&2
        continue
    }

    # Run CLOC
    PRODUCT_LINES=$(run_cloc_product "$WORKTREE_PATH")
    TEST_LINES=$(run_cloc_test "$WORKTREE_PATH")
    DOCS_LINES=$(run_cloc_docs "$WORKTREE_PATH")

    # Remove worktree
    git worktree remove --force "$WORKTREE_PATH" 2>/dev/null || rm -rf "$WORKTREE_PATH"

    # Append to history
    NEW_ENTRY=$(jq -n \
        --arg date "$DATE" \
        --arg commit "$SHORT_COMMIT" \
        --argjson product "$PRODUCT_LINES" \
        --argjson test "$TEST_LINES" \
        --argjson docs "$DOCS_LINES" \
        '{date: $date, commit: $commit, productTotal: $product, testTotal: $test, docsTotal: $docs}')

    # Update history file
    jq --argjson entry "$NEW_ENTRY" '.history += [$entry]' "$HISTORY_FILE" > "$HISTORY_FILE.tmp"
    mv "$HISTORY_FILE.tmp" "$HISTORY_FILE"

    echo "    Product: $PRODUCT_LINES, Test: $TEST_LINES, Docs: $DOCS_LINES" >&2
done

# Update generated timestamp
jq --arg ts "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '.generated = $ts' "$HISTORY_FILE" > "$HISTORY_FILE.tmp"
mv "$HISTORY_FILE.tmp" "$HISTORY_FILE"

echo "Done! Results saved to $HISTORY_FILE" >&2
