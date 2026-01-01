#include "core/cells/luau_sandbox.h"

#include <cstdlib>
#include <cstring>

#include "core/cells/crdt.h"
#include "core/cells/fill_range.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/ref_converter.h"

#include "Luau/Compiler.h"  // NOLINT(build/include_subdir)
#include "lua.h"            // NOLINT(build/include_subdir)
#include "lualib.h"         // NOLINT(build/include_subdir)

namespace cells {

// Registry keys for storing context
static const char* kRegistryWorkbook = "cells_workbook";
static const char* kRegistrySheet = "cells_sheet";
static const char* kRegistrySandbox = "cells_sandbox";

// Helper to get sandbox instance from Lua state
// Uses the main thread's registry (registry is shared across threads)
static LuauSandbox* getSandbox(lua_State* L) {
    // Get the main thread from global state
    lua_State* mainThread = lua_mainthread(L);
    lua_getfield(mainThread, LUA_REGISTRYINDEX, kRegistrySandbox);
    auto* sandbox = static_cast<LuauSandbox*>(lua_touserdata(mainThread, -1));
    lua_pop(mainThread, 1);
    return sandbox;
}

// Interrupt callback - checks instruction count
void LuauSandbox::interruptCallback(lua_State* L, int gc) {
    // gc >= 0 means this is a GC callback, skip
    if (gc >= 0) {
        return;
    }

    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        return;
    }

    sandbox->instructionCount_++;

    if (sandbox->instructionCount_ >= sandbox->config_.maxInstructions) {
        sandbox->interrupted_ = true;
        // Clear interrupt to prevent further calls
        lua_callbacks(L)->interrupt = nullptr;
        luaL_error(L, "Script exceeded instruction limit (%lld instructions)",
                   static_cast<long long>(sandbox->config_.maxInstructions));
    }
}

LuauSandbox::LuauSandbox() : LuauSandbox(SandboxConfig{}) {}

LuauSandbox::LuauSandbox(const SandboxConfig& config) : config_(config) {
    initState();
}

LuauSandbox::~LuauSandbox() {
    if (L_ != nullptr) {
        lua_close(L_);
        L_ = nullptr;
    }
}

LuauSandbox::LuauSandbox(LuauSandbox&& other) noexcept
    : L_(other.L_),
      config_(other.config_),
      workbook_(other.workbook_),
      sheet_(other.sheet_),
      instructionCount_(other.instructionCount_),
      interrupted_(other.interrupted_) {
    other.L_ = nullptr;
    other.workbook_ = nullptr;
    other.sheet_ = nullptr;

    // Update registry pointer to this instance
    if (L_ != nullptr) {
        lua_pushlightuserdata(L_, this);
        lua_setfield(L_, LUA_REGISTRYINDEX, kRegistrySandbox);
    }
}

LuauSandbox& LuauSandbox::operator=(LuauSandbox&& other) noexcept {
    if (this != &other) {
        if (L_ != nullptr) {
            lua_close(L_);
        }

        L_ = other.L_;
        config_ = other.config_;
        workbook_ = other.workbook_;
        sheet_ = other.sheet_;
        instructionCount_ = other.instructionCount_;
        interrupted_ = other.interrupted_;

        other.L_ = nullptr;
        other.workbook_ = nullptr;
        other.sheet_ = nullptr;

        // Update registry pointer to this instance
        if (L_ != nullptr) {
            lua_pushlightuserdata(L_, this);
            lua_setfield(L_, LUA_REGISTRYINDEX, kRegistrySandbox);
        }
    }
    return *this;
}

void LuauSandbox::initState() {
    // Create new Lua state
    L_ = luaL_newstate();
    if (L_ == nullptr) {
        return;
    }

    // Open safe libraries (no io, os, package)
    luaL_openlibs(L_);

    // Store sandbox pointer in registry
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, kRegistrySandbox);

    // Register cells API
    registerCellsAPI();

    // Apply sandbox restrictions
    // This makes globals and builtins read-only
    luaL_sandbox(L_);
}

// ============================================================================
// Helper functions for API implementations
// ============================================================================

Workbook* LuauSandbox::getWorkbook(lua_State* L) {
    lua_State* mainThread = lua_mainthread(L);
    lua_getfield(mainThread, LUA_REGISTRYINDEX, kRegistryWorkbook);
    auto* wb = static_cast<Workbook*>(lua_touserdata(mainThread, -1));
    lua_pop(mainThread, 1);
    return wb;
}

Sheet* LuauSandbox::getSheet(lua_State* L) {
    lua_State* mainThread = lua_mainthread(L);
    lua_getfield(mainThread, LUA_REGISTRYINDEX, kRegistrySheet);
    auto* sheet = static_cast<Sheet*>(lua_touserdata(mainThread, -1));
    lua_pop(mainThread, 1);
    return sheet;
}

// Helper: Escape a string for JSON
static std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

// Helper: Parse column letter (A, B, ..., AA, ...) to 0-based index
static int parseColumnLetter(const char* ref, size_t* endPos) {
    int col = 0;
    size_t pos = 0;

    // Skip leading $
    if (ref[pos] == '$') {
        pos++;
    }

    while (ref[pos] != '\0' &&
           ((ref[pos] >= 'A' && ref[pos] <= 'Z') || (ref[pos] >= 'a' && ref[pos] <= 'z'))) {
        const char c = ref[pos];
        const int digit = (c >= 'A' && c <= 'Z') ? (c - 'A') : (c - 'a');
        col = col * 26 + digit + 1;
        pos++;
    }

    if (endPos != nullptr) {
        *endPos = pos;
    }
    return col - 1;  // Convert to 0-based
}

// Helper: Parse an A1 reference string
// Returns true if valid, sets colIdx and rowIdx (0-based)
static bool parseA1Ref(const char* ref, int* colIdx, int* rowIdx) {
    if (ref == nullptr || ref[0] == '\0') {
        return false;
    }

    size_t pos = 0;

    // Skip leading $
    if (ref[pos] == '$') {
        pos++;
    }

    // Parse column letters
    int col = 0;
    while (ref[pos] != '\0' &&
           ((ref[pos] >= 'A' && ref[pos] <= 'Z') || (ref[pos] >= 'a' && ref[pos] <= 'z'))) {
        const char c = ref[pos];
        const int digit = (c >= 'A' && c <= 'Z') ? (c - 'A') : (c - 'a');
        col = col * 26 + digit + 1;
        pos++;
    }

    if (col == 0) {
        return false;  // No column letter found
    }

    // Skip $ before row
    if (ref[pos] == '$') {
        pos++;
    }

    // Parse row number (1-based in A1 notation)
    int row = 0;
    while (ref[pos] >= '0' && ref[pos] <= '9') {
        row = row * 10 + (ref[pos] - '0');
        pos++;
    }

    if (row == 0) {
        return false;  // No row number found
    }

    *colIdx = col - 1;  // Convert to 0-based
    *rowIdx = row - 1;  // Convert to 0-based
    return true;
}

// Helper: Parse an A1 range string like "A1:B2" or "A1" (single cell)
// Returns true if valid, sets fromCol, fromRow, toCol, toRow (all 0-based)
static bool parseA1Range(const char* range, int* fromCol, int* fromRow, int* toCol, int* toRow) {
    if (range == nullptr || range[0] == '\0') {
        return false;
    }

    // Find the colon separator
    const char* colon = strchr(range, ':');

    if (colon == nullptr) {
        // Single cell reference (e.g., "A1")
        if (!parseA1Ref(range, fromCol, fromRow)) {
            return false;
        }
        *toCol = *fromCol;
        *toRow = *fromRow;
        return true;
    }

    // Range reference (e.g., "A1:B2")
    // Parse the first part
    const std::string firstPart(range, colon - range);
    if (!parseA1Ref(firstPart.c_str(), fromCol, fromRow)) {
        return false;
    }

    // Parse the second part (after the colon)
    if (!parseA1Ref(colon + 1, toCol, toRow)) {
        return false;
    }

    return true;
}

// ============================================================================
// Cells API: getCell(ref, options?)
// Returns cell object or nil if empty
// ============================================================================
int LuauSandbox::luaCellGet(lua_State* L) {
    // Get the ref argument
    const char* ref = luaL_checkstring(L, 1);

    // Parse options (optional second argument)
    bool createIfEmpty = false;
    if (lua_istable(L, 2) != 0) {
        lua_getfield(L, 2, "create");
        if (lua_isboolean(L, -1) != 0) {
            createIfEmpty = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }

    // Get context
    Sheet* sheet = getSheet(L);
    if (sheet == nullptr || getWorkbook(L) == nullptr) {
        luaL_error(L, "getCell: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "getCell: invalid reference '%s'", ref);
    }

    // Get or create the column and row
    // NOLINTBEGIN(misc-const-correctness)
    Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));

    // If column or row doesn't exist and we're not creating, return nil
    if ((col == nullptr || row == nullptr) && !createIfEmpty) {
        lua_pushnil(L);
        return 1;
    }

    // Create column/row if needed
    if (col == nullptr) {
        col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colIdx));
    }
    if (row == nullptr) {
        row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(rowIdx));
    }

    // Get the cell
    Cell* cell = sheet->getCellAt(col->id, row->id);

    // If cell doesn't exist and we're not creating, return nil
    if (cell == nullptr && !createIfEmpty) {
        lua_pushnil(L);
        return 1;
    }

    // Create cell if needed
    if (cell == nullptr) {
        cell = sheet->getOrCreateCellAt(col->id, row->id);
    }
    // NOLINTEND(misc-const-correctness)

    // Get sandbox to push cell object
    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "getCell: sandbox not found");
    }

    sandbox->pushCellObject(L, cell);
    return 1;
}

// ============================================================================
// Cells API: setCell(ref, value)
// Sets cell value (creates cell if needed)
// ============================================================================
int LuauSandbox::luaCellSet(lua_State* L) {
    // Get the ref argument
    const char* ref = luaL_checkstring(L, 1);

    // Get context
    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setCell: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "setCell: invalid reference '%s'", ref);
    }

    // Get or create the column and row
    const Axis* col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colIdx));
    const Axis* row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(rowIdx));

    // Get or create the cell
    const Cell* cell = sheet->getOrCreateCellAt(col->id, row->id);

    // Build payload based on value type
    std::string payload;
    const std::string colIdStr = col->id.toString();
    const std::string rowIdStr = row->id.toString();

    if (lua_isnumber(L, 2) != 0) {
        const double num = lua_tonumber(L, 2);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", num);
        payload = R"({"type":"n","value":")" + std::string(buf) + R"(","col":")" + colIdStr +
                  R"(","row":")" + rowIdStr + R"("})";
    } else if (lua_isstring(L, 2) != 0) {
        const char* str = lua_tostring(L, 2);
        payload = R"({"type":"s","value":")" + jsonEscape(str) + R"(","col":")" + colIdStr +
                  R"(","row":")" + rowIdStr + R"("})";
    } else if (lua_isboolean(L, 2) != 0) {
        const bool val = lua_toboolean(L, 2) != 0;
        payload = R"({"type":"b","value":")" + std::string(val ? "true" : "false") +
                  R"(","col":")" + colIdStr + R"(","row":")" + rowIdStr + R"("})";
    } else if (lua_isnil(L, 2) != 0) {
        // Clear the cell
        const Operation op = makeCellClearOp(*workbook, cell->id);
        applyOperation(*workbook, op);
        return 0;
    } else {
        luaL_error(L, "setCell: unsupported value type");
    }

    // Apply the operation via CRDT
    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: setDocumentTitle(title)
// ============================================================================
int LuauSandbox::luaDocumentSetTitle(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "setDocumentTitle: no context set");
    }

    const std::string payload = R"({"name":")" + jsonEscape(title) + R"("})";
    const Operation op = makeWorkbookRenameOp(*workbook, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: setColumnWidth(col, options)
// options.width: number (pixels)
// ============================================================================
int LuauSandbox::luaColumnSetWidth(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "width");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "setColumnWidth: options.width required");
    }
    const int width = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setColumnWidth: no context set");
    }

    // Parse column reference (just the letter part)
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "setColumnWidth: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "setColumnWidth: column '%s' not found", colRef);
    }

    const std::string payload = R"({"size":)" + std::to_string(width) + "}";
    const Operation op = makeColResizeOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: setRowHeight(row, options)
// options.height: number (pixels)
// ============================================================================
int LuauSandbox::luaRowSetHeight(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "height");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "setRowHeight: options.height required");
    }
    const int height = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setRowHeight: no context set");
    }

    const int rowIdx = rowNum - 1;  // Convert 1-based to 0-based
    if (rowIdx < 0) {
        luaL_error(L, "setRowHeight: invalid row number %d", rowNum);
    }

    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "setRowHeight: row %d not found", rowNum);
    }

    const std::string payload = R"({"size":)" + std::to_string(height) + "}";
    const Operation op = makeRowResizeOp(*workbook, row->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: moveColumn(col, options)
// options.to: number (target position, 0-based)
// ============================================================================
int LuauSandbox::luaColumnMove(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "to");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "moveColumn: options.to required");
    }
    const int toPos = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "moveColumn: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "moveColumn: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "moveColumn: column '%s' not found", colRef);
    }

    const std::string payload = R"({"position":)" + std::to_string(toPos) + "}";
    const Operation op = makeColMoveOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: selectSheet(sheet|name|index)
// Accepts: sheet object, name string, or 0-based index number
// ============================================================================
int LuauSandbox::luaSelectSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "selectSheet: no context set");
    }

    Sheet* newSheet = nullptr;

    if (lua_isnumber(L, 1) != 0) {
        // Select by index
        const int index = static_cast<int>(lua_tonumber(L, 1));
        if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
            luaL_error(L, "selectSheet: index %d out of range", index);
        }
        newSheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    } else if (lua_isstring(L, 1) != 0) {
        // Select by name
        const char* name = lua_tostring(L, 1);
        newSheet = workbook->getSheetByName(name);
        if (newSheet == nullptr) {
            luaL_error(L, "selectSheet: sheet '%s' not found", name);
        }
    } else if (lua_istable(L, 1) != 0) {
        // Select by sheet object
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "selectSheet: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        const ID sheetId(uuidStr);
        newSheet = workbook->getSheet(sheetId);
        if (newSheet == nullptr) {
            luaL_error(L, "selectSheet: sheet not found");
        }
    } else {
        luaL_error(L, "selectSheet: expected sheet object, name string, or index number");
    }

    // Update the context
    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox != nullptr) {
        sandbox->setContext(workbook, newSheet);
    }

    return 0;
}

// ============================================================================
// Cells API: getSheet(options)
// options.index: 0-based sheet index
// options.name: sheet name string
// Returns: sheet object or nil if not found
// ============================================================================
int LuauSandbox::luaGetSheet(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "getSheet: no context set");
    }

    // NOLINTBEGIN(misc-const-correctness) - Sheet lookup returns non-const
    Sheet* sheet = nullptr;

    // Check for index parameter
    lua_getfield(L, 1, "index");
    if (lua_isnumber(L, -1) != 0) {
        const int index = static_cast<int>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
            lua_pushnil(L);
            return 1;
        }
        sheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    } else {
        lua_pop(L, 1);

        // Check for name parameter
        lua_getfield(L, 1, "name");
        if (lua_isstring(L, -1) != 0) {
            const char* name = lua_tostring(L, -1);
            lua_pop(L, 1);

            sheet = workbook->getSheetByName(name);
        } else {
            lua_pop(L, 1);
            luaL_error(L, "getSheet: requires {index = N} or {name = \"...\"}");
        }
    }
    // NOLINTEND(misc-const-correctness)

    if (sheet == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Get sandbox to push sheet object
    const LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "getSheet: sandbox not found");
    }

    sandbox->pushSheetObject(L, sheet);
    return 1;
}

// ============================================================================
// Cells API: addSheet(name?)
// Creates a new sheet, optionally with a name
// Returns: sheet object
// ============================================================================
int LuauSandbox::luaAddSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "addSheet: no context set");
    }

    // Get name argument (optional)
    std::string sheetName;
    if (lua_isstring(L, 1) != 0) {
        sheetName = lua_tostring(L, 1);
    } else {
        // Generate default name: "Sheet" + (count + 1)
        sheetName = "Sheet" + std::to_string(workbook->sheetCount() + 1);
    }

    // Generate a new ID for the sheet
    const ID sheetId = generate_id();

    // Create the sheet operation payload
    const std::string payload = R"({"name":")" + jsonEscape(sheetName) + R"("})";

    // Apply the operation
    const Operation op = makeSheetCreateOp(*workbook, sheetId, payload);
    applyOperation(*workbook, op);

    // Get the created sheet and return it
    Sheet* sheet = workbook->getSheet(sheetId);
    if (sheet == nullptr) {
        luaL_error(L, "addSheet: failed to create sheet");
    }

    // Get sandbox to push sheet object
    const LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "addSheet: sandbox not found");
    }

    sandbox->pushSheetObject(L, sheet);
    return 1;
}

// ============================================================================
// Cells API: selectRange(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeSelect(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "selectRange: options.from required");
    }
    // const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "selectRange: options.to required");
    }
    // const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Note: Selection is a UI concept, not a model concept
    // This function would need to communicate with the UI layer
    // For now, we just validate the input and succeed silently
    // The actual selection would be handled by the TypeScript layer

    return 0;
}

// ============================================================================
// Cells API: deleteRange(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeDelete(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "deleteRange: options.from required");
    }
    const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "deleteRange: options.to required");
    }
    const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "deleteRange: no context set");
    }

    // Parse range references
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Ref(fromRef, &fromCol, &fromRow) || !parseA1Ref(toRef, &toCol, &toRow)) {
        luaL_error(L, "deleteRange: invalid range");
    }

    // Normalize range (ensure from <= to)
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }

    // Delete cells in range
    for (int c = fromCol; c <= toCol; c++) {
        const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(c));
        if (col == nullptr) {
            continue;
        }
        for (int r = fromRow; r <= toRow; r++) {
            const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(r));
            if (row == nullptr) {
                continue;
            }
            const Cell* cell = sheet->getCellAt(col->id, row->id);
            if (cell != nullptr) {
                const Operation op = makeCellClearOp(*workbook, cell->id);
                applyOperation(*workbook, op);
            }
        }
    }

    return 0;
}

// ============================================================================
// Cells API: fillRange(options)
// options.from: string (source range, e.g., "A1:A2" or "A1" for single cell)
// options.to: string (full target range including source, e.g., "A1:A10")
// Returns: {success: boolean, cellsFilled: number, error?: string}
// ============================================================================
int LuauSandbox::luaRangeFill(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // Get source range (from)
    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "fillRange: options.from required");
    }
    const char* fromRange = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get target range (to)
    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "fillRange: options.to required");
    }
    const char* toRange = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "fillRange: no context set");
    }

    // Parse source range (e.g., "A1:A2" or "A1")
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Range(fromRange, &fromCol, &fromRow, &toCol, &toRow)) {
        luaL_error(L, "fillRange: invalid source range '%s'", fromRange);
    }

    // Parse target range (e.g., "A1:A10")
    int targetFromCol = 0;
    int targetFromRow = 0;
    int targetToCol = 0;
    int targetToRow = 0;
    if (!parseA1Range(toRange, &targetFromCol, &targetFromRow, &targetToCol, &targetToRow)) {
        luaL_error(L, "fillRange: invalid target range '%s'", toRange);
    }

    // Normalize ranges (ensure from <= to)
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }
    if (targetFromCol > targetToCol) {
        std::swap(targetFromCol, targetToCol);
    }
    if (targetFromRow > targetToRow) {
        std::swap(targetFromRow, targetToRow);
    }

    // Call the fill function
    const FillResult result = fillRange(workbook, sheet, fromCol, fromRow, toCol, toRow,
                                        targetFromCol, targetFromRow, targetToCol, targetToRow);

    // Return result table
    lua_newtable(L);
    lua_pushboolean(L, result.success ? 1 : 0);
    lua_setfield(L, -2, "success");
    lua_pushnumber(L, result.cellsFilled);
    lua_setfield(L, -2, "cellsFilled");
    if (!result.error.empty()) {
        lua_pushstring(L, result.error.c_str());
        lua_setfield(L, -2, "error");
    }

    return 1;
}

// ============================================================================
// Cell __index metamethod: handles property access (e.g., cell.ref)
// ============================================================================
int LuauSandbox::luaCellIndex(lua_State* L) {
    // Stack: [1] = cell table, [2] = key (string)
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Handle .ref property
    if (strcmp(key, "ref") == 0) {
        // Get the cell UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "ref: invalid cell object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        // NOLINTBEGIN(misc-const-correctness) - Sheet methods not const-correct
        Sheet* sheet = getSheet(L);
        if (sheet == nullptr) {
            luaL_error(L, "ref: no context set");
        }

        const ID cellId(uuidStr);
        Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            luaL_error(L, "ref: cell not found");
        }

        // Get the cell's current position
        Axis* col = sheet->getColumn(cell->colId);
        Axis* row = sheet->getRow(cell->rowId);
        // NOLINTEND(misc-const-correctness)
        if (col == nullptr || row == nullptr) {
            luaL_error(L, "ref: cell position not found");
        }

        // Convert to A1 notation
        const std::string a1Ref =
            RefConverter::columnIndexToLetter(col->position) + std::to_string(row->position + 1);
        lua_pushstring(L, a1Ref.c_str());
        return 1;
    }

    // For other keys, look up in the table itself
    lua_rawget(L, 1);
    return 1;
}

// ============================================================================
// Sheet __index metamethod: handles property access (e.g., sheet.name)
// ============================================================================
int LuauSandbox::luaSheetIndex(lua_State* L) {
    // Stack: [1] = sheet table, [2] = key (string)
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Handle .name property
    if (strcmp(key, "name") == 0) {
        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "name: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "name: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "name: sheet not found");
        }

        lua_pushstring(L, sheet->name.c_str());
        return 1;
    }

    // For other keys, look up in the table itself
    lua_rawget(L, 1);
    return 1;
}

// ============================================================================
// Sheet __newindex metamethod: handles property assignment (e.g., sheet.name = "x")
// ============================================================================
int LuauSandbox::luaSheetNewIndex(lua_State* L) {
    // Stack: [1] = sheet table, [2] = key (string), [3] = value
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        luaL_error(L, "invalid property name");
    }

    // Handle .name = "x" assignment
    if (strcmp(key, "name") == 0) {
        if (lua_isstring(L, 3) == 0) {
            luaL_error(L, "sheet.name must be a string");
        }
        const char* newName = lua_tostring(L, 3);

        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "name: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "name: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "name: sheet not found");
        }

        // Apply rename operation
        const std::string payload = R"({"name":")" + jsonEscape(newName) + R"("})";
        const Operation op = makeSheetRenameOp(*workbook, sheet->id, payload);
        applyOperation(*workbook, op);

        return 0;
    }

    // For other keys, set in the table itself
    lua_rawset(L, 1);
    return 0;
}

// ============================================================================
// Helper: Create and push a sheet object
// ============================================================================
void LuauSandbox::pushSheetObject(lua_State* L, Sheet* sheet) const {
    if (sheet == nullptr) {
        lua_pushnil(L);
        return;
    }

    const std::string sheetUuid = sheet->id.toString();

    // Create new sheet object table
    lua_newtable(L);

    // Store UUID for identity tracking (hidden field)
    lua_pushstring(L, sheetUuid.c_str());
    lua_setfield(L, -2, "_uuid");

    // Apply Sheet metatable for .name property access
    if (sheetMetatableRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, sheetMetatableRef_);  // Get Sheet metatable
        lua_setmetatable(L, -2);                                // setmetatable(sheet, Sheet)
    }
}

// ============================================================================
// Cell object method: getRef() - DEPRECATED, use .ref property
// Kept for backward compatibility during transition
// Returns the current A1 reference for the cell
// ============================================================================
int LuauSandbox::luaCellGetRef(lua_State* L) {
    // Get the cell UUID from the table
    lua_getfield(L, 1, "_uuid");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "getRef: invalid cell object");
    }
    const char* uuidStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    // NOLINTBEGIN(misc-const-correctness) - Sheet methods not const-correct
    Sheet* sheet = getSheet(L);
    if (sheet == nullptr) {
        luaL_error(L, "getRef: no context set");
    }

    const ID cellId(uuidStr);
    Cell* cell = sheet->getCell(cellId);
    if (cell == nullptr) {
        luaL_error(L, "getRef: cell not found");
    }

    // Get the cell's current position
    Axis* col = sheet->getColumn(cell->colId);
    Axis* row = sheet->getRow(cell->rowId);
    // NOLINTEND(misc-const-correctness)
    if (col == nullptr || row == nullptr) {
        luaL_error(L, "getRef: cell position not found");
    }

    // Convert to A1 notation
    const std::string a1Ref =
        RefConverter::columnIndexToLetter(col->position) + std::to_string(row->position + 1);
    lua_pushstring(L, a1Ref.c_str());
    return 1;
}

// ============================================================================
// Helper: Create and push a cell object
// ============================================================================
void LuauSandbox::pushCellObject(lua_State* L, Cell* cell) {
    if (cell == nullptr) {
        lua_pushnil(L);
        return;
    }

    const std::string cellUuid = cell->id.toString();

    // Check if we have a cached object
    if (cellCacheRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellCacheRef_);  // Get cache table
        lua_getfield(L, -1, cellUuid.c_str());             // Get cached object
        if (lua_istable(L, -1) != 0) {
            // Update the cached object's value field
            const CellValue& value = cell->value;
            switch (value.type) {
                case CellValueType::NUMBER:
                case CellValueType::FORMULA_NUMBER:
                    lua_pushnumber(L, value.asNumber());
                    break;
                case CellValueType::STRING:
                case CellValueType::FORMULA_STRING:
                    lua_pushstring(L, value.asString().c_str());
                    break;
                case CellValueType::BOOLEAN:
                case CellValueType::FORMULA_BOOLEAN:
                    lua_pushboolean(L, value.asBoolean() ? 1 : 0);
                    break;
                default:
                    lua_pushnil(L);
            }
            lua_setfield(L, -2, "value");

            // Update formula field
            if (cell->isFormula()) {
                const Formula* f = cell->getFormula();
                if (f != nullptr && f->ast != nullptr) {
                    // Generate UUID formula from AST, then convert to A1 notation for display
                    RefConverter conv;
                    conv.setContext(*sheet_);
                    const std::string uuidFormula = FormulaSerializer::serialize(f->ast);
                    const std::string a1Formula = conv.formulaToA1(uuidFormula);
                    lua_pushstring(L, a1Formula.c_str());
                } else {
                    lua_pushnil(L);
                }
            } else {
                lua_pushnil(L);
            }
            lua_setfield(L, -2, "formula");

            // Remove cache table from stack, keep cell object
            lua_remove(L, -2);
            return;
        }
        lua_pop(L, 2);  // Pop nil and cache table
    }

    // Create new cell object table
    lua_newtable(L);

    // Set value
    const CellValue& value = cell->value;
    switch (value.type) {
        case CellValueType::NUMBER:
        case CellValueType::FORMULA_NUMBER:
            lua_pushnumber(L, value.asNumber());
            break;
        case CellValueType::STRING:
        case CellValueType::FORMULA_STRING:
            lua_pushstring(L, value.asString().c_str());
            break;
        case CellValueType::BOOLEAN:
        case CellValueType::FORMULA_BOOLEAN:
            lua_pushboolean(L, value.asBoolean() ? 1 : 0);
            break;
        default:
            lua_pushnil(L);
    }
    lua_setfield(L, -2, "value");

    // Set formula
    if (cell->isFormula()) {
        const Formula* f = cell->getFormula();
        if (f != nullptr && f->ast != nullptr) {
            RefConverter conv;
            conv.setContext(*sheet_);
            const std::string uuidFormula = FormulaSerializer::serialize(f->ast);
            const std::string a1Formula = conv.formulaToA1(uuidFormula);
            lua_pushstring(L, a1Formula.c_str());
        } else {
            lua_pushnil(L);
        }
    } else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "formula");

    // Store UUID for identity tracking (hidden field)
    lua_pushstring(L, cellUuid.c_str());
    lua_setfield(L, -2, "_uuid");

    // Apply Cell metatable for .ref property access
    if (cellMetatableRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellMetatableRef_);  // Get Cell metatable
        lua_setmetatable(L, -2);                               // setmetatable(cell, Cell)
    }

    // Cache the object (if cache exists)
    if (cellCacheRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellCacheRef_);  // Get cache table
        lua_pushvalue(L, -2);                              // Copy cell object
        lua_setfield(L, -2, cellUuid.c_str());             // cache[uuid] = cell
        lua_pop(L, 1);                                     // Pop cache table
    }
}

void LuauSandbox::registerCellsAPI() {
    // Create weak cache table for cell objects
    lua_newtable(L_);                 // cache table
    lua_newtable(L_);                 // metatable
    lua_pushstring(L_, "v");          // weak values
    lua_setfield(L_, -2, "__mode");   // metatable.__mode = "v"
    lua_setmetatable(L_, -2);         // setmetatable(cache, metatable)
    cellCacheRef_ = lua_ref(L_, -1);  // Store in registry (ref pops the value)
    // lua_ref pops the value from stack, so we don't need to pop

    // Create Cell metatable with __index for property access (e.g., cell.ref)
    lua_newtable(L_);  // Cell metatable
    lua_pushcfunction(L_, &LuauSandbox::luaCellIndex, "Cell.__index");
    lua_setfield(L_, -2, "__index");
    cellMetatableRef_ = lua_ref(L_, -1);  // Store in registry

    // Create Sheet metatable with __index/__newindex for property access (e.g., sheet.name)
    lua_newtable(L_);  // Sheet metatable
    lua_pushcfunction(L_, &LuauSandbox::luaSheetIndex, "Sheet.__index");
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, &LuauSandbox::luaSheetNewIndex, "Sheet.__newindex");
    lua_setfield(L_, -2, "__newindex");
    sheetMetatableRef_ = lua_ref(L_, -1);  // Store in registry

    // Register global API functions
    lua_pushcfunction(L_, &LuauSandbox::luaCellGet, "getCell");
    lua_setglobal(L_, "getCell");

    lua_pushcfunction(L_, &LuauSandbox::luaCellSet, "setCell");
    lua_setglobal(L_, "setCell");

    lua_pushcfunction(L_, &LuauSandbox::luaDocumentSetTitle, "setDocumentTitle");
    lua_setglobal(L_, "setDocumentTitle");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnSetWidth, "setColumnWidth");
    lua_setglobal(L_, "setColumnWidth");

    lua_pushcfunction(L_, &LuauSandbox::luaRowSetHeight, "setRowHeight");
    lua_setglobal(L_, "setRowHeight");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnMove, "moveColumn");
    lua_setglobal(L_, "moveColumn");

    lua_pushcfunction(L_, &LuauSandbox::luaSelectSheet, "selectSheet");
    lua_setglobal(L_, "selectSheet");

    lua_pushcfunction(L_, &LuauSandbox::luaGetSheet, "getSheet");
    lua_setglobal(L_, "getSheet");

    lua_pushcfunction(L_, &LuauSandbox::luaAddSheet, "addSheet");
    lua_setglobal(L_, "addSheet");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeSelect, "selectRange");
    lua_setglobal(L_, "selectRange");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeDelete, "deleteRange");
    lua_setglobal(L_, "deleteRange");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeFill, "fillRange");
    lua_setglobal(L_, "fillRange");
}

void LuauSandbox::setContext(Workbook* workbook, Sheet* sheet) {
    workbook_ = workbook;
    sheet_ = sheet;

    if (L_ != nullptr) {
        lua_pushlightuserdata(L_, workbook);
        lua_setfield(L_, LUA_REGISTRYINDEX, kRegistryWorkbook);

        lua_pushlightuserdata(L_, sheet);
        lua_setfield(L_, LUA_REGISTRYINDEX, kRegistrySheet);
    }
}

void LuauSandbox::clearContext() {
    workbook_ = nullptr;
    sheet_ = nullptr;

    if (L_ != nullptr) {
        lua_pushnil(L_);
        lua_setfield(L_, LUA_REGISTRYINDEX, kRegistryWorkbook);

        lua_pushnil(L_);
        lua_setfield(L_, LUA_REGISTRYINDEX, kRegistrySheet);
    }
}

std::string LuauSandbox::compile(const std::string& source, std::string& bytecodeOut) const {
    // Set up compile options
    Luau::CompileOptions opts;
    opts.optimizationLevel = 1;
    opts.debugLevel = config_.enableDebug ? 2 : 1;

    // Compile the source
    bytecodeOut = Luau::compile(source, opts);

    // Check for compilation errors
    // Luau encodes errors in the bytecode - we need to check by trying to load
    // An error bytecode starts with version 0
    if (!bytecodeOut.empty() && bytecodeOut[0] == 0) {
        // Extract error message from bytecode
        // Format: version(1) + error_len(varint) + error_msg
        if (bytecodeOut.size() > 1) {
            // Skip version byte, read error message
            size_t pos = 1;
            // Read varint length
            size_t len = 0;
            size_t shift = 0;
            while (pos < bytecodeOut.size()) {
                const auto byte = static_cast<uint8_t>(bytecodeOut[pos++]);
                len |= static_cast<size_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    break;
                }
                shift += 7;
            }
            if (pos + len <= bytecodeOut.size()) {
                return bytecodeOut.substr(pos, len);
            }
            return "Compilation failed";
        }
        return "Compilation failed";
    }

    return "";  // Success
}

ScriptResult LuauSandbox::execute(const std::string& script) {
    ScriptResult result;

    if (L_ == nullptr) {
        result.error = "Lua state not initialized";
        return result;
    }

    // Compile script
    std::string bytecode;
    const std::string compileError = compile(script, bytecode);
    if (!compileError.empty()) {
        result.error = "Compile error: " + compileError;
        return result;
    }

    // Reset instruction counter
    instructionCount_ = 0;
    interrupted_ = false;

    // Set up interrupt callback for instruction limiting
    lua_callbacks(L_)->interrupt = &LuauSandbox::interruptCallback;

    // Create a sandboxed thread for execution
    lua_State* T = lua_newthread(L_);
    luaL_sandboxthread(T);

    // Load bytecode
    const int loadResult = luau_load(T, "script", bytecode.data(), bytecode.size(), 0);
    if (loadResult != 0) {
        result.error = "Load error: ";
        if (lua_isstring(T, -1) != 0) {
            result.error += lua_tostring(T, -1);
        } else {
            result.error += "unknown error";
        }
        lua_pop(L_, 1);  // Pop thread
        lua_callbacks(L_)->interrupt = nullptr;
        return result;
    }

    // Execute the script
    const int status = lua_resume(T, nullptr, 0);

    // Clear interrupt callback
    lua_callbacks(L_)->interrupt = nullptr;

    result.instructions = instructionCount_;

    if (status == LUA_OK || status == LUA_YIELD) {
        result.success = true;
        // Check for return value
        if (lua_gettop(T) > 0 && (lua_isnil(T, 1) == 0)) {
            if (lua_isstring(T, 1) != 0) {
                result.output = lua_tostring(T, 1);
            } else if (lua_isnumber(T, 1) != 0) {
                result.output = std::to_string(lua_tonumber(T, 1));
            } else if (lua_isboolean(T, 1) != 0) {
                result.output = (lua_toboolean(T, 1) != 0) ? "true" : "false";
            }
        }
    } else {
        // Error occurred
        if (lua_isstring(T, -1) != 0) {
            result.error = lua_tostring(T, -1);
        } else if (interrupted_) {
            result.error = "Script exceeded instruction limit";
        } else {
            result.error = "Unknown runtime error";
        }
    }

    // Pop the thread
    lua_pop(L_, 1);

    return result;
}

void LuauSandbox::setMaxInstructions(int64_t limit) {
    config_.maxInstructions = limit;
}

}  // namespace cells
