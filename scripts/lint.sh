#!/bin/bash
# Run clang-tidy linter on C++ source files
#
# Usage:
#   ./scripts/lint.sh          # Lint all .cc files
#   ./scripts/lint.sh --fix    # Lint and auto-fix issues
#   ./scripts/lint.sh FILE...  # Lint specific files

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Check if clang-tidy is installed
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy not found${NC}"
    echo "Install with: brew install llvm"
    echo "  and add to PATH: export PATH=\"\$(brew --prefix llvm)/bin:\$PATH\""
    exit 1
fi

# Check for compile_commands.json
COMPILE_DB="$PROJECT_ROOT/compile_commands.json"
if [ ! -f "$COMPILE_DB" ]; then
    echo -e "${YELLOW}Warning: compile_commands.json not found${NC}"
    echo "Generate it with: bazel run @hedron_compile_commands//:refresh_all"
    echo "Proceeding without compilation database..."
    COMPILE_DB=""
fi

# Get Bazel external directory for third-party includes
BAZEL_OUTPUT_BASE=$(bazel info output_base 2>/dev/null || bazelisk info output_base 2>/dev/null || echo "")
PUGIXML_INCLUDE=""
if [ -n "$BAZEL_OUTPUT_BASE" ]; then
    PUGIXML_DIR=$(find "$BAZEL_OUTPUT_BASE/external" -name "pugixml.hpp" -type f 2>/dev/null | grep -v openxlsx | head -1 | xargs dirname 2>/dev/null || echo "")
    if [ -n "$PUGIXML_DIR" ]; then
        PUGIXML_INCLUDE="-I$PUGIXML_DIR"
    fi
fi

# Parse arguments
FIX_MODE=false
FILES=()

for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
            ;;
        *)
            FILES+=("$arg")
            ;;
    esac
done

# Find files to lint (only .cc files, not headers or test files)
# Test files (_test.cc) are excluded because they depend on external test
# framework headers (gtest) that aren't available to clang-tidy
# Native files (core/net/native/) are excluded because they depend on
# libdatachannel headers built by cmake (not available to clang-tidy)
if [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r -d '' file; do
        FILES+=("$file")
    done < <(find "$PROJECT_ROOT/core" -type f -name "*.cc" \
        ! -name "*_test.cc" \
        ! -path "*/net/native/*" \
        -print0 2>/dev/null)
fi

if [ ${#FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}No C++ source files found${NC}"
    exit 0
fi

# Build clang-tidy command
TIDY_CMD=(clang-tidy)

if [ -n "$COMPILE_DB" ]; then
    TIDY_CMD+=(-p "$PROJECT_ROOT")
fi

if $FIX_MODE; then
    TIDY_CMD+=(--fix)
fi

# Determine parallelism (use number of CPU cores, or fall back to 4)
NPROCS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Run clang-tidy
echo -e "${GREEN}Running clang-tidy on ${#FILES[@]} files with $NPROCS parallel jobs...${NC}"
echo ""

# Create temp directory for output files
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Build the extra args string for clang-tidy
EXTRA_ARGS="-std=c++17 -I$PROJECT_ROOT -I$PROJECT_ROOT/third_party/miniz"
if [ -n "${PUGIXML_INCLUDE:-}" ]; then
    EXTRA_ARGS="$EXTRA_ARGS $PUGIXML_INCLUDE"
fi

# Function to lint a single file (exported for xargs)
lint_one_file() {
    local file="$1"
    local tmpdir="$2"
    local project_root="$3"
    local fix_mode="$4"
    local extra_args="$5"

    if [ ! -f "$file" ]; then
        return 0
    fi

    # Create output file named after the source file
    local outfile="$tmpdir/$(echo "$file" | tr '/' '_').out"
    local exitfile="$tmpdir/$(echo "$file" | tr '/' '_').exit"

    # Build command
    local cmd=(clang-tidy)
    if [ -f "$project_root/compile_commands.json" ]; then
        cmd+=(-p "$project_root")
    fi
    if [ "$fix_mode" = "true" ]; then
        cmd+=(--fix)
    fi

    # Run clang-tidy
    local exit_code=0
    "${cmd[@]}" \
        --config-file="$project_root/.clang-tidy" \
        "$file" \
        -- \
        $extra_args \
        > "$outfile" 2>&1 || exit_code=$?

    echo "$exit_code" > "$exitfile"
}
export -f lint_one_file

# Run linting in parallel
printf '%s\n' "${FILES[@]}" | xargs -P "$NPROCS" -I {} bash -c \
    'lint_one_file "$1" "$2" "$3" "$4" "$5"' _ {} "$TMPDIR" "$PROJECT_ROOT" "$FIX_MODE" "$EXTRA_ARGS"

# Collect results
FAILED=0
for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        continue
    fi

    local_outfile="$TMPDIR/$(echo "$file" | tr '/' '_').out"
    local_exitfile="$TMPDIR/$(echo "$file" | tr '/' '_').exit"

    if [ -f "$local_exitfile" ]; then
        exit_code=$(cat "$local_exitfile")
        if [ "$exit_code" -ne 0 ]; then
            FAILED=1
        fi
    fi

    # Only print output if there's something meaningful
    if [ -f "$local_outfile" ]; then
        filtered=$(grep -v -E "^$|warnings generated|Suppressed .* warnings|Use -header-filter|^Error while processing|^Found compiler error" "$local_outfile" 2>/dev/null || true)
        if [ -n "$filtered" ]; then
            echo -e "${YELLOW}Checking:${NC} $file"
            echo "$filtered"
        fi
    fi
done

echo ""
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}Linting completed with warnings/errors${NC}"
    exit 1
else
    echo -e "${GREEN}Linting passed${NC}"
fi
