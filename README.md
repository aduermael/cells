# Cells - A Modern Spreadsheet Engine

## Vision
A high-performance, collaborative spreadsheet engine with:
- Git-friendly persistence
- Real-time collaboration via CRDT
- Multi-dimensional data model
- Cross-platform deployment (native + web)

## Project Stats

| Language | Lines |
|----------|------:|
| C++ | 19,763 |
| JavaScript | 8,109 |
| HTML | 3,075 |
| Go | 1,039 |
| CSS | 986 |

- **Commits**: 306
- **WASM Module**: 613 KB
- **Total Web Bundle**: 1.07 MB

<sub>Generated with `./scripts/generate-stats.sh`</sub>

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        UI Layer                                  │
│   (Platform-native: SwiftUI / WinUI / React+Canvas)             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Grid Renderer (C++17)                        │
│        Virtual scrolling, viewport culling, dirty regions        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Core Engine (C++17)                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Data Model  │  │ Formula     │  │ CRDT / Collaboration    │  │
│  │ (Cells,     │◄─┤ Engine      │  │ Engine                  │  │
│  │ Dimensions) │  │ (Native AST)│  │                         │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Persistence Layer                               │
│         Git-friendly text format / Binary for performance        │
└─────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. [Data Model](./docs/data-model.md)

- UUID-based cell identification
- N-dimensional sparse structure
- Doubly-linked dimension chains with gap encoding

### 2. [Type System](./docs/type-system.md)

- **Completely optional** - works exactly like Excel by default
- Column typing as gradual discovery (not enforced like AirTable)
- Features Excel can't represent (stickiness): relations, select options, validation
- Always exportable to XLSX (with warnings for feature loss)

### 3. [Formula Engine](./docs/formula-engine.md)

- Excel formula parser → AST
- Native AST interpreter in C/C++
- Dependency graph for reactive updates

### 4. [CRDT & Collaboration](./docs/crdt.md)

- Operation-based CRDT for cell mutations
- Dimension structure CRDTs (insert/delete/reorder)
- Conflict resolution strategies

### 5. [Persistence & File Format](./docs/persistence.md)

- Git-diff-friendly text format
- Binary format for large files
- Import/export (xlsx, csv)

### 6. [Rendering](./docs/rendering.md)

- Virtual viewport with aggressive culling
- Dirty region tracking
- Platform-specific backends

### 7. [Networking & Collaboration](./docs/networking.md)

- WebRTC peer-to-peer connections
- No relay server for document data
- Lightweight signaling for connection setup
- Mesh topology for small groups

### 8. [Cross-Platform Strategy](./docs/cross-platform.md)

- Core as shared C++17 library
- WebAssembly compilation for web
- Platform-native UI (SwiftUI, WinUI, React)

## Directory Structure

```
cells/
├── WORKSPACE               # Bazel workspace root
├── MODULE.bazel            # Bzlmod module definition
├── Makefile                # Development commands
├── core/                   # C++17 core engine
│   ├── BUILD
│   ├── cells/              # Main library
│   │   ├── BUILD
│   │   ├── *.h             # Headers
│   │   ├── *.cc            # Implementation
│   │   └── *_test.cc       # Tests (colocated)
│   └── testdata/           # Sample .cells and .xlsx files
├── apps/                   # Applications
│   ├── cli/                # Command-line tool
│   │   ├── BUILD
│   │   └── main.cc         # CLI entry point
│   └── wasm/               # WebAssembly build
│       ├── BUILD
│       ├── bindings.cc     # Embind bindings
│       └── static/         # Web UI (index.html, JS)
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
- `cells_wasm_bin.wasm` - WASM binary (~472KB)
- `cells_wasm_bin.js` - Emscripten JS glue
- `cells.d.ts` - TypeScript definitions
- `index.html` - Full spreadsheet UI
- `worker.js` - Web Worker for async WASM operations
- `client.js` - Main thread API (CellsClient)
- `shared/` - Shared utilities (data source, state machine)

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
- Data model with UUID-based cells, sparse quadtree storage
- XLSX import (basic support)
- .cells text format (read/write)
- Viewport-based querying with spatial indexing
- Multi-sheet support

**WebAssembly Build:**
- Full core engine compiled to WASM via Emscripten
- Web Worker architecture for non-blocking UI
- Canvas2D-based grid renderer
- Interactive editing, selection, resizing
- Keyboard navigation
- Listener-driven UI refresh

**CLI Tool:**
- File format conversion (xlsx → cells, cells → xlsx)
- Basic file inspection

**Not Yet Implemented:**
- Formula engine (AST parsing and evaluation)
- CRDT/collaboration
- Networking (WebRTC)
- Native platform apps (SwiftUI, WinUI)

## Design Decisions

**Implemented:**
- [x] **Language**: C++17 for core engine
- [x] **Build system**: Bazel with Bzlmod - fast incremental builds, hermetic
- [x] **Cell storage**: Sparse quadtree - efficient viewport queries, O(log n) access
- [x] **Cell IDs**: 8-character base62 UUIDs - compact, collision-resistant
- [x] **Web UI**: Canvas2D with Web Worker - non-blocking, responsive
- [x] **State management**: Listener pattern - WASM notifies JS of changes

**Planned (not yet implemented):**
- [ ] **Formula runtime**: Native AST interpreter - no dependencies, simpler, full control
- [ ] **Undo/redo**: Branch-based history - aligns with git-friendly philosophy, clean CRDT semantics
- [ ] **Networking**: P2P via WebRTC - no relay servers, CRDT-native sync
- [ ] **Native apps**: Platform-native UI (SwiftUI for Apple, WinUI for Windows)
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
