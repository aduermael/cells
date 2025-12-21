.PHONY: build test format lint check clean compile-db cli cli-release release wasm wasm-debug wasm-dist wasm-debug-dist wasm-serve

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

# Build WASM module (for browser usage, includes XLSX support)
wasm:
	bazel build --config=wasm //apps/wasm:cells_wasm
	@echo "WASM output: bazel-bin/apps/wasm/cells_wasm/"

# Build WASM module with debug info (for Chrome DevTools debugging)
# See: https://developer.chrome.com/docs/devtools/wasm
wasm-debug:
	bazel build --config=wasm-debug //apps/wasm:cells_wasm
	@echo "WASM debug output: bazel-bin/apps/wasm/cells_wasm/"

# Build production WASM distribution package
# Output: dist/ directory with all files needed for standalone deployment
wasm-dist:
	@echo "Building WASM module..."
	bazel build --config=wasm -c opt //apps/wasm:cells_wasm
	@echo "Creating dist directory..."
	@rm -rf dist
	@mkdir -p dist
	@mkdir -p dist/shared
	@echo "Copying WASM artifacts..."
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/
	@echo "Copying JavaScript files..."
	@cp apps/wasm/worker.js dist/
	@cp apps/wasm/client.js dist/
	@echo "Copying HTML and TypeScript definitions..."
	@cp apps/wasm/static/index.html dist/
	@cp apps/wasm/cells.d.ts dist/
	@echo "Copying shared modules..."
	@cp apps/wasm/static/shared/*.css dist/shared/
	@cp apps/wasm/static/shared/*.js dist/shared/
	@echo ""
	@echo "Distribution package created in dist/"
	@echo "Files:"
	@ls -lh dist/
	@echo ""
	@echo "To test locally: make wasm-serve"
	@echo "Then open: http://localhost:8081/"

# Build debug WASM distribution package (with DWARF for Chrome DevTools)
# See: https://developer.chrome.com/docs/devtools/wasm
# Output: dist/ directory with debug-enabled WASM
wasm-debug-dist:
	@echo "Building WASM module with debug info..."
	bazel build --config=wasm-debug //apps/wasm:cells_wasm
	@echo "Creating dist directory..."
	@rm -rf dist
	@mkdir -p dist
	@mkdir -p dist/shared
	@echo "Copying WASM artifacts (with debug info)..."
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/
	@cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm.map dist/ 2>/dev/null || true
	@echo "Copying JavaScript files..."
	@cp apps/wasm/worker.js dist/
	@cp apps/wasm/client.js dist/
	@echo "Copying HTML and TypeScript definitions..."
	@cp apps/wasm/static/index.html dist/
	@cp apps/wasm/cells.d.ts dist/
	@echo "Copying shared modules..."
	@cp apps/wasm/static/shared/*.css dist/shared/
	@cp apps/wasm/static/shared/*.js dist/shared/
	@echo "Copying C++ source files for debugging..."
	@mkdir -p dist/core dist/apps/wasm
	@cp -r core/* dist/core/
	@cp apps/wasm/*.cc apps/wasm/*.h dist/apps/wasm/ 2>/dev/null || true
	@echo "Fixing source map paths..."
	@if [ -f dist/cells_wasm_bin.wasm.map ]; then \
		sed -i '' 's|[^"]*execroot/_main/||g' dist/cells_wasm_bin.wasm.map; \
	fi
	@echo ""
	@echo "Debug distribution package created in dist/"
	@echo "Files:"
	@ls -lh dist/
	@echo ""
	@echo "To debug in Chrome:"
	@echo "  1. Run: make wasm-serve"
	@echo "  2. Open: http://localhost:8081/"
	@echo "  3. Open Chrome DevTools (F12)"
	@echo "  4. Install C/C++ DevTools Support (DWARF) extension"
	@echo "  5. Source files will appear in Sources panel"

# Serve WASM distribution for local testing
wasm-serve:
	@if [ ! -d dist ]; then echo "Error: dist/ not found. Run 'make wasm-dist' first."; exit 1; fi
	@# Check Go version (require 1.22+ for macOS 15+ compatibility)
	@go version | awk '{print $$3}' | sed 's/go//' | awk -F. '{if ($$1 < 1 || ($$1 == 1 && $$2 < 22)) { \
		print "Error: Go 1.22+ required (you have: " $$0 ")"; \
		print "  macOS 15+ requires Go 1.22+ to avoid dyld errors"; \
		print "  Install: brew install go"; \
		exit 1 \
	}}'
	cd tools/serve && go run . -port 8081 -dir ../../dist -enable-collab

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
