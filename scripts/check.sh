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
SCRIPT_START=$(date +%s)

# Timing helper
time_cmd() {
    local start=$(date +%s)
    "$@"
    local status=$?
    local end=$(date +%s)
    local elapsed=$((end - start))
    echo -e "${BOLD}Time: ${elapsed}s${NC}"
    return $status
}

# 1. Unit tests (C++)
echo -e "${BOLD}=== Unit Tests (C++) ===${NC}"
unit_test_cmd() {
    if command -v bazel &> /dev/null; then
        bazel test //core/...
    elif command -v bazelisk &> /dev/null; then
        bazelisk test //core/...
    else
        echo -e "${YELLOW}Bazel not found, skipping unit tests${NC}"
        return 0
    fi
}
if ! time_cmd unit_test_cmd; then
    FAILED=1
fi

echo ""

# 2. Lint
echo -e "${BOLD}=== Lint Check ($JOBS parallel) ===${NC}"
lint_cmd() {
    if $FIX_MODE; then
        "$SCRIPT_DIR/lint.sh" --fix
    else
        "$SCRIPT_DIR/lint.sh"
    fi
}
if ! time_cmd lint_cmd; then
    FAILED=1
fi

echo ""

# 3. Type checks (TypeScript)
echo -e "${BOLD}=== Type Check (TypeScript) ===${NC}"
typecheck_cmd() {
    if [ -f "$PROJECT_ROOT/apps/wasm/package.json" ]; then
        (cd "$PROJECT_ROOT/apps/wasm" && npm run check-types)
    else
        echo -e "${YELLOW}apps/wasm not found, skipping type checks${NC}"
        return 0
    fi
}
if ! time_cmd typecheck_cmd; then
    FAILED=1
fi

echo ""

# 4. Integration tests (E2E)
echo -e "${BOLD}=== Integration Tests (E2E, $JOBS parallel) ===${NC}"
e2e_cmd() {
    if [ -f "$PROJECT_ROOT/apps/wasm/tests/run-parallel.mjs" ]; then
        (cd "$PROJECT_ROOT/apps/wasm" && npm run test:parallel -- --concurrency "$JOBS" stable)
    else
        echo -e "${YELLOW}E2E test runner not found, skipping integration tests${NC}"
        return 0
    fi
}
if ! time_cmd e2e_cmd; then
    FAILED=1
fi

echo ""

# 5. Format check (last, easiest to fix)
echo -e "${BOLD}=== Format Check ($JOBS parallel) ===${NC}"
format_cmd() {
    if $FIX_MODE; then
        "$SCRIPT_DIR/format.sh"
    else
        "$SCRIPT_DIR/format.sh" --check
    fi
}
if ! time_cmd format_cmd; then
    FAILED=1
fi

echo ""

# Summary
SCRIPT_END=$(date +%s)
TOTAL_TIME=$((SCRIPT_END - SCRIPT_START))
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}${BOLD}Some checks failed!${NC} (total: ${TOTAL_TIME}s)"
    exit 1
else
    echo -e "${GREEN}${BOLD}All checks passed!${NC} (total: ${TOTAL_TIME}s)"
fi
