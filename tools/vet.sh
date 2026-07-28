#!/bin/bash
# Run gdevillele/vet (vendored under third_party/vet) against this repo.
#
# Usage:
#   ./tools/vet.sh
#   bazel run :vet
#   ./tools/vet.sh --format json
#
# Requires Go (1.22+) on PATH. Uses the submodule at third_party/vet.
# Always executes with the repository root as the working directory so
# paths in vet.yaml (tools/serve, .github/workflows) resolve correctly.

set -euo pipefail

if [ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]; then
  REPO_ROOT="$BUILD_WORKSPACE_DIRECTORY"
else
  REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fi

cd "$REPO_ROOT"

VET_DIR="$REPO_ROOT/third_party/vet"
VET_GO="$VET_DIR/implementations/go"
CONFIG="$REPO_ROOT/vet.yaml"

if [ ! -d "$VET_GO/cmd/vet" ]; then
  echo "Error: third_party/vet submodule is missing or incomplete." >&2
  echo "Initialize with: git submodule update --init --recursive third_party/vet" >&2
  exit 1
fi

if [ ! -f "$CONFIG" ]; then
  echo "Error: vet config not found at $CONFIG" >&2
  exit 1
fi

if ! command -v go >/dev/null 2>&1; then
  echo "Error: go not found on PATH (need Go 1.22+)" >&2
  exit 1
fi

# Build the vendored Go runner, then run it from the repo root.
VET_BIN="$(mktemp -t cells-vet.XXXXXX)"
cleanup() {
  rm -f "$VET_BIN"
}
trap cleanup EXIT

(
  cd "$VET_GO"
  go build -o "$VET_BIN" ./cmd/vet
)

# No extra path args by default: languages.go.files selects tools/serve,
# and github-actions-pinned scans .github/workflows when enabled.
"$VET_BIN" --config "$CONFIG" "$@"
