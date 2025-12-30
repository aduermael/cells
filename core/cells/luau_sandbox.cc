#include "core/cells/luau_sandbox.h"

#include <cstdlib>
#include <cstring>

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

void LuauSandbox::registerCellsAPI() {
    // API functions will be implemented in Phase 3
    // For now, just set up empty placeholders

    // Create cells namespace table
    lua_newtable(L_);
    lua_setglobal(L_, "cells");
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
