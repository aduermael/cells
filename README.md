# Cells

Cells is a modern spreadsheet engine, built with agentic use primarily in mind. 

It’s lightweight (<10MB), fully scriptable ([Luau](https://luau.org) native runtime with Python, VBA and TypeScript planned), and uses its own text-based spreadsheet file format (CRDT structured for collaboration + diffs cleanly for versioning).

Use it as a **browser-based alternative to Excel/Google Sheets**, or as a **lightweight headless CLI** for your agents. 

You can test it on the [presentation web page](https://aduermael.github.io/cells/). 

*NOTE: it’s early and not yet a drop-in replacement for Excel. (about 120 formulas supported over almost 500 in Excel, charts not yet supported also)*

## Made for AI agents to work with, not an agent itself

Cells defines no harness, no system prompts. The CLI is designed for agents though, delivering context-window-conscious structured outputs and using code as the main request interface. 

Here’s a quick demo where you can see humans (web clients) collaborating with Grok Build (using the CLI):

<img style="max-width:500px" src="./docs/img/demo.gif">

## Scriptable

Cells is built from the start as a fully scriptable Lua/Luau runtime. 
(Python, VBA and TypeScript not yet supported but planned; they will under the hood be transpiled *into Luau*)

<img style="max-width:400px" src="./docs/img/scripting.png">

```bash
# Run inline script
cells -i data.csv output.xlsx -e 'setCell("A1", 100)'

# Run a script file
cells -i data.xlsx output.csv --script transform.luau

# Script-only mode (no output file)
cells -i report.xlsx --script analyze.luau
cells -i data.csv -e 'print(getCell("A1"))'
```

## Install

Install methods for the **CLI**, in order of preference:

### 1. Agent skill (recommended)

Best default for agentic workflows. Installs a skill that knows how to install
and use `cells`:

```bash
curl -fsSL https://raw.githubusercontent.com/aduermael/cells/main/install-skill.sh | sh
```

The skill gets installed for Claude Code, Codex and Grok Build in your working directory. After this, just prompt for spreadsheet work like `"Open file.xlsx and fix formulas"`.

### 2. Homebrew

```bash
brew install aduermael/tap/cells
```

### 3. Direct install (`curl | sh`)

Downloads the platform binary from the latest GitHub Release:

```bash
curl -fsSL https://raw.githubusercontent.com/aduermael/cells/main/install.sh | sh
```

### 4. Build from source

```bash
bazel run :cli          # → dist/cli/cells
bazel run :cli-release  # optimized
```

### Optional: run the collaboration server

Not required for CLI conversion/scripting. Use this only if you want to host
the web UI + signaling server yourself.

| Method | Command |
|--------|---------|
| **Docker** | `docker build -t cells-server . && docker run -p 8080:8080 cells-server` |
| **Local dev** | `bazel run :wasm-dist && bazel run :serve` → http://localhost:8081 |

Point the CLI at any server URL for collab (prefer **session** for multi-step agent work):

```bash
cells session start 'https://cells-app.fly.dev/?room=YOUR_ROOM'
cells session exec SESSION_ID -e 'setCell("A1", 1)'
# one-shot blocking listen (optional):
cells sync 'https://your-host/?room=YOUR_ROOM'
```

## Quick Start

### Web UI

The simplest is to test the most recent release here: https://cells-app.fly.dev

To build and run locally: 

```bash
# Build and serve locally
bazel run :wasm-dist
bazel run :serve
# Open http://localhost:8081
```

### CLI Tool

```bash
# After install, or: bazel run :cli → ./dist/cli/cells
cells -i spreadsheet.xlsx output.zcd
cells -I spreadsheet.zcd
```

#### Format Conversion

```bash
# Excel to native format
cells -i data.xlsx output.zcd

# CSV to Excel
cells -i report.csv report.xlsx

# Excel to CSV
cells -i budget.xlsx budget.csv
```

#### Formula Evaluation

Use `--eval` to recalculate formulas using the Cells calculation engine before export:

```bash
# Evaluate formulas when exporting to CSV
cells -i budget.xlsx budget.csv --eval

# Useful when XLSX cached values are stale or missing
cells -i calculations.xlsx results.csv --eval
```

#### Empty Workbook Creation

Create new workbooks from scratch, optionally with scripted content:

```bash
# Create empty workbook
cells output.zcd
cells output.xlsx

# Create with initial content via script
cells output.xlsx -e 'setCell("A1", "Hello") setCell("A2", "=A1 & \" World\")'
```

## Architecture

Same engine, two shapes: **WebAssembly** in the browser, **native** for the CLI. The web UI stays intentionally thin (canvas + events in TypeScript). Spreadsheet logic (model, formulas, CRDT ops) lives in C++17 and is shared.

```
┌─────────────────────────────────────────────────────┐
│              Web UI (TypeScript)                    │
│         Canvas rendering, event handling            │
└─────────────────────────────────────────────────────┘
                        │ WASM
                        ▼
┌─────────────────────────────────────────────────────┐
│             Core Engine (C++17)                     │
│    Data Model  │  Formulas  │  CRDT Operations      │
└─────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│              Persistence Layer                      │
│   Native: .zcd  │  Import/Export: .xlsx, .csv       │
└─────────────────────────────────────────────────────┘
```

### Collaboration & `.zcd`

Realtime sync is **WebRTC peer-to-peer**. A small signaling server only helps clients find each other (offers, answers, ICE). Once a DataChannel is up, document ops and presence travel between peers, not through a central document store. Concurrent edits merge automatically with CRDTs.

The native format is **`.zcd`** (Zero Conflict Document). It is plain text on purpose, built around collab and clean git diffs:

- **One entity per line:** edit a cell, column, or row → exactly one line changes
- **Content-addressed styles/formats:** same style encodes to the same base64 blob (auto-deduped; the content *is* the identity)
- **CRDT operation log:** full edit history for sync and conflict resolution

A tiny workbook (value `42` in one cell, formula `=A1*10` next to it) looks roughly like this:

```
D yRUosCbW "Untitled"
S FD3KLIgo "Sheet1"
C C4Jr2s32 0
C pYYl3eZ1 1
R Zv6vRn4q 0
X XO5lD1Nh C4Jr2s32 Zv6vRn4q n 42 fmt:DwICAQEk
X 1MvyBXyr pYYl3eZ1 Zv6vRn4q f "=~~XO5lD1Nh*10" fmt:DwICAQEk sty:BAAB
```

Formulas store stable cell IDs (`~~XO5lD1Nh`), not A1 coordinates, so inserts and renames do not break references. Below the snapshot, the op log records how the doc got there:

```
#oplog
O 1769998913268.0.atyBEwwf CELL_SET XO5lD1Nh {"t":"n","v":"42","col":"C4Jr2s32","row":"Zv6vRn4q"}
O 1769998921630.0.atyBEwwf CELL_SET 1MvyBXyr {"t":"f","v":"=~~XO5lD1Nh*10","col":"pYYl3eZ1","row":"Zv6vRn4q"}
```

Full format spec: [docs/persistence.md](./docs/persistence.md). Deeper architecture notes live under [docs/](./docs/).

## Requirements 📋

- **Bazel** 7.0+
- **C++17** compiler (Clang, GCC, or MSVC)
- **Go 1.22+** (for development server; macOS 15+ requires 1.22)

## Build Commands ⚙️

```bash
# Build
bazel run :cli              # CLI tool → dist/cli/cells
bazel run :wasm-dist        # Web app → dist/wasm/

# Build Linux CLI binaries (requires Docker)
./tools/cli-alpine.sh       # Alpine/musl (converter only) → dist/cli/cells-alpine
./scripts/linux-build.sh    # Debian/glibc (full CLI with sync) → dist/cli/cells-linux

# Test
bazel run :test             # Unit tests
bazel run :e2e              # E2E tests (headless)
bazel run :e2e-headed       # E2E tests (visible browser)

# Code quality
bazel run :lint             # Run linter
bazel run :format           # Format code
bazel run :check            # All checks (test + lint + types)

# Landing site (GitHub Pages artifact)
./tools/prepare-pages.sh    # → dist/pages/
```

## Documentation 📚

| Document | Description |
|----------|-------------|
| [Data Model](./docs/data-model.md) | Cell addressing, spatial indexing |
| [CRDT](./docs/crdt.md) | Collaboration operations and sync |
| [Formula Engine](./docs/formula-engine.md) | Parser, evaluation, dependencies |
| [Excel Parity](./docs/excel-parity.md) | Formulas, features, and XLSX fidelity tracker |
| [Persistence](./docs/persistence.md) | File formats (.zcd, .xlsx) |
| [Networking](./docs/networking.md) | P2P connections, signaling |
| [Rendering](./docs/rendering.md) | Canvas rendering pipeline |
| [Type System](./docs/type-system.md) | Optional column typing |
| [Project Stats](./docs/project-stats.md) | LOC, tests, build sizes, evolution graphs |

## License

Cells is licensed under the [MIT License](./LICENSE).
