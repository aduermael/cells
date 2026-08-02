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
| WASM | Emscripten | cells_wasm.wasm + .js | Web application |
| macOS | Clang | cells (binary) | CLI tool (sync + session) |
| Linux | GCC/Clang | cells (binary) | CLI tool (sync + session), CI |
| Windows | MSVC | cells (binary) | CLI tool (sync + session) |

Native networking third-party stack (all desktop OSes): **libdatachannel** is built as pure Bazel `cc_library` targets (plog, juice, usrsctp, libdatachannel) with BCR **OpenSSL** — same graph on macOS, Linux, and Windows. HTTP/WS remain OS-native (Foundation / curl / WinHTTP).

Build commands (all via Bazel):
- `bazel run :wasm-dist` - Build WASM and copy to dist/wasm/
- `bazel run :cli` - Build and run native CLI (debug)
- `bazel run :cli-release` - Build and run native CLI (release)
- Windows: `bazelisk build //apps/cli:cells` → `bazel-bin/apps/cli/cells.exe`

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
cells -i input.xlsx output.zcd
cells -i data.csv report.xlsx

# File inspection
cells -I file.zcd
cells -I budget.xlsx --sheet Summary

# Real-time sync (join a room)
cells sync 'https://cells.example.com/?room=abc123'
cells sync <url> --apply workbook.zcd   # Apply remote ops to local file
cells sync <url> --send workbook.zcd    # Broadcast local workbook
```

Supported formats: `.zcd` (native), `.csv`/`.tsv`, `.xlsx` (Excel 2007+).

The CLI shares the same C++ core as the WASM build but links directly instead of going through Emscripten.
