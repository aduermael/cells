# Cells - A Modern Spreadsheet Engine

## Vision
A high-performance, collaborative spreadsheet engine with:
- Git-friendly persistence
- Real-time collaboration via CRDT
- Multi-dimensional data model
- Cross-platform deployment (native + web)

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        UI Layer                                  │
│   (Platform-native: SwiftUI / WinUI / React+Canvas)             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Grid Renderer (C/C++)                        │
│        Virtual scrolling, viewport culling, dirty regions        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Core Engine (C/C++)                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Data Model  │  │ Formula     │  │ CRDT / Collaboration    │  │
│  │ (Cells,     │◄─┤ Engine      │  │ Engine                  │  │
│  │ Dimensions) │  │ (Lua + AST) │  │                         │  │
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
- AST → Luau transpiler
- Sandboxed Luau execution
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

- Core as shared C/C++ library
- WebAssembly compilation for web
- Platform-native UI (SwiftUI, WinUI, React)

## Directory Structure (Proposed)

```
cells/
├── core/                   # C/C++ core engine
│   ├── src/
│   │   ├── model/          # Data structures
│   │   ├── formula/        # Formula engine
│   │   ├── crdt/           # Collaboration
│   │   ├── persistence/    # File I/O
│   │   └── api/            # C API for bindings
│   ├── lua/                # Embedded Lua + sandbox
│   └── tests/
├── renderer/               # Grid rendering layer
│   ├── src/
│   └── shaders/            # If using GPU
├── bindings/               # Language bindings
│   ├── wasm/               # WebAssembly build
│   ├── swift/              # iOS/macOS
│   ├── kotlin/             # Android
│   └── node/               # Node.js (optional)
├── apps/                   # Platform apps
│   ├── web/
│   ├── macos/
│   ├── ios/
│   └── windows/
├── docs/                   # Architecture docs
└── tools/                  # Dev tools, converters
```

## Key Design Decisions

### Why UUID-based cells instead of (row, col)?

1. **CRDT-friendly**: No coordinate conflicts during concurrent edits
2. **Stable references**: Moving cells doesn't break formulas
3. **Sparse by nature**: Only allocated cells consume memory
4. **Multi-dimensional**: Generalizes beyond 2D trivially

### Why Luau for formulas?

1. **App Store compliant**: No JIT = accepted on iOS/macOS sandboxed apps
2. **Faster than Lua 5.4**: Roblox-optimized VM, type-aware optimizations
3. **Sandboxable**: Built-in sandboxing, proven at Roblox scale
4. **Type annotations**: Optional types for better tooling and perf hints
5. **Well-maintained**: Active development, open-source (MIT)

### Why doubly-linked dimensions with gaps?

1. **O(1) insert/delete**: No array shifting
2. **Sparse-friendly**: Gap encoding avoids empty nodes
3. **CRDT-compatible**: Each link is independently addressable
4. **Flexible iteration**: Forward/backward traversal

## Build & Compilation Strategy

```
                    C/C++ Core Source
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
      Clang/GCC      Emscripten        MSVC
          │               │               │
          ▼               ▼               ▼
    libcells.a       cells.wasm      cells.dll
    (macOS/Linux)      (Web)         (Windows)
```

## Next Steps

1. **Finalize data model** - Lock down the cell/dimension structures
2. **Prototype formula parser** - Excel subset → AST
3. **Implement basic CRDT** - Cell value operations
4. **Create minimal renderer** - Proof of concept
5. **Design file format** - Git-friendly serialization

## Decisions Made

- [x] **Formula runtime**: Luau (not Lua/LuaJIT) - App Store compliant, faster, well-maintained
- [x] **Cell storage**: Sharded hashmap - O(1) access, parallelizable
- [x] **Undo/redo**: Branch-based history - aligns with git-friendly philosophy, clean CRDT semantics
- [x] **Dimensions**: Start with 2D, but model supports N dimensions - naming and structures are dimension-agnostic
- [x] **UI framework**: Platform-native (SwiftUI for Apple, WinUI for Windows, React+Canvas for web)
- [x] **Networking**: P2P via WebRTC - no relay servers, CRDT-native sync
- [x] **Type system**: Completely optional - Excel-like by default, column types as gradual discovery for stickiness

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
