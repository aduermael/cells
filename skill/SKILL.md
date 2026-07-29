---
name: cells
description: Use the Cells CLI for spreadsheet conversion (.xlsx/.csv/.zcd), Luau scripting, headless automation, and real-time collaboration sync against a server URL.
---

# Cells spreadsheet CLI

## Principles

- Prefer the `cells` CLI for spreadsheet conversion, inspection, scripting, and collaboration sync.
- Install the CLI if it is missing (see below) before inventing ad-hoc converters.
- Use the native `.zcd` format when round-tripping full fidelity; use `.xlsx` / `.csv` for interchange.
- For collaboration, point the CLI at a server URL with `cells sync` (or `--server`).

## CLI availability

Use `cells` from `PATH` when available.

If `cells` is not available and this skill folder contains `install.sh`, run that
script to install the CLI. The installer tries **Homebrew first**, then the
**direct release installer** (no npm):

```bash
sh ./install.sh
```

In this repository, build a local binary if needed:

```bash
bazel run :cli
# → dist/cli/cells
```

Release builds:

```bash
bazel run :cli-release
# → dist/cli/cells (optimized)
```

Then run `./dist/cli/cells` directly.

### Environment overrides (install)

| Variable | Purpose |
|----------|---------|
| `CELLS_VERSION` | Install a specific tag (`v0.0.1`) instead of latest |
| `CELLS_INSTALL_DIR` | Install directory (default `/usr/local/bin`) |
| `CELLS_REPO` | GitHub `owner/repo` for release assets |
| `CELLS_FORCE_INSTALL` | Set `1` to reinstall even if `cells` is on PATH |

## Common commands

```bash
# Version / help
cells --version
cells --help

# Convert formats (auto-detect by extension)
cells -i data.xlsx output.zcd
cells -i budget.xlsx report.csv --eval
cells -i data.csv report.xlsx

# Inspect
cells -I spreadsheet.zcd

# Luau scripting
cells -i data.xlsx output.csv --script transform.luau
cells -i data.csv out.xlsx -e 'setCell("A1", 100)'

# Create empty workbook
cells output.zcd
```

## Collaboration (server URL)

Join a live room using a full app URL (copy from the browser address bar), or
pass the server URL with `--server`:

```bash
# Positional URL
cells sync 'https://cells-app.fly.dev/?room=abc123'

# Explicit --server flag (same meaning)
cells sync --server 'https://cells-app.fly.dev/?room=abc123'

# Apply remote ops into a local workbook
cells sync --server 'https://example.com/?room=abc' --apply workbook.zcd

# Broadcast local workbook as operations
cells sync --server 'https://example.com/?room=abc' --send workbook.zcd
```

Default public demo server: `https://cells-app.fly.dev/`. Self-host options are
documented in the repository README (Docker / `tools/serve`).

## When to use which format

| Format | Use when |
|--------|----------|
| `.zcd` | Full fidelity, git-friendly, collaboration ops |
| `.xlsx` | Excel interchange |
| `.csv` / `.tsv` | Simple tabular export/import (values; limited fidelity) |
