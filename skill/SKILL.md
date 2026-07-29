---
name: cells
description: Cells is a CLI for spreadsheet work (.xlsx/.csv/.zcd) — an Excel CLI equivalent for converting, inspecting, creating, and transforming workbooks from the terminal.
---

# Cells spreadsheet CLI

`cells` is a spreadsheet engine CLI for `.xlsx`, `.csv`, and `.zcd`.

## Check availability

```bash
which cells
```

If `cells` is missing and this skill folder contains `install.sh`, install the CLI with it. The installer tries **Homebrew first**, then the **direct release installer**:

```bash
sh ./install.sh
```

### Install environment overrides

| Variable | Purpose |
|----------|---------|
| `CELLS_VERSION` | Install a specific tag (`v0.0.1`) instead of latest |
| `CELLS_INSTALL_DIR` | Install directory (default `/usr/local/bin`) |
| `CELLS_REPO` | GitHub `owner/repo` for release assets |
| `CELLS_FORCE_INSTALL` | Set `1` to reinstall even if `cells` is on PATH |

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
