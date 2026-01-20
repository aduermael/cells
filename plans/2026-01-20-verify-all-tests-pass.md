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
- [ ] 1a: Run C++ unit tests (`bazel test //core/cells:all`)
- [ ] 1b: Run Go unit tests (`cd tools/serve && go test ./...`)
- [ ] 1c: Run TypeScript unit tests (`npm run test:unit` in apps/wasm)
- [ ] 1d: Run all E2E tests in parallel (`npm run test:parallel` in apps/wasm)

## Phase 2: Analyze and Categorize Failures (if any)
- [ ] 2a: Document which tests failed and their error messages
- [ ] 2b: Categorize failures by area (C++/Go/TS unit tests, E2E, collaboration)
- [ ] 2c: Identify root causes (real bugs vs flaky tests vs environment issues)

## Phase 3+: Fix Failures (dynamically added based on Phase 2 results)
_Phases will be added dynamically based on what failures are discovered in Phase 2._

**Note:** The test suite explicitly states "All tests must pass. There is no 'stable' subset - if a test is flaky or broken, fix it or remove it." (from `run-parallel.mjs:13`)
