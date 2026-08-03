#!/bin/bash
# Run all code quality checks
#
# Usage:
#   bazel run :check          # Run all checks
#   bazel run :check -- --fix # Run checks and fix what's possible
#
# Order: C++ units -> JS units -> Release/install -> XLSX roundtrip -> Lint ->
#        Type checks -> E2E -> Formatter
# Rationale: Faster checks run first for quick feedback

set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

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

export JOBS=${JOBS:-$(get_num_cores)}

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
    # Delegate to tools/test.sh (same targets + foreign_cc toolchain handling)
    "$SCRIPT_DIR/test.sh"
}
if ! time_cmd unit_test_cmd; then
    FAILED=1
fi

echo ""

# 2. Unit tests (JS/TS — collab version stamp, theme, display-name, etc.)
echo -e "${BOLD}=== Unit Tests (JS/TS) ===${NC}"
js_unit_test_cmd() {
    if [ -f "$REPO_ROOT/apps/wasm/scripts/test-unit.mjs" ]; then
        "$SCRIPT_DIR/test-js.sh"
    else
        echo -e "${YELLOW}apps/wasm JS unit runner not found, skipping${NC}"
        return 0
    fi
}
if ! time_cmd js_unit_test_cmd; then
    FAILED=1
fi

echo ""

# 3. Release / install packaging tests (version resolve, package, install)
echo -e "${BOLD}=== Release / Install Tests ===${NC}"
release_test_cmd() {
    if [ -f "$REPO_ROOT/scripts/release/release_test.sh" ]; then
        "$SCRIPT_DIR/release-test.sh"
    else
        echo -e "${YELLOW}release_test.sh not found, skipping${NC}"
        return 0
    fi
}
if ! time_cmd release_test_cmd; then
    FAILED=1
fi

echo ""

# 4. XLSX roundtrip tests
echo -e "${BOLD}=== XLSX Roundtrip Tests ===${NC}"
roundtrip_cmd() {
    "$SCRIPT_DIR/xlsx-roundtrip.sh"
}
if ! time_cmd roundtrip_cmd; then
    FAILED=1
fi

echo ""

# 5. Lint
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

# 6. Type checks (TypeScript)
echo -e "${BOLD}=== Type Check (TypeScript) ===${NC}"
typecheck_cmd() {
    if [ -f "$REPO_ROOT/apps/wasm/scripts/check-types.mjs" ]; then
        export BAZEL_RUN=1
        (cd "$REPO_ROOT/apps/wasm" && node scripts/check-types.mjs)
    else
        echo -e "${YELLOW}apps/wasm not found, skipping type checks${NC}"
        return 0
    fi
}
if ! time_cmd typecheck_cmd; then
    FAILED=1
fi

echo ""

# 7. Integration tests (E2E)
echo -e "${BOLD}=== Integration Tests (E2E, $JOBS parallel) ===${NC}"
e2e_cmd() {
    if [ -f "$REPO_ROOT/apps/wasm/scripts/test-parallel.mjs" ]; then
        export BAZEL_RUN=1
        (cd "$REPO_ROOT/apps/wasm" && node scripts/test-parallel.mjs --concurrency "$JOBS" all)
    else
        echo -e "${YELLOW}E2E test runner not found, skipping integration tests${NC}"
        return 0
    fi
}
if ! time_cmd e2e_cmd; then
    FAILED=1
fi

echo ""

# 8. Format check (last, easiest to fix)
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
