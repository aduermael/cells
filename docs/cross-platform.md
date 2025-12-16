# Cross-Platform Strategy

## Target Platforms

| Platform | Priority | Notes |
|----------|----------|-------|
| Web | P0 | Widest reach, easiest distribution |
| macOS | P0 | Primary development platform |
| Windows | P1 | Largest desktop market |
| iOS | P1 | Mobile companion |
| Android | P2 | Mobile companion |
| Linux | P2 | Developer audience |

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Application Layer                               │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐   │
│  │ Web     │ │ macOS   │ │ Windows │ │ iOS     │ │ Android     │   │
│  │ (React) │ │ (Swift) │ │ (C#/WPF)│ │ (Swift) │ │ (Kotlin)    │   │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └──────┬──────┘   │
│       │           │           │           │              │          │
│       └───────────┴───────────┴─────┬─────┴──────────────┘          │
│                                     │                                │
│                        Platform Bindings                             │
│                                     │                                │
└─────────────────────────────────────┼───────────────────────────────┘
                                      │
┌─────────────────────────────────────┼───────────────────────────────┐
│                      Core Engine (C/C++)                             │
│                                     │                                │
│  ┌──────────────────────────────────┴────────────────────────────┐  │
│  │                        C API Layer                             │  │
│  │  cells_workbook_new()  cells_cell_set()  cells_render()  ...  │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │
│  │ Data Model  │ │ Formula     │ │ CRDT        │ │ Persistence │   │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │
│                                                                      │
│  ┌─────────────┐ ┌─────────────┐                                    │
│  │ Renderer    │ │ Lua Runtime │                                    │
│  └─────────────┘ └─────────────┘                                    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

## Build Targets

### Core Library Compilation

```
C/C++ Source
     │
     ├──► Clang (macOS) ──────────► libcells.a / libcells.dylib
     │
     ├──► Clang (iOS) ────────────► libcells.a (arm64)
     │
     ├──► MSVC (Windows) ─────────► cells.lib / cells.dll
     │
     ├──► GCC/Clang (Linux) ──────► libcells.a / libcells.so
     │
     ├──► Android NDK ────────────► libcells.so (arm64, x86)
     │
     └──► Emscripten ─────────────► cells.wasm + cells.js
```

### Build Configuration (CMake)

```cmake
cmake_minimum_required(VERSION 3.20)
project(cells LANGUAGES C CXX)

# Core library
add_library(cells_core
    src/model/cell.c
    src/model/sheet.c
    src/model/dimension.c
    src/formula/parser.c
    src/formula/codegen.c
    src/formula/lua_sandbox.c
    src/crdt/hlc.c
    src/crdt/operations.c
    src/persistence/text_format.c
    src/renderer/layout.c
    src/renderer/draw_list.c
    src/api/c_api.c
)

# Lua (embedded)
add_subdirectory(vendor/lua)
target_link_libraries(cells_core PRIVATE lua)

# Platform-specific
if(EMSCRIPTEN)
    set_target_properties(cells_core PROPERTIES
        SUFFIX ".wasm"
        LINK_FLAGS "-s WASM=1 -s EXPORTED_FUNCTIONS='[...]' -s MODULARIZE=1"
    )
elseif(APPLE)
    target_compile_options(cells_core PRIVATE -fPIC)
elseif(WIN32)
    target_compile_definitions(cells_core PRIVATE CELLS_EXPORT)
endif()
```

## C API Design

The C API is the contract between core and platform layers:

```c
// api/cells.h - Public API

#ifndef CELLS_API_H
#define CELLS_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #ifdef CELLS_EXPORT
        #define CELLS_API __declspec(dllexport)
    #else
        #define CELLS_API __declspec(dllimport)
    #endif
#else
    #define CELLS_API __attribute__((visibility("default")))
#endif

// Opaque handles
typedef struct CellsWorkbook* CellsWorkbookHandle;
typedef struct CellsSheet* CellsSheetHandle;
typedef struct CellsCell* CellsCellHandle;

// UUID type (16 bytes)
typedef struct {
    uint8_t bytes[16];
} CellsUUID;

// Workbook operations
CELLS_API CellsWorkbookHandle cells_workbook_new(void);
CELLS_API void cells_workbook_free(CellsWorkbookHandle wb);
CELLS_API CellsWorkbookHandle cells_workbook_load(const char* path);
CELLS_API bool cells_workbook_save(CellsWorkbookHandle wb, const char* path);

// Sheet operations
CELLS_API CellsSheetHandle cells_workbook_add_sheet(CellsWorkbookHandle wb,
                                                     const char* name);
CELLS_API CellsSheetHandle cells_workbook_get_sheet(CellsWorkbookHandle wb,
                                                     int index);
CELLS_API int cells_workbook_sheet_count(CellsWorkbookHandle wb);

// Cell operations
CELLS_API CellsUUID cells_sheet_set_cell_number(CellsSheetHandle sheet,
                                                 CellsUUID col, CellsUUID row,
                                                 double value);
CELLS_API CellsUUID cells_sheet_set_cell_string(CellsSheetHandle sheet,
                                                 CellsUUID col, CellsUUID row,
                                                 const char* value);
CELLS_API CellsUUID cells_sheet_set_cell_formula(CellsSheetHandle sheet,
                                                  CellsUUID col, CellsUUID row,
                                                  const char* formula);
CELLS_API double cells_cell_get_number(CellsCellHandle cell);
CELLS_API const char* cells_cell_get_string(CellsCellHandle cell);
CELLS_API const char* cells_cell_get_display_text(CellsCellHandle cell);

// Dimension operations
CELLS_API CellsUUID cells_sheet_insert_column(CellsSheetHandle sheet,
                                               CellsUUID after);
CELLS_API CellsUUID cells_sheet_insert_row(CellsSheetHandle sheet,
                                            CellsUUID after);
CELLS_API void cells_sheet_delete_column(CellsSheetHandle sheet, CellsUUID col);
CELLS_API void cells_sheet_delete_row(CellsSheetHandle sheet, CellsUUID row);

// Rendering
typedef struct {
    float scroll_x, scroll_y;
    float width, height;
    float zoom;
} CellsViewport;

typedef struct {
    void* commands;     // Platform interprets this
    int command_count;
} CellsDrawList;

CELLS_API CellsDrawList* cells_render(CellsSheetHandle sheet,
                                       CellsViewport viewport);
CELLS_API void cells_drawlist_free(CellsDrawList* dl);

// Collaboration
typedef void (*CellsOperationCallback)(const uint8_t* data, size_t len,
                                        void* userdata);
CELLS_API void cells_set_operation_callback(CellsWorkbookHandle wb,
                                             CellsOperationCallback cb,
                                             void* userdata);
CELLS_API void cells_apply_remote_operation(CellsWorkbookHandle wb,
                                             const uint8_t* data, size_t len);

// Events (for UI updates)
typedef void (*CellsChangeCallback)(CellsUUID* changed_cells, int count,
                                     void* userdata);
CELLS_API void cells_set_change_callback(CellsWorkbookHandle wb,
                                          CellsChangeCallback cb,
                                          void* userdata);

#endif // CELLS_API_H
```

## Platform Bindings

### Web (WebAssembly + JavaScript)

```javascript
// cells.js - JavaScript wrapper around WASM

class CellsWorkbook {
    constructor() {
        this._handle = Module._cells_workbook_new();
    }

    static async load(path) {
        const response = await fetch(path);
        const buffer = await response.arrayBuffer();
        const bytes = new Uint8Array(buffer);

        // Copy to WASM memory
        const ptr = Module._malloc(bytes.length);
        Module.HEAPU8.set(bytes, ptr);

        const handle = Module._cells_workbook_load_memory(ptr, bytes.length);
        Module._free(ptr);

        const wb = new CellsWorkbook();
        wb._handle = handle;
        return wb;
    }

    addSheet(name) {
        const namePtr = allocateString(name);
        const handle = Module._cells_workbook_add_sheet(this._handle, namePtr);
        Module._free(namePtr);
        return new CellsSheet(handle);
    }

    // ... more methods
}

class CellsSheet {
    constructor(handle) {
        this._handle = handle;
    }

    setCellNumber(col, row, value) {
        return Module._cells_sheet_set_cell_number(
            this._handle,
            col.toWasm(),
            row.toWasm(),
            value
        );
    }

    render(viewport) {
        const vpPtr = viewportToWasm(viewport);
        const dlPtr = Module._cells_render(this._handle, vpPtr);

        // Convert draw list to canvas commands
        const commands = parseDrawList(dlPtr);
        Module._cells_drawlist_free(dlPtr);

        return commands;
    }
}
```

### macOS/iOS (Swift)

```swift
// Cells.swift - Swift wrapper

import Foundation

class CellsWorkbook {
    private var handle: OpaquePointer

    init() {
        handle = cells_workbook_new()
    }

    deinit {
        cells_workbook_free(handle)
    }

    static func load(from url: URL) throws -> CellsWorkbook {
        let wb = CellsWorkbook()
        guard let h = cells_workbook_load(url.path) else {
            throw CellsError.loadFailed
        }
        wb.handle = h
        return wb
    }

    func addSheet(name: String) -> CellsSheet {
        let h = cells_workbook_add_sheet(handle, name)
        return CellsSheet(handle: h)
    }
}

class CellsSheet {
    private var handle: OpaquePointer

    init(handle: OpaquePointer) {
        self.handle = handle
    }

    func setCell(col: CellsUUID, row: CellsUUID, value: Double) -> CellsUUID {
        return cells_sheet_set_cell_number(handle, col, row, value)
    }

    func render(viewport: CellsViewport) -> [DrawCommand] {
        let dl = cells_render(handle, viewport)
        defer { cells_drawlist_free(dl) }
        return parseDrawList(dl)
    }
}

// Bridging header: Cells-Bridging-Header.h
#import "cells.h"
```

### Windows (C# P/Invoke)

```csharp
// Cells.cs - C# wrapper

using System;
using System.Runtime.InteropServices;

public class CellsWorkbook : IDisposable
{
    [DllImport("cells.dll")]
    private static extern IntPtr cells_workbook_new();

    [DllImport("cells.dll")]
    private static extern void cells_workbook_free(IntPtr handle);

    [DllImport("cells.dll")]
    private static extern IntPtr cells_workbook_load(string path);

    private IntPtr _handle;

    public CellsWorkbook()
    {
        _handle = cells_workbook_new();
    }

    public static CellsWorkbook Load(string path)
    {
        var wb = new CellsWorkbook();
        wb._handle = cells_workbook_load(path);
        if (wb._handle == IntPtr.Zero)
            throw new Exception("Failed to load workbook");
        return wb;
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            cells_workbook_free(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
```

## UI Framework Strategy

### Decision: Platform-Native UIs

For maximum snappiness and native feel, each platform gets its own native UI:

| Platform | Framework | Language | Notes |
|----------|-----------|----------|-------|
| **macOS** | SwiftUI | Swift | Native feel, best performance |
| **iOS** | SwiftUI | Swift | Shared code with macOS |
| **Web** | React + Canvas | TypeScript | Full web integration |
| **Windows** | WinUI 3 | C# | Modern Windows native |
| **Android** | Jetpack Compose | Kotlin | Material 3 native |

### Why Not Flutter/Cross-Platform?

1. **Performance**: Native frameworks are always snappier
2. **Platform integration**: Better gestures, menus, shortcuts
3. **AI-assisted development**: Maintaining multiple UIs is now practical
4. **User expectations**: Spreadsheets need to feel "right" on each platform
5. **App Store**: Native apps get better treatment

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Platform UI Layer                             │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │ SwiftUI │ │ SwiftUI │ │ React   │ │ WinUI 3 │ │ Compose  │  │
│  │ (macOS) │ │ (iOS)   │ │ (Web)   │ │ (Win)   │ │ (Android)│  │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬─────┘  │
│       │           │           │           │           │         │
│       └───────────┴───────────┴─────┬─────┴───────────┘         │
│                                     │                           │
│                        Platform Bindings                        │
│                        (Swift / JS / C# / Kotlin)               │
└─────────────────────────────────────┼──────────────────────────┘
                                      │
┌─────────────────────────────────────┼──────────────────────────┐
│                      Core Engine (C/C++)                        │
│                            C API                                │
└─────────────────────────────────────────────────────────────────┘
```

### Code Sharing Between Platforms

While UIs are separate, significant code is shared:

| Layer | Shared? | Notes |
|-------|---------|-------|
| Core engine | 100% | C/C++, all platforms |
| C API | 100% | Universal interface |
| Bindings | ~80% | Per-language, not per-platform |
| UI patterns | ~60% | Similar architecture, different syntax |
| Business logic | ~50% | Can share via core or thin layer |

## SwiftUI Integration (Apple Platforms)

```swift
// CellsApp/Core/CellsBindings.swift

import Foundation

// Load the core library
@_silgen_name("cells_workbook_new")
func cells_workbook_new() -> OpaquePointer

@_silgen_name("cells_workbook_free")
func cells_workbook_free(_ handle: OpaquePointer)

@_silgen_name("cells_render")
func cells_render(_ sheet: OpaquePointer, _ viewport: UnsafePointer<CellsViewport>) -> UnsafeMutablePointer<CellsDrawList>

// Swift wrapper
@Observable
class Workbook {
    private var handle: OpaquePointer

    init() {
        handle = cells_workbook_new()
    }

    deinit {
        cells_workbook_free(handle)
    }

    func render(sheet: Int, viewport: CellsViewport) -> [DrawCommand] {
        var vp = viewport
        let drawList = cells_render(getSheet(sheet), &vp)
        defer { cells_drawlist_free(drawList) }
        return parseDrawList(drawList)
    }
}
```

```swift
// CellsApp/Views/SpreadsheetView.swift

import SwiftUI

struct SpreadsheetView: View {
    @State private var workbook: Workbook
    @State private var scrollOffset: CGPoint = .zero
    @State private var zoom: CGFloat = 1.0

    var body: some View {
        GeometryReader { geometry in
            Canvas { context, size in
                let viewport = CellsViewport(
                    scroll_x: Float(scrollOffset.x),
                    scroll_y: Float(scrollOffset.y),
                    width: Float(size.width),
                    height: Float(size.height),
                    zoom: Float(zoom)
                )

                let commands = workbook.render(sheet: 0, viewport: viewport)

                for cmd in commands {
                    switch cmd {
                    case .rect(let rect, let color):
                        context.fill(
                            Path(rect),
                            with: .color(Color(color))
                        )
                    case .text(let text, let point, let style):
                        context.draw(
                            Text(text).font(style.font),
                            at: point
                        )
                    case .line(let from, let to, let color):
                        context.stroke(
                            Path { p in
                                p.move(to: from)
                                p.addLine(to: to)
                            },
                            with: .color(Color(color))
                        )
                    }
                }
            }
            .gesture(
                DragGesture()
                    .onChanged { value in
                        scrollOffset.x -= value.translation.width
                        scrollOffset.y -= value.translation.height
                    }
            )
            .gesture(
                MagnificationGesture()
                    .onChanged { value in
                        zoom = value
                    }
            )
        }
    }
}

// For high-performance rendering, can drop to Metal
struct MetalSpreadsheetView: NSViewRepresentable {
    let workbook: Workbook

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView()
        view.device = MTLCreateSystemDefaultDevice()
        // ... Metal setup for 60fps scrolling
        return view
    }
}
```

## Web-Specific Considerations

### WASM Loading

```javascript
// Async initialization
let cellsModule = null;

export async function initCells() {
    cellsModule = await CellsModule({
        locateFile: (path) => `/wasm/${path}`,
    });
}

export function getCells() {
    if (!cellsModule) throw new Error('Cells not initialized');
    return cellsModule;
}
```

### Web Workers for Heavy Operations

```javascript
// cells.worker.js
import { initCells, getCells } from './cells-wasm.js';

let workbook = null;

self.onmessage = async (e) => {
    const { type, payload } = e.data;

    switch (type) {
        case 'init':
            await initCells();
            break;

        case 'load':
            workbook = getCells().CellsWorkbook.load(payload.data);
            break;

        case 'render':
            const commands = workbook.getSheet(0).render(payload.viewport);
            self.postMessage({ type: 'render_result', commands });
            break;

        case 'recalc':
            workbook.recalculate();
            self.postMessage({ type: 'recalc_done' });
            break;
    }
};
```

## Development Workflow

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Development                                   │
│                                                                      │
│   1. Core (C/C++)           2. Web App            3. Desktop/Mobile │
│   ────────────────          ───────────           ──────────────────│
│   - VS Code + clangd        - VS Code             - Xcode / VS      │
│   - CMake                   - Vite                - Flutter         │
│   - Catch2 tests            - React               - Native tools    │
│   - valgrind/asan           - TypeScript          - FFI bindings    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘

Build Pipeline:
  1. Core C/C++ compiles to all targets (native libs + WASM)
  2. Web app bundles WASM
  3. Flutter apps link native libs
  4. CI tests all platforms
```

## Summary

| Component | Technology | Rationale |
|-----------|------------|-----------|
| Core engine | C/C++ | Performance, portability |
| Formulas | Luau | App Store compliant, fast |
| macOS/iOS UI | SwiftUI | Native feel, maximum snappiness |
| Web UI | React + Canvas | Full web integration |
| Windows UI | WinUI 3 | Modern Windows native |
| Android UI | Jetpack Compose | Material 3 native |
| Networking | WebRTC P2P | Modern, serverless sync |
| Build | CMake + platform tools | Industry standard |
| IPC | C API + FFI | Universal, type-safe |

### Development Order

1. **Core engine** (C/C++) with comprehensive C API
2. **Swift bindings** + SwiftUI app (primary platform)
3. **WASM build** + React web app
4. Other platforms as needed
