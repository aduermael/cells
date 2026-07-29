#!/usr/bin/env sh
# Write a Homebrew formula for cells from release checksum files.
# Usage: write-homebrew-formula.sh --version <ver> --tag <tag> --checksum-dir <dir> --output <file>
# Checksum dir must contain cells-*-*.tar.gz.sha256 files (one hex digest per file).
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
# shellcheck source=common.sh
. "$script_dir/common.sh"

version=""
tag=""
checksum_dir=""
output=""
repo="${CELLS_REPO}"
homepage="${CELLS_HOMEPAGE:-https://github.com/aduermael/cells}"

usage() {
  cat <<'USAGE'
Usage: write-homebrew-formula.sh --version <ver> --tag <tag> --checksum-dir <dir> --output <file>
USAGE
}

sha_of() {
  asset="$1"
  file="$checksum_dir/$asset.sha256"
  if [ ! -f "$file" ]; then
    echo "Missing checksum file: $file" >&2
    exit 1
  fi
  awk '{print $1}' "$file"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) version="${2:-}"; shift 2 ;;
    --version=*) version="${1#*=}"; shift ;;
    --tag) tag="${2:-}"; shift 2 ;;
    --tag=*) tag="${1#*=}"; shift ;;
    --checksum-dir) checksum_dir="${2:-}"; shift 2 ;;
    --checksum-dir=*) checksum_dir="${1#*=}"; shift ;;
    --output) output="${2:-}"; shift 2 ;;
    --output=*) output="${1#*=}"; shift ;;
    --repo) repo="${2:-}"; shift 2 ;;
    --repo=*) repo="${1#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$version" ] || [ -z "$tag" ] || [ -z "$checksum_dir" ] || [ -z "$output" ]; then
  usage >&2
  exit 1
fi

mkdir -p "$(dirname "$output")"

mac_arm="$(sha_of cells-macos-arm64.tar.gz)"
mac_intel="$(sha_of cells-macos-x86_64.tar.gz)"
linux_arm="$(sha_of cells-linux-arm64.tar.gz)"
linux_intel="$(sha_of cells-linux-x86_64.tar.gz)"

cat >"$output" <<EOF
class Cells < Formula
  desc "Lightweight spreadsheet engine CLI for conversion, scripting, and collaboration"
  homepage "$homepage"
  version "$version"
  license "GPL-3.0-or-later"

  on_macos do
    on_arm do
      url "https://github.com/$repo/releases/download/$tag/cells-macos-arm64.tar.gz"
      sha256 "$mac_arm"
    end

    on_intel do
      url "https://github.com/$repo/releases/download/$tag/cells-macos-x86_64.tar.gz"
      sha256 "$mac_intel"
    end
  end

  on_linux do
    on_arm do
      url "https://github.com/$repo/releases/download/$tag/cells-linux-arm64.tar.gz"
      sha256 "$linux_arm"
    end

    on_intel do
      url "https://github.com/$repo/releases/download/$tag/cells-linux-x86_64.tar.gz"
      sha256 "$linux_intel"
    end
  end

  def install
    bin.install "cells"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/cells --version")
  end
end
EOF

echo "Wrote Homebrew formula: $output"
