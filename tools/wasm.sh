#!/bin/bash
# Build WASM module for development
# Output: dist/wasm/
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/guard.sh"
cd "$REPO_ROOT"

echo "Building WASM module..."
bazel build --config=wasm --define=CELLS_INTERNAL=1 //apps/wasm:cells_wasm

echo "Creating dist/wasm directory..."
rm -rf dist/wasm
mkdir -p dist/wasm
mkdir -p dist/wasm/shared
mkdir -p dist/wasm/favicons

echo "Copying WASM artifacts..."
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.js dist/wasm/
cp bazel-bin/apps/wasm/cells_wasm/cells_wasm_bin.wasm dist/wasm/

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

echo ""
echo "WASM build complete: dist/wasm/"
