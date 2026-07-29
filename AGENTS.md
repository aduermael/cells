# Agent Guidelines for Cells Project

## Plans are local only — never commit them

Implementation plans (design notes, phased checklists, scratch specs) may be
created **locally** while working, but they **must not** be committed or included
in pull requests.

- Do **not** add a `plans/` directory to the repo
- `plans/` is listed in `.gitignore` on purpose
- Do **not** commit debug dumps, logs, scratch scripts, or temporary assets
- Keep PRs limited to product code, tests, docs that ship with the product, and brand assets that are actually used

If you need a working checklist for yourself, keep it outside the git tree (or
under an ignored path). Reviewers should not see plan files in the diff.

### Commit Messages

**Format rules:**
- **Title + optional body** — brief title (50 chars), optional bullet points
- **Maximum 5 lines** — keep it brief
- **Include "Co-Authored-By:" lines** — add `Co-Authored-By: Claude <noreply@anthropic.com>` at the end when an agent co-authored the change

### Verification

Before handing work off, run:

```bash
bazel run :check
```

This runs unit tests, lint, type checks, E2E tests, and formatter check.

**If any step fails:** Fix the issue before proceeding. Do not introduce regressions.

**IMPORTANT: Fix ALL errors, including pre-existing ones.** When running tests, builds, or checks, you must fix every error that appears—not just errors you introduced. Pre-existing errors are not an excuse to leave a broken build. The codebase should always be in a passing state.

### Working Style

- Prefer small, reviewable commits with clear intent
- Prefer DRY/KISS: one source of truth for assets and constants; no dead exploration files in the tree
- Stop for user review when a change set is large or ambiguous

## Code Style

### Language

- Core engine: C++ (C++17)
- Heavy, independent tasks (few files): C when appropriate
- Tests: Google Test or Catch2
- Build: Bazel (fastest incremental builds, hermetic, scales well)

**WASM constraint:** C++ exceptions are not supported in WASM builds. Never use `throw` or `try/catch` in core engine code. Use error codes or `std::optional`/`std::expected` instead.

### Directory Structure
```
WORKSPACE                   # Bazel workspace root
core/
├── BUILD                   # Bazel build file
└── cells/                  # Main library
    ├── BUILD               # Library targets
    ├── *.h                 # Headers
    ├── *.cc                # Implementation
    └── *_test.cc           # Tests (colocated)
testdata/                   # Sample .zcd files for testing
```

## Build & Test Commands

**IMPORTANT:** Always use `bazel run :target` commands. This ensures correct flags and consistent behavior.

### Build Commands

| Task | Command | Output |
|------|---------|--------|
| Build CLI (dev) | `bazel run :cli` | `dist/cli/cells` |
| Build CLI (release) | `bazel run :cli-release` | `dist/cli/cells` |
| Build WASM (dev) | `bazel run :wasm` | `dist/wasm/` |
| Build WASM (debug) | `bazel run :wasm-debug` | `dist/wasm/` (with DWARF) |
| Build WASM (release) | `bazel run :wasm-dist` | `dist/wasm/` (optimized) |
| Serve locally | `bazel run :serve` | Serves `dist/wasm/` |

### Test Commands

| Task | Command |
|------|---------|
| Run unit tests | `bazel run :test` |
| Run E2E tests (headless) | `bazel run :e2e` |
| Run E2E tests (headed) | `bazel run :e2e-headed` |
| Run single E2E test | `bazel run :e2e -- smoke` |

### Code Quality Commands

| Task | Command |
|------|---------|
| Run all checks | `bazel run :check` |
| Format code | `bazel run :format` |
| Lint code | `bazel run :lint` |
| Type check | `bazel run :check-types` |

### E2E Test Details

Run all tests (default):
```bash
bazel run :e2e
```

Run a specific test file:
```bash
bazel run :e2e -- smoke       # Just smoke tests
bazel run :e2e -- formula     # Just formula tests
bazel run :e2e -- editing     # Just editing tests
```

Run with browser visible (for debugging):
```bash
bazel run :e2e-headed          # All tests, browser visible
bazel run :e2e-headed -- smoke # Single test, browser visible
```

**Available tests:** smoke, formula, editing, column-move, clipboard, selection, collab, initial-sync, collab-demo, merged-cells, named-ranges, borders, format, zoom-*, and more (see `apps/wasm/tests/` for full list)

**Note:** All tests must pass. There is no "stable" subset - if a test is flaky or broken, fix it or remove it.

## Common Mistakes to Avoid

| Wrong | Right |
|-------|-------|
| `bazel build //apps/cli:cells` | `bazel run :cli` |
| `bazel test //core/...` | `bazel run :test` |
| `node tests/smoke.test.mjs && node tests/formula.test.mjs` | `bazel run :e2e` |
| `bazel build --config=wasm //apps/wasm:cells_wasm` | `bazel run :wasm-dist` |
| `cd apps/wasm && npm run test:parallel -- stable` | `bazel run :e2e` |

**Why use `bazel run :` targets?** They include correct flags, handle file copying to `dist/`, and are tested and documented.
