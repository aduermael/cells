#!/bin/bash
# Format C++ source files using clang-format
#
# Usage:
#   bazel run :format          # Format all files
#   bazel run :format -- --check  # Check formatting (CI mode, no changes)
#   bazel run :format -- FILE...  # Format specific files

set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo -e "${RED}Error: clang-format not found${NC}"
    echo "Install with: brew install clang-format"
    exit 1
fi

echo "Using $(command -v clang-format) ($(clang-format --version | head -1))"

# Always use the repo .clang-format (style=file). Falling back to a built-in
# style (e.g. Google/LLVM defaults) causes CI/local version skew failures.
CLANG_FORMAT_STYLE="file:${REPO_ROOT}/.clang-format"
if [ ! -f "$REPO_ROOT/.clang-format" ]; then
    echo -e "${RED}Error: .clang-format not found at $REPO_ROOT/.clang-format${NC}"
    exit 1
fi

# Parse arguments
CHECK_MODE=false
FILES=()

for arg in "$@"; do
    case $arg in
        --check)
            CHECK_MODE=true
            ;;
        *)
            FILES+=("$arg")
            ;;
    esac
done

# Find files to format if none specified
if [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r -d '' file; do
        FILES+=("$file")
    done < <(find "$REPO_ROOT/core" -type f \( -name "*.cc" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0 2>/dev/null)
fi

if [ ${#FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}No C++ files found${NC}"
    exit 0
fi

# Auto-detect parallelism
get_num_cores() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

NPROCS=${JOBS:-$(get_num_cores)}

# Create temp directory for tracking results
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Function to format/check a single file
format_one_file() {
    local file="$1"
    local tmpdir="$2"
    local check_mode="$3"
    local style="$4"

    if [ ! -f "$file" ]; then
        return 0
    fi

    local exitfile="$tmpdir/$(echo "$file" | tr '/' '_').exit"

    if [ "$check_mode" = "true" ]; then
        # Check mode: verify formatting without changes
        if ! clang-format --style="$style" --dry-run --Werror "$file" 2>/dev/null; then
            echo "FAIL:$file"
            echo "1" > "$exitfile"
            # Surface a short unified diff for CI diagnosis (version skew, etc.)
            if command -v diff >/dev/null 2>&1; then
                clang-format --style="$style" "$file" 2>/dev/null | diff -u "$file" - 2>/dev/null | head -n 40 \
                    > "$tmpdir/$(echo "$file" | tr '/' '_').diff" || true
            fi
        fi
    else
        # Format mode: apply changes
        clang-format --style="$style" -i "$file"
        echo "OK:$file"
    fi
}
export -f format_one_file

# Run formatting in parallel
echo -e "${GREEN}Processing ${#FILES[@]} files with $NPROCS parallel jobs...${NC}"

OUTPUT=$(printf '%s\n' "${FILES[@]}" | xargs -P "$NPROCS" -I {} bash -c \
    'format_one_file "$1" "$2" "$3" "$4"' _ {} "$TMPDIR" "$CHECK_MODE" "$CLANG_FORMAT_STYLE")

# Process output
FAILED=0
while IFS= read -r line; do
    if [ -z "$line" ]; then
        continue
    fi
    if [[ "$line" == FAIL:* ]]; then
        file="${line#FAIL:}"
        echo -e "${RED}✗${NC} $file"
        FAILED=1
        difffile="$TMPDIR/$(echo "$file" | tr '/' '_').diff"
        if [ -s "$difffile" ]; then
            echo "  ($(clang-format --version 2>/dev/null | head -1))"
            sed 's/^/  /' "$difffile"
        fi
    elif [[ "$line" == OK:* ]]; then
        file="${line#OK:}"
        echo -e "${GREEN}Formatted:${NC} $file"
    fi
done <<< "$OUTPUT"

if [ $FAILED -ne 0 ]; then
    echo ""
    echo -e "${RED}Formatting check failed!${NC}"
    echo "Run 'bazel run :format' to fix formatting issues."
    echo "Note: use the same clang-format major version as CI (ubuntu-latest apt package)."
    exit 1
fi

if $CHECK_MODE; then
    echo ""
    echo -e "${GREEN}All files are properly formatted${NC}"
fi
