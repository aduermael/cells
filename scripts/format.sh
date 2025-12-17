#!/bin/bash
# Format C++ source files using clang-format
#
# Usage:
#   ./scripts/format.sh          # Format all files
#   ./scripts/format.sh --check  # Check formatting (CI mode, no changes)
#   ./scripts/format.sh FILE...  # Format specific files

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

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

# Find files to format
if [ ${#FILES[@]} -eq 0 ]; then
    # No files specified, find all C++ files
    mapfile -t FILES < <(find "$PROJECT_ROOT/core" -type f \( -name "*.cc" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) 2>/dev/null)
fi

if [ ${#FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}No C++ files found${NC}"
    exit 0
fi

# Format or check files
FAILED=0

for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        continue
    fi

    if $CHECK_MODE; then
        # Check mode: verify formatting without changes
        if ! clang-format --dry-run --Werror "$file" 2>/dev/null; then
            echo -e "${RED}✗${NC} $file"
            FAILED=1
        else
            echo -e "${GREEN}✓${NC} $file"
        fi
    else
        # Format mode: apply changes
        clang-format -i "$file"
        echo -e "${GREEN}Formatted:${NC} $file"
    fi
done

if [ $FAILED -ne 0 ]; then
    echo ""
    echo -e "${RED}Formatting check failed!${NC}"
    echo "Run './scripts/format.sh' to fix formatting issues."
    exit 1
fi

if $CHECK_MODE; then
    echo ""
    echo -e "${GREEN}All files are properly formatted${NC}"
fi
