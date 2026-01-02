Status: IN_PROGRESS
Created At: 2026-01-02 02:46 UTC
Updated At: 2026-01-02 02:46 UTC
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

# Luau API Improvements

A series of improvements to the Luau scripting API for better usability and consistency.

## Summary of Changes

| Feature | Description |
|---------|-------------|
| Fix `setDocumentTitle` | Currently creates the CRDT operation but result is not visible in UI |
| Add `getDocumentTitle` | New function to retrieve current document title |
| `cell.value = 42` | Support assignment via `__newindex` metamethod |
| Empty cell value → `nil` | `print(getCell('A1').value)` prints `nil` for empty cells (already works, verify) |
| `cell.ref` read-only | Assignment to `cell.ref` should error |
| `cell.formula` read-only | Assignment to `cell.formula` should error |
| 1-based sheet indexing | `getSheet({index = 1})` returns first sheet (Lua convention) |
| `getSheet(1)` shorthand | Accepts number directly without table wrapper |
| `getSheet("name")` shorthand | Accepts string directly without table wrapper |
| `__tostring` for cell/sheet | `print(cell)` → `Cell<A1>`, `print(sheet)` → `Sheet<SheetName>` |
| `cell.dependents` | Array of cells that depend on this cell (first-level only) |

---

## Phase 1: Fix setDocumentTitle

**Issue:** `setDocumentTitle("Test")` creates the CRDT operation (`makeWorkbookRenameOp`) and `applyOperation` is called, but the UI doesn't reflect the change because there's no notification to listeners.

- [x] 1a: Investigate setDocumentTitle - add test to verify workbook.name changes after script execution
- [x] 1b: If CRDT operation works, ensure WASM bindings pick up the name change after script runs (may need to trigger UI refresh)

**Analysis:**
- Current code in `luau_sandbox.cc:446-458` creates and applies the operation correctly
- The CRDT `applyWorkbookRename` in `crdt.cc:863-887` parses the payload and sets `workbook.name`
- Issue is likely that the TypeScript side doesn't poll/refresh the name after script execution
- Solution: After script execution, the WASM layer should emit a change notification

**Files:**
- `core/cells/luau_sandbox_test.cc` - Add test to verify workbook name change
- `apps/wasm/bindings.cc` - Ensure `executeScript` triggers appropriate change notifications
- `apps/wasm/src/script-panel.ts` or `apps/wasm/src/client.ts` - Refresh UI after script execution

---

## Phase 2: Add getDocumentTitle ✅

Add a new function to retrieve the current document title.

- [x] 2a: Implement `luaDocumentGetTitle` in `luau_sandbox.cc`
- [x] 2b: Register `getDocumentTitle` global function in `registerCellsAPI()`
- [x] 2c: Add declaration to `luau_sandbox.h`
- [x] 2d: Add unit tests for `getDocumentTitle`
- [x] 2e: Add type definition (Luau Autocomplete can suggest it)

**Implementation:**
```cpp
int LuauSandbox::luaDocumentGetTitle(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "getDocumentTitle: no context set");
    }
    lua_pushstring(L, workbook->name.c_str());
    return 1;
}
```

**Files:**
- `core/cells/luau_sandbox.h` - Add `luaDocumentGetTitle` declaration
- `core/cells/luau_sandbox.cc` - Implement and register function
- `core/cells/luau_sandbox_test.cc` - Add tests
- `core/cells/luau_autocomplete.cc` - Add type definition

---

## Phase 3: Cell __newindex for value assignment

Enable `getCell('A1').value = 42` to actually set the cell value.

- [ ] 3a: Add `luaCellNewIndex` metamethod to handle property assignment
- [ ] 3b: Handle `value` assignment by calling the existing cell-set logic
- [ ] 3c: Make `ref` assignment error ("ref is read-only")
- [ ] 3d: Make `formula` assignment error ("formula is read-only, use setCell with = prefix")
- [ ] 3e: Register `__newindex` in Cell metatable
- [ ] 3f: Add unit tests for value assignment and read-only property errors

**Implementation:**
```cpp
int LuauSandbox::luaCellNewIndex(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (strcmp(key, "value") == 0) {
        // Get cell UUID, then call setCell logic to apply CRDT operation
    } else if (strcmp(key, "ref") == 0) {
        luaL_error(L, "cell.ref is read-only");
    } else if (strcmp(key, "formula") == 0) {
        luaL_error(L, "cell.formula is read-only (use setCell with = prefix)");
    } else {
        lua_rawset(L, 1);  // Allow custom properties
    }
    return 0;
}
```

**Files:**
- `core/cells/luau_sandbox.h` - Add `luaCellNewIndex` declaration
- `core/cells/luau_sandbox.cc` - Implement `__newindex`, update metatable registration
- `core/cells/luau_sandbox_test.cc` - Add tests

---

## Phase 4: Verify empty cell value prints nil

Verify that `print(getCell('A1').value)` correctly prints `nil` for empty/missing cells.

- [ ] 4a: Add unit test to confirm existing behavior
- [ ] 4b: If needed, adjust `pushCellObject` to push nil for empty value types

**Analysis:**
- Looking at `pushCellObject` lines 1214-1231, the default case already pushes nil
- `CellValueType::EMPTY` should fall through to default and push nil
- Just need to verify with a test

**Files:**
- `core/cells/luau_sandbox_test.cc` - Add test for empty cell value

---

## Phase 5: 1-based sheet indexing and shorthand syntax

Change `getSheet` to use 1-based indexing (Lua convention) and accept shorthand arguments.

- [ ] 5a: Modify `luaGetSheet` to accept number directly: `getSheet(1)` → first sheet
- [ ] 5b: Modify `luaGetSheet` to accept string directly: `getSheet("Sheet1")` → sheet by name
- [ ] 5c: Change index to 1-based: `{index = 1}` → first sheet (was 0-based)
- [ ] 5d: Update `selectSheet(index)` to use 1-based indexing as well
- [ ] 5e: Update all unit tests to use 1-based indexing
- [ ] 5f: Update autocomplete type definitions

**Implementation:**
```cpp
int LuauSandbox::luaGetSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    Sheet* sheet = nullptr;

    if (lua_isnumber(L, 1)) {
        // getSheet(1) - direct number, 1-based
        int index = static_cast<int>(lua_tonumber(L, 1)) - 1;  // Convert to 0-based
        // ... validate and get sheet
    } else if (lua_isstring(L, 1)) {
        // getSheet("Sheet1") - direct name
        const char* name = lua_tostring(L, 1);
        sheet = workbook->getSheetByName(name);
    } else if (lua_istable(L, 1)) {
        // getSheet({index = 1}) or getSheet({name = "Sheet1"})
        // ... existing logic but adjust index to be 1-based
    }
    // ...
}
```

**Files:**
- `core/cells/luau_sandbox.cc` - Modify `luaGetSheet` and `luaSelectSheet`
- `core/cells/luau_sandbox_test.cc` - Update tests to 1-based indexing
- `core/cells/luau_autocomplete.cc` - Update type definitions

---

## Phase 6: __tostring for Cell and Sheet

Implement `__tostring` metamethods so `print(cell)` shows useful info.

- [ ] 6a: Implement `luaCellToString` - returns `"Cell<A1>"` format
- [ ] 6b: Implement `luaSheetToString` - returns `"Sheet<SheetName>"` format
- [ ] 6c: Register `__tostring` in Cell and Sheet metatables
- [ ] 6d: Add unit tests for tostring behavior

**Implementation:**
```cpp
int LuauSandbox::luaCellToString(lua_State* L) {
    // Get cell UUID from table
    lua_getfield(L, 1, "_uuid");
    const char* uuidStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    const ID cellId(uuidStr);
    Cell* cell = sheet->getCell(cellId);
    Axis* col = sheet->getColumn(cell->colId);
    Axis* row = sheet->getRow(cell->rowId);

    std::string ref = RefConverter::columnIndexToLetter(col->position)
                    + std::to_string(row->position + 1);
    std::string result = "Cell<" + ref + ">";
    lua_pushstring(L, result.c_str());
    return 1;
}
```

**Files:**
- `core/cells/luau_sandbox.h` - Add `luaCellToString`, `luaSheetToString` declarations
- `core/cells/luau_sandbox.cc` - Implement and register in metatables
- `core/cells/luau_sandbox_test.cc` - Add tests

---

## Phase 7: cell.dependents property

Add `cell.dependents` property that returns an array of cells that directly depend on this cell.

- [ ] 7a: Implement dependent lookup in `luaCellIndex` when key is `"dependents"`
- [ ] 7b: Use `DependencyGraph::getDependentsForCell()` to get first-level dependents
- [ ] 7c: Convert dependent IDs to cell objects and return as Lua table/array
- [ ] 7d: Return empty table `{}` for non-formula cells or cells with no dependents
- [ ] 7e: Add unit tests for dependents property

**Implementation:**
```cpp
// In luaCellIndex, handle "dependents" key:
if (strcmp(key, "dependents") == 0) {
    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);

    // Get cell position
    Axis* col = sheet->getColumn(cell->colId);
    Axis* row = sheet->getRow(cell->rowId);

    // Query dependency graph
    DependencyGraph* depGraph = sheet->getDependencyGraph();
    std::vector<ID> depIds = depGraph->getDependentsForCell(
        cell->id, col->position, row->position);

    // Build Lua array
    lua_newtable(L);
    int idx = 1;
    for (const ID& depId : depIds) {
        Cell* depCell = sheet->getCell(depId);
        if (depCell != nullptr) {
            pushCellObject(L, depCell);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}
```

**Files:**
- `core/cells/luau_sandbox.cc` - Add `dependents` handling in `luaCellIndex`
- `core/cells/luau_sandbox_test.cc` - Add tests with formula dependencies

---

## API Reference (After Changes)

```lua
-- Document title
setDocumentTitle("Budget 2025")
local title = getDocumentTitle()  -- NEW

-- Cell access (1-based sheet indexing)
local cell = getCell("A1")
cell.value = 42                   -- NEW: assignment via __newindex
print(cell.value)                 -- prints 42 or nil if empty
-- cell.ref = "B1"                -- ERROR: read-only
-- cell.formula = "=A1"           -- ERROR: read-only
print(cell)                       -- NEW: "Cell<A1>"

-- Cell dependencies (NEW)
local deps = cell.dependents      -- {} or {cell1, cell2, ...}
for _, dep in ipairs(deps) do
    print(dep.ref)                -- Print refs of dependent cells
end

-- Sheets (now 1-based indexing)
local sheet = getSheet(1)         -- NEW shorthand (was: getSheet({index = 0}))
local sheet = getSheet("Data")    -- NEW shorthand (was: getSheet({name = "Data"}))
local sheet = getSheet({index = 1})  -- Table form still works (now 1-based)
print(sheet)                      -- NEW: "Sheet<Data>"

selectSheet(1)                    -- 1-based (was 0)
selectSheet("Data")               -- By name (unchanged)
```

---

## Testing Checklist

Each phase should verify:
- [ ] `make test` passes (C++ unit tests)
- [ ] `make lint` passes
- [ ] `make format` passes (or `make format-check`)
- [ ] `cd apps/wasm && npm run test:parallel -- stable` passes (E2E tests)
