Status: READY
Created At: 2025-12-31 18:25 UTC
Updated At: 2025-12-31 18:25 UTC
Following plan management guidelines defined in AGENTS.md

# Parallel E2E Test Runner

## Problem

Currently, E2E tests run sequentially via `&&` chains in package.json:
```bash
node tests/smoke.test.mjs && node tests/formula.test.mjs && ...
```

This is slow and provides poor visibility into overall test status. Each test file starts its own server on port 8082, which would conflict if run in parallel.

## Solution

Create a parallel test runner that:
1. Runs up to N test files concurrently (default: 10)
2. Assigns unique ports to each test file to avoid conflicts
3. Collects results from all test files
4. Prints a clean unified report at the end
5. Exits with error code 1 if any test failed

## Test Collections

| Collection | Files |
|------------|-------|
| `all` | All test files |
| `stable` | smoke, formula, editing, column-move, clipboard, selection |
| `collab` | collab, initial-sync, collab-demo |
| `script` | script |

## Usage

```bash
# Run all tests in parallel (up to 10 concurrent)
npm run test:parallel

# Run specific collection
npm run test:parallel -- stable
npm run test:parallel -- collab

# Limit concurrency
npm run test:parallel -- --concurrency 5 stable
```

---

## Phase 1: Parallel Test Runner Core

- [ ] 1a: Create `tests/run-parallel.mjs` with collection definitions, CLI parsing, and worker spawning
- [ ] 1b: Modify harness to accept port via environment variable (`TEST_PORT`)
- [ ] 1c: Add result collection and unified report formatting

## Phase 2: Integration and Polish

- [ ] 2a: Add npm scripts for parallel test execution
- [ ] 2b: Update README.md with new parallel test commands
- [ ] 2c: Run full test suite to verify everything works

## Phase 3: Update AGENTS.md with Build/Test Guidelines

- [ ] 3a: Add mandatory "Commands" section to plan header template
- [ ] 3b: Add comprehensive "Build & Test Commands" reference section
- [ ] 3c: Add "Common Mistakes" section warning against raw bazel/node commands

---

## Implementation Details

### Port Assignment

Each test file gets a unique port: `BASE_PORT + index` where `BASE_PORT = 9000`.

```
smoke.test.mjs      → port 9000
formula.test.mjs    → port 9001
editing.test.mjs    → port 9002
...
```

### Worker Spawning

Use `child_process.spawn` with `TEST_PORT` environment variable. Capture stdout/stderr for each worker.

### Result Format

Each test file already outputs results to stdout. The runner will parse the final summary lines:
```
Passed: N
Failed: M
```

### Report Format

```
═══════════════════════════════════════════════════════════════
                     E2E TEST RESULTS
═══════════════════════════════════════════════════════════════

 ✓ smoke          7/7 passed    (2.3s)
 ✓ formula        8/8 passed    (3.1s)
 ✗ editing        6/7 passed    (2.8s)
   └─ Delete cell content: Expected "" but got "42"
 ✓ column-move    2/2 passed    (1.5s)

───────────────────────────────────────────────────────────────
 TOTAL: 23/24 passed (1 failed)    Duration: 4.2s
───────────────────────────────────────────────────────────────
```

### Concurrency Limit

Use a simple semaphore pattern with Promise.all on batches, or a proper worker pool. The runner waits for all workers to complete before printing the report.

---

## Phase 3 Details: AGENTS.md Updates

### 3a: New Plan Header Template

Plans must include a **Commands** section in the header listing all commands needed:

```markdown
Status: IN-PROGRESS
Created At: YYYY-MM-DD HH:mm UTC
Updated At: YYYY-MM-DD HH:mm UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
```

### 3b: Build & Test Commands Reference

Add to AGENTS.md:

```markdown
## Build & Test Commands

**IMPORTANT:** Always use Makefile targets or npm scripts. Never run raw `bazel` or `node` commands directly.

### Core Engine (C++)

| Task | Command |
|------|---------|
| Build all | `make build` |
| Build CLI | `make cli` |
| Build CLI (optimized) | `make cli-release` |
| Run unit tests | `make test` |
| Format code | `make format` |
| Check format | `make format-check` |
| Lint | `make lint` |
| Lint + fix | `make lint-fix` |
| Full check (format + lint + build) | `make check` |

### WASM / Web App

| Task | Command |
|------|---------|
| Build WASM + dist | `make wasm-dist` |
| Build debug WASM | `make wasm-debug-dist` |
| Serve locally | `make wasm-serve` |
| TypeScript check | `make check-types` |

### E2E Tests (apps/wasm)

| Task | Command |
|------|---------|
| All tests (parallel) | `cd apps/wasm && npm run test:parallel` |
| Stable tests (parallel) | `cd apps/wasm && npm run test:parallel -- stable` |
| Collab tests (parallel) | `cd apps/wasm && npm run test:parallel -- collab` |
| Single suite | `cd apps/wasm && npm run test:smoke` |
| Headed mode (debug) | `HEADED=1 npm run test:smoke` |
```

### 3c: Common Mistakes Section

Add to AGENTS.md:

```markdown
## Common Mistakes to Avoid

❌ **Wrong:** `bazel build //apps/cli:cells`
✅ **Right:** `make cli`

❌ **Wrong:** `bazel test //core/...`
✅ **Right:** `make test`

❌ **Wrong:** `node tests/smoke.test.mjs && node tests/formula.test.mjs`
✅ **Right:** `cd apps/wasm && npm run test:parallel -- stable`

❌ **Wrong:** `bazel build --config=wasm //apps/wasm:cells_wasm`
✅ **Right:** `make wasm-dist`

**Why?** The Makefile targets:
- Include correct flags and configurations
- Handle file copying and setup
- Are tested and reliable
- Are documented and consistent
```
