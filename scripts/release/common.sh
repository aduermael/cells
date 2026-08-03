#!/usr/bin/env sh
# Shared release helpers: version/tag validation and asset naming.
# Source from workflow scripts, installers, and tests (DRY).

# shellcheck disable=SC2034
CELLS_BINARY_NAME="${CELLS_BINARY_NAME:-cells}"
CELLS_REPO="${CELLS_REPO:-aduermael/cells}"

# Validate v-prefixed semver (e.g. v1.2.3, v0.0.1-rc.1). Returns 0 if valid.
cells_is_semver_tag() {
  tag="$1"
  # shellcheck disable=SC3010
  case "$tag" in
    v[0-9]*)
      # Full check with sed-friendly pattern
      echo "$tag" | grep -Eq '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'
      ;;
    *) return 1 ;;
  esac
}

# Strip leading v from a tag → version. e.g. v1.2.3 → 1.2.3
cells_version_from_tag() {
  tag="$1"
  echo "${tag#v}"
}

# Normalize version/tag input to a tag with v prefix.
cells_tag_from_version() {
  version="$1"
  case "$version" in
    v*) echo "$version" ;;
    *) echo "v$version" ;;
  esac
}

# Default product version for unstamped/dev builds (web UI + CLI fallback).
# Keep in sync with apps/wasm/src/version.ts and apps/cli/cli_version.h
# (enforced by scripts/release/release_test.sh "default version sync").
# shellcheck disable=SC2034
CELLS_DEFAULT_VERSION="${CELLS_DEFAULT_VERSION:-0.0.1}"

# Nearest ancestor tag that is a v-prefixed semver (stdout), or empty.
# Prefers git describe with a v[0-9]* match so non-semver tags nearer HEAD
# cannot mask the last product version; falls back to walking v* tags.
cells_nearest_semver_tag() {
  root="$1"
  tag=""
  if ! command -v git >/dev/null 2>&1 || [ ! -d "$root" ]; then
    return 1
  fi
  # Prefer describe limited to v-prefixed numeric tags (avoids e.g. "nightly").
  tag="$(git -C "$root" describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)"
  if [ -n "$tag" ] && cells_is_semver_tag "$tag"; then
    echo "$tag"
    return 0
  fi
  # Walk v* tags highest-first; pick first valid semver that is an ancestor of HEAD.
  # Use a here-doc (not a pipe) so the loop is not a subshell and can return.
  while IFS= read -r candidate; do
    [ -z "$candidate" ] && continue
    if cells_is_semver_tag "$candidate" &&
      git -C "$root" merge-base --is-ancestor "$candidate" HEAD 2>/dev/null; then
      echo "$candidate"
      return 0
    fi
  done <<EOF
$(git -C "$root" tag -l 'v*' --sort=-v:refname 2>/dev/null || true)
EOF
  return 1
}

# Resolve product version for WASM/UI stamp (tools/wasm-ts-build.sh, build.mjs).
# Priority: CELLS_VERSION env (validated semver, v-prefix optional) → nearest
# git semver tag → default.
# Prints bare semver to stdout (no leading v), e.g. 0.0.5.
# Optional: CELLS_VERSION_GIT_DIR / REPO_ROOT for git -C when cwd is not the repo root.
# Invalid CELLS_VERSION is ignored (warning on stderr) so esbuild --define stays safe.
cells_resolve_product_version() {
  if [ -n "${CELLS_VERSION:-}" ]; then
    tag="$(cells_tag_from_version "$CELLS_VERSION")"
    if cells_is_semver_tag "$tag"; then
      cells_version_from_tag "$tag"
      return 0
    fi
    echo "Warning: CELLS_VERSION='${CELLS_VERSION}' is not valid semver; ignoring." >&2
  fi
  root="${CELLS_VERSION_GIT_DIR:-${REPO_ROOT:-.}}"
  tag="$(cells_nearest_semver_tag "$root" || true)"
  if [ -n "$tag" ]; then
    cells_version_from_tag "$tag"
    return 0
  fi
  echo "$CELLS_DEFAULT_VERSION"
}

# OS name used in asset filenames: macos | linux
cells_detect_os() {
  case "$(uname -s)" in
    Darwin) echo "macos" ;;
    Linux) echo "linux" ;;
    *)
      echo "Unsupported OS: $(uname -s)" >&2
      return 1
      ;;
  esac
}

# Arch name used in asset filenames: arm64 | x86_64
cells_detect_arch() {
  case "$(uname -m)" in
    arm64|aarch64) echo "arm64" ;;
    x86_64|amd64) echo "x86_64" ;;
    *)
      echo "Unsupported CPU architecture: $(uname -m)" >&2
      return 1
      ;;
  esac
}

# Asset basename: cells-{os}-{arch}.tar.gz
cells_asset_name() {
  os="$1"
  arch="$2"
  echo "${CELLS_BINARY_NAME}-${os}-${arch}.tar.gz"
}

# GitHub release download base URL for a version (latest or tag/version).
cells_release_base_url() {
  repo="${1:-$CELLS_REPO}"
  version="${2:-latest}"
  if [ "$version" = "latest" ]; then
    echo "https://github.com/$repo/releases/latest/download"
  else
    tag="$(cells_tag_from_version "$version")"
    echo "https://github.com/$repo/releases/download/$tag"
  fi
}

# Compute sha256 of a file (sha256sum or shasum).
cells_sha256_file() {
  file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  else
    echo "Missing sha256sum or shasum" >&2
    return 1
  fi
}

# Verify archive against a .sha256 file containing a single hex digest (optionally with filename).
cells_verify_sha256() {
  archive="$1"
  checksum_file="$2"
  expected="$(awk '{print $1}' "$checksum_file")"
  actual="$(cells_sha256_file "$archive")"
  if [ "$expected" != "$actual" ]; then
    echo "Checksum mismatch for $(basename "$archive")." >&2
    echo "  expected: $expected" >&2
    echo "  actual:   $actual" >&2
    return 1
  fi
}
