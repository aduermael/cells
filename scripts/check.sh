#!/bin/bash
# Run all code quality checks
#
# Usage:
#   ./scripts/check.sh          # Run all checks
#   ./scripts/check.sh --fix    # Run checks and fix what's possible
#   JOBS=8 ./scripts/check.sh   # Run with 8 parallel jobs
#
# Order: Unit tests -> Linter -> Type checks -> Integration tests -> Formatter
# Rationale: Faster checks run first for quick feedback

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Default to 16 parallel jobs
export JOBS=${JOBS:-16}

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

# 1. Unit tests (C++)
echo -e "${BOLD}=== Unit Tests (C++) ===${NC}"
if command -v bazel &> /dev/null; then
    if ! bazel test //core/...; then
        FAILED=1
    fi
elif command -v bazelisk &> /dev/null; then
    if ! bazelisk test //core/...; then
        FAILED=1
    fi
else
    echo -e "${YELLOW}Bazel not found, skipping unit tests${NC}"
fi

echo ""

# 2. Lint
echo -e "${BOLD}=== Lint Check ($JOBS parallel) ===${NC}"
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

# 3. Type checks (TypeScript)
echo -e "${BOLD}=== Type Check (TypeScript) ===${NC}"
if [ -f "$PROJECT_ROOT/apps/wasm/package.json" ]; then
    if ! (cd "$PROJECT_ROOT/apps/wasm" && npm run check-types); then
        FAILED=1
    fi
else
    echo -e "${YELLOW}apps/wasm not found, skipping type checks${NC}"
fi

echo ""

# 4. Integration tests (E2E)
echo -e "${BOLD}=== Integration Tests (E2E, $JOBS parallel) ===${NC}"
if [ -f "$PROJECT_ROOT/apps/wasm/tests/run-parallel.mjs" ]; then
    if ! (cd "$PROJECT_ROOT/apps/wasm" && npm run test:parallel -- --concurrency "$JOBS" stable); then
        FAILED=1
    fi
else
    echo -e "${YELLOW}E2E test runner not found, skipping integration tests${NC}"
fi

echo ""

# 5. Format check (last, easiest to fix)
echo -e "${BOLD}=== Format Check ($JOBS parallel) ===${NC}"
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

# Summary
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}${BOLD}Some checks failed!${NC}"
    exit 1
else
    echo -e "${GREEN}${BOLD}All checks passed!${NC}"
fi
