#!/usr/bin/env bash
# Assemble the static GitHub Pages site from website/ sources.
#
# Usage:
#   ./tools/prepare-pages.sh [out_dir]
#
# Env (optional):
#   APP_URL   iframe / demo URL (default: https://cells-app.fly.dev)
#   REPO_URL  GitHub repository URL (default: https://github.com/aduermael/cells)
#
# Output: a clean directory suitable for actions/upload-pages-artifact.
# Fails if any {{PLACEHOLDER}} tokens remain after substitution.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$REPO_ROOT/website"
OUT_DIR="${1:-$REPO_ROOT/dist/pages}"

APP_URL="${APP_URL:-https://cells-app.fly.dev}"
REPO_URL="${REPO_URL:-https://github.com/aduermael/cells}"

# Normalize: strip trailing slash so iframe/src joins cleanly.
APP_URL="${APP_URL%/}"
REPO_URL="${REPO_URL%/}"

if [[ ! -d "$SRC_DIR" ]]; then
  echo "error: website sources not found at $SRC_DIR" >&2
  exit 1
fi

if [[ ! -f "$SRC_DIR/index.html" || ! -f "$SRC_DIR/styles.css" ]]; then
  echo "error: expected website/index.html and website/styles.css" >&2
  exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Substitute known placeholders only (KISS: sed, fixed token set).
# Use | as delimiter so URLs with / do not break the expression.
subst() {
  local file="$1"
  sed \
    -e "s|{{APP_URL}}|${APP_URL}|g" \
    -e "s|{{REPO_URL}}|${REPO_URL}|g" \
    "$file"
}

subst "$SRC_DIR/index.html" >"$OUT_DIR/index.html"
cp "$SRC_DIR/styles.css" "$OUT_DIR/styles.css"

# Prefer the app favicon; fall back to shared icon.
if [[ -f "$REPO_ROOT/apps/shared/favicons/favicon.svg" ]]; then
  cp "$REPO_ROOT/apps/shared/favicons/favicon.svg" "$OUT_DIR/favicon.svg"
elif [[ -f "$REPO_ROOT/apps/shared/icon.svg" ]]; then
  cp "$REPO_ROOT/apps/shared/icon.svg" "$OUT_DIR/favicon.svg"
else
  echo "error: no favicon.svg source under apps/shared/" >&2
  exit 1
fi

# Landing page media (theme-aware scripting shots + collab demo video from docs/img).
IMG_SRC="$REPO_ROOT/docs/img"
mkdir -p "$OUT_DIR/img"
for f in scripting.png scripting-dark.png demo.mp4; do
  if [[ ! -f "$IMG_SRC/$f" ]]; then
    echo "error: missing website image $IMG_SRC/$f" >&2
    exit 1
  fi
  cp "$IMG_SRC/$f" "$OUT_DIR/img/$f"
done

# Fail closed on unresolved template tokens.
if grep -R -n -E '\{\{[A-Z0-9_]+\}\}|__PLACEHOLDER__' "$OUT_DIR" >/dev/null; then
  echo "error: unresolved placeholders in $OUT_DIR:" >&2
  grep -R -n -E '\{\{[A-Z0-9_]+\}\}|__PLACEHOLDER__' "$OUT_DIR" >&2 || true
  exit 1
fi

# Basic structure checks so a broken assemble fails in CI.
if ! grep -q '<iframe' "$OUT_DIR/index.html"; then
  echo "error: assembled index.html is missing an <iframe>" >&2
  exit 1
fi
# Demo URL may appear as data-app-url / open-in-new-page href (src is set at runtime with theme).
if ! grep -q "${APP_URL}" "$OUT_DIR/index.html"; then
  echo "error: assembled index.html is missing APP_URL=${APP_URL}" >&2
  exit 1
fi
if ! grep -q 'data-app-url=' "$OUT_DIR/index.html"; then
  echo "error: iframe is missing data-app-url for themed demo loading" >&2
  exit 1
fi
if ! grep -q "$REPO_URL" "$OUT_DIR/index.html"; then
  echo "error: assembled index.html is missing REPO_URL" >&2
  exit 1
fi
if [[ ! -f "$OUT_DIR/img/scripting.png" || ! -f "$OUT_DIR/img/scripting-dark.png" ]]; then
  echo "error: assembled site is missing scripting screenshots under img/" >&2
  exit 1
fi
if [[ ! -f "$OUT_DIR/img/demo.mp4" ]]; then
  echo "error: assembled site is missing collab demo video img/demo.mp4" >&2
  exit 1
fi
if ! grep -q 'img/scripting.png' "$OUT_DIR/index.html"; then
  echo "error: index.html should reference img/scripting.png" >&2
  exit 1
fi
if ! grep -q 'img/demo.mp4' "$OUT_DIR/index.html"; then
  echo "error: index.html should reference img/demo.mp4" >&2
  exit 1
fi

echo "Pages artifact ready: $OUT_DIR"
ls -la "$OUT_DIR"
ls -la "$OUT_DIR/img"
