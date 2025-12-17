.PHONY: build test format lint check clean compile-db

# Build
build:
	bazel build //core/...

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
