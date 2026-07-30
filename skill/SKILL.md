---
name: cells
description: Cells is a CLI for spreadsheet work (.xlsx/.csv/.zcd) — an Excel CLI equivalent for converting, inspecting, creating, and transforming workbooks from the terminal. Also used to join collab rooms via long-running sessions (or one-shot cells sync).
---

# Cells spreadsheet CLI

`cells` is a spreadsheet engine CLI for `.xlsx`, `.csv`, and `.zcd`.

Humans often use the web client; agents should use this CLI (and this skill). The product does not host agent intelligence — use Codex, Claude Code, Grok, or any other agent with the CLI.

## Check availability

```bash
which cells
```

If `cells` is missing and this skill folder contains `install.sh`, install the CLI with it. The installer tries **Homebrew first**, then the **direct release installer**:

```bash
sh ./install.sh
```

If this skill directory also contains `REPO_LOCAL.md` (present when working from the cells git checkout via skill symlinks), read that file for how to build and use the in-repo CLI instead of downloading a release.

### Install environment overrides

| Variable | Purpose |
|----------|---------|
| `CELLS_VERSION` | Install a specific tag (`v0.0.1`) instead of latest |
| `CELLS_INSTALL_DIR` | Install directory (default `/usr/local/bin`) |
| `CELLS_REPO` | GitHub `owner/repo` for release assets |
| `CELLS_FORCE_INSTALL` | Set `1` to reinstall even if `cells` is on PATH |

## Collaborate via room URL (preferred: sessions)

When a human (or peer) shares a Cells collab link that includes a **room id** (`?room=...` or path `/{room-id}`), agents should join with a **long-running session**. The CLI peer stays connected so the human sees it in the browser Collaborate UI while you run multiple scripts/actions.

### Multi-step agent workflow (session daemon)

```bash
# 1) Start a background peer (prints JSON with session id)
cells session start 'https://example.com/?room=ROOM_ID' --idle-minutes 30 --name 'CLI Agent'
# → {"id":"a1b2c3d4","url":"...","room":"ROOM_ID",...}

# 2) Run scripts/actions against the live session (no reconnect each time)
cells session exec a1b2c3d4 -e 'setCell("A1", 42); print(getCell("A1").value)'
cells session exec a1b2c3d4 --script transform.luau

# 3) Optional: stream room/session events (ops, peers, state)
cells session watch a1b2c3d4 --duration 30

# 4) Inspect / list / stop
cells session status a1b2c3d4
cells session list
cells session stop a1b2c3d4
```

| Command | Purpose |
|---------|---------|
| `session start <url>` | Fork a daemon, join the room, print `{"id",...}` |
| `session exec <id> -e` / `--script` | Run Luau on the live workbook and broadcast ops |
| `session watch <id>` | Stream JSON events (`--duration SECS` to auto-exit) |
| `session list` | Active sessions (JSON array) |
| `session status <id>` | Connection state, peers, idle settings |
| `session stop <id>` | Stop daemon and leave the room |

**Idle timeout:** sessions auto-stop after N minutes with no action (default **30**). Override with `--idle-minutes N` (fractions allowed, e.g. `0.05` ≈ 3s for tests).

**Why sessions?** `cells sync <url>` is a one-shot **blocking** listener (Ctrl+C to exit). For multi-command agent work, **always prefer `session start` + `session exec`** so the peer remains connected between commands.

Give the agent the full URL from the web **Collaborate** menu (Copy Link).

### One-shot sync (optional)

```bash
cells sync 'https://example.com/?room=ROOM_ID'
# or
cells sync --server 'https://example.com/?room=ROOM_ID'
```

Use only when you intentionally want a single blocking process that logs ops until exit. Prefer `cells --help` for full sync flags (`--apply`, `--send`, `--ops-only`).

## How to use

Prefer `cells --help` for full flags and modes. Common workflows:

```bash
# Help / version
cells --help
cells --version

# Convert (format from extension)
cells -i data.xlsx output.zcd
cells -i budget.xlsx report.csv --eval
cells -i data.csv report.xlsx

# Inspect
cells -I spreadsheet.zcd

# Create empty workbook
cells output.zcd

# Script while converting (or script-only with no output path)
cells -i data.xlsx output.csv --script transform.luau
cells -i data.csv out.xlsx -e 'setCell("A1", 100)'
cells -i report.csv -e 'print(getCell("A1").value)'
```

### Large CLI output

If a command prints a large payload (script output above a size threshold), the CLI writes the full body under `/tmp` and prints a small JSON pointer on stdout instead of dumping megabytes into the agent context:

```json
{"path":"/tmp/cells-out-XXXXXX","bytes":123456,"preview":"..."}
```

Open `path` to read the full output. Small payloads still print inline.

### Formats

| Format | Use when |
|--------|----------|
| `.zcd` | Full fidelity (formulas, multi-sheet, styles) |
| `.xlsx` | Excel interchange |
| `.csv` / `.tsv` | Simple tabular import/export (values; limited fidelity) |

## Scripting

Scripts run with `--script <file>` or `-e '<code>'` (offline convert path), or with `session exec <id> -e` / `--script` (live collab session). They use the Cells scripting API (`getCell`, `setCell`, sheets, ranges, and more).

- **API reference:** [SCRIPTING.md](SCRIPTING.md) — load this before inventing API names
- **Sample scripts:** [samples/](samples/)

```bash
cells -i data.xlsx out.xlsx --script samples/set-values.luau
cells -i data.csv -e 'local c = getCell("A1"); print(c and c.value)'
# Live session (peer stays connected):
cells session exec SESSION_ID -e 'setCell("A1", 100)'
```
