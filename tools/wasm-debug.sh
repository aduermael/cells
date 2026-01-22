#!/bin/bash
# Build WASM module with debug info (for Chrome DevTools debugging)
# See: https://developer.chrome.com/docs/devtools/wasm
# Output: dist/wasm/
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building WASM module with debug info..."
bazel build --config=wasm-debug --define=CELLS_INTERNAL=1 //apps/wasm:cells_wasm

echo "Creating dist/wasm directory..."
rm -rf dist/wasm
mkdir -p dist/wasm
mkdir -p dist/wasm/shared
mkdir -p dist/wasm/favicons

echo "Copying WASM artifacts (with debug info)..."
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/wasm/
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/wasm/
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm.map dist/wasm/ 2>/dev/null || true

echo "Building TypeScript..."
cd apps/wasm && npm run build
cd "$REPO_ROOT"

echo "Copying HTML, CSS, and TypeScript definitions..."
cp apps/wasm/static/index.html dist/wasm/
cp apps/wasm/cells.d.ts dist/wasm/
cp apps/wasm/static/shared/*.css dist/wasm/shared/

echo "Copying favicons and icons..."
cp apps/shared/favicons/* dist/wasm/favicons/
cp apps/shared/icon.svg dist/wasm/

echo "Copying C++ source files for debugging..."
mkdir -p dist/wasm/core dist/wasm/apps/wasm
cp -r core/* dist/wasm/core/
cp apps/wasm/*.cc apps/wasm/*.h dist/wasm/apps/wasm/ 2>/dev/null || true

echo "Fixing source map paths..."
if [ -f dist/wasm/cells_wasm_bin.wasm.map ]; then
    sed -i '' 's|[^"]*execroot/_main/||g' dist/wasm/cells_wasm_bin.wasm.map
fi

echo ""
echo "Debug WASM build complete: dist/wasm/"
echo ""
echo "To debug in Chrome:"
echo "  1. Run: bazel run :serve"
echo "  2. Open: http://localhost:8081/"
echo "  3. Open Chrome DevTools (F12)"
echo "  4. Install C/C++ DevTools Support (DWARF) extension"
echo "  5. Source files will appear in Sources panel"
