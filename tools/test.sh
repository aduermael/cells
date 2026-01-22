#!/bin/bash
# Run unit tests (C++)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/guard.sh"
cd "$REPO_ROOT"

bazel test //core/...
