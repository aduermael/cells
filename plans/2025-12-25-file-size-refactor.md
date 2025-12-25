Status: READY
Created At: 2025-12-25 07:34 UTC
Updated At: 2025-12-25 08:10 UTC
Following plan management guidelines defined in AGENTS.md

# File Size Refactoring Plan

**Goal**: Keep all source files under 500 lines for maintainability and easier AI processing.

## Current State Analysis

### Files Over 500 Lines (Excluding node_modules)

**TypeScript/JavaScript (apps/wasm/):**
| File | Lines | Priority |
|------|-------|----------|
| `static/index.html` | 3082 | P0 - Critical (3000 lines inline JS) |
| `src/grid-renderer.ts` | 1177 | P1 |
| `src/client.ts` | 1040 | P1 |
| `src/worker.ts` | 953 | P2 |
| `src/collab-ui.ts` | 871 | P2 |
| `src/cpp-sync-adapter.ts` | 809 | P2 |
| `src/ui-state.ts` | 681 | P2 |
| `src/grid-events.ts` | 651 | P2 |
| `src/rtc-proxy.ts` | 642 | P2 |

**C++ (core/ and apps/):**
| File | Lines | Priority |
|------|-------|----------|
| `apps/wasm/bindings.cc` | 2402 | P3 - Defer (single WASM boundary file) |
| `core/net/common/SyncClient.cc` | 964 | P3 |
| `core/cells/xlsx_writer_test.cc` | 934 | P4 - Test files OK to be large |
| `apps/cli/converter_test.cc` | 804 | P4 |
| `core/cells/parser_test.cc` | 781 | P4 |
| `core/cells/parser.cc` | 709 | P3 |
| `core/cells/formula_lexer_test.cc` | 701 | P4 |
| `core/cells/crdt.cc` | 695 | P3 |
| `core/cells/xlsx_writer.cc` | 668 | P3 |
| `core/cells/serializer_test.cc` | 644 | P4 |

### Strategy

**Phase 1-3: TypeScript extraction from index.html** (most impactful)
- Extract ~3000 lines of inline JS to TypeScript modules
- All modules get bundled by esbuild
- index.html becomes pure HTML template (<100 lines)

**Phase 4: Split large TypeScript files**
- Target: each file under 500 lines
- Group by responsibility (rendering, events, state, etc.)

**Phase 5: C++ refactoring** (optional, lower priority)
- bindings.cc is large but serves as single WASM boundary - may leave as-is
- Test files can stay large (test organization differs from production code)

---

## Phase 1: Extract index.html - Core Application Class

Extract the main application orchestration to a new TypeScript module.

### Current index.html sections to extract:
- `WasmDataSource` class (lines 99-234) → `src/wasm-data-source.ts`
- Global state and DOM elements (lines 240-417) → `src/app.ts`
- State machine helpers (lines 418-500) → `src/app.ts`
- Utility functions (lines 501-627) → `src/grid-utils.ts`

- [x] 1a: Create `src/wasm-data-source.ts` - extract WasmDataSource class
- [x] 1b: Create `src/grid-utils.ts` - extract coordinate/cell lookup utilities
- [x] 1c: Create `src/app.ts` - extract App class with state and initialization
- [x] 1d: Update `build.mjs` to add `src/app.ts` as entry point (N/A - bundled via client.ts re-export; separate entry point deferred to Phase 3g)
- [x] 1e: Verify build succeeds and app functions correctly

---

## Phase 2: Extract index.html - Feature Modules

Extract feature-specific code to dedicated modules.

### Sections to extract:
- Presence broadcasting (lines 629-820) → `src/presence-broadcast.ts`
- Formula bar editing (lines 824-877) → integrate into app or existing module
- Cell editing (lines 1074-1315) → `src/cell-editor.ts`
- Column header editing (lines 1318-1407) → `src/header-editor.ts`
- Sheet tabs (lines 1408-1594) → `src/sheet-tabs.ts`

- [x] 2a: Create `src/presence-broadcast.ts` - extract presence functions
- [x] 2b: Create `src/cell-editor.ts` - extract cell editing logic
- [x] 2c: Create `src/header-editor.ts` - extract column/row header editing
- [x] 2d: Create `src/sheet-tabs.ts` - extract sheet tab management
- [x] 2e: Verify build succeeds and all editing features work

---

## Phase 3: Extract index.html - Remaining Code

Extract remaining code and reduce index.html to pure HTML.

### Sections to extract:
- Event handlers (lines 1595-2270) → `src/app-events.ts`
- Drag and drop (lines 2272-2557) → integrate with app-events or separate
- File loading (lines 2558-2635) → `src/file-loader.ts`
- File persistence/IndexedDB (lines 2636-2785) → `src/persistence.ts`
- Export dropdown (lines 2787-2850) → integrate into app
- AST debug panel (lines 2851-2884) → `src/ast-debug.ts`
- Initialization (lines 2885-3079) → `src/app.ts` (main entry)

- [x] 3a: Create `src/app-events.ts` - extract all event handlers
- [x] 3b: Create `src/file-loader.ts` - extract file loading logic
- [x] 3c: Create `src/persistence.ts` - extract IndexedDB persistence
- [ ] 3d: Create `src/ast-debug.ts` - extract AST debug panel
- [ ] 3e: Update `src/app.ts` with initialization and connect all modules
- [ ] 3f: Reduce `index.html` to pure HTML template (no inline JS)
- [ ] 3g: Update build.mjs - app.ts becomes the single entry point
- [ ] 3h: Full integration test - verify all features work

---

## Phase 4: Split Large TypeScript Files

After extraction, assess and split any files still over 500 lines.

**Expected splits:**
- `grid-renderer.ts` (1177) → split rendering concerns (base, selection, presence)
- `client.ts` (1040) → may naturally shrink after WasmDataSource extraction
- `worker.ts` (953) → consider splitting CRDT operations from file ops
- `collab-ui.ts` (871) → consider splitting dialog/form from state
- `cpp-sync-adapter.ts` (809) → may be acceptable as is (single concern)

- [ ] 4a: Split `grid-renderer.ts` into smaller modules
- [ ] 4b: Evaluate and split `client.ts` if still large
- [ ] 4c: Evaluate and split `worker.ts` if still large
- [ ] 4d: Evaluate `collab-ui.ts` - split if logical separation exists
- [ ] 4e: Final line count audit - ensure all files under 500 lines
- [ ] 4f: Verify build and all tests pass

---

## Phase 5: C++ Refactoring (Optional)

Lower priority - evaluate after TypeScript work is complete.

**Candidates:**
- `bindings.cc` (2402 lines) - Consider splitting by feature area
- `SyncClient.cc` (964 lines) - Could split transport from protocol
- `crdt.cc` (695 lines) - May be acceptable as single cohesive unit

**Decision:** Defer C++ refactoring. WASM bindings benefit from being in one file
(single compilation unit, simpler debugging). Test files can remain large.

- [ ] 5a: Evaluate if C++ splitting provides meaningful benefit
- [ ] 5b: If yes, create separate plan for C++ refactoring

---

## Architecture After Refactoring

```
apps/wasm/
├── static/
│   └── index.html              # Pure HTML template (<100 lines)
├── src/
│   ├── app.ts                  # Main application class + init (NEW)
│   ├── app-events.ts           # Event handlers (NEW)
│   ├── wasm-data-source.ts     # WASM worker communication (NEW)
│   ├── grid-utils.ts           # Coordinate utilities (NEW)
│   ├── cell-editor.ts          # Cell editing (NEW)
│   ├── header-editor.ts        # Column/row header editing (NEW)
│   ├── sheet-tabs.ts           # Sheet tab management (NEW)
│   ├── presence-broadcast.ts   # Presence broadcasting (NEW)
│   ├── file-loader.ts          # File loading (NEW)
│   ├── persistence.ts          # IndexedDB persistence (NEW)
│   ├── ast-debug.ts            # AST debug panel (NEW)
│   ├── client.ts               # Worker client API (existing, smaller)
│   ├── worker.ts               # Web worker (existing)
│   ├── grid-renderer.ts        # Grid rendering (existing, split)
│   ├── grid-events.ts          # Grid event types (existing)
│   ├── ui-state.ts             # UI state machine (existing)
│   ├── collab-ui.ts            # Collaboration UI (existing)
│   ├── cpp-sync-adapter.ts     # C++ sync adapter (existing)
│   ├── rtc-proxy.ts            # WebRTC proxy (existing)
│   ├── room-url.ts             # Room URL handling (existing)
│   ├── presence.ts             # Presence types (existing)
│   ├── types.ts                # Type definitions (existing)
│   └── utils.ts                # Utility functions (existing)
└── build.mjs                   # esbuild config (updated)
```

## Success Criteria

1. All TypeScript files under 500 lines
2. index.html contains only HTML (no inline JavaScript)
3. All functionality preserved (editing, collaboration, file I/O)
4. Build still works (`npm run build`)
5. Type checking passes (`npm run check-types`)
