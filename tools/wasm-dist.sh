#!/bin/bash
# Build production WASM distribution package
# Output: dist/wasm/ (optimized)
set -euo pipefail

# Use Bazel's workspace directory if available (when run via bazel run)
# Otherwise fall back to script location (when run directly)
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
else
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$REPO_ROOT"

echo "Building WASM module (optimized)..."
bazel build --config=wasm -c opt //apps/wasm:cells_wasm

echo "Creating dist/wasm directory..."
rm -rf dist/wasm
mkdir -p dist/wasm
mkdir -p dist/wasm/shared
mkdir -p dist/wasm/favicons

echo "Copying WASM artifacts..."
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/wasm/
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/wasm/

echo "Building TypeScript..."
export BAZEL_RUN=1
(cd apps/wasm && node scripts/build.mjs)

echo "Copying HTML, CSS, and TypeScript definitions..."
cp apps/wasm/static/index.html dist/wasm/
cp apps/wasm/cells.d.ts dist/wasm/
cp apps/wasm/static/shared/*.css dist/wasm/shared/

echo "Copying favicons and icons..."
cp apps/shared/favicons/* dist/wasm/favicons/
cp apps/shared/icon.svg dist/wasm/

echo ""
echo "Production WASM build complete: dist/wasm/"
ls -lh dist/wasm/
echo ""
echo "To test locally: bazel run :serve"
echo "Then open: http://localhost:8081/"
