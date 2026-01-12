// =============================================================================
// Luau Scripting Sandbox - Main Entry
// =============================================================================
//
// Core sandbox implementation providing VM lifecycle management, script
// compilation, and execution control. This file contains the main LuauSandbox
// class implementation while API functions and type wrappers are in separate
// files for maintainability.
//
// Key responsibilities:
// - Initialize and manage Lua state lifecycle
// - Compile Luau source to bytecode
// - Execute scripts with instruction limiting
// - Register API functions and metatables
// - Manage workbook/sheet execution context
//
// File organization:
// - luau_sandbox.cc (this file): VM lifecycle, compilation, execution
// - luau_api.cc: API functions (getCell, setCell, addSheet, etc.)
// - luau_types.cc: Type wrappers (Cell/Sheet objects, metamethods)
//
// Dependencies: Luau VM (external), model.h
// Used by: bindings.cc (script execution), agent_client.h (AI tool execution)
//
// =============================================================================

#include "core/cells/luau_sandbox.h"

#include <cstdlib>
#include <cstring>

#include "core/cells/model.h"

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
LuauSandbox* LuauSandbox::getSandbox(lua_State* L) {
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

void LuauSandbox::registerCellsAPI() {
    // Create weak cache table for cell objects
    lua_newtable(L_);                 // cache table
    lua_newtable(L_);                 // metatable
    lua_pushstring(L_, "v");          // weak values
    lua_setfield(L_, -2, "__mode");   // metatable.__mode = "v"
    lua_setmetatable(L_, -2);         // setmetatable(cache, metatable)
    cellCacheRef_ = lua_ref(L_, -1);  // Store in registry (ref pops the value)
    // lua_ref pops the value from stack, so we don't need to pop

    // Create Cell metatable with __index/__newindex/__tostring for property access
    lua_newtable(L_);  // Cell metatable
    lua_pushcfunction(L_, &LuauSandbox::luaCellIndex, "Cell.__index");
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, &LuauSandbox::luaCellNewIndex, "Cell.__newindex");
    lua_setfield(L_, -2, "__newindex");
    lua_pushcfunction(L_, &LuauSandbox::luaCellToString, "Cell.__tostring");
    lua_setfield(L_, -2, "__tostring");
    cellMetatableRef_ = lua_ref(L_, -1);  // Store in registry

    // Create Sheet metatable with __index/__newindex/__tostring for property access
    lua_newtable(L_);  // Sheet metatable
    lua_pushcfunction(L_, &LuauSandbox::luaSheetIndex, "Sheet.__index");
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, &LuauSandbox::luaSheetNewIndex, "Sheet.__newindex");
    lua_setfield(L_, -2, "__newindex");
    lua_pushcfunction(L_, &LuauSandbox::luaSheetToString, "Sheet.__tostring");
    lua_setfield(L_, -2, "__tostring");
    sheetMetatableRef_ = lua_ref(L_, -1);  // Store in registry

    // Register global API functions
    lua_pushcfunction(L_, &LuauSandbox::luaCellGet, "getCell");
    lua_setglobal(L_, "getCell");

    lua_pushcfunction(L_, &LuauSandbox::luaCellSet, "setCell");
    lua_setglobal(L_, "setCell");

    lua_pushcfunction(L_, &LuauSandbox::luaDocumentSetTitle, "setDocumentTitle");
    lua_setglobal(L_, "setDocumentTitle");

    lua_pushcfunction(L_, &LuauSandbox::luaDocumentGetTitle, "getDocumentTitle");
    lua_setglobal(L_, "getDocumentTitle");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnSetWidth, "setColumnWidth");
    lua_setglobal(L_, "setColumnWidth");

    lua_pushcfunction(L_, &LuauSandbox::luaRowSetHeight, "setRowHeight");
    lua_setglobal(L_, "setRowHeight");

    lua_pushcfunction(L_, &LuauSandbox::luaColumnMove, "moveColumn");
    lua_setglobal(L_, "moveColumn");

    lua_pushcfunction(L_, &LuauSandbox::luaHideColumn, "hideColumn");
    lua_setglobal(L_, "hideColumn");

    lua_pushcfunction(L_, &LuauSandbox::luaShowColumn, "showColumn");
    lua_setglobal(L_, "showColumn");

    lua_pushcfunction(L_, &LuauSandbox::luaHideRow, "hideRow");
    lua_setglobal(L_, "hideRow");

    lua_pushcfunction(L_, &LuauSandbox::luaShowRow, "showRow");
    lua_setglobal(L_, "showRow");

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

    // Format and style functions
    lua_pushcfunction(L_, &LuauSandbox::luaSetFormat, "setFormat");
    lua_setglobal(L_, "setFormat");

    lua_pushcfunction(L_, &LuauSandbox::luaSetStyle, "setStyle");
    lua_setglobal(L_, "setStyle");

    lua_pushcfunction(L_, &LuauSandbox::luaGetFormats, "getFormats");
    lua_setglobal(L_, "getFormats");

    // Register print() for console output
    lua_pushcfunction(L_, &LuauSandbox::luaPrint, "print");
    lua_setglobal(L_, "print");

    // Register style constants
    // Horizontal alignment
    lua_pushstring(L_, "left");
    lua_setglobal(L_, "ALIGN_LEFT");
    lua_pushstring(L_, "center");
    lua_setglobal(L_, "ALIGN_CENTER");
    lua_pushstring(L_, "right");
    lua_setglobal(L_, "ALIGN_RIGHT");
    lua_pushstring(L_, "justify");
    lua_setglobal(L_, "ALIGN_JUSTIFY");

    // Vertical alignment
    lua_pushstring(L_, "top");
    lua_setglobal(L_, "VALIGN_TOP");
    lua_pushstring(L_, "middle");
    lua_setglobal(L_, "VALIGN_MIDDLE");
    lua_pushstring(L_, "bottom");
    lua_setglobal(L_, "VALIGN_BOTTOM");

    // Common colors
    lua_pushstring(L_, "#FF0000");
    lua_setglobal(L_, "COLOR_RED");
    lua_pushstring(L_, "#00FF00");
    lua_setglobal(L_, "COLOR_GREEN");
    lua_pushstring(L_, "#0000FF");
    lua_setglobal(L_, "COLOR_BLUE");
    lua_pushstring(L_, "#FFFF00");
    lua_setglobal(L_, "COLOR_YELLOW");
    lua_pushstring(L_, "#FF00FF");
    lua_setglobal(L_, "COLOR_MAGENTA");
    lua_pushstring(L_, "#00FFFF");
    lua_setglobal(L_, "COLOR_CYAN");
    lua_pushstring(L_, "#FFFFFF");
    lua_setglobal(L_, "COLOR_WHITE");
    lua_pushstring(L_, "#000000");
    lua_setglobal(L_, "COLOR_BLACK");
    lua_pushstring(L_, "#808080");
    lua_setglobal(L_, "COLOR_GRAY");
    lua_pushstring(L_, "#FFA500");
    lua_setglobal(L_, "COLOR_ORANGE");
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

    // Reset instruction counter and print buffer
    instructionCount_ = 0;
    interrupted_ = false;
    printBuffer_.clear();

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

        // Include print buffer output
        result.output = printBuffer_;

        // Append return value if any
        if (lua_gettop(T) > 0 && (lua_isnil(T, 1) == 0)) {
            std::string returnValue;
            if (lua_isstring(T, 1) != 0) {
                returnValue = lua_tostring(T, 1);
            } else if (lua_isnumber(T, 1) != 0) {
                returnValue = std::to_string(lua_tonumber(T, 1));
            } else if (lua_isboolean(T, 1) != 0) {
                returnValue = (lua_toboolean(T, 1) != 0) ? "true" : "false";
            }
            if (!returnValue.empty()) {
                if (!result.output.empty()) {
                    result.output += "=> " + returnValue;
                } else {
                    result.output = returnValue;
                }
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
