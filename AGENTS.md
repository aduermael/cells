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

**Integration tests must pass at the end of each phase.** Before completing a phase, run `cd apps/wasm && npm run test:stable` to verify all stable E2E tests pass. Do not introduce regressions in passing tests.

### Working Style

- **One commit per sub-task:** Each lettered subtask (a, b, c...) gets its own commit
- **Never more than one phase at a time:** Complete a phase, stop, let user review, then proceed

## Code Style

### Language

- Core engine: C++ (C++17)
- Heavy, independent tasks (few files): C when appropriate
- Tests: Google Test or Catch2
- Build: Bazel (fastest incremental builds, hermetic, scales well)

### Building

**Native (CLI):**
```bash
bazel build //apps/cli:cells    # Build CLI app
bazel test //core/...           # Run all C++ tests
```

**WASM (Web app):**
```bash
make wasm-dist                  # Build WASM and package for web
make wasm-serve                 # Serve locally at http://localhost:8081
```

Note: Always use `make wasm-dist` to build WASM, not `bazel build` directly.

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

### Testing

**Unit tests (C++):**
- Each module should have corresponding tests
- Test files named `<module>_test.cc` (colocated with source)
- Sample data files in `testdata/`
- Run tests with `bazel test //core/...`

**E2E tests (Puppeteer):**
- Test files in `apps/wasm/tests/`
- Run stable tests: `cd apps/wasm && npm run test:stable`
- Stable suites: `smoke`, `formula`
- Experimental suites: `collab` (may fail, excluded from `test:stable`)
