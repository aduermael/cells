#include "core/cells/luau_sandbox.h"

#include <cstdlib>
#include <cstring>

#include "core/cells/crdt.h"
#include "core/cells/fill_range.h"
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

// ============================================================================
// Cells API: cellGet(ref, options?)
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
        luaL_error(L, "cellGet: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "cellGet: invalid reference '%s'", ref);
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
        luaL_error(L, "cellGet: sandbox not found");
    }

    sandbox->pushCellObject(L, cell);
    return 1;
}

// ============================================================================
// Cells API: cellSet(ref, value)
// Sets cell value (creates cell if needed)
// ============================================================================
int LuauSandbox::luaCellSet(lua_State* L) {
    // Get the ref argument
    const char* ref = luaL_checkstring(L, 1);

    // Get context
    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "cellSet: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "cellSet: invalid reference '%s'", ref);
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
        luaL_error(L, "cellSet: unsupported value type");
    }

    // Apply the operation via CRDT
    const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: documentSetTitle(title)
// ============================================================================
int LuauSandbox::luaDocumentSetTitle(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "documentSetTitle: no context set");
    }

    const std::string payload = R"({"name":")" + jsonEscape(title) + R"("})";
    const Operation op = makeWorkbookRenameOp(*workbook, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: columnSetWidth(col, options)
// options.width: number (pixels)
// ============================================================================
int LuauSandbox::luaColumnSetWidth(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "width");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "columnSetWidth: options.width required");
    }
    const int width = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "columnSetWidth: no context set");
    }

    // Parse column reference (just the letter part)
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "columnSetWidth: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "columnSetWidth: column '%s' not found", colRef);
    }

    const std::string payload = R"({"size":)" + std::to_string(width) + "}";
    const Operation op = makeColResizeOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: rowSetHeight(row, options)
// options.height: number (pixels)
// ============================================================================
int LuauSandbox::luaRowSetHeight(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "height");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "rowSetHeight: options.height required");
    }
    const int height = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "rowSetHeight: no context set");
    }

    const int rowIdx = rowNum - 1;  // Convert 1-based to 0-based
    if (rowIdx < 0) {
        luaL_error(L, "rowSetHeight: invalid row number %d", rowNum);
    }

    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "rowSetHeight: row %d not found", rowNum);
    }

    const std::string payload = R"({"size":)" + std::to_string(height) + "}";
    const Operation op = makeRowResizeOp(*workbook, row->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: columnMove(col, options)
// options.to: number (target position, 0-based)
// ============================================================================
int LuauSandbox::luaColumnMove(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "to");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "columnMove: options.to required");
    }
    const int toPos = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "columnMove: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "columnMove: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "columnMove: column '%s' not found", colRef);
    }

    const std::string payload = R"({"position":)" + std::to_string(toPos) + "}";
    const Operation op = makeColMoveOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: sheetSelect(index)
// index: 0-based sheet index
// ============================================================================
int LuauSandbox::luaSheetSelect(lua_State* L) {
    const int index = static_cast<int>(luaL_checknumber(L, 1));

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "sheetSelect: no context set");
    }

    if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
        luaL_error(L, "sheetSelect: index %d out of range", index);
    }

    Sheet* newSheet = workbook->getSheetByIndex(static_cast<size_t>(index));

    // Update the context
    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox != nullptr) {
        sandbox->setContext(workbook, newSheet);
    }

    return 0;
}

// ============================================================================
// Cells API: sheetSetName(index, options)
// options.name: string
// ============================================================================
int LuauSandbox::luaSheetSetName(lua_State* L) {
    const int index = static_cast<int>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "name");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "sheetSetName: options.name required");
    }
    const char* name = lua_tostring(L, -1);
    lua_pop(L, 1);

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "sheetSetName: no context set");
    }

    if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
        luaL_error(L, "sheetSetName: index %d out of range", index);
    }

    const Sheet* sheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    const std::string payload = R"({"name":")" + jsonEscape(name) + R"("})";
    const Operation op = makeSheetRenameOp(*workbook, sheet->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: sheetGetName(index)
// Returns: string (sheet name)
// ============================================================================
int LuauSandbox::luaSheetGetName(lua_State* L) {
    const int index = static_cast<int>(luaL_checknumber(L, 1));

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "sheetGetName: no context set");
    }

    if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
        luaL_error(L, "sheetGetName: index %d out of range", index);
    }

    const Sheet* sheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    lua_pushstring(L, sheet->name.c_str());
    return 1;
}

// ============================================================================
// Cells API: rangeSelect(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeSelect(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeSelect: options.from required");
    }
    // const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeSelect: options.to required");
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
// Cells API: rangeDelete(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeDelete(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeDelete: options.from required");
    }
    const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeDelete: options.to required");
    }
    const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "rangeDelete: no context set");
    }

    // Parse range references
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Ref(fromRef, &fromCol, &fromRow) || !parseA1Ref(toRef, &toCol, &toRow)) {
        luaL_error(L, "rangeDelete: invalid range");
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
// Cells API: rangeFill(options)
// options.from: string (start cell ref of source range)
// options.to: string (end cell ref of source range)
// options.targetFrom: string (start cell ref of target range)
// options.targetTo: string (end cell ref of target range)
// Returns: {success: boolean, cellsFilled: number, error?: string}
// ============================================================================
int LuauSandbox::luaRangeFill(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // Get source range (from, to)
    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeFill: options.from required");
    }
    const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeFill: options.to required");
    }
    const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get target range (targetFrom, targetTo)
    lua_getfield(L, 1, "targetFrom");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeFill: options.targetFrom required");
    }
    const char* targetFromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "targetTo");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "rangeFill: options.targetTo required");
    }
    const char* targetToRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "rangeFill: no context set");
    }

    // Parse source range references
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Ref(fromRef, &fromCol, &fromRow) || !parseA1Ref(toRef, &toCol, &toRow)) {
        luaL_error(L, "rangeFill: invalid source range");
    }

    // Parse target range references
    int targetFromCol = 0;
    int targetFromRow = 0;
    int targetToCol = 0;
    int targetToRow = 0;
    if (!parseA1Ref(targetFromRef, &targetFromCol, &targetFromRow) ||
        !parseA1Ref(targetToRef, &targetToCol, &targetToRow)) {
        luaL_error(L, "rangeFill: invalid target range");
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
// Cell object method: getRef()
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
                if (f != nullptr && f->text != nullptr) {
                    // Convert formula to A1 notation for display
                    RefConverter conv;
                    conv.setContext(*sheet_);
                    const std::string a1Formula = conv.formulaToA1(f->text);
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
        if (f != nullptr && f->text != nullptr) {
            RefConverter conv;
            conv.setContext(*sheet_);
            const std::string a1Formula = conv.formulaToA1(f->text);
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

    // Set getRef method
    lua_pushcfunction(L, &LuauSandbox::luaCellGetRef, "Cell.getRef");
    lua_setfield(L, -2, "getRef");

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

    // Register global API functions
    lua_pushcfunction(L_, &LuauSandbox::luaCellGet, "cellGet");
    lua_setglobal(L_, "cellGet");

    lua_pushcfunction(L_, &LuauSandbox::luaCellSet, "cellSet");
    lua_setglobal(L_, "cellSet");

    lua_pushcfunction(L_, &LuauSandbox::luaDocumentSetTitle, "documentSetTitle");
    lua_setglobal(L_, "documentSetTitle");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnSetWidth, "columnSetWidth");
    lua_setglobal(L_, "columnSetWidth");

    lua_pushcfunction(L_, &LuauSandbox::luaRowSetHeight, "rowSetHeight");
    lua_setglobal(L_, "rowSetHeight");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnMove, "columnMove");
    lua_setglobal(L_, "columnMove");

    lua_pushcfunction(L_, &LuauSandbox::luaSheetSelect, "sheetSelect");
    lua_setglobal(L_, "sheetSelect");

    lua_pushcfunction(L_, &LuauSandbox::luaSheetSetName, "sheetSetName");
    lua_setglobal(L_, "sheetSetName");

    lua_pushcfunction(L_, &LuauSandbox::luaSheetGetName, "sheetGetName");
    lua_setglobal(L_, "sheetGetName");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeSelect, "rangeSelect");
    lua_setglobal(L_, "rangeSelect");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeDelete, "rangeDelete");
    lua_setglobal(L_, "rangeDelete");

    lua_pushcfunction(L_, &LuauSandbox::luaRangeFill, "rangeFill");
    lua_setglobal(L_, "rangeFill");
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
