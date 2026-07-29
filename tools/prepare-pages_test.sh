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
assert_ok "APP_URL present for demo" grep -q 'https://example.test/app' "$OUT/index.html"
assert_ok "data-app-url present" grep -q 'data-app-url="https://example.test/app"' "$OUT/index.html"
assert_ok "GitHub link present" grep -q 'https://github.com/example/cells' "$OUT/index.html"
assert_ok "title is agent-focused" grep -q 'A spreadsheet engine for agents' "$OUT/index.html"
assert_ok "CTA is try it right here" grep -q 'Try it right here' "$OUT/index.html"
assert_ok "CTA is plain text not a button" \
  bash -c "! grep -E 'class=\"btn[^\"]*\"[^>]*>Try it right here' '$OUT/index.html'"
assert_ok "Lua/Luau feature copy" grep -q 'Lua/Luau' "$OUT/index.html"
assert_ok "headless CLI (no pipelines)" \
  bash -c "grep -q 'headless CLI' '$OUT/index.html' && ! grep -q 'pipelines' '$OUT/index.html'"
assert_ok ".zcd links to file-format docs" \
  grep -q 'docs/file-format.md' "$OUT/index.html"
assert_ok "no Open app nav" bash -c "! grep -q 'Open app' '$OUT/index.html'"
assert_ok "Open in new page below demo" grep -q 'Open in new page' "$OUT/index.html"
assert_ok "no dual-license footer" bash -c "! grep -qi 'dual-licensed' '$OUT/index.html'"
assert_ok "theme toggle present" grep -q 'id="theme-toggle"' "$OUT/index.html"
assert_ok "brand green token" grep -q '#0a9208' "$OUT/styles.css"
assert_ok "logo uses currentColor (theme-aware)" \
  grep -q 'stroke="currentColor"' "$OUT/index.html"
assert_ok "logo color uses brand token" grep -q 'color: var(--brand)' "$OUT/styles.css"
assert_ok "drag-drop privacy note" grep -q 'stays in your browser' "$OUT/index.html"
assert_ok "data-theme dark tokens" grep -q 'data-theme="dark"' "$OUT/styles.css"
assert_ok "demo uses theme-aware background" grep -q -- '--demo-bg' "$OUT/styles.css"
assert_ok "no leftover {{tokens}}" \
  bash -c "! grep -E '\\{\\{[A-Z0-9_]+\\}\\}' '$OUT/index.html'"
assert_ok "no __PLACEHOLDER__" \
  bash -c "! grep -F '__PLACEHOLDER__' '$OUT/index.html'"

# Trailing slash on APP_URL is stripped
OUT2="$WORKDIR/out2"
APP_URL="https://example.test/app/" REPO_URL="https://github.com/example/cells/" \
  "$SCRIPT" "$OUT2" >/dev/null
assert_ok "trailing slash stripped from app url" \
  grep -q 'data-app-url="https://example.test/app"' "$OUT2/index.html"
assert_ok "trailing slash stripped from repo links" \
  grep -q 'href="https://github.com/example/cells"' "$OUT2/index.html"

# Defaults produce the production Fly demo URL
OUT3="$WORKDIR/out3"
unset APP_URL REPO_URL || true
"$SCRIPT" "$OUT3" >/dev/null
assert_ok "default APP_URL is cells-app.fly.dev" \
  grep -q 'https://cells-app.fly.dev' "$OUT3/index.html"
assert_ok "default REPO_URL is aduermael/cells" \
  grep -q 'https://github.com/aduermael/cells' "$OUT3/index.html"

if [[ "$FAIL" -ne 0 ]]; then
  echo "prepare-pages tests FAILED" >&2
  exit 1
fi
echo "prepare-pages tests PASSED"
