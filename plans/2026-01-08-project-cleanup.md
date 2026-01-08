Status: IN_PROGRESS
Created At: 2026-01-08 18:07 UTC
Updated At: 2026-01-08 19:30 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

---

## Overview

Project maturity cleanup to improve documentation quality, ensure code files are not too long (500 lines max for non-test files), and add 10-20 line module documentation headers to all files.

### Key Architecture Updates

The documentation should reflect these architectural truths:

1. **UI: Web-only** - The engine compiles as C++ WASM module, UI is TypeScript only. Native builds are supported for CLI/headless use, but no native UI (SwiftUI, WinUI, etc.) is planned.

2. **Order Statistic Tree** - The OSTree provides O(log n) spatial indexing between UI pixel coordinates and the sparse UUID-based representation. It's the bridge between "show me pixels 500-1000" and "which columns are there?"

3. **UUID-based Source of Truth** - All cells, columns, and rows are identified by UUIDs. The 2D grid view is derived from the sparse UUID representation via the Order Statistic Tree.

4. **CRDT-First Operations** - All workbook mutations MUST go through CRDT operations. This enforces native collaboration (human and AI agents) by design.

5. **C++ Core / TypeScript UI Split**:
   - C++: CRDT operations, networking, file import/export, formula AST parser, Workbook model, dependency graph, Order Statistic Tree, serialization
   - TypeScript: UI rendering, event handling, minimal caching for display purposes only

---

## Files Over 500 Lines (Excluding Tests)

### C++ Core (35 files)

| File | Lines | Action |
|------|-------|--------|
| `apps/wasm/bindings.cc` | 4852 | Split into multiple binding files |
| `core/cells/luau_sandbox.cc` | 1885 | Split by functionality |
| `core/cells/crdt.cc` | 1520 | Split by operation type |
| `core/cells/model.cc` | 1070 | Split Sheet/Workbook |
| `core/cells/formula_eval.cc` | 973 | Keep (formula evaluation is cohesive) |
| `core/net/common/SyncClient.cc` | 964 | Split by transport type |
| `core/cells/number_format.cc` | 869 | Keep (number formatting is cohesive) |
| `core/cells/ref_converter.cc` | 860 | Keep (reference conversion is cohesive) |
| `core/cells/parser.cc` | 825 | Keep (.zcd parser is cohesive) |
| `core/cells/functions/fn_text.cc` | 791 | Keep (text functions are related) |
| `core/cells/fill_range.cc` | 786 | Keep (fill range logic is cohesive) |
| `core/cells/ostree.cc` | 783 | Keep (data structure impl is cohesive) |
| `core/cells/formula_parser.cc` | 775 | Keep (parser is cohesive) |
| `core/cells/xlsx_writer.cc` | 672 | Keep (XLSX writer is cohesive) |
| `core/net/common/Presence.cc` | 628 | Keep |
| `core/cells/functions/fn_datetime.cc` | 607 | Keep |
| `core/cells/dependency_graph.cc` | 606 | Keep |
| `core/cells/sync_manager.cc` | 604 | Keep |
| `core/cells/xlsx_reader.cc` | 599 | Keep |
| `core/cells/formula_lexer.cc` | 583 | Keep |
| `core/cells/input_parser.cc` | 569 | Keep |
| `core/cells/functions/fn_lookup.cc` | 558 | Keep |

### TypeScript UI (15 files)

| File | Lines | Action |
|------|-------|--------|
| `apps/wasm/src/app-events.ts` | 2163 | Split by event type |
| `apps/wasm/src/init.ts` | 1595 | Split by component |
| `apps/wasm/src/worker.ts` | 1535 | Split by functionality |
| `apps/wasm/src/script-panel.ts` | 1035 | Split editor/autocomplete |
| `apps/wasm/src/header-editor.ts` | 963 | Keep (formula bar + column header) |
| `apps/wasm/src/collab-ui.ts` | 960 | Keep |
| `apps/wasm/src/client.ts` | 932 | Keep (WASM client is cohesive) |
| `apps/wasm/src/cell-editor.ts` | 830 | Keep |
| `apps/wasm/src/cpp-sync-adapter.ts` | 815 | Keep |
| `apps/wasm/src/clipboard.ts` | 795 | Keep |
| `apps/wasm/src/format-controls.ts` | 742 | Keep |
| `apps/wasm/src/grid-events.ts` | 729 | Keep |
| `apps/wasm/src/ui-state.ts` | 691 | Keep |
| `apps/wasm/src/rtc-proxy.ts` | 642 | Keep |
| `apps/wasm/src/file-loader.ts` | 580 | Keep |

**Note:** `cells.d.ts` files are auto-generated, not manually maintained.

---

## Phase 1: Documentation Cleanup

Update README.md and docs/ to reflect current architecture accurately.

- [x] 1a: Update README.md architecture diagram (remove native UI mentions, add OSTree layer)
- [x] 1b: Update README.md "Core Components" section (clarify CRDT-first, UUID source of truth)
- [x] 1c: Update README.md "Current Implementation Status" (remove "Not Yet Implemented" for native apps)
- [x] 1d: Update README.md "Design Decisions" (clarify C++ vs TS responsibilities)
- [x] 1e: Update docs/cross-platform.md (native builds for CLI only, no native UI planned)
- [x] 1f: Update docs/data-model.md (add OSTree explanation, clarify UUID source of truth)
- [x] 1g: Update docs/rendering.md (remove native backend mentions, focus on Canvas2D)
- [x] 1h: Review and update docs/crdt.md (clarify all ops must go through CRDT)

## Phase 2: Add File Headers to C++ Core

Add 10-20 line documentation headers to all C++ files explaining what the module does.

### Header Template

```cpp
// =============================================================================
// Module Name
// =============================================================================
//
// Brief description of what this module does and its role in the system.
//
// Key responsibilities:
// - Responsibility 1
// - Responsibility 2
//
// Dependencies:
// - What this module depends on
//
// Used by:
// - What uses this module
//
// =============================================================================
```

- [ ] 2a: Add headers to core/cells/*.h (model, types, id, hlc, operation, oplog)
- [ ] 2b: Add headers to core/cells/*.h (crdt, sync_manager, dependency_graph)
- [ ] 2c: Add headers to core/cells/*.h (formula_*, ref_converter)
- [ ] 2d: Add headers to core/cells/*.h (ostree, axis_index, viewport_index)
- [ ] 2e: Add headers to core/cells/*.h (parser, serializer, xlsx_*, csv_*)
- [ ] 2f: Add headers to core/cells/*.h (number_format, format_code_*, input_parser)
- [ ] 2g: Add headers to core/cells/*.h (luau_*, agent_client, fill_range)
- [ ] 2h: Add headers to core/cells/functions/*.h (fn_*)

## Phase 3: Add File Headers to TypeScript UI

Add 10-20 line documentation headers to all TypeScript files.

### Header Template

```typescript
// =============================================================================
// Module Name
// =============================================================================
//
// Brief description of what this module does.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge.
//
// Key responsibilities:
// - Responsibility 1
// - Responsibility 2
//
// =============================================================================
```

- [ ] 3a: Add headers to apps/wasm/src/*.ts (client, worker, wasm-data-source)
- [ ] 3b: Add headers to apps/wasm/src/*.ts (init, app, app-events)
- [ ] 3c: Add headers to apps/wasm/src/*.ts (grid-renderer, grid-events, grid-utils)
- [ ] 3d: Add headers to apps/wasm/src/*.ts (cell-editor, header-editor, sheet-tabs)
- [ ] 3e: Add headers to apps/wasm/src/*.ts (ui-state, clipboard, format-controls)
- [ ] 3f: Add headers to apps/wasm/src/*.ts (collab-ui, cpp-sync-adapter, rtc-proxy)
- [ ] 3g: Add headers to apps/wasm/src/*.ts (script-panel, agent-panel, ast-debug)
- [ ] 3h: Add headers to apps/wasm/src/*.ts (file-loader, room-url, presence-broadcast)

## Phase 4: Split Large C++ Files

Split files that are significantly over 500 lines and have clear separation boundaries.

### 4.1: bindings.cc (4852 lines → 8 files)

- [ ] 4.1a: Extract `bindings_core.cc` (Workbook, Sheet, Cell types and basic ops)
- [ ] 4.1b: Extract `bindings_formula.cc` (formula parsing, eval, display)
- [ ] 4.1c: Extract `bindings_crdt.cc` (CRDT operations, OpLog, SyncManager)
- [ ] 4.1d: Extract `bindings_viewport.cc` (ViewportIndex, spatial queries)
- [ ] 4.1e: Extract `bindings_file.cc` (XLSX/CSV/ZCD import/export)
- [ ] 4.1f: Extract `bindings_format.cc` (number formatting, input parsing)
- [ ] 4.1g: Extract `bindings_luau.cc` (Luau sandbox, autocomplete)
- [ ] 4.1h: Keep `bindings.cc` as main entry with module registration

### 4.2: luau_sandbox.cc (1885 lines → 3 files)

- [ ] 4.2a: Extract `luau_api.cc` (API functions exposed to Luau scripts)
- [ ] 4.2b: Extract `luau_types.cc` (type coercion, Cell/Sheet wrappers)
- [ ] 4.2c: Keep `luau_sandbox.cc` as main sandbox (VM lifecycle, execution)

### 4.3: crdt.cc (1520 lines → 3 files)

- [ ] 4.3a: Extract `crdt_cell.cc` (cell operations: set, clear, format)
- [ ] 4.3b: Extract `crdt_axis.cc` (axis operations: insert, delete, move, resize)
- [ ] 4.3c: Keep `crdt.cc` as main entry (applyOperation dispatch, helpers)

### 4.4: model.cc (1070 lines → 2 files)

- [ ] 4.4a: Extract `sheet.cc` (Sheet class implementation)
- [ ] 4.4b: Keep `model.cc` for Workbook (Workbook owns Sheets)

## Phase 5: Split Large TypeScript Files

### 5.1: app-events.ts (2163 lines → 5 files)

- [ ] 5.1a: Extract `mouse-events.ts` (canvas mouse handlers)
- [ ] 5.1b: Extract `keyboard-events.ts` (keyboard navigation, shortcuts)
- [ ] 5.1c: Extract `resize-events.ts` (column/row resize handling)
- [ ] 5.1d: Extract `drag-events.ts` (column/row drag-and-drop)
- [ ] 5.1e: Keep `app-events.ts` as coordinator (event manager setup)

### 5.2: init.ts (1595 lines → 4 files)

- [ ] 5.2a: Extract `init-components.ts` (component creation/wiring)
- [ ] 5.2b: Extract `init-listeners.ts` (data change listeners setup)
- [ ] 5.2c: Extract `init-collab.ts` (collaboration setup)
- [ ] 5.2d: Keep `init.ts` as main entry (createAppFromWorker, high-level flow)

### 5.3: worker.ts (1535 lines → 3 files)

- [ ] 5.3a: Extract `worker-handlers.ts` (message handlers for each operation)
- [ ] 5.3b: Extract `worker-collab.ts` (collaboration/sync handling in worker)
- [ ] 5.3c: Keep `worker.ts` as main entry (message routing, WASM loading)

### 5.4: script-panel.ts (1035 lines → 2 files)

- [ ] 5.4a: Extract `script-autocomplete.ts` (autocomplete UI and logic)
- [ ] 5.4b: Keep `script-panel.ts` (editor panel, syntax highlighting)

## Phase 6: Final Review

- [ ] 6a: Run `make check` to verify all tests pass
- [ ] 6b: Update scripts/generate-stats.sh if line counts changed significantly
- [ ] 6c: Review all modified files for consistency
- [ ] 6d: Update GETTING_STARTED.md if needed

---

## Success Criteria

1. All documentation accurately reflects current architecture
2. All non-test code files are under 500 lines (or have documented exceptions)
3. Every C++ header (.h) has 10-20 line module documentation
4. Every TypeScript file has 10-20 line module documentation
5. All tests pass (`make check`)

## Estimated Scope

- **Phase 1**: 8 subtasks (documentation)
- **Phase 2**: 8 subtasks (C++ headers)
- **Phase 3**: 8 subtasks (TypeScript headers)
- **Phase 4**: 14 subtasks (C++ file splits)
- **Phase 5**: 11 subtasks (TypeScript file splits)
- **Phase 6**: 4 subtasks (final review)

**Total**: 53 subtasks across 6 phases
