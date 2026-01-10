# Cells - A Modern Spreadsheet Engine

## Vision
A high-performance, collaborative spreadsheet engine with:
- Git-friendly persistence
- Real-time collaboration via CRDT
- Multi-dimensional data model
- Cross-platform deployment (native + web)

## Project Stats

### Source Code

| Language | Lines |
|----------|------:|
| C++ | 32,705 |
| TypeScript | 19,043 |
| Markdown | 13,071 |
| CSS | 2,445 |
| Starlark | 1,492 |
| Go | 1,363 |
| JavaScript | 1,173 |
| Objective-C++ | 1,007 |
| HTML | 854 |
| Shell | 754 |

### Test Code

| Language | Lines |
|----------|------:|
| C++ | 23,851 |
| JavaScript | 4,631 |
| Go | 315 |

### Test Counts

| Category | Tests |
|----------|------:|
| Unit (C++) | 2249 |
| Unit (Go) | 13 |
| Unit (JavaScript) | 30 |
| E2E (Puppeteer) | 153 |
| **Total** | **2445** |

- **Commits**: 915
- **WASM Module**: 4.70 MB
- **Total Web Bundle**: 6.39 MB

<sub>Lines counted with [CLOC](https://github.com/AlDanial/cloc) (excludes comments and blanks). Generated with `./scripts/generate-stats.sh`</sub>
## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    UI Layer (TypeScript)                         │
│    Canvas2D rendering, event handling, minimal display cache     │
└─────────────────────────────────────────────────────────────────┘
                              │ WASM Bridge
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                Order Statistic Tree (C++17)                      │
│   O(log n) spatial indexing: pixel coords ↔ UUID cells/axes     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Core Engine (C++17)                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Data Model  │  │ Formula     │  │ CRDT Operations         │  │
│  │ (UUID-based │◄─┤ Engine      │  │ (All mutations)         │  │
│  │ cells/axes) │  │ (Native AST)│  │                         │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Persistence Layer                               │
│         Git-friendly text format / Binary for performance        │
└─────────────────────────────────────────────────────────────────┘
```

**Key Architectural Principles:**
- **UUID Source of Truth**: All cells, columns, rows identified by UUIDs, not coordinates
- **CRDT-First**: ALL workbook mutations go through CRDT operations (enables collaboration by design)
- **OSTree Bridge**: Order Statistic Tree translates "pixels 500-1000" ↔ "which UUID columns?"
- **Web-Only UI**: Engine compiles to WASM, UI is TypeScript+Canvas2D (native builds for CLI only)

## Core Components

### 1. [Data Model](./docs/data-model.md)

- **UUID-based identification** - cells, columns, and rows use UUIDs (source of truth)
- The 2D grid view is derived from sparse UUID representation via Order Statistic Tree
- N-dimensional sparse structure with doubly-linked dimension chains

### 2. [Order Statistic Tree](./docs/data-model.md#order-statistic-tree)

- O(log n) spatial indexing between UI pixel coordinates and sparse UUID representation
- Translates "show me pixels 500-1000" ↔ "which columns/rows are there?"
- Maintained incrementally as columns/rows are inserted, deleted, or resized

### 3. [CRDT Operations](./docs/crdt.md)

- **All mutations go through CRDT ops** - enforces collaboration-native design
- Operation-based CRDT with HLC ordering, LWW conflict resolution
- Cell operations: set value, set formula, set format, clear
- Axis operations: insert, delete, move, resize columns/rows

### 4. [Formula Engine](./docs/formula-engine.md)

- Excel formula parser → AST → native evaluation
- Dependency graph for reactive updates
- Reference adjustment during copy/paste (AST-based, not string manipulation)

### 5. [Type System](./docs/type-system.md)

- **Completely optional** - works exactly like Excel by default
- Column typing as gradual discovery (not enforced like AirTable)
- Features Excel can't represent: relations, select options, validation
- Always exportable to XLSX (with warnings for feature loss)

### 6. [Persistence & File Format](./docs/persistence.md)

- Git-diff-friendly text format (.zcd)
- Binary format for large files
- Import/export (xlsx, csv)

### 7. [Rendering](./docs/rendering.md)

- Canvas2D rendering in TypeScript (web only)
- Virtual viewport with aggressive culling
- Dirty region tracking for efficient redraws

### 8. [Networking & Collaboration](./docs/networking.md)

- WebRTC peer-to-peer connections (no relay servers)
- Lightweight signaling for connection setup
- Real-time presence/cursor sharing

## Directory Structure

```
cells/
├── WORKSPACE               # Bazel workspace root
├── MODULE.bazel            # Bzlmod module definition
├── Makefile                # Development commands
├── core/                   # C++17 core engine
│   ├── BUILD
│   └── cells/              # Main library
│       ├── BUILD
│       ├── *.h             # Headers
│       ├── *.cc            # Implementation
│       └── *_test.cc       # Tests (colocated)
├── testdata/               # Sample .zcd and .xlsx files
├── apps/                   # Applications
│   ├── cli/                # Command-line tool
│   │   ├── BUILD
│   │   └── main.cc         # CLI entry point
│   └── wasm/               # WebAssembly build
│       ├── BUILD
│       ├── bindings.cc     # Embind bindings
│       ├── src/            # TypeScript source (client, worker, grid-renderer)
│       ├── cells.d.ts      # TypeScript definitions for WASM API
│       └── static/         # Web UI (index.html, CSS)
├── docs/                   # Architecture docs
├── plans/                  # Implementation plans
├── scripts/                # Build and dev scripts
└── dist/                   # Built WASM distribution (generated)
```

## Key Design Decisions

### Why UUID-based cells instead of (row, col)?

1. **CRDT-friendly**: No coordinate conflicts during concurrent edits
2. **Stable references**: Moving cells doesn't break formulas
3. **Sparse by nature**: Only allocated cells consume memory
4. **Multi-dimensional**: Generalizes beyond 2D trivially

### Why native AST execution?

1. **Simpler architecture**: No codegen step, no runtime embedding
2. **Better performance**: No interpreter overhead, direct function calls
3. **Easier debugging**: Stack traces are native, not VM traces
4. **Smaller binary**: No embedded runtime (~500KB+ for scripting VMs)
5. **Full control**: Custom memory management, precise error handling

### Why doubly-linked dimensions with gaps?

1. **O(1) insert/delete**: No array shifting
2. **Sparse-friendly**: Gap encoding avoids empty nodes
3. **CRDT-compatible**: Each link is independently addressable
4. **Flexible iteration**: Forward/backward traversal

## Requirements

- **Bazel** 7.0+ (build system)
- **C++17** compatible compiler (Clang, GCC, or MSVC)
- **Go 1.22+** (for WASM development server)
  - **Important**: macOS 15+ (Sequoia) requires Go 1.22 or newer
  - Older Go versions will fail with `dyld: missing LC_UUID load command` error
  - Install/upgrade: `brew install go`
- **Python 3** (optional, for simple HTTP server)

## Build System

**Bazel** - Fast incremental builds, hermetic, scales well.

```bash
# Build core library
make build          # or: bazel build //core/...

# Run tests
make test           # or: bazel test //core/...

# Run e2e tests (requires dist/ from wasm-dist)
cd apps/wasm && npm test

# Build CLI (development)
make cli            # Creates ./cells binary

# Build CLI (optimized)
make release        # Creates optimized ./cells binary
```

### WebAssembly Build

The cells engine compiles to WebAssembly for a fully functional in-browser spreadsheet:

```bash
# Build WASM module (development)
make wasm

# Build distribution package (optimized)
make wasm-dist

# Output: dist/ folder ready for deployment
```

The distribution package includes:
- `cells_wasm_bin.wasm` - WASM binary (~1.04MB)
- `cells_wasm_bin.js` - Emscripten JS glue
- `cells.d.ts` - TypeScript definitions for WASM API
- `index.html` - Full spreadsheet UI
- `worker.js` - Web Worker for async WASM operations (bundled from TypeScript)
- `client.js` - Main thread API with GridRenderer, CollabUI, etc. (bundled from TypeScript)
- `shared/` - CSS styles

**Web UI Features:**
- Canvas2D grid rendering with virtual scrolling
- Cell editing (inline and formula bar)
- Selection with keyboard navigation (arrow keys, Tab, Enter)
- Column/row resizing and reordering
- Multi-sheet support with tab bar
- XLSX file import (drag-and-drop)
- Listener-driven UI refresh (WASM notifies JS of changes)

**Test locally:**
```bash
make wasm-dist
python3 -m http.server 8080 --directory dist
# Open http://localhost:8080/
```

**Run e2e tests:**

The web app includes e2e tests using Chrome headless via Puppeteer:

```bash
# Build distribution first
make wasm-dist

# Run tests in parallel (recommended - much faster)
cd apps/wasm && npm run test:parallel              # All tests
npm run test:parallel -- stable                    # Stable tests only
npm run test:parallel -- collab                    # Collaboration tests
npm run test:parallel -- --concurrency 5 stable    # Limit concurrency

# Run tests sequentially (legacy)
npm run test:stable   # Stable tests
npm run test:all      # All tests

# Run specific test suites
npm run test:smoke        # Basic UI functionality
npm run test:formula      # Formula entry and computation
npm run test:editing      # Cell editing operations
npm run test:column-move  # Column/row operations
npm run test:collab       # Two-peer collaboration sync (experimental, may fail)

# Watch tests run in a visible browser window
HEADED=1 npm run test:smoke

# Slow down for debugging (100ms between actions)
HEADED=1 SLOWMO=100 npm run test:smoke
```

Test files are in `apps/wasm/tests/`:
- `harness.mjs` - Test harness (starts server + Chrome)
- `helpers.mjs` - Helper functions (clickCell, setCellValue, etc.)
- `smoke.test.mjs` - Basic UI tests (page load, cell selection, value entry)
- `formula.test.mjs` - Formula tests (entry, computation, dependencies)
- `editing.test.mjs` - Cell editing (delete, overwrite, Tab/Enter navigation)
- `column-move.test.mjs` - Column/row operations (sparse columns, drag to reorder)
- `collab.test.mjs` - Collaboration tests (experimental, may fail)

**Deploy to static hosting:**
- Upload contents of `dist/` to any static host (GitHub Pages, Netlify, Vercel, etc.)
- No server-side code required - runs entirely in browser

```
                    C++17 Core Source
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
      Clang/GCC      Emscripten        MSVC
          │               │               │
          ▼               ▼               ▼
    libcells.a       cells.wasm      cells.dll
    (macOS/Linux)      (Web)         (Windows)
```

## Current Implementation Status

**Core Engine (C++17):**
- Data model with UUID-based cells and Order Statistic Tree spatial indexing
- CRDT operations for all mutations (collaboration-native)
- Formula engine with Excel-compatible parser, native AST evaluation, dependency graph
- XLSX import/export, .zcd text format (git-friendly)
- Number formatting with Excel-compatible format codes
- Multi-sheet support

**WebAssembly Build:**
- Full core engine compiled to WASM via Emscripten
- Web Worker architecture for non-blocking UI
- Canvas2D-based grid renderer with virtual scrolling
- Interactive editing, selection, column/row resizing and reordering
- Keyboard navigation, clipboard support
- Luau scripting with autocomplete

**Real-time Collaboration:**
- P2P sync via WebRTC (no relay servers)
- CRDT-based conflict resolution (Last-Writer-Wins)
- Presence/cursor sharing
- Signaling server for connection setup only
- See [Networking](./docs/networking.md) and [Sync Protocol](./docs/sync-protocol.md)

**CLI Tool:**
- File format conversion (xlsx ↔ zcd)
- Basic file inspection and validation

## Design Decisions

**Architecture:**
- [x] **Language**: C++17 for core engine, TypeScript for web UI
- [x] **Build system**: Bazel with Bzlmod - fast incremental builds, hermetic
- [x] **Web deployment**: WASM + Web Worker - non-blocking, runs entirely in browser

**Data Model:**
- [x] **Cell IDs**: 8-character base62 UUIDs - CRDT-friendly, stable references
- [x] **Spatial indexing**: Order Statistic Tree - O(log n) pixel ↔ UUID mapping
- [x] **CRDT-first**: All mutations go through CRDT operations (collaboration by design)

**Formula Engine:**
- [x] **Formula runtime**: Native AST interpreter - no scripting VM, direct C++ execution
- [x] **Dependency graph**: Automatic recalculation on cell changes
- [x] **Reference adjustment**: AST-based (not string manipulation) for copy/paste

**Collaboration:**
- [x] **Networking**: P2P via WebRTC - no relay servers for document data
- [x] **CRDT sync**: Operation-based CRDT with HLC ordering, LWW conflict resolution
- [x] **Presence**: Real-time cursor/selection sharing

**Planned:**
- [ ] **Undo/redo**: Branch-based history - aligns with git-friendly philosophy, clean CRDT semantics
- [ ] **Type system**: Completely optional - Excel-like by default, column types as gradual discovery

## Design Philosophy

### Excel-First, Power Features as Gradual Discovery

Cells works exactly like Excel out of the box. Advanced features are **opt-in and discoverable**, not required:

1. **Start familiar**: Import XLSX, edit like Excel, export back to XLSX
2. **Discover gradually**: Column typing, validation, relations are there when you need them
3. **Never locked in**: Always export to XLSX (with clear warnings about feature loss)

### Stickiness Through Features Excel Can't Represent

These features create value that keeps users in Cells:

- **Relations**: Link cells to rows in other sheets (stable UUID references, not fragile A1)
- **Select/Multi-select**: Dropdown options with colors (stored in column, not data validation hacks)
- **Column validation**: Required fields, unique constraints, regex patterns
- **Views**: Multiple views of same data (grid, kanban, calendar, gallery)
- **API-first**: Every sheet is queryable via API

**Critical**: When exporting to XLSX, clearly inform users which features will be lost:

```
⚠ Export to Excel

These features will be lost:
• Column "Status" - Select options and colors
• Column "Project" - Relation to Projects sheet (will become plain text)
• Column "Email" - Validation pattern

The data itself will be preserved. Continue?
```

### Not AirTable

AirTable enforces structure. We don't:
- **AirTable**: Must define field types upfront, structured-first
- **Cells**: Type anywhere, constrain later (or never), spreadsheet-first

Column types in Cells are like training wheels you can add to any column at any time - not a requirement to use the product.
