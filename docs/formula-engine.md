# Formula Engine

## Overview

The formula engine transforms Excel-style formulas into executable Luau code, runs them in a sandbox, and manages the dependency graph for reactive updates.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Excel        │    │     AST      │    │   Luau       │    │  Sandboxed   │
│ Formula      │───►│    Parser    │───►│  Codegen     │───►│  Execution   │
│ "=SUM(A1:B2)"│    │              │    │              │    │              │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
                                                                   │
                           ┌───────────────────────────────────────┘
                           ▼
                    ┌──────────────┐
                    │ Dependency   │
                    │ Graph        │
                    │ (for recalc) │
                    └──────────────┘
```

## Why Luau (not Lua or LuaJIT)

| | Lua 5.4 | LuaJIT | Luau |
|---|---------|--------|------|
| **Performance** | Baseline | ~10x faster | ~2-5x faster than Lua 5.4 |
| **iOS/macOS App Store** | ✅ | ❌ (JIT forbidden) | ✅ |
| **WASM support** | ✅ | ❌ | ✅ |
| **Type annotations** | ❌ | ❌ | ✅ (optional) |
| **Sandboxing** | Manual | Manual | Built-in |
| **Maintenance** | Slow releases | Stalled | Active (Roblox) |

Luau: https://github.com/luau-lang/luau

## Phase 1: Lexer

Tokenize Excel formula syntax:

```c
typedef enum TokenType {
    // Literals
    TOK_NUMBER,           // 42, 3.14, 1E10
    TOK_STRING,           // "hello"
    TOK_BOOLEAN,          // TRUE, FALSE
    TOK_ERROR,            // #REF!, #VALUE!, etc.

    // References
    TOK_CELL_REF,         // A1, $A$1, Sheet1!A1
    TOK_RANGE_REF,        // A1:B2
    TOK_NAMED_RANGE,      // MyRange

    // Operators
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_CARET,            // ^
    TOK_PERCENT,          // %
    TOK_AMPERSAND,        // & (concat)
    TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE,

    // Punctuation
    TOK_LPAREN, TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,        // European argument separator

    // Functions
    TOK_IDENTIFIER,       // SUM, VLOOKUP, custom names

    TOK_EOF
} TokenType;

typedef struct Token {
    TokenType type;
    char* lexeme;
    int line, col;
} Token;
```

## Phase 2: Parser (AST)

Build an abstract syntax tree:

```c
typedef enum ASTNodeType {
    AST_NUMBER,
    AST_STRING,
    AST_BOOLEAN,
    AST_ERROR,
    AST_CELL_REF,
    AST_RANGE_REF,
    AST_NAMED_RANGE,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_FUNCTION_CALL,
    AST_ARRAY_LITERAL,    // {1,2;3,4}
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    union {
        double number;
        char* string;
        bool boolean;

        struct {
            char* sheet;       // NULL if same sheet
            char* col;         // "A", "$A"
            char* row;         // "1", "$1"
            bool col_absolute;
            bool row_absolute;
        } cell_ref;

        struct {
            ASTNode* start;
            ASTNode* end;
        } range_ref;

        struct {
            char* op;          // "+", "-", "*", "/", etc.
            ASTNode* left;
            ASTNode* right;
        } binary_op;

        struct {
            char* op;
            ASTNode* operand;
        } unary_op;

        struct {
            char* name;        // "SUM", "IF", etc.
            ASTNode** args;
            int arg_count;
        } function_call;
    };
} ASTNode;
```

### Example Parse

`=IF(A1>10, SUM(B1:B10), "small")`

```
FunctionCall("IF")
├── BinaryOp(">")
│   ├── CellRef(A1)
│   └── Number(10)
├── FunctionCall("SUM")
│   └── RangeRef(B1:B10)
└── String("small")
```

## Phase 3: Semantic Analysis

### Reference Resolution
Convert A1-style references to UUID-based references:

```c
typedef struct ResolvedRef {
    uuid_t sheet_id;      // Resolved sheet UUID
    uuid_t col_id;        // Resolved column UUID
    uuid_t row_id;        // Resolved row UUID
    bool col_absolute;    // For copy/paste behavior
    bool row_absolute;
} ResolvedRef;

// During analysis:
ASTNode* resolve_references(ASTNode* ast, Sheet* context_sheet,
                            Cell* context_cell);
```

### Dependency Extraction
Extract all cells this formula depends on:

```c
typedef struct Dependencies {
    uuid_t* cell_ids;
    int count;
    bool has_volatile;    // NOW(), RAND(), etc.
} Dependencies;

Dependencies* extract_dependencies(ASTNode* ast);
```

## Phase 4: Lua Code Generation

Transform AST to Lua code:

### Example Transformations

| Excel | Lua |
|-------|-----|
| `=A1+B1` | `return cell("uuid-a1") + cell("uuid-b1")` |
| `=SUM(A1:A10)` | `return excel.SUM(range("uuid-a1","uuid-a10"))` |
| `=IF(A1>0,A1,-A1)` | `return excel.IF(cell("uuid-a1")>0, cell("uuid-a1"), -cell("uuid-a1"))` |
| `=A1&" "&B1` | `return tostring(cell("uuid-a1")).." "..tostring(cell("uuid-b1"))` |
| `="Hello"` | `return "Hello"` |

### Code Generator

```c
typedef struct LuaCodegen {
    StringBuilder* output;
    int temp_counter;
} LuaCodegen;

char* generate_lua(ASTNode* ast) {
    LuaCodegen gen = {0};
    sb_append(&gen.output, "return ");
    emit_node(&gen, ast);
    return sb_to_string(gen.output);
}

void emit_node(LuaCodegen* gen, ASTNode* node) {
    switch (node->type) {
        case AST_NUMBER:
            sb_appendf(gen->output, "%g", node->number);
            break;

        case AST_CELL_REF:
            sb_appendf(gen->output, "cell(\"%s\")", node->resolved_id);
            break;

        case AST_FUNCTION_CALL:
            sb_appendf(gen->output, "excel.%s(", node->function_call.name);
            for (int i = 0; i < node->function_call.arg_count; i++) {
                if (i > 0) sb_append(gen->output, ",");
                emit_node(gen, node->function_call.args[i]);
            }
            sb_append(gen->output, ")");
            break;
        // ...
    }
}
```

## Phase 5: Luau Sandbox

### Sandbox Setup

```c
#include "luacode.h"  // Luau compiler
#include "lua.h"
#include "lualib.h"

lua_State* create_formula_sandbox() {
    lua_State* L = luaL_newstate();

    // Luau: Use restricted sandbox mode (no dangerous globals by default)
    luaL_sandboxthread(L);

    // Load only safe libraries
    luaL_openlibs(L);  // Luau's openlibs is already sandboxed

    // Register our spreadsheet functions
    register_cell_function(L);      // cell("uuid") -> value
    register_range_function(L);     // range("start", "end") -> iterator
    register_excel_namespace(L);    // excel.SUM, excel.IF, etc.

    // Set memory limit (Luau has built-in support)
    lua_setmemcat(L, 0);  // Memory category for tracking
    // Custom allocator for hard limits:
    lua_setallocf(L, limited_allocator, &memory_limit);

    return L;
}

// Compile Excel formula to Luau bytecode
CompiledFormula* compile_formula(const char* luau_source) {
    // Luau compilation options
    lua_CompileOptions options = {0};
    options.optimizationLevel = 2;
    options.debugLevel = 0;  // No debug info in production

    size_t bytecode_size;
    char* bytecode = luau_compile(luau_source, strlen(luau_source),
                                   &options, &bytecode_size);

    if (bytecode == NULL) {
        return NULL;  // Compilation error
    }

    CompiledFormula* cf = malloc(sizeof(CompiledFormula));
    cf->bytecode = bytecode;
    cf->bytecode_size = bytecode_size;
    return cf;
}
```

### Luau Type Annotations (Generated)

Generated Luau code can include type hints for better performance:

```lua
-- Generated from: =SUM(A1:B10) * 2
local function formula(): number
    return excel.SUM(range("uuid-a1", "uuid-b10")) * 2
end
return formula()
```

### Execution with Limits

```c
typedef struct ExecResult {
    bool success;
    CellValue value;
    char* error;
    int cycles_used;
} ExecResult;

ExecResult execute_formula(Cell* cell, Sheet* sheet) {
    lua_State* L = get_sandbox();

    // Set execution context
    set_context(L, sheet, cell);

    // Set instruction limit (prevent infinite loops)
    lua_sethook(L, instruction_limit_hook, LUA_MASKCOUNT, 10000);

    // Load and execute
    if (luaL_loadstring(L, cell->compiled->lua_code) != 0) {
        return (ExecResult){.success = false, .error = lua_tostring(L, -1)};
    }

    int status = lua_pcall(L, 0, 1, 0);
    if (status != 0) {
        return (ExecResult){.success = false, .error = lua_tostring(L, -1)};
    }

    // Extract result
    CellValue value = lua_to_cell_value(L, -1);
    lua_pop(L, 1);

    return (ExecResult){.success = true, .value = value};
}
```

## Excel Function Library

Implement Excel functions in Luau (with optional type annotations for performance):

```lua
-- excel_functions.luau (loaded into sandbox)
local excel = {}

function excel.SUM(...)
    local total = 0
    for _, v in ipairs(flatten({...})) do
        if type(v) == "number" then
            total = total + v
        end
    end
    return total
end

function excel.IF(condition, true_val, false_val)
    if condition then
        return true_val
    else
        return false_val or false
    end
end

function excel.VLOOKUP(lookup_value, table_array, col_index, range_lookup)
    -- Implementation...
end

function excel.INDEX(array, row_num, col_num)
    -- Implementation...
end

-- ... hundreds more
```

### Function Categories to Implement

| Category | Examples | Priority |
|----------|----------|----------|
| Math | SUM, AVERAGE, MIN, MAX, COUNT | P0 |
| Logic | IF, AND, OR, NOT, IFS | P0 |
| Text | CONCATENATE, LEFT, RIGHT, MID, LEN | P0 |
| Lookup | VLOOKUP, HLOOKUP, INDEX, MATCH | P0 |
| Date/Time | NOW, TODAY, DATE, YEAR, MONTH | P1 |
| Statistical | STDEV, VAR, MEDIAN, PERCENTILE | P1 |
| Financial | PMT, NPV, IRR | P2 |
| Array | FILTER, SORT, UNIQUE (Excel 365) | P2 |

## Dependency Graph & Recalculation

### Graph Structure

```c
typedef struct DepGraph {
    // cell_id -> list of cells that depend on it
    HashMap* dependents;
    // cell_id -> list of cells it depends on
    HashMap* dependencies;
    // Cells with volatile functions (need recalc every time)
    HashSet* volatile_cells;
} DepGraph;
```

### Topological Recalculation

When cell X changes, recalculate all dependents in correct order:

```c
void recalculate(Sheet* sheet, uuid_t changed_cell) {
    // 1. Find all affected cells (transitive dependents)
    HashSet* affected = find_all_dependents(sheet->dep_graph, changed_cell);

    // 2. Topological sort
    uuid_t* order = topological_sort(sheet->dep_graph, affected);

    // 3. Recalculate in order
    for (int i = 0; i < array_len(order); i++) {
        Cell* cell = cell_get(sheet, order[i]);
        if (cell->formula) {
            ExecResult result = execute_formula(cell, sheet);
            cell->value = result.value;
        }
    }
}
```

### Circular Reference Detection

```c
bool has_circular_ref(DepGraph* graph, uuid_t cell_id, Dependencies* new_deps) {
    // DFS from each dependency to see if we can reach cell_id
    for (int i = 0; i < new_deps->count; i++) {
        if (can_reach(graph, new_deps->cell_ids[i], cell_id)) {
            return true;
        }
    }
    return false;
}
```

## WASM Considerations

Luau compiles cleanly to WebAssembly:

1. **No JIT dependency**: Luau's interpreter works everywhere
2. **Memory**: Pre-allocate Luau state pool for reuse
3. **Async**: Long calculations yield to browser via Luau's interrupt mechanism

```c
// WASM-friendly execution with yielding
static int instruction_count = 0;

void interrupt_callback(lua_State* L, int gc) {
    instruction_count++;
    if (instruction_count > 100000) {
        // Yield to browser event loop
        emscripten_sleep(0);  // WASM-specific
        instruction_count = 0;
    }
}

ExecResult execute_formula_wasm(Cell* cell, Sheet* sheet) {
    lua_State* L = get_sandbox();

    // Set interrupt callback (Luau feature)
    lua_callbacks(L)->interrupt = interrupt_callback;

    // Load bytecode (already compiled)
    luau_load(L, "formula", cell->compiled->bytecode,
              cell->compiled->bytecode_size, 0);

    // Execute
    int status = lua_pcall(L, 0, 1, 0);
    // ...
}
```

Luau's bytecode is portable - compile once, run on native and WASM.

## Performance Optimizations

1. **Bytecode caching**: Compile formula to Luau bytecode once, reuse
2. **Batch recalc**: Group multiple changes, recalc once
3. **Parallel recalc**: Independent branches can run in parallel
4. **Lazy evaluation**: Only calc visible cells first
5. **Luau optimizations**: Type annotations enable faster codegen

## Testing Strategy

1. **Parser tests**: Verify AST structure for various formulas
2. **Codegen tests**: Check generated Lua is correct
3. **Execution tests**: Compare results with Excel/Sheets
4. **Edge cases**: Errors, empty cells, type coercion
5. **Performance tests**: Large ranges, complex formulas
