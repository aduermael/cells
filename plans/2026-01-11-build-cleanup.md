Status: READY
Created At: 2026-01-11 07:03 UTC
Updated At: 2026-01-11 07:13 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build WASM | `bazel run :wasm` |
| Unit tests | `bazel run :test` |
| E2E tests | `bazel run :e2e` |
| Lint | `bazel run :lint` |
| Format | `bazel run :format` |

---

# Build System Cleanup

## Problem Statement

The current build system has several issues:
1. **Confusing output locations**: Two `dist` directories exist (`/dist` at root and `apps/wasm/dist`)
2. **Too many similar commands**: Multiple ways to do the same thing leads to confusion
3. **Complex artifact copying**: Build outputs require manual copying between directories
4. **Undocumented flags**: Developers use wrong build flags and think things are broken
5. **Make vs Bazel inconsistency**: Makefile wraps Bazel but adds confusion

## Goals

1. **Pure Bazel**: Use `sh_binary` targets instead of Makefile
2. **Single output location**: All artifacts go to `dist/cli/` or `dist/wasm/`
3. **One way to do each thing**: Clear, minimal set of commands
4. **Accurate documentation**: Update AGENTS.md and GETTING_STARTED.md
5. **Auto-detect parallelism**: Tests use all available cores automatically

## New Command Structure

After cleanup, these will be the ONLY build commands:

| Operation | Command | Output |
|-----------|---------|--------|
| Build CLI (dev) | `bazel run :cli` | `dist/cli/cells` |
| Build CLI (release) | `bazel run :cli-release` | `dist/cli/cells` |
| Build WASM (dev) | `bazel run :wasm` | `dist/wasm/` |
| Build WASM (debug) | `bazel run :wasm-debug` | `dist/wasm/` (with DWARF) |
| Build WASM (release) | `bazel run :wasm-dist` | `dist/wasm/` (optimized) |
| Serve locally | `bazel run :serve` | Serves `dist/wasm/` |
| Run unit tests | `bazel run :test` | - |
| Run E2E tests | `bazel run :e2e` | Headless (default) |
| Run E2E tests (headed) | `bazel run :e2e-headed` | With browser visible |
| Run all checks | `bazel run :check` | - |
| Format code | `bazel run :format` | - |
| Lint code | `bazel run :lint` | - |
| Type check | `bazel run :check-types` | - |

**What gets removed:**
- `Makefile` (deleted entirely)
- `apps/wasm/dist/` directory (all output goes to `dist/wasm/`)
- Redundant targets like `build`, `release`, `wasm-serve`
- Hardcoded parallelism values (auto-detect instead)

---

## Phase 1: Create sh_binary Build Scripts

Create shell scripts in `tools/` directory that will be wrapped by `sh_binary`:

```
tools/
├── cli.sh           # Build CLI
├── cli-release.sh   # Build CLI optimized
├── wasm.sh          # Build WASM dev
├── wasm-debug.sh    # Build WASM with DWARF
├── wasm-dist.sh     # Build WASM optimized
├── serve.sh         # Serve dist/wasm/
├── test.sh          # Run unit tests
├── e2e.sh           # Run E2E tests (headless)
├── e2e-headed.sh    # Run E2E tests (headed)
├── check-types.sh   # TypeScript type check
├── check.sh         # Run all checks (move from scripts/)
├── format.sh        # Format code (move from scripts/)
└── lint.sh          # Lint code (move from scripts/)
```

- [ ] 1a: Create `tools/cli.sh` - builds CLI binary to `dist/cli/cells`
- [ ] 1b: Create `tools/cli-release.sh` - builds optimized CLI to `dist/cli/cells`
- [ ] 1c: Create `tools/wasm.sh` - builds WASM + TS to `dist/wasm/`
- [ ] 1d: Create `tools/wasm-debug.sh` - builds debug WASM to `dist/wasm/`
- [ ] 1e: Create `tools/wasm-dist.sh` - builds optimized WASM to `dist/wasm/`
- [ ] 1f: Create `tools/serve.sh` - serves `dist/wasm/` directory
- [ ] 1g: Create `tools/test.sh` - runs unit tests with auto-detected parallelism
- [ ] 1h: Create `tools/e2e.sh` - runs E2E tests headless with auto-detected parallelism
- [ ] 1i: Create `tools/e2e-headed.sh` - runs E2E tests with browser visible
- [ ] 1j: Create `tools/check-types.sh` - runs TypeScript type checking
- [ ] 1k: Move `scripts/check.sh` to `tools/check.sh` and update for new commands
- [ ] 1l: Move `scripts/format.sh` to `tools/format.sh`
- [ ] 1m: Move `scripts/lint.sh` to `tools/lint.sh`

## Phase 2: Update Root BUILD File

Add `sh_binary` targets that wrap the shell scripts:

- [ ] 2a: Add `sh_binary` targets for cli, cli-release
- [ ] 2b: Add `sh_binary` targets for wasm, wasm-debug, wasm-dist
- [ ] 2c: Add `sh_binary` targets for serve
- [ ] 2d: Add `sh_binary` targets for test, e2e, e2e-headed
- [ ] 2e: Add `sh_binary` targets for check, format, lint, check-types
- [ ] 2f: Remove any obsolete targets from BUILD file

## Phase 3: Update TypeScript Build

- [ ] 3a: Update `apps/wasm/build.mjs` to output to `../../dist/wasm/` instead of `dist/`
- [ ] 3b: Remove watch mode from build.mjs (not needed)
- [ ] 3c: Update `apps/wasm/package.json` to remove watch script

## Phase 4: Update Supporting Files

- [ ] 4a: Update `tools/serve/main.go` default `-dir` to `dist/wasm`
- [ ] 4b: Update `.gitignore` - remove `apps/wasm/dist/`, keep `dist/`
- [ ] 4c: Delete `scripts/` directory (all scripts moved to `tools/`)

## Phase 5: Delete Makefile

- [ ] 5a: Delete `Makefile` from repo root

## Phase 6: Update Documentation

- [ ] 6a: Update AGENTS.md - replace all `make` commands with `bazel run :` equivalents
- [ ] 6b: Update AGENTS.md - update "Common Mistakes to Avoid" section
- [ ] 6c: Update GETTING_STARTED.md with new build commands
- [ ] 6d: Update README.md if it references make commands

## Phase 7: Cleanup and Validation

- [ ] 7a: Delete `apps/wasm/dist/` directory if it exists
- [ ] 7b: Test all `bazel run :` commands work correctly
- [ ] 7c: Run `bazel run :check` to verify nothing is broken
- [ ] 7d: Verify E2E tests pass with `bazel run :e2e`

---

## Detailed Implementation

### Root BUILD File (Phase 2)

```python
# Build scripts
sh_binary(
    name = "cli",
    srcs = ["tools/cli.sh"],
)

sh_binary(
    name = "cli-release",
    srcs = ["tools/cli-release.sh"],
)

sh_binary(
    name = "wasm",
    srcs = ["tools/wasm.sh"],
)

sh_binary(
    name = "wasm-debug",
    srcs = ["tools/wasm-debug.sh"],
)

sh_binary(
    name = "wasm-dist",
    srcs = ["tools/wasm-dist.sh"],
)

sh_binary(
    name = "serve",
    srcs = ["tools/serve.sh"],
)

sh_binary(
    name = "test",
    srcs = ["tools/test.sh"],
)

sh_binary(
    name = "e2e",
    srcs = ["tools/e2e.sh"],
)

sh_binary(
    name = "e2e-headed",
    srcs = ["tools/e2e-headed.sh"],
)

sh_binary(
    name = "check",
    srcs = ["tools/check.sh"],
)

sh_binary(
    name = "format",
    srcs = ["tools/format.sh"],
)

sh_binary(
    name = "lint",
    srcs = ["tools/lint.sh"],
)

sh_binary(
    name = "check-types",
    srcs = ["tools/check-types.sh"],
)
```

### Auto-detect Parallelism

All test scripts will auto-detect available cores:

```bash
# Cross-platform core detection
get_num_cores() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 4  # Fallback
    fi
}

JOBS=$(get_num_cores)
```

### Example Script: tools/e2e.sh (Phase 1h)

```bash
#!/bin/bash
# Run E2E tests (headless)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Auto-detect available cores
get_num_cores() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

JOBS=$(get_num_cores)
echo "Running E2E tests with $JOBS parallel workers..."

cd apps/wasm
npm run test:parallel -- --concurrency "$JOBS" stable
```

### Example Script: tools/e2e-headed.sh (Phase 1i)

```bash
#!/bin/bash
# Run E2E tests with browser visible (for debugging)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/apps/wasm"

export HEADED=1
npm run test:parallel -- stable
```

### Example Script: tools/test.sh (Phase 1g)

```bash
#!/bin/bash
# Run unit tests
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

bazel test //core/...
```

### esbuild Changes (Phase 3a)

```javascript
// apps/wasm/build.mjs
const mainOptions = {
  ...commonOptions,
  entryPoints: ['src/main.ts'],
  outfile: '../../dist/wasm/main.js',  // Output directly to dist/wasm/
};

const workerOptions = {
  ...commonOptions,
  entryPoints: ['src/worker.ts'],
  outfile: '../../dist/wasm/worker.js',  // Output directly to dist/wasm/
};
```

---

## Migration Notes

**For developers:**
- Replace `make X` with `bazel run :X`
- Example: `make wasm-dist` → `bazel run :wasm-dist`
- Example: `make test` → `bazel run :test`
- Example: E2E tests → `bazel run :e2e` (headless) or `bazel run :e2e-headed`

**Output location changes:**
- CLI binary: `./cells` → `dist/cli/cells`
- WASM build: `dist/` → `dist/wasm/`
- TypeScript: `apps/wasm/dist/` → `dist/wasm/`

**Parallelism:**
- No more `JOBS=N` environment variable needed
- All tests auto-detect available cores
