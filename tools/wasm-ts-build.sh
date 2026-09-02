#!/bin/bash
# Bundle apps/wasm TypeScript into dist/wasm/{main,worker}.js via esbuild.
#
# Node/npm are NOT required. esbuild is a native binary (Go); we call it
# directly. Resolution order:
#   1. ESBUILD env override (must be a real native binary)
#   2. esbuild on PATH (native only — not the npm JS shim)
#   3. apps/wasm/node_modules/@esbuild/<platform>/bin/esbuild
#   4. Cached binary under $REPO_ROOT/tmp/esbuild/ (auto-downloaded once)
#
# Note: apps/wasm/node_modules/esbuild/bin/esbuild is a Node JS shim and is
# never used (it fails with "cannot execute binary file" without Node).
#
# Version is pinned to apps/wasm/package.json "esbuild" dep (single source of truth).
#
# Usage: executed after REPO_ROOT is set (via tools/guard.sh).
set -euo pipefail

if [ -z "${REPO_ROOT:-}" ]; then
  echo "Error: REPO_ROOT is not set (source tools/guard.sh first)" >&2
  exit 1
fi

wasm_dir="$REPO_ROOT/apps/wasm"
out_dir="$REPO_ROOT/dist/wasm"
pkg_json="$wasm_dir/package.json"

# Pin to package.json so npm-based and binary-only paths stay aligned.
esbuild_version="$(
  sed -n 's/.*"esbuild"[[:space:]]*:[[:space:]]*"[\^~]*\([0-9][^"]*\)".*/\1/p' "$pkg_json" | head -1
)"
if [ -z "$esbuild_version" ]; then
  echo "Error: could not read esbuild version from $pkg_json" >&2
  exit 1
fi

platform_id() {
  # Map uname to @esbuild npm package suffix (same as esbuild install script).
  case "$(uname -ms)" in
    'Darwin arm64') echo 'darwin-arm64' ;;
    'Darwin x86_64') echo 'darwin-x64' ;;
    'Linux arm64' | 'Linux aarch64') echo 'linux-arm64' ;;
    'Linux x86_64') echo 'linux-x64' ;;
    *)
      echo "error: unsupported platform for esbuild binary: $(uname -ms)" >&2
      return 1
      ;;
  esac
}

# True if path is a native binary for this machine (not a #! script / wrong-arch).
is_usable_esbuild() {
  local p="$1"
  [ -f "$p" ] && [ -x "$p" ] || return 1
  # JS/shell wrappers start with a shebang; the real Go binary does not.
  local magic
  magic="$(head -c 2 "$p" 2>/dev/null || true)"
  if [ "$magic" = "#!" ]; then
    return 1
  fi
  # Reject wrong-arch / broken binaries ("cannot execute binary file").
  if ! "$p" --version >/dev/null 2>&1; then
    return 1
  fi
  return 0
}

find_esbuild() {
  if [ -n "${ESBUILD:-}" ]; then
    if is_usable_esbuild "$ESBUILD"; then
      printf '%s\n' "$ESBUILD"
      return 0
    fi
    echo "Warning: ESBUILD=$ESBUILD is not a usable native esbuild; ignoring." >&2
  fi

  if command -v esbuild >/dev/null 2>&1; then
    local on_path
    on_path="$(command -v esbuild)"
    if is_usable_esbuild "$on_path"; then
      printf '%s\n' "$on_path"
      return 0
    fi
  fi

  local plat nm_bin
  plat="$(platform_id)" || return 1
  # Prefer the platform package (always the real Go binary when present).
  nm_bin="$wasm_dir/node_modules/@esbuild/$plat/bin/esbuild"
  if is_usable_esbuild "$nm_bin"; then
    printf '%s\n' "$nm_bin"
    return 0
  fi
  # Some npm installs also place a native copy under esbuild/bin/ — accept only
  # if it actually runs (skip JS shims and wrong-arch leftovers).
  nm_bin="$wasm_dir/node_modules/esbuild/bin/esbuild"
  if is_usable_esbuild "$nm_bin"; then
    printf '%s\n' "$nm_bin"
    return 0
  fi

  local cache_bin="$REPO_ROOT/tmp/esbuild/${esbuild_version}/esbuild"
  if is_usable_esbuild "$cache_bin"; then
    printf '%s\n' "$cache_bin"
    return 0
  fi
  return 1
}

download_esbuild() {
  local plat tgz_url cache_dir cache_bin tmp
  plat="$(platform_id)"
  cache_dir="$REPO_ROOT/tmp/esbuild/${esbuild_version}"
  cache_bin="$cache_dir/esbuild"
  tgz_url="https://registry.npmjs.org/@esbuild/${plat}/-/${plat}-${esbuild_version}.tgz"

  if ! command -v curl >/dev/null 2>&1; then
    echo "Error: curl is required to download esbuild (or set ESBUILD to a native binary)." >&2
    exit 1
  fi
  if ! command -v tar >/dev/null 2>&1; then
    echo "Error: tar is required to extract the esbuild binary." >&2
    exit 1
  fi

  echo "Downloading esbuild ${esbuild_version} (${plat}) — no Node/npm needed..." >&2
  mkdir -p "$cache_dir"
  tmp="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" RETURN
  curl -fsSL -o "$tmp/esbuild.tgz" "$tgz_url"
  tar -xzf "$tmp/esbuild.tgz" -C "$tmp" package/bin/esbuild
  mv "$tmp/package/bin/esbuild" "$cache_bin"
  chmod +x "$cache_bin"
  if ! is_usable_esbuild "$cache_bin"; then
    echo "Error: downloaded esbuild is not runnable on this machine: $cache_bin" >&2
    exit 1
  fi
  echo "Cached esbuild at $cache_bin" >&2
  # Only the path goes to stdout (callers capture it).
  printf '%s\n' "$cache_bin"
}

resolve_esbuild() {
  if bin="$(find_esbuild)"; then
    printf '%s\n' "$bin"
    return 0
  fi
  download_esbuild
}

esbuild_bin="$(resolve_esbuild)"

mkdir -p "$out_dir"

# Product version stamp for collab menu / UI (env → git tag → default).
# Same resolver as apps/wasm/scripts/build.mjs (shared common.sh helper).
# shellcheck source=../scripts/release/common.sh
. "$REPO_ROOT/scripts/release/common.sh"
product_version="$(cells_resolve_product_version)"
build_id="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)-$(date +%s)"
echo "Stamping frontend CELLS_VERSION=${product_version} BUILD_ID=${build_id}"

# Production TS bundle flags (bazel run :wasm / :wasm-dist / :wasm-debug).
# esbuild --define expects a JSON value; quote so the replacement is a string literal.
# product_version is validated semver (or default), so it is safe to interpolate.
common_flags=(
  --bundle
  --format=esm
  --target=es2020
  --sourcemap
  --minify
  --log-level=info
  "--define:__CELLS_VERSION__=\"${product_version}\""
  "--define:__CELLS_BUILD_ID__=\"${build_id}\""
)

echo "Bundling TypeScript with esbuild ${esbuild_version} ($esbuild_bin)..."
(
  cd "$wasm_dir"
  "$esbuild_bin" src/main.ts "${common_flags[@]}" --outfile=../../dist/wasm/main.js
  "$esbuild_bin" src/worker.ts "${common_flags[@]}" --outfile=../../dist/wasm/worker.js
)
echo "Build complete!"
