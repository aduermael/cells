# Agent Guidelines for Cells Project

## Plan Management

### Plan File Naming
Plans should be placed in the `plans/` folder and follow this naming convention:
```
YYYY-MM-DD-topic-slug-STATUS.md
```

**STATUS values:**
- `READY` - Plan is approved and ready for implementation
- `IN-PROGRESS` - Plan is currently being executed
- `DONE` - Plan has been completed

**Examples:**
- `2025-12-16-parser-serializer-READY.md`
- `2025-12-16-formula-engine-IN-PROGRESS.md`
- `2025-12-10-data-model-DONE.md`

### Plan Structure
Each plan should contain:
1. Clear phases (numbered: 1, 2, 3...)
2. Subtasks within each phase (lettered: a, b, c...)
3. Checkmarks for tracking progress: `- [ ]` (pending) or `- [x]` (done)

### Commit Naming During Plan Execution
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
├── cells/                  # Main library
│   ├── BUILD               # Library targets
│   ├── *.h                 # Headers
│   ├── *.cc                # Implementation
│   └── *_test.cc           # Tests (colocated)
└── testdata/               # Sample .cells files for testing
```

### Testing
- Each module should have corresponding tests
- Test files named `<module>_test.cc` (colocated with source)
- Sample data files in `testdata/`
- Run tests with `bazel test //core/...`
