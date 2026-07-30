# Using Cells from this repository (developers / local agents)

This file is present when the skill is discovered via **repo symlinks**
(`.agents/skills/cells`, `.claude/skills/cells`, `.grok/skills/cells` → `skill/`).
It is **not** part of the end-user install-skill package.

## Build the CLI from source

```bash
# From the repository root
bazel run :cli
# Binary is written to dist/cli/cells — put it on PATH or invoke directly:
./dist/cli/cells --help
./dist/cli/cells sync 'http://localhost:8081/?room=ROOM_ID'
```

Release install (`install.sh` / Homebrew) is for end users without a checkout.
Here, prefer the in-repo build so agents match local engine changes.

## Local human + agent collab demo

1. Terminal A — web + collab server:
   ```bash
   bazel run :wasm
   bazel run :serve
   # open http://localhost:8081/
   ```
2. In the browser: Collaborate → Copy Link (room URL).
3. Terminal B — agent with CLI + this skill:
   ```bash
   bazel run :cli -- sync 'http://localhost:8081/?room=ROOM_ID'
   # or use ./dist/cli/cells after bazel run :cli
   ```

The human edits in the web UI; the agent uses the CLI against the same room.
