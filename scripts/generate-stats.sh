#!/bin/bash
# Generate project stats for README
# Usage: ./scripts/generate-stats.sh [--build]
# Use --build to rebuild WASM before calculating sizes

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Parse arguments
BUILD_WASM=false
for arg in "$@"; do
    case $arg in
        --build)
            BUILD_WASM=true
            shift
            ;;
    esac
done

echo "# Project Stats"
echo ""
echo "Generated: $(date '+%Y-%m-%d')"
echo ""

# -----------------------------------------------------------------------------
# Lines of Code
# -----------------------------------------------------------------------------

echo "## Lines of Code"
echo ""

EXCLUDE_PATTERN="bazel-|\.git|node_modules|dist|compile_commands|\.cache|external"

# Function to count lines for source files (excluding tests)
count_source_lines() {
    local patterns="$1"
    local total=0

    for pattern in $patterns; do
        count=$(find "$PROJECT_ROOT" -type f -name "$pattern" 2>/dev/null | \
            grep -vE "$EXCLUDE_PATTERN" | \
            grep -vE "_test\.(cc|cpp|go|js|ts)$|\.test\.(js|ts)$|\.spec\.(js|ts)$|test_.*\.(cc|cpp)$" | \
            xargs cat 2>/dev/null | wc -l | tr -d ' ')
        total=$((total + count))
    done
    echo "$total"
}

# Function to count lines for test files
count_test_lines() {
    local patterns="$1"
    local total=0

    for pattern in $patterns; do
        count=$(find "$PROJECT_ROOT" -type f -name "$pattern" 2>/dev/null | \
            grep -vE "$EXCLUDE_PATTERN" | \
            grep -E "_test\.(cc|cpp|go|js|ts)$|\.test\.(js|ts)$|\.spec\.(js|ts)$|test_.*\.(cc|cpp)$" | \
            xargs cat 2>/dev/null | wc -l | tr -d ' ')
        total=$((total + count))
    done
    echo "$total"
}

# Count source lines for each language (excluding tests)
cpp_lines=$(count_source_lines "*.cc *.cpp *.h")
js_lines=$(count_source_lines "*.js")
ts_lines=$(count_source_lines "*.ts")
html_lines=$(count_source_lines "*.html")
css_lines=$(count_source_lines "*.css")
go_lines=$(count_source_lines "*.go")
md_lines=$(count_source_lines "*.md")
sh_lines=$(count_source_lines "*.sh")

# Starlark needs special handling for BUILD files
bzl_lines=$(find "$PROJECT_ROOT" -type f \( -name "BUILD" -o -name "*.bzl" -o -name "BUILD.bazel" \) 2>/dev/null | \
    grep -vE "$EXCLUDE_PATTERN" | xargs cat 2>/dev/null | wc -l | tr -d ' ')

# Count test lines for each language
cpp_test_lines=$(count_test_lines "*.cc *.cpp")
js_test_lines=$(count_test_lines "*.js")
ts_test_lines=$(count_test_lines "*.ts")
go_test_lines=$(count_test_lines "*.go")

# Create temp files for sorting
TEMP_FILE=$(mktemp)
TEMP_TEST_FILE=$(mktemp)
trap "rm -f $TEMP_FILE $TEMP_TEST_FILE" EXIT

# Add all languages with lines > 0 to source table
[ "$cpp_lines" -gt 0 ] && echo "$cpp_lines C++" >> "$TEMP_FILE"
[ "$js_lines" -gt 0 ] && echo "$js_lines JavaScript" >> "$TEMP_FILE"
[ "$ts_lines" -gt 0 ] && echo "$ts_lines TypeScript" >> "$TEMP_FILE"
[ "$html_lines" -gt 0 ] && echo "$html_lines HTML" >> "$TEMP_FILE"
[ "$css_lines" -gt 0 ] && echo "$css_lines CSS" >> "$TEMP_FILE"
[ "$go_lines" -gt 0 ] && echo "$go_lines Go" >> "$TEMP_FILE"
[ "$md_lines" -gt 0 ] && echo "$md_lines Markdown" >> "$TEMP_FILE"
[ "$bzl_lines" -gt 0 ] && echo "$bzl_lines Starlark" >> "$TEMP_FILE"
[ "$sh_lines" -gt 0 ] && echo "$sh_lines Shell" >> "$TEMP_FILE"

# Add test lines to test table
[ "$cpp_test_lines" -gt 0 ] && echo "$cpp_test_lines C++" >> "$TEMP_TEST_FILE"
[ "$js_test_lines" -gt 0 ] && echo "$js_test_lines JavaScript" >> "$TEMP_TEST_FILE"
[ "$ts_test_lines" -gt 0 ] && echo "$ts_test_lines TypeScript" >> "$TEMP_TEST_FILE"
[ "$go_test_lines" -gt 0 ] && echo "$go_test_lines Go" >> "$TEMP_TEST_FILE"

echo "### Source Code"
echo ""
echo "| Language | Lines |"
echo "|----------|------:|"

# Sort and display source code
sort -rn "$TEMP_FILE" | while read lines lang; do
    # Format with thousands separator (portable)
    formatted=$(echo "$lines" | awk '{printf "%'\''d", $1}')
    echo "| $lang | $formatted |"
done

echo ""
echo "### Test Code"
echo ""
echo "| Language | Lines |"
echo "|----------|------:|"

# Sort and display test code
if [ -s "$TEMP_TEST_FILE" ]; then
    sort -rn "$TEMP_TEST_FILE" | while read lines lang; do
        formatted=$(echo "$lines" | awk '{printf "%'\''d", $1}')
        echo "| $lang | $formatted |"
    done
else
    echo "| (none) | 0 |"
fi

echo ""

# -----------------------------------------------------------------------------
# Git Stats
# -----------------------------------------------------------------------------

echo "## Repository Stats"
echo ""

commit_count=$(git rev-list --count HEAD)
echo "- **Commits**: $commit_count"
echo ""

# -----------------------------------------------------------------------------
# Build Sizes
# -----------------------------------------------------------------------------

echo "## Build Sizes"
echo ""

# Build WASM if requested
if [ "$BUILD_WASM" = true ]; then
    echo "Building WASM distribution..."
    make wasm-dist > /dev/null 2>&1
    echo ""
fi

# Function to format bytes to human readable
format_size() {
    local bytes=$1
    local kb=$((bytes / 1024))

    if [ "$kb" -ge 1024 ]; then
        # Use awk for floating point
        echo "${kb}" | awk '{printf "%.2f MB", $1/1024}'
    else
        echo "${kb} KB"
    fi
}

# Check if dist exists
if [ -d "$PROJECT_ROOT/dist" ]; then
    # WASM file size
    wasm_file="$PROJECT_ROOT/dist/cells_wasm_bin.wasm"
    if [ -f "$wasm_file" ]; then
        # macOS uses -f%z, Linux uses -c%s
        wasm_bytes=$(stat -f%z "$wasm_file" 2>/dev/null || stat -c%s "$wasm_file" 2>/dev/null)
        echo "- **WASM Module**: $(format_size $wasm_bytes)"
    else
        echo "- **WASM Module**: Not built (run with --build)"
    fi

    # Total static files size (excluding WASM for separate count)
    total_bytes=0
    while IFS= read -r file; do
        size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
        total_bytes=$((total_bytes + size))
    done < <(find "$PROJECT_ROOT/dist" -type f)

    if [ "$total_bytes" -gt 0 ]; then
        echo "- **Total Web Bundle**: $(format_size $total_bytes)"
    fi
else
    echo "- **WASM Module**: Not built (run with --build)"
    echo "- **Total Web Bundle**: Not built (run with --build)"
fi

echo ""
echo "---"
echo ""
echo "To rebuild and update stats: \`./scripts/generate-stats.sh --build\`"
