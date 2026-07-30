---
name: cells
description: Cells is a CLI for spreadsheet work (.xlsx/.csv/.zcd) — an Excel CLI equivalent for converting, inspecting, creating, and transforming workbooks from the terminal. Also used to join collab rooms via cells sync.
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

## Collaborate via room URL

When a human (or peer) shares a Cells collab link that includes a **room id** (`?room=...` or path `/{room-id}`), join with the CLI:

```bash
cells sync 'https://example.com/?room=ROOM_ID'
# or
cells sync --server 'https://example.com/?room=ROOM_ID'
```

That starts CRDT sync so the agent and human edit the same workbook. Prefer `cells --help` for full sync flags (`--apply`, `--send`, `--ops-only`).

Give the agent the full URL from the web **Collaborate** menu (Copy Link).

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

Scripts run with `--script <file>` or `-e '<code>'`. They use the Cells scripting API (`getCell`, `setCell`, sheets, ranges, and more).

- **API reference:** [SCRIPTING.md](SCRIPTING.md) — load this before inventing API names
- **Sample scripts:** [samples/](samples/)

```bash
cells -i data.xlsx out.xlsx --script samples/set-values.luau
cells -i data.csv -e 'local c = getCell("A1"); print(c and c.value)'
```
