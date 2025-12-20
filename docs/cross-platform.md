# Cross-Platform Strategy

## Implementation Status

**Current state (December 2024):** Web only via WebAssembly (WASM).

| Component | Status |
|-----------|--------|
| Core engine (C++) | ✅ Implemented |
| WASM build | ✅ Implemented |
| Web UI (Canvas2D) | ✅ Implemented |
| Swift bindings | ❌ Not started |
| macOS/iOS apps | ❌ Not started |
| Windows/Android | ❌ Not started |

The architecture below describes the full vision. Currently only the Web/WASM path is implemented.

---

## Target Platforms

| Platform | Priority | Notes |
|----------|----------|-------|
| Web | P0 | Widest reach |
| macOS | P0 | Primary development |
| Windows | P1 | Largest desktop market |
| iOS | P1 | Mobile companion |
| Android | P2 | Mobile companion |
| Linux | P2 | Developer audience |

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Application Layer                               │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐   │
│  │ Web     │ │ macOS   │ │ Windows │ │ iOS     │ │ Android     │   │
│  │ (React) │ │ (Swift) │ │ (C#/WPF)│ │ (Swift) │ │ (Kotlin)    │   │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └──────┬──────┘   │
│       └───────────┴───────────┴─────┬─────┴──────────────┘          │
│                                     │                                │
│                        Platform Bindings (FFI)                       │
└─────────────────────────────────────┼───────────────────────────────┘
                                      │
┌─────────────────────────────────────┼───────────────────────────────┐
│                      Core Engine (C++)                               │
│                            C API                                     │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │
│  │ Data Model  │ │ Formula     │ │ CRDT        │ │ Persistence │   │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

## Build Targets

| Platform | Compiler | Output |
|----------|----------|--------|
| macOS | Clang | libcells.a / .dylib |
| iOS | Clang | libcells.a (arm64) |
| Windows | MSVC | cells.lib / .dll |
| Linux | GCC/Clang | libcells.a / .so |
| Android | NDK | libcells.so |
| Web | Emscripten | cells.wasm + .js |

## C API Design

The C API is the contract between core and platform layers:

### Workbook Operations
- `cells_workbook_new()` / `cells_workbook_free()`
- `cells_workbook_load()` / `cells_workbook_save()`

### Sheet Operations
- `cells_workbook_add_sheet()` / `cells_workbook_get_sheet()`
- `cells_workbook_sheet_count()`

### Cell Operations
- `cells_sheet_set_cell_number()` / `cells_sheet_set_cell_string()`
- `cells_sheet_set_cell_formula()`
- `cells_cell_get_number()` / `cells_cell_get_string()`

### Dimension Operations
- `cells_sheet_insert_column()` / `cells_sheet_insert_row()`
- `cells_sheet_delete_column()` / `cells_sheet_delete_row()`

### Rendering
- `cells_render(sheet, viewport)` → DrawList
- `cells_drawlist_free()`

### Collaboration
- `cells_set_operation_callback()` - notify when ops need sync
- `cells_apply_remote_operation()` - apply op from peer
- `cells_set_change_callback()` - notify UI of changes

## UI Framework Strategy

### Decision: Platform-Native UIs

For maximum performance and native feel:

| Platform | Framework |
|----------|-----------|
| macOS | SwiftUI |
| iOS | SwiftUI |
| Web | React + Canvas |
| Windows | WinUI 3 |
| Android | Jetpack Compose |

### Why Not Flutter/Cross-Platform?

1. **Performance**: Native is always snappier
2. **Platform integration**: Better gestures, menus, shortcuts
3. **User expectations**: Spreadsheets need to feel "right"
4. **App Store**: Native apps get better treatment

### Code Sharing

| Layer | Shared |
|-------|--------|
| Core engine | 100% (C++) |
| C API | 100% |
| Bindings | ~80% (per-language) |
| UI patterns | ~60% (similar architecture) |

## Platform Bindings

Each platform wraps the C API in idiomatic code:

| Platform | Binding Style |
|----------|---------------|
| Swift | `@_silgen_name` + wrapper classes |
| JavaScript | WASM + JS wrapper |
| C# | P/Invoke + wrapper classes |
| Kotlin | JNI + wrapper classes |

## Web-Specific Notes

- WASM loaded asynchronously
- Heavy operations in Web Workers
- DrawList rendered to Canvas2D or WebGL

## Development Order

1. **Core engine** (C++) with comprehensive C API
2. **Swift bindings** + SwiftUI app (primary platform)
3. **WASM build** + React web app
4. Other platforms as needed
