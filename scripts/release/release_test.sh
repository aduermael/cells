#!/usr/bin/env bash
# Unit/integration tests for release packaging and install helpers.
# Drives the real scripts (no reimplementation). Run: ./scripts/release/release_test.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SCRATCH="${CELLS_TEST_SCRATCH:-$(mktemp -d "${TMPDIR:-/tmp}/cells-release-test.XXXXXX")}"
mkdir -p "$SCRATCH"
# shellcheck source=common.sh
. "$REPO_ROOT/scripts/release/common.sh"

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

assert_eq() {
  local name="$1" expected="$2" actual="$3"
  if [ "$expected" = "$actual" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name (expected '$expected', got '$actual')" >&2
    FAIL=1
  fi
}

echo "== common helpers =="
assert_ok "semver v0.0.1" cells_is_semver_tag "v0.0.1"
assert_ok "semver v1.2.3-rc.1" cells_is_semver_tag "v1.2.3-rc.1"
if cells_is_semver_tag "0.0.1"; then
  echo "not ok - reject no-v" >&2; FAIL=1
else
  echo "ok - reject no-v"
fi
if cells_is_semver_tag "vaboogie"; then
  echo "not ok - reject garbage" >&2; FAIL=1
else
  echo "ok - reject garbage"
fi
assert_eq "version from tag" "0.0.1" "$(cells_version_from_tag v0.0.1)"
assert_eq "tag from version" "v0.0.1" "$(cells_tag_from_version 0.0.1)"
assert_eq "asset name" "cells-linux-arm64.tar.gz" "$(cells_asset_name linux arm64)"
assert_eq "release latest url" \
  "https://github.com/aduermael/cells/releases/latest/download" \
  "$(cells_release_base_url aduermael/cells latest)"
assert_eq "release tag url" \
  "https://github.com/aduermael/cells/releases/download/v0.0.1" \
  "$(cells_release_base_url aduermael/cells v0.0.1)"

echo "== validate-tag.sh =="
out="$(scripts/release/validate-tag.sh v1.2.3)"
assert_ok "validate prints tag" grep -q '^tag=v1.2.3$' <<<"$out"
assert_ok "validate prints version" grep -q '^version=1.2.3$' <<<"$out"
assert_ok "validate rejects bad tag" bash -c '! scripts/release/validate-tag.sh not-a-tag >/dev/null 2>&1'

echo "== package-cli.sh (twice for consistency) =="
fake_bin="$SCRATCH/fake-cells"
cat >"$fake_bin" <<'EOF'
#!/bin/sh
echo "cells 9.9.9"
EOF
chmod +x "$fake_bin"

package_once() {
  local run_dir="$1"
  mkdir -p "$run_dir"
  scripts/release/package-cli.sh \
    --binary "$fake_bin" \
    --os linux \
    --arch x86_64 \
    --out-dir "$run_dir" \
    --version 9.9.9
  test -s "$run_dir/cells-linux-x86_64.tar.gz"
  test -s "$run_dir/cells-linux-x86_64.tar.gz.sha256"
  grep -Eq '^[a-f0-9]{64}$' "$run_dir/cells-linux-x86_64.tar.gz.sha256"
  tar -tzf "$run_dir/cells-linux-x86_64.tar.gz" | grep -Fx cells
  cells_verify_sha256 \
    "$run_dir/cells-linux-x86_64.tar.gz" \
    "$run_dir/cells-linux-x86_64.tar.gz.sha256"
}

package_once "$SCRATCH/pkg1"
package_once "$SCRATCH/pkg2"
assert_ok "package run 1 checksum file" test -f "$SCRATCH/pkg1/cells-linux-x86_64.tar.gz.sha256"
assert_ok "package run 2 checksum file" test -f "$SCRATCH/pkg2/cells-linux-x86_64.tar.gz.sha256"
# Same input binary → same digest
sum1="$(cat "$SCRATCH/pkg1/cells-linux-x86_64.tar.gz.sha256")"
sum2="$(cat "$SCRATCH/pkg2/cells-linux-x86_64.tar.gz.sha256")"
assert_eq "package deterministic checksum" "$sum1" "$sum2"

echo "== write-homebrew-formula.sh =="
ckdir="$SCRATCH/cksums"
mkdir -p "$ckdir"
for asset in cells-macos-arm64.tar.gz cells-macos-x86_64.tar.gz \
             cells-linux-arm64.tar.gz cells-linux-x86_64.tar.gz; do
  # package a tiny unique payload so each has a real sha
  stage="$SCRATCH/stage-$asset"
  mkdir -p "$stage"
  echo "payload-$asset" >"$stage/cells"
  tar -C "$stage" -czf "$ckdir/$asset" cells
  cells_sha256_file "$ckdir/$asset" >"$ckdir/$asset.sha256"
done
scripts/release/write-homebrew-formula.sh \
  --version 0.0.1 \
  --tag v0.0.1 \
  --checksum-dir "$ckdir" \
  --output "$SCRATCH/cells.rb"
assert_ok "formula exists" test -f "$SCRATCH/cells.rb"
assert_ok "formula has version" grep -q 'version "0.0.1"' "$SCRATCH/cells.rb"
assert_ok "formula has macos arm url" grep -q 'cells-macos-arm64.tar.gz' "$SCRATCH/cells.rb"
assert_ok "formula has linux intel url" grep -q 'cells-linux-x86_64.tar.gz' "$SCRATCH/cells.rb"
assert_ok "formula installs cells" grep -q 'bin.install "cells"' "$SCRATCH/cells.rb"
mac_arm_sha="$(awk '{print $1}' "$ckdir/cells-macos-arm64.tar.gz.sha256")"
assert_ok "formula embeds real checksum" grep -q "$mac_arm_sha" "$SCRATCH/cells.rb"

echo "== install.sh dry-run against local HTTP server (twice) =="
# Serve packaged assets
serve_dir="$SCRATCH/pkg1"
# Detect host os/arch for asset name expected by install.sh
host_os="$(cells_detect_os)"
host_arch="$(cells_detect_arch)"
host_asset="$(cells_asset_name "$host_os" "$host_arch")"
# Re-package for host platform
scripts/release/package-cli.sh \
  --binary "$fake_bin" \
  --os "$host_os" \
  --arch "$host_arch" \
  --out-dir "$serve_dir"

# Pick a free port
port=8765
for try in $(seq 8765 8799); do
  if ! (echo >/dev/tcp/127.0.0.1/$try) 2>/dev/null; then
    port=$try
    break
  fi
done

# Python http.server in background
python3 -m http.server "$port" --directory "$serve_dir" >/dev/null 2>&1 &
server_pid=$!
cleanup_server() { kill "$server_pid" 2>/dev/null || true; }
trap cleanup_server EXIT

# Wait for server
for _ in $(seq 1 50); do
  if curl -fsS "http://127.0.0.1:$port/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

run_install_dry() {
  local log="$1"
  CELLS_BASE_URL="http://127.0.0.1:$port" \
  CELLS_DRY_RUN=1 \
  CELLS_INSTALL_DIR="$SCRATCH/install-bin" \
    sh "$REPO_ROOT/install.sh" >"$log" 2>&1
}

run_install_dry "$SCRATCH/install1.log"
run_install_dry "$SCRATCH/install2.log"
assert_ok "dry-run 1 mentions Dry run" grep -q 'Dry run' "$SCRATCH/install1.log"
assert_ok "dry-run 2 mentions Dry run" grep -q 'Dry run' "$SCRATCH/install2.log"
assert_ok "dry-run 1 downloaded asset" grep -q "$host_asset" "$SCRATCH/install1.log"
assert_ok "dry-run did not require real install dir write for binary" true

# Real install into a writable dir
CELLS_BASE_URL="http://127.0.0.1:$port" \
CELLS_INSTALL_DIR="$SCRATCH/install-bin" \
  sh "$REPO_ROOT/install.sh" >"$SCRATCH/install-real.log" 2>&1
assert_ok "real install binary exists" test -x "$SCRATCH/install-bin/cells"
assert_ok "real install log" grep -q "Installed cells" "$SCRATCH/install-real.log"

echo "== install-skill.sh into temp project =="
proj="$SCRATCH/project"
mkdir -p "$proj"
(
  cd "$proj"
  sh "$REPO_ROOT/install-skill.sh" --codex --claude --grok
)
assert_ok "skill SKILL.md codex" test -f "$proj/.agents/skills/cells/SKILL.md"
assert_ok "skill install.sh codex" test -x "$proj/.agents/skills/cells/install.sh"
assert_ok "skill SCRIPTING.md codex" test -f "$proj/.agents/skills/cells/SCRIPTING.md"
assert_ok "skill sample set-values" test -f "$proj/.agents/skills/cells/samples/set-values.luau"
assert_ok "skill sample read-print" test -f "$proj/.agents/skills/cells/samples/read-print.luau"
assert_ok "skill sample transform" test -f "$proj/.agents/skills/cells/samples/transform.luau"
assert_ok "skill SKILL.md claude" test -f "$proj/.claude/skills/cells/SKILL.md"
assert_ok "skill SCRIPTING.md claude" test -f "$proj/.claude/skills/cells/SCRIPTING.md"
assert_ok "skill SKILL.md grok" test -f "$proj/.grok/skills/cells/SKILL.md"
assert_ok "skill SCRIPTING.md grok" test -f "$proj/.grok/skills/cells/SCRIPTING.md"
assert_ok "skill body mentions brew" grep -qi 'Homebrew' "$proj/.agents/skills/cells/SKILL.md"
assert_ok "skill checks which cells" grep -q 'which cells' "$proj/.agents/skills/cells/SKILL.md"
# End-user installed skill must not require a bazel checkout; repo-local REPO_LOCAL.md is not packaged.
assert_ok "skill has no bazel" bash -c "! grep -Eiq 'bazel|Build from source' '$proj/.agents/skills/cells/SKILL.md'"
assert_ok "skill documents room URL sync" grep -q 'cells sync' "$proj/.agents/skills/cells/SKILL.md"
assert_ok "skill documents room id" grep -Eq 'room|ROOM_ID' "$proj/.agents/skills/cells/SKILL.md"
assert_ok "installed skill has no REPO_LOCAL" bash -c "! test -f '$proj/.agents/skills/cells/REPO_LOCAL.md'"
assert_ok "skill points to SCRIPTING.md" grep -q 'SCRIPTING.md' "$proj/.agents/skills/cells/SKILL.md"
assert_ok "skill API ref documents getCell" grep -q 'getCell' "$proj/.agents/skills/cells/SCRIPTING.md"
assert_ok "skill API ref documents setCell" grep -q 'setCell' "$proj/.agents/skills/cells/SCRIPTING.md"
assert_ok "skill installer has no npm install" \
  bash -c "! grep -Eiq 'npm[[:space:]]+install|npm i |npx ' '$proj/.agents/skills/cells/install.sh'"
assert_ok "skill installer tries brew" grep -q 'brew install' "$proj/.agents/skills/cells/install.sh"
assert_ok "skill installer falls back to direct" grep -q 'direct installer' "$proj/.agents/skills/cells/install.sh"

# Idempotent second run
(
  cd "$proj"
  out="$(sh "$REPO_ROOT/install-skill.sh" --codex 2>&1)"
  echo "$out" | grep -qi 'up to date\|Updated\|Installed'
)

kill "$server_pid" 2>/dev/null || true
trap - EXIT

if [ "$FAIL" -ne 0 ]; then
  echo "FAILED: $FAIL assertion(s)" >&2
  exit 1
fi
echo "All release/install tests passed."
