# Getting Started

This guide covers how to build, test, and develop the Cells spreadsheet engine.

## Prerequisites

- **Bazel** (or Bazelisk) - build system
- **Clang** - C++17 compiler
- **clang-format** - code formatting
- **clang-tidy** - static analysis (optional)

### macOS

```bash
brew install bazelisk llvm
```

### Linux (Ubuntu/Debian)

```bash
# Bazelisk
npm install -g @bazel/bazelisk
# or download from https://github.com/bazelbuild/bazelisk/releases

# Clang
sudo apt install clang clang-format clang-tidy
```

## Building

Build the CLI:

```bash
bazel run :cli           # Development build
bazel run :cli-release   # Optimized build
```

Output: `dist/cli/cells`

Build the WASM web app:

```bash
bazel run :wasm          # Development build
bazel run :wasm-debug    # Debug build (with DWARF)
bazel run :wasm-dist     # Optimized build for deployment
```

Output: `dist/wasm/`

## Running Tests

### Unit Tests

```bash
bazel run :test
```

### E2E Tests

Run all E2E tests (headless):
```bash
bazel run :e2e
```

Run a specific test:
```bash
bazel run :e2e -- smoke
bazel run :e2e -- formula
bazel run :e2e -- editing
```

Run with browser visible (for debugging):
```bash
bazel run :e2e-headed
bazel run :e2e-headed -- smoke
```

**Available tests:** smoke, formula, editing, column-move, clipboard, selection, script, collab, initial-sync, collab-demo

## CLI Tool

The `cells` CLI converts between spreadsheet formats.

### Building

```bash
bazel run :cli           # Development build
bazel run :cli-release   # Optimized build
```

The binary is at `dist/cli/cells`.

### Basic Usage

```bash
# Convert between formats
cells -i data.csv output.zcd
cells -i budget.xlsx report.csv
cells -i legacy.csv modern.xlsx

# File inspection
cells -I data.xlsx
cells -I spreadsheet.zcd
```

### Supported Formats

| Format | Extension | Notes |
|--------|-----------|-------|
| ZCD | `.zcd` | Native format, preserves all features |
| CSV | `.csv`, `.tsv` | Single sheet, values only |
| Excel | `.xlsx` | Excel 2007+ format |

### Examples

```bash
# CSV with custom delimiter
cells -i data.tsv output.zcd          # Auto-detects tab
cells -i data.txt --delimiter ';' out.zcd

# XLSX sheet selection
cells -i workbook.xlsx --sheet 'Q1' q1.csv
cells -i workbook.xlsx --all-sheets -t csv reports/

# Scripting
cells -i input.xlsx output.csv -q -y    # Quiet, overwrite
cells -i data.csv out.xlsx --time       # Show timing
```

Run `cells --help` for full documentation.

## WASM/Browser UI

The primary UI is a browser-based spreadsheet using WebAssembly.

### Building

```bash
bazel run :wasm          # Development build
bazel run :wasm-debug    # Debug build (with DWARF)
bazel run :wasm-dist     # Optimized build for deployment
```

### Running Locally

Build the distribution and start the local server:

```bash
bazel run :wasm-dist     # Build optimized WASM
bazel run :serve         # Start local server on port 8081
```

Then open http://localhost:8081/ in your browser.

### Distribution Package

The `bazel run :wasm-dist` command creates a `dist/wasm/` directory with all files needed for deployment:

```
dist/wasm/
├── cells_wasm_bin.js      # WASM loader (Emscripten)
├── cells_wasm_bin.wasm    # Compiled engine (~729KB)
├── worker.js              # Web Worker (bundled from TypeScript)
├── main.js                # Main thread code (bundled from TypeScript)
├── index.html             # Spreadsheet UI
├── cells.d.ts             # TypeScript definitions for WASM API
└── shared/                # CSS styles
```

### Features

- Full spreadsheet with resizable columns/rows
- Cell editing with formula bar
- Multi-sheet support with tabs
- Keyboard navigation
- Import/export: CSV, XLSX, native .zcd format
- File persistence across page refreshes

## Code Formatting

Format all code:

```bash
bazel run :format
```

## Linting

Run the linter:

```bash
bazel run :lint
```

## TypeScript Type Check

```bash
bazel run :check-types
```

## Running All Checks

Run all verification in one command (unit tests, lint, type checks, E2E tests, format):

```bash
bazel run :check
```

## IDE Setup

### Generate compile_commands.json

For IDE features like code completion and go-to-definition:

```bash
bazel run //:refresh_compile_commands
```

This generates `compile_commands.json` in the workspace root, which is recognized by:
- VS Code (with clangd extension)
- CLion
- Vim/Neovim (with coc.nvim or similar)

## Project Structure

```
cells/
├── apps/                   # Applications
│   ├── cli/                # CLI converter tool
│   └── wasm/               # Browser UI (WebAssembly)
│       ├── bindings.cc     # C++ to WASM bindings
│       ├── src/            # TypeScript source files
│       │   ├── client.ts   # Main thread API
│       │   ├── worker.ts   # Web Worker
│       │   └── grid-renderer.ts  # Canvas2D rendering
│       ├── cells.d.ts      # TypeScript definitions for WASM
│       └── static/         # HTML/CSS
├── core/                   # C++17 core engine
│   └── cells/              # Main library
│       ├── *.h             # Headers
│       ├── *.cc            # Implementation
│       └── *_test.cc       # Tests (colocated)
├── testdata/               # Sample .zcd files
├── docs/                   # Architecture documentation
├── plans/                  # Implementation plans
├── tools/                  # Build/lint scripts and utilities
│   └── serve/              # Local WASM server
├── MODULE.bazel            # Bazel module definition
└── WORKSPACE               # Bazel workspace
```

## Quick Reference

| Task | Command | Output |
|------|---------|--------|
| Build CLI (dev) | `bazel run :cli` | `dist/cli/cells` |
| Build CLI (release) | `bazel run :cli-release` | `dist/cli/cells` |
| Build WASM (dev) | `bazel run :wasm` | `dist/wasm/` |
| Build WASM (debug) | `bazel run :wasm-debug` | `dist/wasm/` |
| Build WASM (release) | `bazel run :wasm-dist` | `dist/wasm/` |
| Serve locally | `bazel run :serve` | - |
| Run unit tests | `bazel run :test` | - |
| Run E2E tests | `bazel run :e2e` | - |
| Run E2E tests (headed) | `bazel run :e2e-headed` | - |
| Run single E2E test | `bazel run :e2e -- smoke` | - |
| Format code | `bazel run :format` | - |
| Lint | `bazel run :lint` | - |
| TypeScript check | `bazel run :check-types` | - |
| All checks | `bazel run :check` | - |
| Generate compile DB | `bazel run //:refresh_compile_commands` | - |
| Convert files | `dist/cli/cells -i input.xlsx output.csv` | - |
| File info | `dist/cli/cells -I file.xlsx` | - |

## Troubleshooting

### Bazel cache issues

```bash
bazel clean --expunge
```

### clang-format not found

Make sure LLVM tools are in your PATH:

```bash
# macOS with Homebrew
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

### Test failures

Run with verbose output to see details:

```bash
bazel test //core/... --test_output=errors
```
