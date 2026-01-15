#!/bin/bash
# Diff Size Evolution Tracker - collects historical diff sizes for code files only
# Usage: ./tools/diff-tracker.sh [--full-rebuild]
# Tracks average diff size as a project maturity indicator (smaller diffs = more mature)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
STATS_DIR="$PROJECT_ROOT/stats"
HISTORY_FILE="$STATS_DIR/diff-history.json"

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

# Get the last processed commit
LAST_COMMIT=""
if [ -f "$HISTORY_FILE" ]; then
    LAST_COMMIT=$(jq -r '.history[-1].commit // empty' "$HISTORY_FILE" 2>/dev/null || echo "")
fi

# File patterns to EXCLUDE (tests, docs, etc.)
# These patterns match test files and documentation
EXCLUDE_PATTERNS=(
    '*_test.cc'
    '*_test.cpp'
    '*_test.go'
    '*.test.mjs'
    '*.test.js'
    '*.test.ts'
    '*.spec.mjs'
    '*.spec.js'
    '*.spec.ts'
    '*.md'
    '*.txt'
    '*.json'
    '*.yaml'
    '*.yml'
    'BUILD'
    'BUILD.bazel'
    'WORKSPACE'
    '*.bzl'
    'package.json'
    'package-lock.json'
    'pnpm-lock.yaml'
    '.gitignore'
    'LICENSE'
    'Makefile'
)

# Build the exclude pattern for git diff
build_exclude_args() {
    local args=""
    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        args="$args -- . ':!$pattern'"
    done
    # Also exclude test directories
    args="$args ':!**/tests/**' ':!**/test/**'"
    echo "$args"
}

# Get commits to process
get_commits_to_process() {
    if [ -n "$LAST_COMMIT" ] && [ "$FULL_REBUILD" = false ]; then
        # Get commits after LAST_COMMIT
        git rev-list --reverse "$LAST_COMMIT"..HEAD 2>/dev/null || git rev-list --reverse HEAD
    else
        # Get all commits
        git rev-list --reverse HEAD
    fi
}

COMMITS_TO_PROCESS=$(get_commits_to_process)

if [ -z "$COMMITS_TO_PROCESS" ]; then
    echo "No new commits to process." >&2
    exit 0
fi

COMMIT_COUNT=$(echo "$COMMITS_TO_PROCESS" | wc -l | tr -d ' ')
echo "Processing $COMMIT_COUNT commit(s)..." >&2

# Process each commit
CURRENT=0
for COMMIT in $COMMITS_TO_PROCESS; do
    CURRENT=$((CURRENT + 1))
    SHORT_COMMIT=$(echo "$COMMIT" | cut -c1-7)
    DATE=$(git log -1 --format="%ci" "$COMMIT" | cut -d' ' -f1)

    # Get diff stats for this commit (code files only)
    # Use numstat for machine-readable output
    DIFF_STATS=$(git diff --numstat "$COMMIT^" "$COMMIT" 2>/dev/null || git diff --numstat --root "$COMMIT" 2>/dev/null || echo "")

    ADDED=0
    REMOVED=0

    while IFS=$'\t' read -r add rem file; do
        # Skip binary files (shown as - -)
        [ "$add" = "-" ] && continue
        [ -z "$file" ] && continue

        # Check if file matches any exclude pattern
        EXCLUDE=false
        for pattern in "${EXCLUDE_PATTERNS[@]}"; do
            case "$file" in
                $pattern) EXCLUDE=true; break ;;
            esac
        done

        # Also check for test directories and other exclusions
        case "$file" in
            */tests/*|*/test/*|*.md|BUILD|BUILD.bazel|WORKSPACE|*.bzl|*.json|*.yaml|*.yml|Makefile|.gitignore|LICENSE|*.txt)
                EXCLUDE=true
                ;;
        esac

        if [ "$EXCLUDE" = false ]; then
            ADDED=$((ADDED + add))
            REMOVED=$((REMOVED + rem))
        fi
    done <<< "$DIFF_STATS"

    TOTAL_DIFF=$((ADDED + REMOVED))

    if [ $((CURRENT % 50)) -eq 0 ] || [ "$CURRENT" -eq "$COMMIT_COUNT" ]; then
        echo "  [$CURRENT/$COMMIT_COUNT] $DATE ($SHORT_COMMIT): +$ADDED -$REMOVED = $TOTAL_DIFF lines" >&2
    fi

    # Append to history
    NEW_ENTRY=$(jq -n \
        --arg date "$DATE" \
        --arg commit "$SHORT_COMMIT" \
        --argjson added "$ADDED" \
        --argjson removed "$REMOVED" \
        --argjson total "$TOTAL_DIFF" \
        '{date: $date, commit: $commit, added: $added, removed: $removed, total: $total}')

    # Update history file
    jq --argjson entry "$NEW_ENTRY" '.history += [$entry]' "$HISTORY_FILE" > "$HISTORY_FILE.tmp"
    mv "$HISTORY_FILE.tmp" "$HISTORY_FILE"
done

# Update generated timestamp
jq --arg ts "$(date -u +%Y-%m-%dT%H:%M:%SZ)" '.generated = $ts' "$HISTORY_FILE" > "$HISTORY_FILE.tmp"
mv "$HISTORY_FILE.tmp" "$HISTORY_FILE"

echo "Done! Results saved to $HISTORY_FILE" >&2
