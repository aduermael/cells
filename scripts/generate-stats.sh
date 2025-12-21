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

# Function to count lines for a set of patterns
count_lang_lines() {
    local patterns="$1"
    local total=0

    for pattern in $patterns; do
        count=$(find "$PROJECT_ROOT" -type f -name "$pattern" 2>/dev/null | \
            grep -vE "$EXCLUDE_PATTERN" | \
            xargs cat 2>/dev/null | wc -l | tr -d ' ')
        total=$((total + count))
    done
    echo "$total"
}

# Count lines for each language
cpp_lines=$(count_lang_lines "*.cc *.cpp *.h")
js_lines=$(count_lang_lines "*.js")
ts_lines=$(count_lang_lines "*.ts")
html_lines=$(count_lang_lines "*.html")
css_lines=$(count_lang_lines "*.css")
go_lines=$(count_lang_lines "*.go")
md_lines=$(count_lang_lines "*.md")
sh_lines=$(count_lang_lines "*.sh")

# Starlark needs special handling for BUILD files
bzl_lines=$(find "$PROJECT_ROOT" -type f \( -name "BUILD" -o -name "*.bzl" -o -name "BUILD.bazel" \) 2>/dev/null | \
    grep -vE "$EXCLUDE_PATTERN" | xargs cat 2>/dev/null | wc -l | tr -d ' ')

# Create temp file for sorting
TEMP_FILE=$(mktemp)
trap "rm -f $TEMP_FILE" EXIT

# Add languages with >= 500 lines
[ "$cpp_lines" -ge 500 ] && echo "$cpp_lines C++" >> "$TEMP_FILE"
[ "$js_lines" -ge 500 ] && echo "$js_lines JavaScript" >> "$TEMP_FILE"
[ "$ts_lines" -ge 500 ] && echo "$ts_lines TypeScript" >> "$TEMP_FILE"
[ "$html_lines" -ge 500 ] && echo "$html_lines HTML" >> "$TEMP_FILE"
[ "$css_lines" -ge 500 ] && echo "$css_lines CSS" >> "$TEMP_FILE"
[ "$go_lines" -ge 500 ] && echo "$go_lines Go" >> "$TEMP_FILE"
[ "$md_lines" -ge 500 ] && echo "$md_lines Markdown" >> "$TEMP_FILE"
[ "$bzl_lines" -ge 500 ] && echo "$bzl_lines Starlark" >> "$TEMP_FILE"
[ "$sh_lines" -ge 500 ] && echo "$sh_lines Shell" >> "$TEMP_FILE"

echo "| Language | Lines |"
echo "|----------|------:|"

# Sort and display
sort -rn "$TEMP_FILE" | while read lines lang; do
    # Format with thousands separator (portable)
    formatted=$(echo "$lines" | awk '{printf "%'\''d", $1}')
    echo "| $lang | $formatted |"
done

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
