Status: IN-PROGRESS
Created At: 2025-12-30 22:40 UTC
Updated At: 2025-12-31 03:05 UTC
Following plan management guidelines defined in AGENTS.md

# Luau Scripting Integration

Integrate Luau (typed/optimized Lua from Roblox) as a sandboxed scripting engine. Users can execute scripts via `/` prefix in the formula bar. The API also supports multi-line scripts for future AI agent use.

## Phase 1: Luau Library Integration

- [x] 1a: Add Luau git_repository to MODULE.bazel
- [x] 1b: Create third_party/luau/BUILD.luau with cc_library targets
- [x] 1c: Verify WASM compilation with `bazel build --config=wasm //third_party/luau:luau`

**Files:**
- `MODULE.bazel` - Add git_repository for https://github.com/luau-lang/luau
- `third_party/luau/BUILD.luau` (new) - Define luau_vm, luau_compiler, luau_ast targets

## Phase 2: Luau Sandbox Core

- [x] 2a: Create luau_sandbox.h with LuauSandbox class interface
- [x] 2b: Implement sandbox initialization with luaL_sandbox
- [x] 2c: Add instruction limit/timeout via interrupt callback
- [x] 2d: Add BUILD target linking @luau and add unit tests

**Files:**
- `core/cells/luau_sandbox.h` (new)
- `core/cells/luau_sandbox.cc` (new)
- `core/cells/luau_sandbox_test.cc` (new)
- `core/cells/BUILD` - Add luau_sandbox cc_library
- `scripts/lint.sh` - Add Luau include paths for clang-tidy

## Phase 3: Cells API Functions

Expose spreadsheet operations to Luau scripts. All use A1 notation (converted via RefConverter).

- [x] 3a: Implement cellGet(ref) and cellSet(ref, value)
- [x] 3b: Implement documentSetTitle(title)
- [x] 3c: Implement columnSetWidth(col, width) and rowSetHeight(row, height)
- [x] 3d: Implement sheetSelect(index), sheetSetName(index, name), sheetGetName(index)
- [x] 3e: Implement rangeSelect(start, end) and rangeDelete(start, end)
- [x] 3f: Implement columnMove(fromCol, toPos)
- [x] 3g: Add unit tests for all API functions

**Files:**
- `core/cells/luau_sandbox.cc` - Add API function implementations
- `core/cells/luau_sandbox_test.cc` - Add API tests
- `core/cells/BUILD` - Add dependencies for luau_sandbox

## Phase 4: WASM Bindings

- [ ] 4a: Add LuauSandbox member to CellsEngine class
- [ ] 4b: Implement executeScript(script) method in bindings.cc
- [ ] 4c: Register executeScript in EMSCRIPTEN_BINDINGS
- [ ] 4d: Add WASM deps and verify build

**Files:**
- `apps/wasm/bindings.cc` - Add _luauSandbox member and executeScript method
- `apps/wasm/BUILD` - Add //core/cells:luau_sandbox to deps
- `apps/wasm/cells.d.ts` - Add executeScript type definition

## Phase 5: TypeScript Client Layer

- [ ] 5a: Add executeScript handler to worker.ts
- [ ] 5b: Add executeScript method to client.ts with ScriptResult type
- [ ] 5c: Add executeScript to WasmDataSource

**Files:**
- `apps/wasm/src/worker.ts` - Add message handler
- `apps/wasm/src/client.ts` - Add client method
- `apps/wasm/src/client-types.ts` - Add ScriptResult type
- `apps/wasm/src/wasm-data-source.ts` - Add wrapper method

## Phase 6: Formula Bar Integration

- [ ] 6a: Add isScriptMode() to header-editor.ts (detects `/` prefix)
- [ ] 6b: Modify commitFormulaBarEdit() to execute scripts instead of cell update
- [ ] 6c: Add visual feedback for script mode and execution results
- [ ] 6d: Add E2E tests for script execution via formula bar

**Files:**
- `apps/wasm/src/header-editor.ts` - Script mode detection and execution
- `apps/wasm/tests/script.test.mjs` (new) - E2E tests

---

## API Reference

Scripts use A1 notation exclusively. Internal details (UUIDs) are never exposed.

### Cell Object Identity (Critical)

`cellGet` returns a **cell object** (Lua table), not a raw value. Object identity
allows comparing if two references point to the same cell:

```lua
a = cellGet("A1")
b = cellGet("A1")
-- a == b (same cell, same object)

c = cellGet("A2")
-- a ~= c (different cells)
```

**A1 resolves at call time.** If a cell moves, the reference tracks it:
```lua
a = cellGet("A1")      -- Gets the cell currently at A1
columnMove("A", 1)     -- Column A moves to position 1 (now B)
a:getRef()             -- Returns "B1" (cell moved with its column)
b = cellGet("A1")      -- Gets whatever cell is now at A1
-- a ~= b (different cells, a moved to B1)
```

**Internal note:** Sandbox uses weak table keyed by cell UUID internally,
but UUIDs are never exposed to scripts. Scripts only see A1 notation.

### Functions

Prefer typed single-parameter options tables over multiple positional arguments.

```lua
-- Cell access
cellGet("A1")                          -- Returns cell object or nil if empty
cellGet("A1", {create = true})         -- Creates cell if empty, never returns nil
cellSet("A1", 100)                     -- Set cell value (creates if needed)

-- Cell object structure (returned by cellGet)
type Cell = {
    value: any,        -- number | string | boolean | nil
    formula: string?,  -- formula text if cell has formula
    getRef: () -> string,  -- Returns current A1 position (e.g., "B1")
}

-- Document
documentSetTitle("Budget")

-- Structure (use options table for multiple params)
columnSetWidth("A", {width = 150})
rowSetHeight(1, {height = 30})
columnMove("A", {to = 2})              -- Move column A to position 2

-- Sheets
sheetSelect(1)
sheetSetName(0, {name = "Data"})
sheetGetName(0)                        -- Returns string

-- Ranges (options table for range bounds)
rangeSelect({from = "A1", to = "C3"})
rangeDelete({from = "A1", to = "C3"})
```

## Security

- No file I/O, network, or process spawning
- Instruction limit (1M instructions default)
- Only whitelisted API functions available
- All mutations via CRDT operations (collaborative-safe)

## Key Dependencies

- `core/cells/ref_converter.h` - A1 to UUID conversion (already exists)
- `core/cells/crdt.h` - makeCellSetValueOp, applyOperation
- `apps/wasm/bindings.cc` - CellsEngine class pattern

## Implementation Notes

### Cell Object Cache (Internal)

The sandbox maintains a weak table keyed by internal cell UUID to ensure object identity.
UUIDs are never exposed to scripts - only used internally for cache lookup.

```cpp
// C++ side: resolve A1 to UUID, check cache, return or create cell object
// Lua side: scripts only see Cell table with value, formula, getRef()
```

This ensures:
1. Same cell → same Lua table reference (object identity)
2. Garbage collection works (weak refs don't prevent collection)
3. A1 resolution happens at call time (reflects current grid state)
4. `cell:getRef()` always returns current position (recomputed from UUID)
