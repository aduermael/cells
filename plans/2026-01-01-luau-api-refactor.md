Status: IN_PROGRESS
Created At: 2026-01-01 21:04 UTC
Updated At: 2026-01-01 21:32 UTC
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

# Luau API Refactor

Refactor the Luau scripting API for better naming conventions, add missing features, and investigate autocomplete support.

## Summary of Changes

### Naming Convention Changes
| Old Name | New Name |
|----------|----------|
| `cellGet(ref)` | `getCell(ref)` |
| `cellSet(ref, value)` | `setCell(ref, value)` |
| `documentSetTitle(title)` | `setDocumentTitle(title)` |
| `columnSetWidth(col, opts)` | `setColumnWidth(col, opts)` |
| `rowSetHeight(row, opts)` | `setRowHeight(row, opts)` |
| `columnMove(col, opts)` | `moveColumn(col, opts)` |
| `sheetSelect(index)` | `selectSheet(sheet)` - now accepts name, index, or sheet object |
| `sheetSetName(index, opts)` | `getSheet(...).name = "new"` (property) |
| `sheetGetName(index)` | `getSheet(...).name` (property) |
| `rangeSelect(opts)` | `selectRange(range)` |
| `rangeDelete(opts)` | `deleteRange(range)` |
| `rangeFill(opts)` | `fillRange(opts)` |
| `cell:getRef()` | `cell.ref` (property via `__index`) |

### New Functions
| Function | Description |
|----------|-------------|
| `getSheet({name = "..."})` | Get sheet object by name |
| `getSheet({index = N})` | Get sheet object by 0-based index |
| `addSheet(name?)` | Create new sheet, optionally with name |

### Behavior Changes
- `setCell('C2', '=B2')` now sets a formula, not the literal string '=B2'
- `selectSheet` accepts sheet object, name string, or index number

---

## Phase 1: Rename Simple Functions (C++)

Rename functions that don't require structural changes.

- [x] 1a: Rename `cellGet` → `getCell`, `cellSet` → `setCell`
- [x] 1b: Rename `documentSetTitle` → `setDocumentTitle`
- [x] 1c: Rename `columnSetWidth` → `setColumnWidth`, `rowSetHeight` → `setRowHeight`
- [x] 1d: Rename `columnMove` → `moveColumn`
- [x] 1e: Rename `rangeSelect` → `selectRange`, `rangeDelete` → `deleteRange`, `rangeFill` → `fillRange`
- [x] 1f: Update all unit tests to use new names

**Files:**
- `core/cells/luau_sandbox.cc` - Rename functions in `registerCellsAPI()`
- `core/cells/luau_sandbox_test.cc` - Update test scripts

---

## Phase 2: Cell Object `ref` Property

Change `cell:getRef()` method to `cell.ref` property using Lua metatables.

- [x] 2a: Create Cell metatable with `__index` metamethod
- [x] 2b: Implement `__index` to handle `.ref` property (calls `luaCellGetRef` internally)
- [x] 2c: Update `pushCellObject` to use metatable instead of direct method assignment
- [x] 2d: Update unit tests to use `cell.ref` instead of `cell:getRef()`

**Files:**
- `core/cells/luau_sandbox.cc` - Add metatable setup, modify `pushCellObject`
- `core/cells/luau_sandbox_test.cc` - Update tests

---

## Phase 3: Sheet Object API

Replace `sheetSelect`, `sheetSetName`, `sheetGetName` with sheet object API.

- [x] 3a: Create Sheet metatable with `__index`/`__newindex` for `.name` property
- [x] 3b: Implement `getSheet({name = ...})` and `getSheet({index = ...})`
- [x] 3c: Implement `selectSheet(sheet|name|index)` - accepts sheet object, string name, or number index
- [x] 3d: Implement `addSheet(name?)` - creates new sheet
- [x] 3e: Remove old `sheetSetName`, `sheetGetName`, `sheetSelect` functions
- [x] 3f: Update unit tests for new sheet API

**Files:**
- `core/cells/luau_sandbox.h` - Add Sheet object helper declarations
- `core/cells/luau_sandbox.cc` - Implement sheet object, `getSheet`, `selectSheet`, `addSheet`
- `core/cells/luau_sandbox_test.cc` - Update tests

---

## Phase 4: Formula Detection in setCell

Make `setCell('A1', '=B1+1')` parse as formula instead of literal string.

- [x] 4a: In `luaCellSet`, detect strings starting with `=` and parse as formula
- [x] 4b: Use existing formula parsing infrastructure (RefConverter, FormulaParser)
- [x] 4c: Generate formula payload for formula strings, `makeCellSetValueOp` for literals
- [x] 4d: Add unit tests for formula detection (`setCell("A1", "=B1")` vs `setCell("A1", "hello")`)

**Files:**
- `core/cells/luau_sandbox.cc` - Modify `luaCellSet` to detect and handle formulas
- `core/cells/luau_sandbox_test.cc` - Add formula tests

---

## Phase 5: Autocomplete Investigation

Research and prototype autocomplete support using Luau's Analysis library.

- [ ] 5a: Add Luau Analysis library to `third_party/luau/BUILD.luau`
- [ ] 5b: Create `luau_autocomplete.h/cc` wrapper exposing `getCompletions(source, position)`
- [ ] 5c: Add WASM binding for `getCompletions`
- [ ] 5d: Add TypeScript types and client method for autocomplete
- [ ] 5e: Basic integration in script editor (show popup on Ctrl+Space or after `.`)

**Note:** This phase may require significant research. Luau's Autocomplete API requires:
- Setting up a `Frontend` with proper configuration
- Defining type definitions for our API (e.g., `getCell` returns `Cell` type)
- Handling asynchronous completion requests

**Resources:**
- [Luau GitHub - Autocomplete.test.cpp](https://github.com/luau-lang/luau/blob/master/tests/Autocomplete.test.cpp)
- [Luau Analysis library](https://github.com/luau-lang/luau/tree/master/Analysis)

**Files:**
- `third_party/luau/BUILD.luau` - Add luau_analysis target
- `core/cells/luau_autocomplete.h` (new) - Autocomplete wrapper
- `core/cells/luau_autocomplete.cc` (new) - Implementation
- `core/cells/luau_autocomplete_test.cc` (new) - Tests
- `apps/wasm/bindings.cc` - Add `getCompletions` binding
- `apps/wasm/cells.d.ts` - Add TypeScript types
- `apps/wasm/src/client.ts` - Add client method
- `apps/wasm/src/script-panel.ts` - UI integration

---

## API Reference (After Refactor)

```lua
-- Cell access
local cell = getCell("A1")              -- Returns cell object or nil
local cell = getCell("A1", {create = true})  -- Creates if needed
setCell("A1", 100)                      -- Set number
setCell("A1", "hello")                  -- Set string
setCell("A1", "=B1+C1")                 -- Set formula (NEW: detected by = prefix)
setCell("A1", nil)                      -- Clear cell

-- Cell object
cell.value                              -- number | string | boolean | nil
cell.formula                            -- formula string or nil
cell.ref                                -- "A1" (current position, was getRef())

-- Document
setDocumentTitle("Budget 2025")

-- Structure
setColumnWidth("A", {width = 150})
setRowHeight(1, {height = 30})
moveColumn("A", {to = 2})

-- Sheets
local sheet = getSheet({index = 0})     -- By index
local sheet = getSheet({name = "Data"}) -- By name
sheet.name                              -- Get name
sheet.name = "New Name"                 -- Set name
selectSheet(sheet)                      -- By sheet object
selectSheet("Data")                     -- By name
selectSheet(0)                          -- By index
addSheet()                              -- New sheet with default name
addSheet("Summary")                     -- New sheet with name

-- Ranges
selectRange("A1:C3")                    -- Or selectRange({from="A1", to="C3"})
deleteRange("A1:C3")                    -- Or deleteRange({from="A1", to="C3"})
fillRange({from = "A1", to = "A1:A10"}) -- Fill pattern
```

---

## Implementation Notes

### Metatable Pattern for Properties

```cpp
// Create metatable once during init
lua_newtable(L);  // metatable

// __index for property reads
lua_pushcfunction(L, &luaCellIndex, "Cell.__index");
lua_setfield(L, -2, "__index");

// For sheet.name = "x", also need __newindex
lua_pushcfunction(L, &luaSheetNewIndex, "Sheet.__newindex");
lua_setfield(L, -2, "__newindex");

// Store metatable in registry for reuse
cellMetatableRef_ = lua_ref(L, -1);
```

### Formula Detection in setCell

```cpp
if (lua_isstring(L, 2)) {
    const char* str = lua_tostring(L, 2);
    if (str[0] == '=') {
        // Parse as formula using RefConverter
        RefConverter conv;
        conv.setContext(*sheet);
        std::string uuidFormula = conv.formulaToUuid(str);
        // Use makeCellSetFormulaOp
    } else {
        // Use makeCellSetValueOp with type "s"
    }
}
```
