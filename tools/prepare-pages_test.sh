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
assert_ok "highlight-bash.js copied" test -f "$OUT/highlight-bash.js"
assert_ok "APP_URL present for demo" grep -q 'https://example.test/app' "$OUT/index.html"
assert_ok "data-app-url present" grep -q 'data-app-url="https://example.test/app"' "$OUT/index.html"
assert_ok "GitHub link present" grep -q 'https://github.com/example/cells' "$OUT/index.html"
assert_ok "title is agent-focused" grep -q 'A spreadsheet engine for agents' "$OUT/index.html"
assert_ok "intro matches README agentic framing" \
  grep -q 'built with agentic use primarily in mind' "$OUT/index.html"
assert_ok "intro headless CLI line" grep -q 'lightweight headless CLI' "$OUT/index.html"
assert_ok "CTA is try it right here" grep -q 'Try it right here' "$OUT/index.html"
assert_ok "CTA is plain text not a button" \
  bash -c "! grep -E 'class=\"btn[^\"]*\"[^>]*>Try it right here' '$OUT/index.html'"
assert_ok "drag-drop note is separate dimmed line" grep -q 'class="cta-note"' "$OUT/index.html"
assert_ok "no Why Cells section" bash -c "! grep -q 'Why Cells' '$OUT/index.html'"
assert_ok "agents section matches README title" \
  grep -q 'Made for AI agents to work with, not an agent itself' "$OUT/index.html"
assert_ok "collab demo credits Grok Build only" \
  bash -c "grep -q 'Grok Build (using the CLI)' '$OUT/index.html' && ! grep -q 'Codex agents' '$OUT/index.html'"
assert_ok "collab video autoplay loop muted" \
  bash -c "grep -A8 'img/demo.mp4' '$OUT/index.html' | grep -q autoplay && grep -A8 'img/demo.mp4' '$OUT/index.html' | grep -q loop && grep -A8 'img/demo.mp4' '$OUT/index.html' | grep -q muted"
assert_ok "Scriptable section heading" grep -q '>Scriptable</h2>' "$OUT/index.html"
assert_ok "Luau runtime mentioned" grep -q 'Luau' "$OUT/index.html"
assert_ok "no scripting image caption" \
  bash -c "! grep -q 'In-browser Luau scripting' '$OUT/index.html'"
assert_ok "scripting screenshot light" grep -q 'img/scripting.png' "$OUT/index.html"
assert_ok "scripting screenshot dark" grep -q 'img/scripting-dark.png' "$OUT/index.html"
assert_ok "scripting.png copied" test -f "$OUT/img/scripting.png"
assert_ok "scripting-dark.png copied" test -f "$OUT/img/scripting-dark.png"
assert_ok "collab demo video referenced" grep -q 'img/demo.mp4' "$OUT/index.html"
assert_ok "demo.mp4 copied" test -f "$OUT/img/demo.mp4"
assert_ok "collab video after interactive demo" \
  bash -c "demo=\$(grep -n 'id=\"demo\"' '$OUT/index.html' | head -1 | cut -d: -f1); vid=\$(grep -n 'img/demo.mp4' '$OUT/index.html' | head -1 | cut -d: -f1); test \"\$demo\" -lt \"\$vid\""
assert_ok "skill install in agents section" \
  bash -c "agents=\$(grep -n 'id=\"agents\"' '$OUT/index.html' | head -1 | cut -d: -f1); install=\$(grep -n 'id=\"install\"' '$OUT/index.html' | head -1 | cut -d: -f1); skill=\$(grep -n 'install-skill.sh' '$OUT/index.html' | head -1 | cut -d: -f1); test \"\$agents\" -lt \"\$skill\" && test \"\$skill\" -lt \"\$install\""
assert_ok "CLI setCell example" grep -q 'setCell("A1"' "$OUT/index.html"
assert_ok "Install section id" grep -q 'id="install"' "$OUT/index.html"
assert_ok "nav Install scrolls to section" grep -q 'href="#install"' "$OUT/index.html"
assert_ok "Install heading" grep -q '>Install</h2>' "$OUT/index.html"
assert_ok "Homebrew install" grep -q 'brew install aduermael/tap/cells' "$OUT/index.html"
assert_ok "code blocks have data-lang" grep -q 'data-lang="bash"' "$OUT/index.html"
assert_ok "page loads highlighter script" grep -q 'highlight-bash.js' "$OUT/index.html"
assert_ok "syntax token styles" grep -q 'tok-cmd' "$OUT/styles.css"
assert_ok "interactive demo still first" \
  bash -c "demo=\$(grep -n 'id=\"demo\"' '$OUT/index.html' | head -1 | cut -d: -f1); script=\$(grep -n 'scripting.png' '$OUT/index.html' | head -1 | cut -d: -f1); test \"\$demo\" -lt \"\$script\""
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
assert_ok "theme-aware shot CSS" grep -q 'shot-dark' "$OUT/styles.css"
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

# Unit-test the real shipped highlighter (assembled artifact path).
assert_ok "shipped highlightBash colors bash tokens" \
  node -e '
    const hl = require(process.argv[1]);
    const out = hl.highlightBash(
      "# comment\ncells -i data.csv -e '\''setCell(\"A1\", 100)'\'' | sh"
    );
    if (!out.includes("tok-comment")) process.exit(2);
    if (!out.includes("tok-cmd")) process.exit(3);
    if (!out.includes("tok-flag")) process.exit(4);
    if (!out.includes("tok-string")) process.exit(5);
    if (!out.includes("tok-op")) process.exit(6);
    if (out.includes("<script")) process.exit(7);
    // Escape check
    const esc = hl.highlightBash("echo <raw> & more");
    if (esc.includes("<raw>") || !esc.includes("&lt;raw&gt;")) process.exit(8);
    console.log("highlight ok");
  ' "$OUT/highlight-bash.js"

if [[ "$FAIL" -ne 0 ]]; then
  echo "prepare-pages tests FAILED" >&2
  exit 1
fi
echo "prepare-pages tests PASSED"
