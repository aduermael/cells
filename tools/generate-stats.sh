#!/bin/bash
# Generate project stats for README
# Usage: ./tools/generate-stats.sh [--build] [--update]
# Use --build to rebuild WASM before calculating sizes
# Use --update to update README.md and commit

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Parse arguments
BUILD_WASM=false
UPDATE_README=false
for arg in "$@"; do
    case $arg in
        --build)
            BUILD_WASM=true
            shift
            ;;
        --update)
            UPDATE_README=true
            shift
            ;;
    esac
done

# Check for CLOC
if ! command -v cloc &> /dev/null; then
    echo "Error: cloc not installed. Install with: brew install cloc" >&2
    exit 1
fi

CLOC_EXCLUDE="bazel-bin,bazel-out,bazel-cells,bazel-testlogs,node_modules,dist,.cache,external,third_party,.git"

# Run CLOC and get JSON output for source files (excluding tests)
get_cloc_source_json() {
    cloc --json \
         --exclude-dir="$CLOC_EXCLUDE" \
         --not-match-f='_test\.(cc|cpp|go)|\.test\.(mjs|js|ts)|\.spec\.(mjs|js|ts)|/tests/' \
         --quiet \
         "$PROJECT_ROOT" 2>/dev/null
}

# Run CLOC for test files in /tests/ directories
get_cloc_tests_dir_json() {
    if [ -d "$PROJECT_ROOT/apps/wasm/tests" ]; then
        cloc --json \
             --exclude-dir=node_modules \
             --quiet \
             "$PROJECT_ROOT/apps/wasm/tests" 2>/dev/null
    else
        echo '{}'
    fi
}

# Run CLOC for _test.* and *.test.* files
get_cloc_test_files_json() {
    cloc --json \
         --exclude-dir="$CLOC_EXCLUDE,tests" \
         --match-f='_test\.(cc|cpp|go)|\.test\.(mjs|js|ts)|\.spec\.(mjs|js|ts)' \
         --quiet \
         "$PROJECT_ROOT" 2>/dev/null || echo '{}'
}

# Extract code lines for a language from CLOC JSON
extract_lines() {
    local json="$1"
    local lang="$2"
    echo "$json" | jq -r ".[\"$lang\"].code // 0" 2>/dev/null || echo 0
}

# Function to format bytes to human readable
format_size() {
    local bytes=$1
    local kb=$((bytes / 1024))

    if [ "$kb" -ge 1024 ]; then
        echo "${kb}" | awk '{printf "%.2f MB", $1/1024}'
    else
        echo "${kb} KB"
    fi
}

# -----------------------------------------------------------------------------
# Collect all stats
# -----------------------------------------------------------------------------

# Build WASM if requested
if [ "$BUILD_WASM" = true ]; then
    echo "Building WASM distribution..." >&2
    make wasm-dist > /dev/null 2>&1
fi

echo "Collecting stats with CLOC..." >&2

# Get CLOC data for source files
SOURCE_JSON=$(get_cloc_source_json)

# Extract source lines for each language (CLOC uses specific language names)
cpp_lines=$(extract_lines "$SOURCE_JSON" "C++")
# Add header files to C++ count
cpp_header_lines=$(extract_lines "$SOURCE_JSON" "C/C++ Header")
cpp_lines=$((cpp_lines + cpp_header_lines))

js_lines=$(extract_lines "$SOURCE_JSON" "JavaScript")
ts_lines=$(extract_lines "$SOURCE_JSON" "TypeScript")
html_lines=$(extract_lines "$SOURCE_JSON" "HTML")
css_lines=$(extract_lines "$SOURCE_JSON" "CSS")
go_lines=$(extract_lines "$SOURCE_JSON" "Go")
luau_lines=$(extract_lines "$SOURCE_JSON" "Luau")
objcpp_lines=$(extract_lines "$SOURCE_JSON" "Objective-C++")
md_lines=$(extract_lines "$SOURCE_JSON" "Markdown")
sh_lines=$(extract_lines "$SOURCE_JSON" "Bourne Shell")
# Starlark: CLOC may report as "Starlark" or "Bazel" depending on file extension
bzl_lines=$(($(extract_lines "$SOURCE_JSON" "Starlark") + $(extract_lines "$SOURCE_JSON" "Bazel")))

# Get CLOC data for test files (two separate calls for simpler parsing)
TESTS_DIR_JSON=$(get_cloc_tests_dir_json)
TEST_FILES_JSON=$(get_cloc_test_files_json)

# Extract test lines (combine both sources)
cpp_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "C++") + $(extract_lines "$TESTS_DIR_JSON" "C++")))
objcpp_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "Objective-C++") + $(extract_lines "$TESTS_DIR_JSON" "Objective-C++")))
js_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "JavaScript") + $(extract_lines "$TESTS_DIR_JSON" "JavaScript")))
ts_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "TypeScript") + $(extract_lines "$TESTS_DIR_JSON" "TypeScript")))
go_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "Go") + $(extract_lines "$TESTS_DIR_JSON" "Go")))
luau_test_lines=$(($(extract_lines "$TEST_FILES_JSON" "Luau") + $(extract_lines "$TESTS_DIR_JSON" "Luau")))

# Count C++ unit tests (TEST and TEST_F macros)
cpp_unit_tests=0
for f in "$PROJECT_ROOT"/core/cells/*_test.cc "$PROJECT_ROOT"/core/cells/functions/*_test.cc "$PROJECT_ROOT"/core/net/*_test.cc; do
    if [ -f "$f" ]; then
        count=$(grep -cE "^TEST|^TEST_F" "$f" 2>/dev/null) || count=0
        cpp_unit_tests=$((cpp_unit_tests + count))
    fi
done

# Count Go unit tests (func Test functions)
go_unit_tests=0
for f in "$PROJECT_ROOT"/tools/serve/*_test.go; do
    if [ -f "$f" ]; then
        count=$(grep -c "func Test" "$f" 2>/dev/null) || count=0
        go_unit_tests=$((go_unit_tests + count))
    fi
done

# Count JavaScript E2E tests
e2e_tests=0
for f in "$PROJECT_ROOT"/apps/wasm/tests/*.test.mjs; do
    if [ -f "$f" ]; then
        count=$(grep -cE "': async|runTest\(" "$f" 2>/dev/null) || count=0
        e2e_tests=$((e2e_tests + count))
    fi
done

# Count JavaScript unit tests
js_unit_tests=0
for f in "$PROJECT_ROOT"/apps/wasm/tests/unit/*.test.mjs; do
    if [ -f "$f" ]; then
        count=$(grep -c "^test(" "$f" 2>/dev/null) || count=0
        js_unit_tests=$((js_unit_tests + count))
    fi
done

total_tests=$((cpp_unit_tests + go_unit_tests + js_unit_tests + e2e_tests))

# Git stats
commit_count=$(git rev-list --count HEAD)

# Build sizes
wasm_size="Not built"
bundle_size="Not built"
if [ -d "$PROJECT_ROOT/dist/wasm" ]; then
    wasm_file="$PROJECT_ROOT/dist/wasm/cells_wasm_bin.wasm"
    if [ -f "$wasm_file" ]; then
        wasm_bytes=$(stat -f%z "$wasm_file" 2>/dev/null || stat -c%s "$wasm_file" 2>/dev/null)
        wasm_size=$(format_size $wasm_bytes)
    fi

    total_bytes=0
    while IFS= read -r file; do
        size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
        total_bytes=$((total_bytes + size))
    done < <(find "$PROJECT_ROOT/dist/wasm" -type f)

    if [ "$total_bytes" -gt 0 ]; then
        bundle_size=$(format_size $total_bytes)
    fi
fi

# -----------------------------------------------------------------------------
# Generate README section
# -----------------------------------------------------------------------------

generate_readme_section() {
    # Create temp files for sorting
    local TEMP_FILE=$(mktemp)
    local TEMP_TEST_FILE=$(mktemp)
    trap "rm -f $TEMP_FILE $TEMP_TEST_FILE" RETURN

    # Add all languages with lines > 0 to source table (excluding Markdown - that's documentation)
    [ "$cpp_lines" -gt 0 ] && echo "$cpp_lines C++" >> "$TEMP_FILE"
    [ "$js_lines" -gt 0 ] && echo "$js_lines JavaScript" >> "$TEMP_FILE"
    [ "$ts_lines" -gt 0 ] && echo "$ts_lines TypeScript" >> "$TEMP_FILE"
    [ "$html_lines" -gt 0 ] && echo "$html_lines HTML" >> "$TEMP_FILE"
    [ "$css_lines" -gt 0 ] && echo "$css_lines CSS" >> "$TEMP_FILE"
    [ "$go_lines" -gt 0 ] && echo "$go_lines Go" >> "$TEMP_FILE"
    [ "$luau_lines" -gt 0 ] && echo "$luau_lines Luau" >> "$TEMP_FILE"
    [ "$objcpp_lines" -gt 0 ] && echo "$objcpp_lines Objective-C++" >> "$TEMP_FILE"
    [ "$bzl_lines" -gt 0 ] && echo "$bzl_lines Starlark" >> "$TEMP_FILE"
    [ "$sh_lines" -gt 0 ] && echo "$sh_lines Shell" >> "$TEMP_FILE"

    # Add test lines to test table
    [ "$cpp_test_lines" -gt 0 ] && echo "$cpp_test_lines C++" >> "$TEMP_TEST_FILE"
    [ "$objcpp_test_lines" -gt 0 ] && echo "$objcpp_test_lines Objective-C++" >> "$TEMP_TEST_FILE"
    [ "$js_test_lines" -gt 0 ] && echo "$js_test_lines JavaScript" >> "$TEMP_TEST_FILE"
    [ "$ts_test_lines" -gt 0 ] && echo "$ts_test_lines TypeScript" >> "$TEMP_TEST_FILE"
    [ "$go_test_lines" -gt 0 ] && echo "$go_test_lines Go" >> "$TEMP_TEST_FILE"
    [ "$luau_test_lines" -gt 0 ] && echo "$luau_test_lines Luau" >> "$TEMP_TEST_FILE"

    echo "## Project Stats"
    echo ""
    echo "### Source Code"
    echo ""
    echo "| Language | Lines |"
    echo "|----------|------:|"

    sort -rn "$TEMP_FILE" | while read lines lang; do
        formatted=$(echo "$lines" | awk '{printf "%'\''d", $1}')
        echo "| $lang | $formatted |"
    done

    echo ""
    echo "### Test Code"
    echo ""
    echo "| Language | Lines |"
    echo "|----------|------:|"

    if [ -s "$TEMP_TEST_FILE" ]; then
        sort -rn "$TEMP_TEST_FILE" | while read lines lang; do
            formatted=$(echo "$lines" | awk '{printf "%'\''d", $1}')
            echo "| $lang | $formatted |"
        done
    else
        echo "| (none) | 0 |"
    fi

    echo ""
    echo "### Documentation"
    echo ""
    echo "| Language | Lines |"
    echo "|----------|------:|"
    if [ "$md_lines" -gt 0 ]; then
        formatted=$(echo "$md_lines" | awk '{printf "%'\''d", $1}')
        echo "| Markdown | $formatted |"
    else
        echo "| (none) | 0 |"
    fi

    echo ""
    echo "### Test Counts"
    echo ""
    echo "| Category | Tests |"
    echo "|----------|------:|"
    echo "| Unit (C++) | $cpp_unit_tests |"
    [ "$go_unit_tests" -gt 0 ] && echo "| Unit (Go) | $go_unit_tests |"
    [ "$js_unit_tests" -gt 0 ] && echo "| Unit (JavaScript) | $js_unit_tests |"
    echo "| E2E (Puppeteer) | $e2e_tests |"
    echo "| **Total** | **$total_tests** |"

    echo ""
    echo "- **Commits**: $commit_count"
    echo "- **WASM Module**: $wasm_size"
    echo "- **Total Web Bundle**: $bundle_size"
    echo ""
    echo "<sub>Lines counted with [CLOC](https://github.com/AlDanial/cloc) (excludes comments and blanks). Generated with \`./tools/generate-stats.sh\`</sub>"
    echo ""
    echo "### LOC Evolution"
    echo ""
    echo '<img src="stats/loc-evolution.svg" alt="Lines of Code Evolution" width="100%">'
    echo ""
    echo "<sub>Actual lines of code (excluding comments and blanks), tracked with [CLOC](https://github.com/AlDanial/cloc). Generate with \`./tools/loc-tracker.sh && node tools/generate-loc-svg.mjs\`</sub>"
    echo ""
    echo "### Diff Size Evolution"
    echo ""
    echo '<img src="stats/diff-size-evolution.svg" alt="Diff Size Evolution" width="100%">'
    echo ""
    echo "<sub>Average diff size per commit (lines added + removed, code files only). Generate with \`./tools/diff-tracker.sh && node tools/generate-diff-svg.mjs\`</sub>"
}

# -----------------------------------------------------------------------------
# Output
# -----------------------------------------------------------------------------

if [ "$UPDATE_README" = true ]; then
    README_FILE="$PROJECT_ROOT/README.md"
    TEMP_SECTION=$(mktemp)
    TEMP_OUTPUT=$(mktemp)
    trap "rm -f $TEMP_SECTION $TEMP_OUTPUT" EXIT

    # Generate new stats section to temp file
    generate_readme_section > "$TEMP_SECTION"

    # Replace section in README using awk with file input
    awk '
        /^## Project Stats/ {
            in_section=1
            while ((getline line < "'"$TEMP_SECTION"'") > 0) print line
            next
        }
        /^## / && in_section { in_section=0 }
        !in_section { print }
    ' "$README_FILE" > "$TEMP_OUTPUT" && mv "$TEMP_OUTPUT" "$README_FILE"

    echo "Updated README.md tables"

    # Update LOC evolution graph
    echo "Updating LOC evolution graph..."
    "$SCRIPT_DIR/loc-tracker.sh"
    node "$SCRIPT_DIR/generate-loc-svg.mjs"

    # Update diff size evolution graph
    echo "Updating diff size evolution graph..."
    "$SCRIPT_DIR/diff-tracker.sh"
    node "$SCRIPT_DIR/generate-diff-svg.mjs"

    # Commit all changes
    git add README.md stats/
    git commit -m "Update project stats

- Source: $(echo "$cpp_lines" | awk '{printf "%'\''d", $1}') C++, $(echo "$ts_lines" | awk '{printf "%'\''d", $1}') TypeScript
- Tests: $total_tests total ($cpp_unit_tests C++ unit, $e2e_tests E2E)
- Commits: $commit_count

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>"

    echo "Committed changes"
else
    # CLI output format
    echo "# Project Stats"
    echo ""
    echo "Generated: $(date '+%Y-%m-%d')"
    echo ""

    generate_readme_section | tail -n +2  # Skip the "## Project Stats" line for CLI

    echo ""
    echo "---"
    echo ""
    echo "To update README and commit: \`./tools/generate-stats.sh --update\`"
fi
