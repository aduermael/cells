# Cells

Cells is a modern spreadsheet engine, built with agentic use primarily in mind. 

It’s lightweight (~XMB), fully scriptable ([Luau](https://luau.org) native runtime with Python, VBA and Typescript planned), and uses its own text-based spreadsheet file format (CRDT structured for collaboration + diffs cleanly for versioning).

Use it as a **browser-based alternative to Excel/Google Sheets**, or as a **lightweight headless CLI** for your agents. 

You can test it on the [presentation web page](https://aduermael.github.io/cells/). 

*NOTE: it’s early and not yet a drop-in replacement for Excel. (about 120 formula supported over almost 500 in Excel, charts not yet supported also)*

## Made for AI agents to work with, not an agent itself

Cells defines no harness, no system prompts. The CLI is designed for agents through, delivering context-window-conscious structured outputs and using code as the main request interface. 

Here’s a quick demo where you can see humans (web clients) collaborating with Grok Build and Codex agents (using the CLI):

<img style="max-width:500px" src="./docs/img/demo.gif">

## Scriptable

Cells is built from the start as fully scriptable Lua/Luau runtime. 
(Python, VBA and Typescript not yet supported but planned, they will under the hood be transpired *into Luau)*

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

The skill gets installed for Claude Code, Codex and Grok Build in your working directory. After this, just prompt for spreadsheet work like `“Open file.xlsx and fix formulas”`.

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

## Architecture 🏛️

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

The engine compiles to WebAssembly for the browser and native code for the CLI. All mutations go through CRDT operations, making collaboration a first-class feature rather than an afterthought.

### Collaboration & ZCD Format 📄

Cells uses peer-to-peer sync via WebRTC. Document data travels directly between clients with no relay servers. Concurrent edits merge automatically using CRDTs.

The native `.zcd` format (Zero Conflict Document) is designed for this:

- **One entity per line** - Editing a cell changes exactly one line, minimal diffs
- **Content-addressed styles** - Styles/formats stored as base64 hashes, auto-deduplicated
- **CRDT operation log** - Full edit history for sync and conflict resolution

```
D yRUosCbW "Untitled"
S FD3KLIgo "Sheet1"
C C4Jr2s32 1
C pYYl3eZ1 2
R Zv6vRn4q 1
X XO5lD1Nh C4Jr2s32 Zv6vRn4q n 42 fmt:DwICAQEk
X 1MvyBXyr pYYl3eZ1 Zv6vRn4q f "=~~XO5lD1Nh*10" fmt:DwICAQEk sty:BAAB
```
<sub>

```
#oplog
O 1769998913268.0.atyBEwwf CELL_SET XO5lD1Nh {"t":"n","v":"42","col":"C4Jr2s32","row":"Zv6vRn4q"}
O 1769998921630.0.atyBEwwf CELL_SET 1MvyBXyr {"t":"f","v":"=~~XO5lD1Nh*10","col":"pYYl3eZ1","row":"Zv6vRn4q"}
```

</sub>

See [docs/persistence.md](./docs/persistence.md) for the full specification.

For detailed architecture documentation, see [docs/](./docs/).

## AI Agents 🤖

Cells does **not** host an in-product AI agent. Humans use the web client; agents use the **CLI** (and the agent skill) like any other tool—Codex, Claude Code, Grok, or anything else.

**Collaborate with an agent:** open the Collaborate menu in the web UI, copy the room link, and give it to an agent. The agent starts a long-running **session** so the CLI peer stays connected while it runs multiple scripts:

```bash
cells session start '<room-url>' --name 'CLI Agent'
cells session exec SESSION_ID -e 'setCell("A1", 42)'
cells session watch SESSION_ID --duration 30
cells session stop SESSION_ID
```

Sessions auto-stop after idle minutes (default 30; `--idle-minutes N`). For a one-shot blocking listener, `cells sync '<room-url>'` still works.

Install the skill (CLI + docs) with `./install-skill.sh` or see [skill/SKILL.md](./skill/SKILL.md). In this repository, skills are also symlinked for local agent discovery under `.agents/skills/cells`, `.claude/skills/cells`, and `.grok/skills/cells`.

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

## Deploy 🚀

Production deploys are **manual** GitHub Actions workflows (no auto-deploy on push):

| Workflow | What it deploys |
|----------|-----------------|
| **Deploy server (Fly.io)** | Collaboration server + WASM app → [cells-app.fly.dev](https://cells-app.fly.dev/) |
| **Deploy website (GitHub Pages)** | Minimal landing page (iframe demo + links) → GitHub Pages |
| **Deploy all** | Both of the above (reuses the same workflows) |

### GitHub `production` environment secrets

| Secret | Where | Required | Purpose |
|--------|-------|----------|---------|
| `FLY_API_TOKEN` | GitHub Environment **production** | Yes (server) | `flyctl deploy` |

### Fly app secrets (not GitHub)

Set with `fly secrets set` on the `cells-app` app:

| Secret | Required | Purpose |
|--------|----------|---------|
| *(none required for agent features)* | — | Agents use the CLI; the server only needs collab for multi-peer rooms |

### One-time GitHub Pages setting

**Settings → Pages → Build and deployment → Source: GitHub Actions**

Local preview of the landing page:

```bash
./tools/prepare-pages.sh
# open dist/pages/index.html (iframe still loads the live Fly app)
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
