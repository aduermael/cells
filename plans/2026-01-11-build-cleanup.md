Status: READY
Created At: 2026-01-11 07:03 UTC
Updated At: 2026-01-11 07:03 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make wasm-dist` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |

---

# Build System Cleanup

## Problem Statement

The current build system has several issues:
1. **Confusing output locations**: Two `dist` directories exist (`/dist` at root and `apps/wasm/dist`)
2. **Too many similar commands**: Multiple ways to do the same thing leads to confusion
3. **Complex artifact copying**: Build outputs require manual copying between directories
4. **Undocumented flags**: Developers use wrong build flags and think things are broken
5. **Make vs Bazel inconsistency**: Raw bazel commands sometimes slip through

## Goals

1. Single, clear way to perform each build operation
2. All artifacts output directly to correct locations (no manual copying)
3. Consolidated output directory: `dist/cli/`, `dist/wasm/`
4. Accurate, minimal documentation
5. Remove unused/redundant Makefile targets

## New Command Structure

After cleanup, these will be the ONLY build commands:

| Operation | Command | Output |
|-----------|---------|--------|
| Build CLI (dev) | `make cli` | `dist/cli/cells` |
| Build CLI (release) | `make cli-release` | `dist/cli/cells` |
| Build WASM (dev) | `make wasm` | `dist/wasm/` |
| Build WASM (debug) | `make wasm-debug` | `dist/wasm/` (with DWARF) |
| Build WASM (release) | `make wasm-dist` | `dist/wasm/` (optimized) |
| Serve locally | `make serve` | Serves `dist/wasm/` |
| Run unit tests | `make test` | - |
| Run all checks | `make check` | - |
| Format code | `make format` | - |
| Lint code | `make lint` | - |

**Removed/renamed targets:**
- `make build` → removed (too generic, use `make cli` or `make wasm`)
- `make wasm-serve` → renamed to `make serve` (shorter)
- `make wasm-debug-dist` → renamed to `make wasm-debug` (shorter)
- `make release` → removed (alias for `cli-release`)

---

## Phase 1: Restructure Output Directory

Consolidate all build outputs under `dist/`:
```
dist/
├── cli/
│   └── cells           # CLI binary
└── wasm/
    ├── cells_wasm_bin.js
    ├── cells_wasm_bin.wasm
    ├── main.js
    ├── worker.js
    ├── *.js.map
    ├── index.html
    ├── cells.d.ts
    ├── shared/
    │   └── *.css
    ├── favicons/
    │   └── ...
    └── icon.svg
```

- [ ] 1a: Update `apps/wasm/build.mjs` to output directly to `dist/wasm/` instead of `apps/wasm/dist/`
- [ ] 1b: Update Makefile CLI targets to output to `dist/cli/cells`
- [ ] 1c: Update Makefile WASM targets to output directly to `dist/wasm/`
- [ ] 1d: Update `.gitignore` to remove `apps/wasm/dist/` entry (no longer needed)
- [ ] 1e: Update `tools/serve/main.go` default `-dir` to `dist/wasm`

## Phase 2: Simplify Makefile Targets

- [ ] 2a: Remove `build` target (replace with explicit `cli` or `wasm`)
- [ ] 2b: Remove `release` alias (use `cli-release` directly)
- [ ] 2c: Rename `wasm-serve` to `serve`
- [ ] 2d: Rename `wasm-debug-dist` to `wasm-debug` (consolidate with simpler names)
- [ ] 2e: Update all targets to use new `dist/` structure without intermediate copies
- [ ] 2f: Add validation to catch common mistakes (e.g., running raw bazel)

## Phase 3: Consider sh_binary (Optional - User Decision Required)

**Option A: Keep Makefile (Recommended)**
- Familiar to most developers
- Works well with existing tooling
- Easier to read and modify

**Option B: Use sh_binary**
- More "Bazel native"
- Better caching of script execution
- Hermetic (uses Bazel's toolchain)

Decision: Defer to user preference. Current plan assumes Option A.

## Phase 4: Update Documentation

- [ ] 4a: Update AGENTS.md "Build & Test Commands" section with new command names
- [ ] 4b: Update AGENTS.md "Common Mistakes to Avoid" section
- [ ] 4c: Update GETTING_STARTED.md with simplified build instructions
- [ ] 4d: Add brief comments in Makefile explaining each target

## Phase 5: Cleanup and Validation

- [ ] 5a: Remove any obsolete BUILD file targets (e.g., unused filegroups)
- [ ] 5b: Test all build commands work correctly
- [ ] 5c: Run `make check` to verify nothing is broken
- [ ] 5d: Delete `apps/wasm/dist/` directory if it exists (artifacts now in `dist/wasm/`)

---

## Detailed Changes

### Makefile Changes (Phase 2)

```makefile
# BEFORE: Multiple confusing targets
build:           # What does this build? CLI? WASM? Both?
wasm:            # Just builds, doesn't copy
wasm-dist:       # Builds AND copies (confusing name)
wasm-debug-dist: # Long name
wasm-serve:      # OK but verbose
release:         # Alias - unnecessary

# AFTER: Clear, single-purpose targets
cli:         # Build CLI for development → dist/cli/cells
cli-release: # Build CLI optimized → dist/cli/cells
wasm:        # Build WASM for development → dist/wasm/
wasm-debug:  # Build WASM with DWARF → dist/wasm/
wasm-dist:   # Build WASM optimized → dist/wasm/
serve:       # Serve dist/wasm/ locally
test:        # Run unit tests
check:       # Run all checks
```

### esbuild Changes (Phase 1a)

```javascript
// BEFORE: apps/wasm/build.mjs
const mainOptions = {
  entryPoints: ['src/main.ts'],
  outfile: 'dist/main.js',  // Relative to apps/wasm/
};

// AFTER: apps/wasm/build.mjs
const mainOptions = {
  entryPoints: ['src/main.ts'],
  outfile: '../../dist/wasm/main.js',  // Direct to root dist/wasm/
};
```

### serve Changes (Phase 1e)

```go
// BEFORE: tools/serve/main.go
dir := flag.String("dir", "dist", "Directory to serve")

// AFTER: tools/serve/main.go
dir := flag.String("dir", "dist/wasm", "Directory to serve")
```

---

## Open Questions

1. **sh_binary**: Do you want to pursue the more "Bazel native" approach using sh_binary instead of Make? This would require more significant changes.

2. **Symlink for convenience**: Should we create a `cells` symlink at repo root pointing to `dist/cli/cells` for backwards compatibility?

3. **Watch mode**: Should `make wasm` include a watch mode flag, or keep `npm run watch` as the development workflow?
