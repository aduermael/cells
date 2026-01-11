#!/bin/bash
# Serve dist/wasm/ for local testing
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [ ! -d dist/wasm ]; then
    echo "Error: dist/wasm/ not found. Run 'bazel run :wasm' first."
    exit 1
fi

# Check Go version (require 1.22+ for macOS 15+ compatibility)
go version | awk '{print $3}' | sed 's/go//' | awk -F. '{if ($1 < 1 || ($1 == 1 && $2 < 22)) {
    print "Error: Go 1.22+ required (you have: " $0 ")";
    print "  macOS 15+ requires Go 1.22+ to avoid dyld errors";
    print "  Install: brew install go";
    exit 1
}}'

echo "Serving dist/wasm/ at http://localhost:8081/"
cd tools/serve && go run . -port 8081 -dir "$REPO_ROOT/dist/wasm" -enable-collab -enable-agent
