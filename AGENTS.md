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
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
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
- **Plain text only** — no title/body separation, just raw text
- **Maximum 5 lines** — keep it brief
- **No "Co-Authored-By:" lines** — never include these
- **No "Generated with" text** — never include AI attribution

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
make check
```

This runs all verification steps in sequence with 16 parallel jobs:
1. Unit tests (C++)
2. Linter
3. Type checks (TypeScript)
4. Integration tests (E2E stable suite)
5. Formatter check

**If any step fails:** Fix the issue before proceeding. Do not introduce regressions.

### Working Style

- **One commit per sub-task:** Each lettered subtask (a, b, c...) gets its own commit
- **Never more than one phase at a time:** Complete a phase, stop, let user review, then proceed

## Code Style

### Language

- Core engine: C++ (C++17)
- Heavy, independent tasks (few files): C when appropriate
- Tests: Google Test or Catch2
- Build: Bazel (fastest incremental builds, hermetic, scales well)

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

**IMPORTANT:** Always use Makefile targets or npm scripts. Never run raw `bazel` or `node` commands directly.

**Which build command to use:**
- `make build` - C++ core engine only (no WASM)
- `make wasm` or `make wasm-dist` - TypeScript/WASM web app (use this for frontend work)

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
| Generate compile_commands.json | `make compile-db` |
| Clean | `make clean` |

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

### Test Suites

- **Stable suites:** smoke, formula, editing, column-move, clipboard, selection
- **Experimental suites:** collab, initial-sync, collab-demo (may fail)

## Common Mistakes to Avoid

| Wrong | Right |
|-------|-------|
| `bazel build //apps/cli:cells` | `make cli` |
| `bazel test //core/...` | `make test` |
| `node tests/smoke.test.mjs && node tests/formula.test.mjs` | `cd apps/wasm && npm run test:parallel -- stable` |
| `bazel build --config=wasm //apps/wasm:cells_wasm` | `make wasm-dist` |

**Why use Makefile targets?** They include correct flags, handle file copying, and are tested and documented.
