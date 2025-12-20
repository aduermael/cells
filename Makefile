.PHONY: build test format lint check clean compile-db cli cli-release release wasm wasm-full wasm-dist wasm-dist-full

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
	bazel build //apps/wasm:cells_wasm
	@echo "WASM output: bazel-bin/apps/wasm/cells_wasm/"

# Build WASM module with XLSX support (larger binary)
wasm-full:
	bazel build //apps/wasm:cells_wasm_full
	@echo "WASM output: bazel-bin/apps/wasm/cells_wasm_full/"

# Build production WASM distribution package
# Output: dist/ directory with all files needed for standalone deployment
wasm-dist:
	@echo "Building WASM module..."
	bazel build -c opt //apps/wasm:cells_wasm
	@echo "Creating dist directory..."
	@rm -rf dist
	@mkdir -p dist
	@echo "Copying WASM artifacts..."
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/cells_wasm.js
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/cells_wasm.wasm
	@echo "Copying JavaScript files..."
	@cp apps/wasm/worker.js dist/
	@cp apps/wasm/client.js dist/
	@echo "Copying HTML and TypeScript definitions..."
	@cp apps/wasm/static/index.html dist/
	@cp apps/wasm/cells.d.ts dist/
	@echo ""
	@echo "Distribution package created in dist/"
	@echo "Files:"
	@ls -lh dist/
	@echo ""
	@echo "To test locally, run: python3 -m http.server 8080 --directory dist"
	@echo "Then open: http://localhost:8080/"

# Build production WASM distribution with XLSX support
wasm-dist-full:
	@echo "Building WASM module with XLSX support..."
	bazel build -c opt //apps/wasm:cells_wasm_full
	@echo "Creating dist directory..."
	@rm -rf dist
	@mkdir -p dist
	@echo "Copying WASM artifacts..."
	@cp bazel-bin/apps/wasm/cells_wasm_full/cells_wasm_full_bin.js dist/cells_wasm.js
	@cp bazel-bin/apps/wasm/cells_wasm_full/cells_wasm_full_bin.wasm dist/cells_wasm.wasm
	@echo "Copying JavaScript files..."
	@cp apps/wasm/worker.js dist/
	@cp apps/wasm/client.js dist/
	@echo "Copying HTML and TypeScript definitions..."
	@cp apps/wasm/static/index.html dist/
	@cp apps/wasm/cells.d.ts dist/
	@echo ""
	@echo "Distribution package created in dist/"
	@echo "Files:"
	@ls -lh dist/
	@echo ""
	@echo "To test locally, run: python3 -m http.server 8080 --directory dist"
	@echo "Then open: http://localhost:8080/"

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
