# Formula Engine

## Overview

The formula engine parses Excel-style formulas into an AST and executes them natively in C/C++, managing the dependency graph for reactive updates.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Excel        │    │     AST      │    │   Native     │
│ Formula      │───►│    Parser    │───►│  Execution   │
│ "=SUM(A1:B2)"│    │              │    │  (C/C++)     │
└──────────────┘    └──────────────┘    └──────────────┘
                                               │
                          ┌────────────────────┘
                          ▼
                   ┌──────────────┐
                   │ Dependency   │
                   │ Graph        │
                   │ (for recalc) │
                   └──────────────┘
```

## Why Native AST Execution

Instead of transpiling to a scripting language, we execute the AST directly in C/C++:

| Approach | Pros | Cons |
|----------|------|------|
| **Native AST** | No dependencies, full control, simpler build | Must implement all functions |
| Scripting (Lua/etc) | Existing ecosystem | Extra dependency, context switching |

Benefits of native execution:
- **Simpler architecture**: No codegen step, no runtime embedding
- **Better performance**: No interpreter overhead, direct function calls
- **Easier debugging**: Stack traces are native, not VM traces
- **Smaller binary**: No embedded runtime
- **Full control**: Custom memory management, precise error handling

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

## Phase 4: Native AST Execution

Execute the AST directly in C/C++ via a tree-walking interpreter:

```c
typedef struct ExecContext {
    Sheet* sheet;
    Cell* cell;               // Current cell being evaluated
    int recursion_depth;      // For cycle detection
    int max_recursion;        // Limit (e.g., 1000)
} ExecContext;

typedef struct ExecResult {
    bool success;
    CellValue value;
    char* error;
} ExecResult;

ExecResult execute_ast(ASTNode* node, ExecContext* ctx) {
    switch (node->type) {
        case AST_NUMBER:
            return (ExecResult){
                .success = true,
                .value = {.type = CELL_NUMBER, .number = node->number}
            };

        case AST_STRING:
            return (ExecResult){
                .success = true,
                .value = {.type = CELL_STRING, .string = strdup(node->string)}
            };

        case AST_CELL_REF: {
            Cell* ref_cell = resolve_cell_ref(ctx->sheet, node);
            if (!ref_cell) {
                return (ExecResult){.success = false, .error = "#REF!"};
            }
            return (ExecResult){.success = true, .value = ref_cell->value};
        }

        case AST_BINARY_OP: {
            ExecResult left = execute_ast(node->binary_op.left, ctx);
            if (!left.success) return left;

            ExecResult right = execute_ast(node->binary_op.right, ctx);
            if (!right.success) return right;

            return eval_binary_op(node->binary_op.op, left.value, right.value);
        }

        case AST_FUNCTION_CALL:
            return call_function(node->function_call.name,
                                 node->function_call.args,
                                 node->function_call.arg_count, ctx);

        // ... other cases
    }
}
```

### Binary Operator Implementation

```c
ExecResult eval_binary_op(const char* op, CellValue left, CellValue right) {
    // Coerce to numbers for arithmetic
    if (strcmp(op, "+") == 0) {
        double l = to_number(left);
        double r = to_number(right);
        return (ExecResult){
            .success = true,
            .value = {.type = CELL_NUMBER, .number = l + r}
        };
    }

    if (strcmp(op, "&") == 0) {
        // String concatenation
        char* l = to_string(left);
        char* r = to_string(right);
        char* result = concat_strings(l, r);
        return (ExecResult){
            .success = true,
            .value = {.type = CELL_STRING, .string = result}
        };
    }

    // Comparison operators
    if (strcmp(op, "=") == 0 || strcmp(op, "<") == 0 /* ... */) {
        return eval_comparison(op, left, right);
    }

    return (ExecResult){.success = false, .error = "#VALUE!"};
}
```

## Excel Function Library

Implement Excel functions natively in C/C++:

```c
// Function registry
typedef ExecResult (*ExcelFn)(CellValue* args, int arg_count, ExecContext* ctx);

typedef struct FunctionDef {
    const char* name;
    ExcelFn fn;
    int min_args;
    int max_args;  // -1 for variadic
} FunctionDef;

// Function implementations
ExecResult fn_sum(CellValue* args, int arg_count, ExecContext* ctx) {
    double total = 0;
    for (int i = 0; i < arg_count; i++) {
        if (args[i].type == CELL_ARRAY) {
            // Iterate array/range
            for (int j = 0; j < args[i].array->count; j++) {
                if (args[i].array->values[j].type == CELL_NUMBER) {
                    total += args[i].array->values[j].number;
                }
            }
        } else if (args[i].type == CELL_NUMBER) {
            total += args[i].number;
        }
        // Skip non-numbers (Excel behavior)
    }
    return (ExecResult){
        .success = true,
        .value = {.type = CELL_NUMBER, .number = total}
    };
}

ExecResult fn_if(CellValue* args, int arg_count, ExecContext* ctx) {
    if (arg_count < 2) {
        return (ExecResult){.success = false, .error = "#VALUE!"};
    }

    bool condition = to_boolean(args[0]);
    if (condition) {
        return (ExecResult){.success = true, .value = args[1]};
    } else if (arg_count >= 3) {
        return (ExecResult){.success = true, .value = args[2]};
    } else {
        return (ExecResult){
            .success = true,
            .value = {.type = CELL_BOOLEAN, .boolean = false}
        };
    }
}

ExecResult fn_vlookup(CellValue* args, int arg_count, ExecContext* ctx) {
    // lookup_value, table_array, col_index, [range_lookup]
    // ... implementation
}

// Function registry
static FunctionDef functions[] = {
    {"SUM",     fn_sum,     1, -1},
    {"AVERAGE", fn_average, 1, -1},
    {"COUNT",   fn_count,   1, -1},
    {"IF",      fn_if,      2, 3},
    {"VLOOKUP", fn_vlookup, 3, 4},
    {"INDEX",   fn_index,   2, 3},
    {"MATCH",   fn_match,   2, 3},
    // ... hundreds more
    {NULL, NULL, 0, 0}  // Sentinel
};

ExecResult call_function(const char* name, ASTNode** args, int arg_count,
                          ExecContext* ctx) {
    // Find function
    for (int i = 0; functions[i].name; i++) {
        if (strcasecmp(functions[i].name, name) == 0) {
            FunctionDef* fn = &functions[i];

            // Validate arg count
            if (arg_count < fn->min_args ||
                (fn->max_args >= 0 && arg_count > fn->max_args)) {
                return (ExecResult){.success = false, .error = "#VALUE!"};
            }

            // Evaluate arguments
            CellValue* evaluated = malloc(arg_count * sizeof(CellValue));
            for (int j = 0; j < arg_count; j++) {
                ExecResult r = execute_ast(args[j], ctx);
                if (!r.success) {
                    free(evaluated);
                    return r;
                }
                evaluated[j] = r.value;
            }

            ExecResult result = fn->fn(evaluated, arg_count, ctx);
            free(evaluated);
            return result;
        }
    }

    return (ExecResult){.success = false, .error = "#NAME?"};
}
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
            ExecContext ctx = {.sheet = sheet, .cell = cell};
            ExecResult result = execute_ast(cell->formula->ast, &ctx);
            if (result.success) {
                cell->value = result.value;
            } else {
                cell->error = parse_error(result.error);
            }
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

Native C/C++ compiles cleanly to WebAssembly via Emscripten:

1. **No special runtime**: Same code runs native and WASM
2. **Memory**: Pre-allocate pools for cells, AST nodes
3. **Async**: Long calculations can yield via Emscripten's asyncify

```c
// WASM-friendly execution with yielding
static int iteration_count = 0;

void maybe_yield() {
    iteration_count++;
    if (iteration_count > 100000) {
        #ifdef __EMSCRIPTEN__
        emscripten_sleep(0);  // Yield to browser event loop
        #endif
        iteration_count = 0;
    }
}

ExecResult execute_ast_wasm(ASTNode* node, ExecContext* ctx) {
    maybe_yield();
    return execute_ast(node, ctx);
}
```

## Performance Optimizations

1. **AST caching**: Parse formula once, store AST for reuse
2. **Batch recalc**: Group multiple changes, recalc once
3. **Parallel recalc**: Independent branches can run in parallel
4. **Lazy evaluation**: Only calc visible cells first
5. **Inline hot functions**: Mark SUM, IF, etc. for inlining

## Testing Strategy

1. **Parser tests**: Verify AST structure for various formulas
2. **Execution tests**: Compare results with Excel/Sheets
3. **Edge cases**: Errors, empty cells, type coercion
4. **Performance tests**: Large ranges, complex formulas
