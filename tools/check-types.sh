#!/bin/bash
# Run TypeScript type checking
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/guard.sh"
cd "$REPO_ROOT/apps/wasm"

export BAZEL_RUN=1
node scripts/check-types.mjs
