#!/bin/bash
# Run all code quality checks
#
# Usage:
#   ./scripts/check.sh          # Run all checks
#   ./scripts/check.sh --fix    # Run checks and fix what's possible

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Parse arguments
FIX_MODE=false
for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
            ;;
    esac
done

FAILED=0

# Run format check
echo -e "${BOLD}=== Format Check ===${NC}"
if $FIX_MODE; then
    if ! "$SCRIPT_DIR/format.sh"; then
        FAILED=1
    fi
else
    if ! "$SCRIPT_DIR/format.sh" --check; then
        FAILED=1
    fi
fi

echo ""

# Run lint
echo -e "${BOLD}=== Lint Check ===${NC}"
if $FIX_MODE; then
    if ! "$SCRIPT_DIR/lint.sh" --fix; then
        FAILED=1
    fi
else
    if ! "$SCRIPT_DIR/lint.sh"; then
        FAILED=1
    fi
fi

echo ""

# Run build
echo -e "${BOLD}=== Build ===${NC}"
if command -v bazel &> /dev/null; then
    if ! bazel build //core/...; then
        FAILED=1
    fi
else
    echo -e "${YELLOW}Bazel not found, skipping build${NC}"
fi

echo ""

# Summary
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}${BOLD}Some checks failed!${NC}"
    exit 1
else
    echo -e "${GREEN}${BOLD}All checks passed!${NC}"
fi
