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
./dist/cli/cells session start 'http://localhost:8081/?room=ROOM_ID'
# → {"ok":true,"id":"...","room":"ROOM_ID",...}
./dist/cli/cells session exec SESSION_ID -e 'setCell("A1", 1)'
# → {"ok":true,"id":"...","output":...}
./dist/cli/cells session watch SESSION_ID --duration 10   # JSONL event stream
./dist/cli/cells session list                            # [] or [{...}]
./dist/cli/cells session stop SESSION_ID
# One-shot blocking listen (optional):
./dist/cli/cells sync 'http://localhost:8081/?room=ROOM_ID'
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
   # Start a long-running session so the CLI peer stays visible in the browser
   "$CELLS" session start 'http://localhost:8081/?room=ROOM_ID' --name 'CLI Agent'
   # Use the printed "id" for further commands:
   "$CELLS" session exec SESSION_ID -e 'setCell("A1", "hello from agent")'
   "$CELLS" session watch SESSION_ID --duration 60
   # When done:
   "$CELLS" session stop SESSION_ID
   ```

The human edits in the web UI; the agent uses the CLI session against the same room.
The session daemon keeps the peer connected between `exec`/`watch` calls (idle
auto-stop defaults to 30 minutes; override with `--idle-minutes`).
