.PHONY: build test format lint check clean compile-db cli cli-release release wasm

# Build
build:
	bazel build //core/...

# Build CLI and copy to repo root (fast build, for development)
cli:
	bazel build //apps/cli:cells
	cp -f bazel-bin/apps/cli/cells ./cells

# Build CLI with optimizations (for production/release)
cli-release:
	bazel build -c opt //apps/cli:cells
	cp -f bazel-bin/apps/cli/cells ./cells

# Alias for cli-release
release: cli-release

# Build WASM module (for browser usage)
wasm:
	bazel build //apps/wasm:hello_wasm
	@echo "WASM output: bazel-bin/apps/wasm/hello_wasm/"

# Test (when tests exist)
test:
	bazel test //core/...

# Format C++ code
format:
	./scripts/format.sh

# Check formatting without changes
format-check:
	./scripts/format.sh --check

# Run linter
lint:
	./scripts/lint.sh

# Lint with auto-fix
lint-fix:
	./scripts/lint.sh --fix

# Run all checks (format + lint + build)
check:
	./scripts/check.sh

# Fix all auto-fixable issues
fix:
	./scripts/format.sh
	./scripts/lint.sh --fix

# Generate compile_commands.json for clang tools
compile-db:
	bazel run //:refresh_compile_commands

# Clean build artifacts
clean:
	bazel clean
