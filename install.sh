#!/usr/bin/env sh
# Install the cells CLI from GitHub release assets.
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/aduermael/cells/main/install.sh | sh
#   CELLS_VERSION=v0.0.1 CELLS_INSTALL_DIR=$HOME/.local/bin sh install.sh
set -eu

repo="${CELLS_REPO:-aduermael/cells}"
install_dir="${CELLS_INSTALL_DIR:-/usr/local/bin}"
version="${CELLS_VERSION:-latest}"
binary_name="${CELLS_BINARY_NAME:-cells}"

# Allow tests / mirrors to override the download base (no trailing slash).
# Example: CELLS_BASE_URL=http://127.0.0.1:8765
base_url_override="${CELLS_BASE_URL:-}"

script_dir=""
case "$0" in
  */*) script_dir=$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd) || script_dir="" ;;
esac

if [ -n "$script_dir" ] && [ -f "$script_dir/scripts/release/common.sh" ]; then
  # shellcheck source=scripts/release/common.sh
  . "$script_dir/scripts/release/common.sh"
else
  # Inline minimal helpers when curl|sh without the repo tree
  cells_detect_os() {
    case "$(uname -s)" in
      Darwin) echo "macos" ;;
      Linux) echo "linux" ;;
      *) echo "Unsupported OS: $(uname -s)" >&2; return 1 ;;
    esac
  }
  cells_detect_arch() {
    case "$(uname -m)" in
      arm64|aarch64) echo "arm64" ;;
      x86_64|amd64) echo "x86_64" ;;
      *) echo "Unsupported CPU architecture: $(uname -m)" >&2; return 1 ;;
    esac
  }
  cells_asset_name() { echo "${binary_name}-$1-$2.tar.gz"; }
  cells_tag_from_version() {
    case "$1" in v*) echo "$1" ;; *) echo "v$1" ;; esac
  }
  cells_release_base_url() {
    r="$1"; v="$2"
    if [ "$v" = "latest" ]; then
      echo "https://github.com/$r/releases/latest/download"
    else
      echo "https://github.com/$r/releases/download/$(cells_tag_from_version "$v")"
    fi
  }
  cells_sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
      sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
      shasum -a 256 "$1" | awk '{print $1}'
    else
      echo "Missing sha256sum or shasum" >&2
      return 1
    fi
  }
  cells_verify_sha256() {
    expected="$(awk '{print $1}' "$2")"
    actual="$(cells_sha256_file "$1")"
    if [ "$expected" != "$actual" ]; then
      echo "Checksum mismatch for $(basename "$1")." >&2
      return 1
    fi
  }
fi

if [ -n "${CELLS_INSTALL_URL:-}" ]; then
  install_url="$CELLS_INSTALL_URL"
else
  install_url="https://raw.githubusercontent.com/$repo/main/install.sh"
fi

os="$(cells_detect_os)"
arch="$(cells_detect_arch)"

for cmd in curl tar; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command: $cmd" >&2
    exit 1
  fi
done

asset="$(cells_asset_name "$os" "$arch")"
if [ -n "$base_url_override" ]; then
  base_url="$base_url_override"
else
  base_url="$(cells_release_base_url "$repo" "$version")"
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/cells-install.XXXXXX")"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT HUP TERM

archive="$tmpdir/$asset"
checksum="$archive.sha256"

echo "Downloading $asset from $base_url..."
curl -fL "$base_url/$asset" -o "$archive"
curl -fL "$base_url/$asset.sha256" -o "$checksum"

cells_verify_sha256 "$archive" "$checksum"

tar -xzf "$archive" -C "$tmpdir"
if [ ! -f "$tmpdir/$binary_name" ]; then
  echo "Release asset did not contain a $binary_name binary." >&2
  exit 1
fi

if [ "${CELLS_DRY_RUN:-0}" = "1" ]; then
  echo "Dry run: verified $asset and extracted $binary_name (not installing)."
  echo "Would install to $install_dir/$binary_name"
  "$tmpdir/$binary_name" --version 2>/dev/null || true
  exit 0
fi

install_binary() {
  src="$1"
  dest="$2"
  if command -v install >/dev/null 2>&1; then
    install -m 0755 "$src" "$dest"
  else
    cp "$src" "$dest"
    chmod 0755 "$dest"
  fi
}

sudo_install_binary() {
  src="$1"
  dest="$2"
  if command -v install >/dev/null 2>&1; then
    sudo install -m 0755 "$src" "$dest"
  else
    sudo cp "$src" "$dest"
    sudo chmod 0755 "$dest"
  fi
}

print_permission_help() {
  echo "Could not write to $install_dir." >&2
  echo >&2
  if [ "$install_dir" = "/usr/local/bin" ]; then
    echo "To install to /usr/local/bin with admin permissions:" >&2
    echo "  curl -fsSL $install_url | sudo sh" >&2
    echo >&2
    echo "Or choose a user-writable install directory:" >&2
    echo "  curl -fsSL $install_url | env CELLS_INSTALL_DIR=\$HOME/.local/bin sh" >&2
  else
    echo "Choose a writable install directory with CELLS_INSTALL_DIR, for example:" >&2
    echo "  curl -fsSL $install_url | env CELLS_INSTALL_DIR=\$HOME/.local/bin sh" >&2
  fi
}

if ! mkdir -p "$install_dir" 2>/dev/null || ! install_binary "$tmpdir/$binary_name" "$install_dir/$binary_name" 2>/dev/null; then
  if [ "$install_dir" = "/usr/local/bin" ] && command -v sudo >/dev/null 2>&1 && [ -r /dev/tty ]; then
    printf "Installing to /usr/local/bin requires admin permissions. Use sudo? [y/N] " >/dev/tty
    IFS= read -r answer </dev/tty
    case "$answer" in
      y|Y|yes|YES) sudo_install_binary "$tmpdir/$binary_name" "$install_dir/$binary_name" ;;
      *)
        print_permission_help
        exit 1
        ;;
    esac
  else
    print_permission_help
    exit 1
  fi
fi

echo "Installed $binary_name to $install_dir/$binary_name"
if ! command -v "$binary_name" >/dev/null 2>&1; then
  echo "Make sure $install_dir is on PATH."
fi
