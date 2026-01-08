# Cross-Platform Strategy

## Current Architecture

The cells engine is designed for **web deployment only**. Native builds exist for CLI/headless use cases, but no native UI (SwiftUI, WinUI, etc.) is planned.

| Component | Status |
|-----------|--------|
| Core engine (C++) | Implemented |
| WASM build | Implemented |
| Web UI (TypeScript + Canvas2D) | Implemented |
| CLI tool (native) | Implemented |
| Native UI apps | Not planned |

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Web Application (TypeScript)                      │
│   Canvas2D rendering, event handling, collaboration UI               │
└─────────────────────────────────────────────────────────────────────┘
                              │
                    WASM Bridge (Emscripten)
                              │
┌─────────────────────────────────────────────────────────────────────┐
│                      Core Engine (C++17)                             │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │
│  │ Data Model  │ │ Formula     │ │ CRDT        │ │ Persistence │   │
│  │ (UUID-based)│ │ Engine      │ │ Operations  │ │ (zcd, xlsx) │   │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Build Targets

| Target | Compiler | Output | Use Case |
|--------|----------|--------|----------|
| WASM | Emscripten | cells.wasm + .js | Web application |
| macOS | Clang | libcells.a | CLI tool |
| Linux | GCC/Clang | libcells.a | CLI tool, CI |
| Windows | MSVC | cells.lib | CLI tool |

## Web-Specific Architecture

The web application uses a **Web Worker architecture** for non-blocking UI:

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Main Thread                                     │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │
│  │ Grid        │ │ Cell        │ │ Collab      │ │ UI State    │   │
│  │ Renderer    │ │ Editor      │ │ UI          │ │ Manager     │   │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │
│                              │                                       │
│                    postMessage (async)                               │
│                              │                                       │
└──────────────────────────────┼──────────────────────────────────────┘
                               │
┌──────────────────────────────┼──────────────────────────────────────┐
│                      Web Worker                                      │
│                              │                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    WASM Core Engine                          │   │
│  │  Data Model, Formula Engine, CRDT, File I/O, Networking      │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

- **Main thread**: UI rendering (Canvas2D), event handling, minimal display cache
- **Web Worker**: All WASM operations, file parsing, CRDT sync, networking
- **Communication**: Async postMessage with structured clone

## TypeScript Responsibilities

The TypeScript layer handles UI concerns only:

- Canvas2D grid rendering with virtual scrolling
- Mouse and keyboard event handling
- Cell editing (inline and formula bar)
- Collaboration UI (connection status, presence indicators)
- Clipboard operations (with browser APIs)
- File dialogs and drag-drop (browser APIs)

**Important**: TypeScript does NOT maintain workbook state. All data lives in the WASM core. TypeScript only caches display information (formatted values, column widths for rendering).

## C++ Core Responsibilities

The C++ core handles all data and logic:

- Workbook data model (cells, sheets, axes)
- CRDT operations (all mutations must go through CRDT ops)
- Formula parsing, evaluation, dependency graph
- Order Statistic Tree (spatial indexing)
- File import/export (xlsx, csv, zcd)
- Number formatting and input parsing
- WebRTC networking (via platform-specific delegates)

## Why Web-Only UI?

1. **Reach**: Web works everywhere (desktop, mobile, tablet)
2. **Deployment**: No app store approval, instant updates
3. **Collaboration**: P2P WebRTC works great in browsers
4. **Development speed**: Single UI codebase to maintain

Native builds exist for CLI/headless scenarios (batch processing, CI, testing) but the full spreadsheet UI is web-only.

## CLI Tool

The CLI tool uses native builds (not WASM) for performance:

```bash
# File conversion
./cells convert input.xlsx output.zcd

# File inspection
./cells inspect file.zcd

# Validation
./cells validate file.zcd
```

The CLI shares the same C++ core as the WASM build but links directly instead of going through Emscripten.
