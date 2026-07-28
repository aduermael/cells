#!/usr/bin/env bash
# Unit test for tools/prepare-pages.sh — drives the real assemble script.
# Run: ./tools/prepare-pages_test.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$REPO_ROOT/tools/prepare-pages.sh"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/prepare-pages-test.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

OUT="$WORKDIR/out"
FAIL=0

assert_ok() {
  local name="$1"
  shift
  if "$@"; then
    echo "ok - $name"
  else
    echo "not ok - $name" >&2
    FAIL=1
  fi
}

assert_fail() {
  local name="$1"
  shift
  if "$@" >/dev/null 2>&1; then
    echo "not ok - $name (expected failure)" >&2
    FAIL=1
  else
    echo "ok - $name"
  fi
}

# --- happy path with custom URLs ---
APP_URL="https://example.test/app" REPO_URL="https://github.com/example/cells" \
  "$SCRIPT" "$OUT"

assert_ok "index.html exists" test -f "$OUT/index.html"
assert_ok "styles.css exists" test -f "$OUT/styles.css"
assert_ok "favicon.svg exists" test -f "$OUT/favicon.svg"
assert_ok "iframe points at APP_URL" grep -q 'src="https://example.test/app"' "$OUT/index.html"
assert_ok "GitHub link present" grep -q 'https://github.com/example/cells' "$OUT/index.html"
assert_ok "no leftover {{tokens}}" \
  bash -c "! grep -E '\\{\\{[A-Z0-9_]+\\}\\}' '$OUT/index.html'"
assert_ok "no __PLACEHOLDER__" \
  bash -c "! grep -F '__PLACEHOLDER__' '$OUT/index.html'"

# Trailing slash on APP_URL is stripped
OUT2="$WORKDIR/out2"
APP_URL="https://example.test/app/" REPO_URL="https://github.com/example/cells/" \
  "$SCRIPT" "$OUT2" >/dev/null
assert_ok "trailing slash stripped from iframe src" \
  grep -q 'src="https://example.test/app"' "$OUT2/index.html"
assert_ok "trailing slash stripped from repo links" \
  grep -q 'href="https://github.com/example/cells"' "$OUT2/index.html"

# Defaults produce the production Fly demo URL
OUT3="$WORKDIR/out3"
unset APP_URL REPO_URL || true
"$SCRIPT" "$OUT3" >/dev/null
assert_ok "default APP_URL is cells-app.fly.dev" \
  grep -q 'src="https://cells-app.fly.dev"' "$OUT3/index.html"
assert_ok "default REPO_URL is aduermael/cells" \
  grep -q 'https://github.com/aduermael/cells' "$OUT3/index.html"

if [[ "$FAIL" -ne 0 ]]; then
  echo "prepare-pages tests FAILED" >&2
  exit 1
fi
echo "prepare-pages tests PASSED"
