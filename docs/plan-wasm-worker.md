# Plan: WASM Worker Build

**Status:** IN PROGRESS
**Goal:** Run the spreadsheet engine fully in-browser via WebAssembly Worker, with no server dependency

## Overview

Build the cells app as a WASM module that runs in a Web Worker, enabling:
- Fully offline browser experience (no server required)
- File loading via drag & drop
- Shared codebase with CLI (same core library)
- Keep CLI `serve` command intact for development/debugging

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Browser                                  │
│  ┌──────────────────┐     ┌──────────────────────────────────┐  │
│  │    Main Thread   │     │         Web Worker               │  │
│  │                  │     │                                  │  │
│  │  ┌────────────┐  │     │  ┌─────────────────────────────┐ │  │
│  │  │  index.html │  │ msg │  │     cells.wasm              │ │  │
│  │  │  (UI Layer) │◄─┼─────┼─►│  (Core + Bindings)          │ │  │
│  │  └────────────┘  │     │  └─────────────────────────────┘ │  │
│  │        │         │     │              │                   │  │
│  │        ▼         │     │              ▼                   │  │
│  │  ┌────────────┐  │     │  ┌─────────────────────────────┐ │  │
│  │  │   Canvas   │  │     │  │  Workbook (in WASM memory)  │ │  │
│  │  │  Renderer  │  │     │  └─────────────────────────────┘ │  │
│  │  └────────────┘  │     │                                  │  │
│  └──────────────────┘     └──────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

**Code Sharing Strategy:**
- `core/cells/` - Shared between CLI and WASM (model, parser, readers/writers, quadtree)
- `apps/cli/server.cc` - CLI only (HTTP server)
- `apps/wasm/` - NEW: WASM bindings and worker wrapper
- `apps/cli/web/` - Updated: Support both server mode and WASM mode

## Phases

### Phase 1: Emscripten Toolchain Setup

**Goal:** Configure Bazel to build WASM targets with Emscripten

- [ ] 1.1 Add `emsdk` to MODULE.bazel as external dependency
- [ ] 1.2 Create `.bazelrc` config for `--config=wasm`
- [ ] 1.3 Create `platforms/BUILD` for wasm32 platform definition
- [ ] 1.4 Test minimal "hello world" WASM build
- [ ] 1.5 Update Makefile with `make wasm` target

**Files to create/modify:**
- `MODULE.bazel` - Add rules_emscripten
- `.bazelrc` - Add wasm config
- `platforms/BUILD` - Platform definitions
- `Makefile` - Add wasm targets

### Phase 2: WASM Bindings Layer

**Goal:** Create JavaScript bindings for core types using Embind

- [ ] 2.1 Create `apps/wasm/BUILD` with cc_binary target for WASM
- [ ] 2.2 Create `apps/wasm/bindings.cc` with Embind bindings for:
  - Workbook, Sheet, Cell, Axis types
  - Parser functions (parseFromString)
  - Serializer functions (serialize)
  - CSV/XLSX readers (from ArrayBuffer)
  - CSV/XLSX writers (to ArrayBuffer)
  - Quadtree viewport queries
  - RefConverter for formula display
- [ ] 2.3 Create TypeScript type definitions (`apps/wasm/cells.d.ts`)
- [ ] 2.4 Test bindings with simple JavaScript test

**API Design:**
```typescript
// apps/wasm/cells.d.ts
declare module 'cells' {
  interface CellValue {
    raw: string;
    type: 'n' | 's' | 'f' | 'b' | 'd' | 't' | 'e';
    error?: string;
  }

  interface Cell {
    id: string;
    colId: string;
    rowId: string;
    value: CellValue;
    formula?: string;
  }

  interface Axis {
    id: string;
    name: string;
    position: number;
    size: number;
  }

  interface ViewportResult {
    cells: Array<{ cell: Cell; x: number; y: number }>;
  }

  class Workbook {
    static parseFromCells(content: string): Workbook;
    static parseFromCSV(data: Uint8Array, options?: CSVOptions): Workbook;
    static parseFromXLSX(data: Uint8Array): Workbook;

    getSheetCount(): number;
    getSheetName(index: number): string;
    setActiveSheet(index: number): void;

    // Viewport queries
    queryViewport(x1: number, y1: number, x2: number, y2: number): ViewportResult;

    // Column/row info
    getColumns(): Axis[];
    getRows(): Axis[];
    getColumnByPosition(pos: number): Axis | null;
    getRowByPosition(pos: number): Axis | null;

    // Cell operations
    getCell(id: string): Cell | null;
    setCell(colId: string, rowId: string, value: string): Cell;

    // Resize/move
    resizeColumn(id: string, size: number): void;
    resizeRow(id: string, size: number): void;
    moveColumn(id: string, newPos: number): void;
    moveRow(id: string, newPos: number): void;

    // Export
    toCells(): string;
    toCSV(sheetIndex?: number): Uint8Array;
    toXLSX(): Uint8Array;
  }
}
```

### Phase 3: Web Worker Integration

**Goal:** Wrap WASM module in a Web Worker with message-based API

- [ ] 3.1 Create `apps/wasm/worker.js` - Worker entry point that loads WASM
- [ ] 3.2 Define message protocol (matching existing REST API semantics)
- [ ] 3.3 Create `apps/wasm/client.js` - Main thread client library
- [ ] 3.4 Handle file loading in worker (ArrayBuffer transfer)
- [ ] 3.5 Test worker with sample .cells file

**Message Protocol:**
```typescript
// Request types (main → worker)
type WorkerRequest =
  | { type: 'load'; format: 'cells' | 'csv' | 'xlsx'; data: ArrayBuffer }
  | { type: 'getSheetInfo' }
  | { type: 'queryViewport'; x1: number; y1: number; x2: number; y2: number }
  | { type: 'setCell'; colId: string; rowId: string; value: string }
  | { type: 'resizeColumn'; id: string; size: number }
  | { type: 'resizeRow'; id: string; size: number }
  | { type: 'moveColumn'; id: string; newPos: number }
  | { type: 'moveRow'; id: string; newPos: number }
  | { type: 'export'; format: 'cells' | 'csv' | 'xlsx' };

// Response types (worker → main)
type WorkerResponse =
  | { type: 'loaded'; sheetCount: number; sheetNames: string[] }
  | { type: 'sheetInfo'; name: string; cols: number; rows: number; ... }
  | { type: 'viewport'; cells: CellData[]; columns: Axis[]; rows: Axis[] }
  | { type: 'cellUpdated'; cell: Cell }
  | { type: 'exported'; format: string; data: ArrayBuffer; filename: string }
  | { type: 'error'; message: string };
```

### Phase 4: Web UI Updates

**Goal:** Update index.html to support both server mode and WASM mode

- [ ] 4.1 Add drag & drop file loading UI (drop zone overlay)
- [ ] 4.2 Create abstraction layer for data source (server vs WASM)
- [ ] 4.3 Update API calls to use abstraction layer
- [ ] 4.4 Add "Open File" button and file input
- [ ] 4.5 Add mode indicator (Server/Local) in UI
- [ ] 4.6 Handle empty state (no file loaded) gracefully
- [ ] 4.7 Test with both server mode and WASM mode

**UI Changes:**
```
┌────────────────────────────────────────────────────────┐
│  [Open File ▼]  [Export ▼]  │ Mode: Local │ file.xlsx │
├────────────────────────────────────────────────────────┤
│                                                        │
│            ┌─────────────────────────┐                │
│            │                         │                │
│            │   Drop file here        │   (shown when  │
│            │   .cells .csv .xlsx     │    no file or  │
│            │                         │    dragging)   │
│            └─────────────────────────┘                │
│                                                        │
│            ─ or ─                                      │
│                                                        │
│            [Choose File]                               │
│                                                        │
└────────────────────────────────────────────────────────┘
```

### Phase 5: Build & Distribution

**Goal:** Package WASM build for standalone deployment

- [ ] 5.1 Create `apps/wasm/static/` with standalone HTML (no server)
- [ ] 5.2 Add build step to copy WASM artifacts to web directory
- [ ] 5.3 Create `make wasm-dist` target for production build
- [ ] 5.4 Test standalone deployment (file:// protocol or static server)
- [ ] 5.5 Document deployment options (GitHub Pages, static hosting)
- [ ] 5.6 Update README with WASM build instructions

**Output Structure:**
```
dist/
├── index.html          # Standalone viewer
├── cells.js            # Emscripten JS glue
├── cells.wasm          # WASM binary
├── worker.js           # Web Worker
└── client.js           # Main thread API
```

### Phase 6: Testing & Polish

- [ ] 6.1 Test large file loading (1MB+ XLSX)
- [ ] 6.2 Test formula evaluation in WASM
- [ ] 6.3 Add progress indicator for file loading
- [ ] 6.4 Add error handling for invalid files
- [ ] 6.5 Test cross-browser compatibility (Chrome, Firefox, Safari)
- [ ] 6.6 Performance comparison: native server vs WASM

## Technical Considerations

### Memory Management
- Emscripten default: 16MB initial, can grow
- Large spreadsheets may need explicit memory sizing: `-s INITIAL_MEMORY=64MB`
- Use Transferable ArrayBuffers for file data to avoid copying

### Asyncify
- Not needed initially (operations are fast)
- Add later if formula evaluation causes UI jank
- Alternative: chunked processing with progress callbacks

### File Format Detection
```javascript
function detectFormat(filename, data) {
  const ext = filename.split('.').pop().toLowerCase();
  if (ext === 'cells') return 'cells';
  if (ext === 'csv' || ext === 'tsv') return 'csv';
  if (ext === 'xlsx') return 'xlsx';

  // Fallback: check magic bytes
  const view = new Uint8Array(data.slice(0, 4));
  if (view[0] === 0x50 && view[1] === 0x4B) return 'xlsx'; // PK (ZIP)
  return 'csv'; // Default to CSV
}
```

### ID Generation in WASM
Current `generate_id()` uses `std::random_device`. In WASM:
- Option A: Use Emscripten's random (uses crypto.getRandomValues)
- Option B: Pass random bytes from JS via binding

## Dependencies

**New Bazel Dependencies:**
- `rules_emscripten` or `emsdk` toolchain

**No New JS Dependencies** - Keep it vanilla JS like existing web UI

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Emscripten build complexity | Start with minimal example, iterate |
| Large WASM binary size | Use `-Os` optimization, consider splitting |
| Memory limits | Test with large files early, set appropriate limits |
| Browser compatibility | Test on major browsers in Phase 6 |
| Formula engine in WASM | Already C++, should work unchanged |

## Success Criteria

1. User can open index.html locally (file:// or static server)
2. User can drag & drop .cells, .csv, or .xlsx file
3. Spreadsheet renders with full functionality (scroll, edit, resize)
4. User can export to any format
5. CLI `serve` command continues to work unchanged
6. WASM binary size < 2MB (compressed)
7. Load time for 10K cell spreadsheet < 2 seconds

## References

- [Emscripten Documentation](https://emscripten.org/docs/)
- [Embind Guide](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
- [Web Workers API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API)
- Existing architecture docs: `docs/cross-platform.md`, `docs/formula-engine.md`
