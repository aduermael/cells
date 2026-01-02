#ifndef CELLS_LUAU_SANDBOX_H_
#define CELLS_LUAU_SANDBOX_H_

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations for Luau types
struct lua_State;

namespace cells {

// Forward declarations
struct Workbook;
struct Sheet;
struct Cell;
struct ID;

// Result of script execution
struct ScriptResult {
    bool success{false};
    std::string error;        // Error message if !success
    std::string output;       // Script output (if any)
    int64_t instructions{0};  // Number of instructions executed
};

// Configuration for the sandbox
struct SandboxConfig {
    int64_t maxInstructions{1'000'000};  // Instruction limit (default 1M)
    bool enableDebug{false};             // Enable debug library
};

// LuauSandbox - sandboxed Luau scripting environment for cells
//
// Provides a safe execution environment for user scripts with:
// - Instruction limits to prevent infinite loops
// - Restricted standard library (no IO, network, process spawning)
// - Cells API functions (cellGet, cellSet, etc.)
//
// Usage:
//   LuauSandbox sandbox;
//   sandbox.setContext(workbook, &sheet);
//   auto result = sandbox.execute("cellSet('A1', 100)");
//
class LuauSandbox {
public:
    LuauSandbox();
    explicit LuauSandbox(const SandboxConfig& config);
    ~LuauSandbox();

    // Non-copyable
    LuauSandbox(const LuauSandbox&) = delete;
    LuauSandbox& operator=(const LuauSandbox&) = delete;

    // Movable
    LuauSandbox(LuauSandbox&& other) noexcept;
    LuauSandbox& operator=(LuauSandbox&& other) noexcept;

    // Set the workbook/sheet context for API functions
    // Must be called before execute() if using cells API
    void setContext(Workbook* workbook, Sheet* sheet);

    // Clear the context
    void clearContext();

    // Execute a Luau script
    // Returns result with success status and any output/error
    [[nodiscard]] ScriptResult execute(const std::string& script);

    // Get current configuration
    [[nodiscard]] const SandboxConfig& config() const { return config_; }

    // Set instruction limit (for runtime adjustment)
    void setMaxInstructions(int64_t limit);

private:
    // Initialize Lua state with sandboxing
    void initState();

    // Register cells API functions
    void registerCellsAPI();

    // Compile script to bytecode
    // Returns empty string on success, error message on failure
    [[nodiscard]] std::string compile(const std::string& source, std::string& bytecodeOut) const;

    // Interrupt callback for instruction limiting
    static void interruptCallback(lua_State* L, int gc);

    lua_State* L_{nullptr};
    SandboxConfig config_;

    // Context for API functions (stored in Lua registry for callbacks)
    Workbook* workbook_{nullptr};
    Sheet* sheet_{nullptr};

    // Instruction counter for current execution
    int64_t instructionCount_{0};
    bool interrupted_{false};

    // Print buffer for capturing print() output
    std::string printBuffer_;

    // ========================================================================
    // Cells API function implementations (called from Lua)
    // ========================================================================

    // Cell operations
    static int luaCellGet(lua_State* L);
    static int luaCellSet(lua_State* L);

    // Document operations
    static int luaDocumentSetTitle(lua_State* L);
    static int luaDocumentGetTitle(lua_State* L);

    // Axis operations
    static int luaColumnSetWidth(lua_State* L);
    static int luaRowSetHeight(lua_State* L);
    static int luaColumnMove(lua_State* L);

    // Sheet operations
    static int luaSelectSheet(lua_State* L);  // selectSheet(sheet|name|index)
    static int luaGetSheet(lua_State* L);     // getSheet({name=...}) or getSheet({index=...})
    static int luaAddSheet(lua_State* L);     // addSheet(name?)

    // Range operations
    static int luaRangeSelect(lua_State* L);
    static int luaRangeDelete(lua_State* L);
    static int luaRangeFill(lua_State* L);

    // Output operations
    static int luaPrint(lua_State* L);

    // Cell object methods (called on cell tables via __index)
    static int luaCellGetRef(lua_State* L);

    // Helper: Get context from registry
    static Workbook* getWorkbook(lua_State* L);
    static Sheet* getSheet(lua_State* L);

    // Helper: Create a cell Lua object and cache it
    void pushCellObject(lua_State* L, Cell* cell) const;

    // Cell object cache (UUID string -> Lua registry reference)
    // Uses weak table in Lua to allow garbage collection
    int cellCacheRef_{-1};  // -1 = LUA_NOREF

    // Cell metatable reference for __index access to .ref property
    int cellMetatableRef_{-1};

    // Cell __index metamethod (handles .ref property access)
    static int luaCellIndex(lua_State* L);

    // Cell __newindex metamethod (handles cell.value = x assignment)
    static int luaCellNewIndex(lua_State* L);

    // Sheet object support
    int sheetMetatableRef_{-1};  // Sheet metatable for __index/__newindex

    // Sheet object metamethods
    static int luaSheetIndex(lua_State* L);     // Get sheet.name
    static int luaSheetNewIndex(lua_State* L);  // Set sheet.name = "..."

    // __tostring metamethods
    static int luaCellToString(lua_State* L);   // print(cell) → "Cell<A1>"
    static int luaSheetToString(lua_State* L);  // print(sheet) → "Sheet<Name>"

    // Helper: Create a sheet Lua object
    void pushSheetObject(lua_State* L, Sheet* sheet) const;
};

}  // namespace cells

#endif  // CELLS_LUAU_SANDBOX_H_
