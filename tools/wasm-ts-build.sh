#!/bin/bash
# Bundle apps/wasm TypeScript into dist/wasm/{main,worker}.js via esbuild.
#
# Requires: Node.js on PATH, and apps/wasm/node_modules (esbuild).
# Does NOT require the npm CLI at build time — only `node` runs the bundler.
# One-time dependency install still uses npm/pnpm/yarn (see message below).
#
# Usage: sourced or executed after REPO_ROOT is set (via tools/guard.sh).
set -euo pipefail

if [ -z "${REPO_ROOT:-}" ]; then
  echo "Error: REPO_ROOT is not set (source tools/guard.sh first)" >&2
  exit 1
fi

wasm_dir="$REPO_ROOT/apps/wasm"
build_script="$wasm_dir/scripts/build.mjs"

if ! command -v node >/dev/null 2>&1; then
  cat >&2 <<'EOF'
Error: Node.js is required to bundle the web UI TypeScript (esbuild).

npm is NOT required for the build itself. Install Node, then install
esbuild once into apps/wasm:

  # macOS (Homebrew installs node + npm together)
  brew install node
  (cd apps/wasm && npm ci)

  # or any Node 18+ distribution, then:
  (cd apps/wasm && npm ci)

Then re-run: bazel run :wasm
EOF
  exit 1
fi

if [ ! -d "$wasm_dir/node_modules/esbuild" ]; then
  cat >&2 <<'EOF'
Error: apps/wasm/node_modules/esbuild is missing.

The web UI bundle step needs esbuild (listed in apps/wasm/package.json).
This is a one-time install of JS build tools — not an npm dependency of
the runtime product. From the repo root:

  (cd apps/wasm && npm ci)

If you do not have npm, install Node (which includes npm), e.g.:
  brew install node

Then re-run: bazel run :wasm
EOF
  exit 1
fi

export BAZEL_RUN=1
(cd "$wasm_dir" && node scripts/build.mjs)
