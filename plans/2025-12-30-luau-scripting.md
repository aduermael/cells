Status: IN-PROGRESS
Created At: 2025-12-30 22:40 UTC
Updated At: 2025-12-30 22:53 UTC
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

- [ ] 2a: Create luau_sandbox.h with LuauSandbox class interface
- [ ] 2b: Implement sandbox initialization with luaL_sandbox
- [ ] 2c: Add instruction limit/timeout via interrupt callback
- [ ] 2d: Add BUILD target linking @luau and add unit tests

**Files:**
- `core/cells/luau_sandbox.h` (new)
- `core/cells/luau_sandbox.cc` (new)
- `core/cells/luau_sandbox_test.cc` (new)
- `core/cells/BUILD` - Add luau_sandbox cc_library

## Phase 3: Cells API Functions

Expose spreadsheet operations to Luau scripts. All use A1 notation (converted via RefConverter).

- [ ] 3a: Implement cellGet(ref) and cellSet(ref, value)
- [ ] 3b: Implement documentSetTitle(title)
- [ ] 3c: Implement columnSetWidth(col, width) and rowSetHeight(row, height)
- [ ] 3d: Implement sheetSelect(index), sheetSetName(index, name), sheetGetName(index)
- [ ] 3e: Implement rangeSelect(start, end) and rangeDelete(start, end)
- [ ] 3f: Implement columnMove(fromCol, toPos)
- [ ] 3g: Add unit tests for all API functions

**Files:**
- `core/cells/luau_sandbox.cc` - Add API function implementations
- `core/cells/luau_sandbox_test.cc` - Add API tests

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

Scripts use A1 notation for all cell/range references:

```lua
-- Cell access
cellGet("A1")              -- Returns value (number, string, boolean, or nil)
cellSet("A1", 100)         -- Set cell value

-- Document
documentSetTitle("Budget") -- Set workbook title

-- Structure
columnSetWidth("A", 150)   -- Set column width in pixels
rowSetHeight(1, 30)        -- Set row height in pixels
columnMove("B", 0)         -- Move column B to position 0 (before A)

-- Sheets
sheetSelect(1)             -- Switch to sheet at index 1
sheetSetName(0, "Data")    -- Rename sheet 0
sheetGetName(0)            -- Get sheet name

-- Ranges
rangeSelect("A1", "C3")    -- Select range (UI feedback)
rangeDelete("A1", "C3")    -- Delete cells in range
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
