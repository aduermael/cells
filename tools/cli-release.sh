#!/bin/bash
# Build CLI binary with optimizations (for production/release)
# Output: dist/cli/cells
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "Building CLI (optimized)..."
bazel build -c opt //apps/cli:cells

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells dist/cli/cells

echo "Built: dist/cli/cells (optimized)"
