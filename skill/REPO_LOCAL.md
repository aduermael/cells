# Using Cells from this repository (developers / local agents)

This file is present when the skill is discovered via **repo symlinks**
(`.agents/skills/cells`, `.claude/skills/cells`, `.grok/skills/cells` → `skill/`).
It is **not** part of the end-user install-skill package.

## Prefer an already-built CLI (do not rebuild by default)

From the repository root, resolve the CLI in this order and **stop at the first hit**:

1. **In-repo binary** (usual after a prior build):
   ```bash
   test -x ./dist/cli/cells && ./dist/cli/cells --version
   ```
2. **`cells` on PATH** (Homebrew / release install):
   ```bash
   which cells && cells --version
   ```
3. **Build only if neither exists** (or the user explicitly asks to rebuild):
   ```bash
   bazel run :cli
   # writes ./dist/cli/cells
   ```

**Agents:** do **not** run `bazel run :cli` on every task. Building is slow. Use
`./dist/cli/cells` or `cells` when present. Rebuild only when:

- there is no usable binary, or
- the user asks to rebuild / refresh the CLI after engine changes.

Invoke the chosen binary for all work (convert, inspect, script, **session**, sync):

```bash
./dist/cli/cells --help
# Multi-step collab (preferred for agents; stdout is pure JSON / JSONL):
./dist/cli/cells session start 'http://localhost:8081/?room=ROOM_ID' --wait-seconds 15
# → {"ok":true,"id":"...","state":"ONLINE","ready":true,...}
./dist/cli/cells session exec SESSION_ID -e 'setCell("A1", 1)'
./dist/cli/cells session export SESSION_ID /tmp/room.xlsx
./dist/cli/cells session watch SESSION_ID --duration 10   # JSONL
./dist/cli/cells session list
./dist/cli/cells session stop SESSION_ID
# One-shot blocking listen / apply-to-file (optional):
./dist/cli/cells sync --server 'http://localhost:8081/?room=ROOM_ID' --apply /tmp/room.zcd
# (--apply creates the file if missing, then saves on Ctrl+C)
```

Release install (`install.sh` / Homebrew) is for end users without a checkout.
In this repo, a local `./dist/cli/cells` is preferred so agents match local
engine changes — once it has been built.

## Local human + agent collab demo

1. Terminal A — web + collab server (human):
   ```bash
   bazel run :wasm
   bazel run :serve
   # open http://localhost:8081/
   ```
2. In the browser: Collaborate → Copy Link (room URL).
3. Terminal B — agent with this skill:
   ```bash
   # Use existing binary if present; build only if missing
   CELLS=./dist/cli/cells
   test -x "$CELLS" || bazel run :cli
   # Start a long-running session so the CLI peer stays visible in the browser.
   # start waits until ONLINE/SYNCING (fails if stuck CONNECTING).
   OUT=$("$CELLS" session start 'http://localhost:8081/?room=ROOM_ID' --name 'CLI Agent')
   SID=$(echo "$OUT" | python3 -c 'import sys,json; print(json.load(sys.stdin)["id"])')
   "$CELLS" session exec "$SID" -e 'setCell("A1", "hello from agent")'
   "$CELLS" session export "$SID" /tmp/room.xlsx
   "$CELLS" session watch "$SID" --duration 60
   "$CELLS" session stop "$SID"
   ```

The human edits in the web UI; the agent uses the CLI session against the same room.
The session daemon keeps the peer connected between commands (idle auto-stop default
30 minutes). If `start` returns `ready:false` / error about CONNECTING, rebuild the
CLI (`bazel run :cli`) — older builds did not pump the macOS network run loop.
