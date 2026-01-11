#!/bin/bash
# Run TypeScript type checking
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/apps/wasm"

npm run check-types
