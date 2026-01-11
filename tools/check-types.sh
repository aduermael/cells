#!/bin/bash
# Run TypeScript type checking
set -euo pipefail

# Use Bazel's workspace directory if available (when run via bazel run)
# Otherwise fall back to script location (when run directly)
if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
    REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
else
    REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$REPO_ROOT/apps/wasm"

npm run check-types
