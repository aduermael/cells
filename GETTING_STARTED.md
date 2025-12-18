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
├── core/                   # C++17 core engine
│   ├── cells/              # Main library
│   │   ├── *.h             # Headers
│   │   ├── *.cc            # Implementation
│   │   └── *_test.cc       # Tests (colocated)
│   └── testdata/           # Sample .cells files
├── docs/                   # Architecture documentation
├── plans/                  # Implementation plans
├── scripts/                # Build/lint scripts
├── Makefile                # Convenience targets
├── MODULE.bazel            # Bazel module definition
└── WORKSPACE               # Bazel workspace
```

## Quick Reference

| Task | Command |
|------|---------|
| Build all | `make build` |
| Run tests | `make test` |
| Format code | `make format` |
| Check formatting | `make format-check` |
| Lint | `make lint` |
| Lint + fix | `make lint-fix` |
| All checks | `make check` |
| Fix all | `make fix` |
| Generate compile DB | `make compile-db` |
| Clean | `make clean` |

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
