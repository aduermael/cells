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
if [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r -d '' file; do
        FILES+=("$file")
    done < <(find "$PROJECT_ROOT/core" -type f -name "*.cc" \
        ! -name "*_test.cc" \
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

# Run clang-tidy
echo -e "${GREEN}Running clang-tidy...${NC}"
echo ""

FAILED=0

for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        continue
    fi

    echo -e "${YELLOW}Checking:${NC} $file"

    # Run clang-tidy with output to temp file to capture exit code reliably
    TMPFILE=$(mktemp)
    TIDY_EXIT=0
    "${TIDY_CMD[@]}" \
        --config-file="$PROJECT_ROOT/.clang-tidy" \
        "$file" \
        -- \
        -std=c++17 \
        -I"$PROJECT_ROOT" \
        > "$TMPFILE" 2>&1 || TIDY_EXIT=$?

    # Filter and display output (remove noise from system headers)
    grep -v -E "^$|warnings generated|Suppressed .* warnings|Use -header-filter|^Error while processing|^Found compiler error" "$TMPFILE" || true
    rm -f "$TMPFILE"

    if [ $TIDY_EXIT -ne 0 ]; then
        FAILED=1
    fi
done

echo ""
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}Linting completed with warnings/errors${NC}"
    exit 1
else
    echo -e "${GREEN}Linting passed${NC}"
fi
