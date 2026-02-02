# Cells

A modern spreadsheet engine—use it as a **browser-based alternative to Excel/Google Sheets**, or as a **lightweight headless CLI** for automation and scripting.

**[Try the Live Demo](https://cells-app.fly.dev/)** | [Documentation](./docs/)

## What is Cells?

Cells is a spreadsheet engine that works two ways:

1. **Web App** — A full spreadsheet UI in your browser. Real-time collaboration, Excel import/export.
2. **Headless CLI** — Run spreadsheet operations from the command line. Convert formats, batch process files, integrate into pipelines.

Both share the same core engine, so formulas and files work identically across environments.

**Key features:**
- **Real-time collaboration** - Edit together with P2P sync (no server required for document data)
- **Excel compatible** - Import and export .xlsx files seamlessly
- **Fully scriptable** - Automate with [Luau](https://luau.org) scripts (Python, VBA, and TypeScript support planned)
- **Git-friendly** - Text-based file format that diffs cleanly

## Quick Start

### Web UI

```bash
# Build and serve locally
bazel run :wasm-dist
bazel run :serve
# Open http://localhost:8081
```

### CLI Tool

```bash
# Build the CLI
bazel run :cli

# Convert Excel to Cells format
./dist/cli/cells convert spreadsheet.xlsx output.zcd

# Inspect a file
./dist/cli/cells info spreadsheet.zcd
```

## Features

### Spreadsheet Engine
- Formula engine with 80+ Excel-compatible functions
- Dependency graph for reactive updates
- Multi-sheet support
- Number formatting with Excel format codes

### Collaboration
- Peer-to-peer sync via WebRTC
- CRDT-based conflict resolution (concurrent edits just work)
- Real-time cursor and selection sharing
- No relay servers - document data stays between peers

### Web UI
- Canvas-based rendering with virtual scrolling
- Inline cell editing and formula bar
- Column/row resizing and drag-to-reorder
- Keyboard navigation (arrow keys, Tab, Enter)
- Drag-and-drop XLSX import

### CLI Tools
- Format conversion: `xlsx <-> zcd <-> csv`
- File inspection and validation
- Headless spreadsheet operations

### Scripting
- **[Luau](https://luau.org)** - Full scripting support with access to the cell engine API
- Automate data transformations, generate reports, run calculations
- **Coming soon:** Python, VBA, and TypeScript bindings

## Architecture

```
┌─────────────────────────────────────────────────────┐
│              Web UI (TypeScript)                    │
│         Canvas rendering, event handling           │
└─────────────────────────────────────────────────────┘
                        │ WASM
                        ▼
┌─────────────────────────────────────────────────────┐
│             Core Engine (C++17)                     │
│    Data Model  │  Formulas  │  CRDT Operations     │
└─────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│              Persistence Layer                      │
│   Native: .zcd  │  Import/Export: .xlsx, .csv      │
└─────────────────────────────────────────────────────┘
```

The engine compiles to WebAssembly for the browser and native code for the CLI. All mutations go through CRDT operations, making collaboration a first-class feature rather than an afterthought.

### ZCD File Format

Cells uses its own text-based format (`.zcd`) designed for Git-friendly persistence and CRDT collaboration:

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

## Requirements

- **Bazel** 7.0+
- **C++17** compiler (Clang, GCC, or MSVC)
- **Go 1.22+** (for development server; macOS 15+ requires 1.22)

## Build Commands

```bash
# Build
bazel run :cli              # CLI tool → dist/cli/cells
bazel run :wasm-dist        # Web app → dist/wasm/

# Test
bazel run :test             # Unit tests
bazel run :e2e              # E2E tests (headless)
bazel run :e2e-headed       # E2E tests (visible browser)

# Code quality
bazel run :lint             # Run linter
bazel run :format           # Format code
bazel run :check            # All checks (test + lint + types)
```

## Documentation

| Document | Description |
|----------|-------------|
| [Data Model](./docs/data-model.md) | Cell addressing, spatial indexing |
| [CRDT](./docs/crdt.md) | Collaboration operations and sync |
| [Formula Engine](./docs/formula-engine.md) | Parser, evaluation, dependencies |
| [Persistence](./docs/persistence.md) | File formats (.zcd, .xlsx) |
| [Networking](./docs/networking.md) | P2P connections, signaling |
| [Rendering](./docs/rendering.md) | Canvas rendering pipeline |
| [Type System](./docs/type-system.md) | Optional column typing |

## Project Stats

| Category | Count |
|----------|------:|
| C++ (core) | 45,004 lines |
| TypeScript (UI) | 23,659 lines |
| Unit tests | 3,481 |
| E2E tests | 384 |
| Total tests | 3,908 |
| Commits | 1,521 |

<details>
<summary>Detailed stats</summary>

### Source Code

| Language | Lines |
|----------|------:|
| C++ | 45,004 |
| TypeScript | 23,659 |
| CSS | 2,706 |
| Starlark | 1,947 |
| JavaScript | 1,680 |
| Go | 1,363 |
| Shell | 1,168 |
| HTML | 1,051 |
| Objective-C++ | 1,007 |

### Test Code

| Language | Lines |
|----------|------:|
| C++ | 42,844 |
| JavaScript | 13,606 |
| Go | 315 |

### Bundle Size

- WASM Module: 5.14 MB
- Total Web Bundle: 7.23 MB

<sub>Lines counted with [CLOC](https://github.com/AlDanial/cloc). Generate with `./tools/generate-stats.sh`</sub>

### LOC Evolution

<img src="stats/loc-evolution.svg" alt="Lines of Code Evolution" width="100%">

### Diff Size Evolution

<img src="stats/diff-size-evolution.svg" alt="Diff Size Evolution" width="100%">

</details>

## License

[License information here]
