#!/usr/bin/env sh
# Package a built CLI binary into cells-{os}-{arch}.tar.gz + .sha256
# Usage: package-cli.sh --binary <path> --os <macos|linux> --arch <arm64|x86_64> --out-dir <dir> [--version <ver>]
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
# shellcheck source=common.sh
. "$script_dir/common.sh"

binary=""
os=""
arch=""
out_dir=""
version=""

usage() {
  cat <<'USAGE'
Usage: package-cli.sh --binary <path> --os <macos|linux> --arch <arm64|x86_64> --out-dir <dir> [--version <ver>]

Creates:
  <out-dir>/cells-{os}-{arch}.tar.gz
  <out-dir>/cells-{os}-{arch}.tar.gz.sha256

The archive contains a single binary named "cells".
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --binary) binary="${2:-}"; shift 2 ;;
    --binary=*) binary="${1#*=}"; shift ;;
    --os) os="${2:-}"; shift 2 ;;
    --os=*) os="${1#*=}"; shift ;;
    --arch) arch="${2:-}"; shift 2 ;;
    --arch=*) arch="${1#*=}"; shift ;;
    --out-dir) out_dir="${2:-}"; shift 2 ;;
    --out-dir=*) out_dir="${1#*=}"; shift ;;
    --version) version="${2:-}"; shift 2 ;;
    --version=*) version="${1#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$binary" ] || [ -z "$os" ] || [ -z "$arch" ] || [ -z "$out_dir" ]; then
  usage >&2
  exit 1
fi

if [ ! -f "$binary" ]; then
  echo "Binary not found: $binary" >&2
  exit 1
fi

case "$os" in macos|linux) ;; *) echo "Invalid --os: $os" >&2; exit 1 ;; esac
case "$arch" in arm64|x86_64) ;; *) echo "Invalid --arch: $arch" >&2; exit 1 ;; esac

mkdir -p "$out_dir"
asset="$(cells_asset_name "$os" "$arch")"
stage="$(mktemp -d "${TMPDIR:-/tmp}/cells-package.XXXXXX")"
cleanup() { rm -rf "$stage"; }
trap cleanup EXIT INT HUP TERM

cp "$binary" "$stage/$CELLS_BINARY_NAME"
chmod 0755 "$stage/$CELLS_BINARY_NAME"
# Best-effort strip (may fail on some platforms / stripped already)
strip "$stage/$CELLS_BINARY_NAME" 2>/dev/null || true

if [ -n "$version" ]; then
  if ! "$stage/$CELLS_BINARY_NAME" --version 2>/dev/null | grep -Fq "$version"; then
    # Version stamp may be missing on local unstamped builds; warn only if binary runs
    if "$stage/$CELLS_BINARY_NAME" --version >/dev/null 2>&1; then
      echo "Warning: binary --version does not contain '$version' (got: $("$stage/$CELLS_BINARY_NAME" --version))" >&2
    fi
  fi
fi

tar -C "$stage" -czf "$out_dir/$asset" "$CELLS_BINARY_NAME"
cells_sha256_file "$out_dir/$asset" >"$out_dir/$asset.sha256"

# Sanity: archive lists the binary
tar -tzf "$out_dir/$asset" | grep -Fx "$CELLS_BINARY_NAME" >/dev/null

echo "Wrote $out_dir/$asset"
echo "Wrote $out_dir/$asset.sha256"
