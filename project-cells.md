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
│   (Platform-specific: Native views / WebGL Canvas / Flutter)    │
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

### 2. [Formula Engine](./docs/formula-engine.md)
- Excel formula parser → AST
- AST → Lua transpiler
- Sandboxed Lua execution
- Dependency graph for reactive updates

### 3. [CRDT & Collaboration](./docs/crdt.md)
- Operation-based CRDT for cell mutations
- Dimension structure CRDTs (insert/delete/reorder)
- Conflict resolution strategies

### 4. [Persistence & File Format](./docs/persistence.md)
- Git-diff-friendly text format
- Binary format for large files
- Import/export (xlsx, csv)

### 5. [Rendering](./docs/rendering.md)
- Virtual viewport with aggressive culling
- Dirty region tracking
- Platform-specific backends

### 6. [Cross-Platform Strategy](./docs/cross-platform.md)
- Core as shared C/C++ library
- WebAssembly compilation for web
- UI layer options and recommendations

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
- [x] **UI framework**: Flutter (desktop/mobile) + React+Canvas (web)

## Open Questions

- [ ] Cell type system - dynamic (like Excel) or optional typing?
