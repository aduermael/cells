# Plan: Verify All Tests Pass

**Goal:** Ensure all tests pass in the current branch state, including collaboration tests. Identify any disabled tests and re-enable them if they should be passing.

## Test Infrastructure Overview

The project has multiple test layers:

1. **C++ Unit Tests (Bazel)** - `//core/cells:*_test` targets
2. **Go Unit Tests** - `tools/serve/rooms_test.go`
3. **TypeScript Unit Tests** - `apps/wasm/tests/unit/editing-session.test.mjs`
4. **E2E Browser Tests (Puppeteer)** - `apps/wasm/tests/*.test.mjs`
   - Including collaboration tests: `collab.test.mjs`, `initial-sync.test.mjs`, `collab-demo.test.mjs`

## Phase 1: Run All Tests and Identify Failures
- [x] 1a: Run C++ unit tests (`bazel test //core/cells:all`) - **45/45 passed**
- [x] 1b: Run Go unit tests (`cd tools/serve && go test ./...`) - **passed**
- [x] 1c: Run TypeScript unit tests (`bazel run :check-types`) - **30/30 passed**
- [x] 1d: Run E2E tests (`bazel run :e2e`) - **189/190 passed** (1 frozen pane test failed - feature disabled)

## Phase 2: Analyze and Categorize Failures
- [x] 2a: Document which tests failed and their error messages
  - `lbo-integration.test.mjs`: "Frozen cells remain visible when scrolling" - frozen pane rendering is disabled
- [x] 2b: Categorize failures by area
  - The frozen pane failure is expected (feature intentionally disabled per commit e2abc5b)
- [x] 2c: Identify root causes
  - **Critical finding:** 12 E2E test files are NOT included in `COLLECTIONS.all` in `scripts/test-parallel.mjs`

## Phase 3: Add Missing Tests to Test Runner
- [x] 3a: Add all 12 missing test files to `COLLECTIONS.all` in `apps/wasm/scripts/test-parallel.mjs`
- [ ] 3b: Run the full test suite and identify which tests fail
- [ ] 3c: For each failing test, either fix it or remove it (no flaky/broken tests allowed)

### Missing Test Files (not currently run by `bazel run :e2e`)
1. `borders.test.mjs`
2. `bug-a-repro.test.mjs`
3. `cross-sheet-formula-edit.test.mjs`
4. `cursor.test.mjs`
5. `custom-format.test.mjs`
6. `format.test.mjs`
7. `merged-cells.test.mjs`
8. `multi-cell-format.test.mjs`
9. `sheet-order.test.mjs`
10. `spill.test.mjs`
11. `styled-empty-cells.test.mjs`
12. `xlsx-export.test.mjs`

## Phase 4: Final Verification
- [ ] 4a: Run `bazel run :check` to verify all checks pass
- [ ] 4b: Confirm all 36 E2E test files are included and passing

**Note:** The test suite explicitly states "All tests must pass. There is no 'stable' subset - if a test is flaky or broken, fix it or remove it." (from `scripts/test-parallel.mjs`)
