#!/bin/bash
# Build the production-trimmed CLI:
#   - headless (no WASM/UI)
#   - no operation ledger (OpLog compiled out)
#   - no connectivity (sync/WebRTC/signaling not linked)
# Local file-backed sessions still work.
# Output: dist/cli/cells
set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"
cd "$REPO_ROOT"

echo "Building headless no-collab CLI (no WASM/UI, no ledger, no network)..."
# shellcheck disable=SC2046
bazel build --config=headless --config=no-collab //apps/cli:cells-no-collab \
  $(foreign_cc_toolchain_args)

mkdir -p dist/cli
cp -f bazel-bin/apps/cli/cells-no-collab dist/cli/cells

echo "Built: dist/cli/cells (headless, no-collab)"
