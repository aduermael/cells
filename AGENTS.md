# Agent Guidelines for Cells Project

## Plan Management

### Plan File Naming

Plans should be placed in the `plans/` folder and follow this naming convention:

```
YYYY-MM-DD-topic-slug.md
```

When done:

```
YYYY-MM-DD-topic-slug-DONE.md
```

**Examples:**
- `2025-12-16-data-model.md`
- `2025-12-16-data-model-DONE.md`

### Plan Structure

**HEADER**

Each plan should have a header of this form:

```
Status: IN-PROGRESS
Created At: YYYY-MM-DD HH:mm UTC
Updated At: YYYY-MM-DD HH:mm UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build WASM | `bazel run :wasm` |
| Unit tests | `bazel run :test` |
| E2E tests | `bazel run :e2e` |
| Lint | `bazel run :lint` |
| Format | `bazel run :format` |
```

All timestamps must be in **UTC timezone**. To get the current UTC time, run:
```bash
date -u '+%Y-%m-%d %H:%M'
```

It's important to always include the "Following plan management guidelines defined in AGENTS.md" mention.

The **Commands** section is **mandatory** and should list all commands needed to build, test, and verify the work in this plan. Adjust commands based on what the plan touches (e.g., omit E2E tests if the plan is pure C++ backend work).

**STATUS values:**

- `READY` - Plan is approved and ready for implementation
- `IN-PROGRESS` - Plan is currently being executed
- `DONE` - Plan has been completed

**PHASES AND SUB-TASKS**

Each plan should contain:

1. Clear phases (numbered: 1, 2, 3...)
2. Subtasks within each phase as checkmarks: `- [ ]` (pending) or `- [x]` (done)

**Important:** ALL phases must use checkmarks for their sub-tasks. Each checkmark corresponds to a single commit. The checkmark should be marked as `- [x]` in the same commit that completes the sub-task.

Example:
```markdown
## Phase 1: Core Data Model

- [x] 1a: Add Cell and Axis structs
- [x] 1b: Add Workbook and Sheet structs
- [ ] 1c: Add serialization helpers

## Phase 2: Parser Implementation

- [ ] 2a: Implement text format parser
- [ ] 2b: Add parser error handling
- [ ] 2c: Add parser tests
```

### Commit Messages

**Format rules:**
- **Title + optional body** — brief title (50 chars), optional bullet points
- **Maximum 5 lines** — keep it brief
- **Include "Co-Authored-By:" lines** — add `Co-Authored-By: Claude <noreply@anthropic.com>` at the end

When executing a plan, each subtask gets its own commit named by phase and subtask:

- Phase 1, subtask a: `1a: <description>`
- Phase 1, subtask b: `1b: <description>`
- Phase 2, subtask a: `2a: <description>`

**Example commits:**

```
1a: Add Cell and Axis structs
1b: Add Workbook and Sheet structs
2a: Implement text format parser
2b: Add parser error handling
```

### Phase Review Process

**Stop at the end of each phase** to let the user review the commits before proceeding to the next phase. Do not continue to the next phase until the user gives approval.

**All checks must pass at the end of each phase.** Run:

```bash
bazel run :check
```

This runs all verification steps in sequence (parallelism is auto-detected):
1. Unit tests (C++)
2. Linter
3. Type checks (TypeScript)
4. E2E tests (all tests)
5. Formatter check

**If any step fails:** Fix the issue before proceeding. Do not introduce regressions.

**IMPORTANT: Fix ALL errors, including pre-existing ones.** When running tests, builds, or checks, you must fix every error that appears—not just errors you introduced. Pre-existing errors are not an excuse to leave a broken build. The codebase should always be in a passing state.

### Working Style

- **One commit per sub-task:** Each lettered subtask (a, b, c...) gets its own commit
- **Never more than one phase at a time:** Complete a phase, stop, let user review, then proceed

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
