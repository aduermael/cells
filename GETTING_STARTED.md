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

Build all targets:

```bash
make build
# or directly:
bazel build //core/...
```

Build a specific target:

```bash
bazel build //core/cells:parser
bazel build //core/cells:serializer
```

## Running Tests

Run all tests:

```bash
make test
# or directly:
bazel test //core/...
```

Run a specific test:

```bash
bazel test //core/cells:parser_test
bazel test //core/cells:serializer_test
bazel test //core/cells:id_test
```

Run with verbose output:

```bash
bazel test //core/... --test_output=all
```

## CLI Tool

The `cells` CLI converts between spreadsheet formats.

### Building

```bash
bazel build //apps/cli:cells
```

The binary is at `bazel-bin/apps/cli/cells`.

### Basic Usage

```bash
# Convert between formats
cells -i data.csv output.cells
cells -i budget.xlsx report.csv
cells -i legacy.csv modern.xlsx

# File inspection
cells -I data.xlsx
cells -I spreadsheet.cells
```

### Supported Formats

| Format | Extension | Notes |
|--------|-----------|-------|
| Cells | `.cells` | Native format, preserves all features |
| CSV | `.csv`, `.tsv` | Single sheet, values only |
| Excel | `.xlsx` | Excel 2007+ format |

### Examples

```bash
# CSV with custom delimiter
cells -i data.tsv output.cells          # Auto-detects tab
cells -i data.txt --delimiter ';' out.cells

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
make wasm
# or directly:
bazel build --config=wasm //apps/wasm:cells_wasm
```

### Running Locally

Build the distribution and start the local server:

```bash
make wasm-dist    # Build optimized WASM and copy to dist/
make wasm-serve   # Start local server on port 8081
```

Then open http://localhost:8081/ in your browser.

### Distribution Package

The `make wasm-dist` command creates a `dist/` directory with all files needed for deployment:

```
dist/
├── cells_wasm_bin.js      # WASM loader
├── cells_wasm_bin.wasm    # Compiled engine
├── worker.js              # Web Worker for async operations
├── client.js              # Main thread client API
├── index.html             # Spreadsheet UI
├── cells.d.ts             # TypeScript definitions
└── shared/                # CSS and JS modules
```

### Features

- Full spreadsheet with resizable columns/rows
- Cell editing with formula bar
- Multi-sheet support with tabs
- Keyboard navigation
- Import/export: CSV, XLSX, native .cells format
- File persistence across page refreshes

## Code Formatting

Format all C++ code:

```bash
make format
# or directly:
./scripts/format.sh
```

Check formatting without making changes:

```bash
make format-check
# or directly:
./scripts/format.sh --check
```

## Linting

Run the linter:

```bash
make lint
# or directly:
./scripts/lint.sh
```

Run linter with auto-fix:

```bash
make lint-fix
# or directly:
./scripts/lint.sh --fix
```

## Running All Checks

Run formatting, linting, and build in one command:

```bash
make check
# or directly:
./scripts/check.sh
```

Fix all auto-fixable issues:

```bash
make fix
```

## IDE Setup

### Generate compile_commands.json

For IDE features like code completion and go-to-definition:

```bash
make compile-db
# or directly:
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
│       ├── worker.js       # Web Worker
│       ├── client.js       # Main thread API
│       └── static/         # HTML/CSS/JS
├── core/                   # C++17 core engine
│   ├── cells/              # Main library
│   │   ├── *.h             # Headers
│   │   ├── *.cc            # Implementation
│   │   └── *_test.cc       # Tests (colocated)
│   └── testdata/           # Sample .cells files
├── docs/                   # Architecture documentation
├── plans/                  # Implementation plans
├── scripts/                # Build/lint scripts
├── tools/                  # Development utilities
│   └── serve/              # Local WASM server
├── Makefile                # Convenience targets
├── MODULE.bazel            # Bazel module definition
└── WORKSPACE               # Bazel workspace
```

## Quick Reference

| Task | Command |
|------|---------|
| Build all | `make build` |
| Build CLI | `make cli` |
| Build WASM | `make wasm` |
| Build WASM dist | `make wasm-dist` |
| Run WASM UI | `make wasm-serve` |
| Run tests | `make test` |
| Format code | `make format` |
| Check formatting | `make format-check` |
| Lint | `make lint` |
| Lint + fix | `make lint-fix` |
| All checks | `make check` |
| Fix all | `make fix` |
| Generate compile DB | `make compile-db` |
| Clean | `make clean` |
| Convert files | `cells -i input.xlsx output.csv` |
| File info | `cells -I file.xlsx` |

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
