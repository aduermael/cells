#!/usr/bin/env sh
# Validate a release tag and print version= / tag= for GitHub Actions outputs.
# Usage: validate-tag.sh <tag>
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
# shellcheck source=common.sh
. "$script_dir/common.sh"

tag="${1:-}"
if [ -z "$tag" ]; then
  echo "Usage: $0 <tag>" >&2
  exit 1
fi

if ! cells_is_semver_tag "$tag"; then
  echo "Release tags must use v-prefixed semver, such as v1.2.3. Got: $tag" >&2
  exit 1
fi

version="$(cells_version_from_tag "$tag")"
echo "tag=$tag"
echo "version=$version"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "tag=$tag" >>"$GITHUB_OUTPUT"
  echo "version=$version" >>"$GITHUB_OUTPUT"
fi
