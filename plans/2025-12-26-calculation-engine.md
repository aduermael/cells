# Calculation Engine

Status: IN PROGRESS
Created At: 2025-12-26 01:22 UTC
Updated At: 2025-12-26 04:51 UTC
Following plan management guidelines defined in AGENTS.md

## Overview

Implement a pure C++ calculation engine that evaluates parsed formula ASTs and displays actual results. The formula parser, AST, reference resolution, and dependency graph are already complete. Now we need to:

1. **Evaluate AST nodes** to compute values
2. **Implement core functions** (SUM, IF, AVERAGE, etc.)
3. **Handle type coercion** (string ↔ number ↔ boolean)
4. **Propagate errors** (#DIV/0!, #VALUE!, #REF!, etc.)
5. **Trigger recalculation** when dependencies change
6. **Integrate with WASM** for web UI

### Design Decisions

**Pure C++ Interpreter**: No scripting language (Luau/Lua). Direct AST traversal with C++ function implementations. This keeps the stack simple, performant, and portable to WASM.

**Lazy Evaluation**: Formulas only recalculate when marked dirty or when a dependency changes. The `DependencyGraph::getRecalcOrder()` provides topological sort for efficient batch recalculation.

**Value Representation**: Results stored in `Cell.value` (CellValue struct with raw string, type, and error fields). The calculation engine produces `EvalResult` which is then stored appropriately.

**Error Propagation**: Errors propagate through calculations. If any input is an error, the output is typically that error (with some function-specific exceptions like IFERROR).

**CRDT Contract**: This is a collaborative application using CRDTs for real-time sync. The calculation engine is **read-only** with respect to the Workbook—it must **never** introduce Workbook operations. Formula results are computed on-demand, not cached in the CRDT state. Each client evaluates formulas locally from the same synced input data, ensuring consistent results without sync overhead for computed values. This keeps the CRDT layer simple (only user edits sync) and avoids conflicts from derived data.

### Testing Philosophy

**This is critical infrastructure. Tests must be comprehensive and pass after each step.**

Every function gets its own test cases covering:
- Normal inputs (happy path)
- Edge cases (empty, single item, large numbers)
- Type coercion scenarios
- Error inputs and outputs
- Comparison with Excel/Numbers behavior

---

## Phase 1: Evaluation Foundation

Build the core evaluation infrastructure that traverses AST nodes and computes values.

### 1a: Define EvalResult and evaluation context

Create `core/cells/formula_eval.h`:

```cpp
// Result of evaluating a formula or sub-expression
struct EvalResult {
    enum class Type { NUMBER, STRING, BOOLEAN, ERROR, ARRAY };
    Type type;
    double numberValue;
    std::string stringValue;
    bool boolValue;
    CellError error;
    std::vector<EvalResult> arrayValue;  // For array formulas (future)

    // Factory methods
    static EvalResult Number(double v);
    static EvalResult String(std::string v);
    static EvalResult Boolean(bool v);
    static EvalResult Error(CellError e);

    // Type coercion
    double toNumber() const;      // String→number, bool→0/1, error→propagate
    std::string toString() const; // Number→string, bool→"TRUE"/"FALSE"
    bool toBoolean() const;       // Number→(!=0), String→error, error→propagate
    bool isError() const;
    bool isNumber() const;
    bool isString() const;
    bool isBoolean() const;
};

// Context for evaluation (sheet access, cell positions, etc.)
struct EvalContext {
    Sheet* sheet;
    Workbook* workbook;
    ID currentCellId;  // For relative reference resolution
    int recursionDepth;
    static const int MAX_RECURSION = 1000;

    // Circular reference detection during evaluation
    std::unordered_set<ID>* evaluatingCells;
};
```

**Test**: Header compiles, EvalResult construction and type coercion works

- [x] 1a: Define EvalResult struct and EvalContext in formula_eval.h

### 1b: Implement basic literal evaluation

Create `core/cells/formula_eval.cc`:

```cpp
EvalResult evaluate(const ASTNode* node, EvalContext& ctx);

// Literal evaluation
EvalResult evaluateLiteral(const ASTNode* node) {
    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL:
            return EvalResult::Number(static_cast<const NumberLiteralNode*>(node)->value);
        case ASTNodeType::STRING_LITERAL:
            return EvalResult::String(static_cast<const StringLiteralNode*>(node)->value);
        case ASTNodeType::BOOLEAN_LITERAL:
            return EvalResult::Boolean(static_cast<const BooleanLiteralNode*>(node)->value);
        default:
            return EvalResult::Error(CellError::VALUE);
    }
}
```

**Test**: Evaluating literals returns correct values

- [x] 1b: Implement literal node evaluation (NUMBER, STRING, BOOLEAN)

### 1c: Implement cell reference evaluation

```cpp
EvalResult evaluateCellRef(const CellRefNode* node, EvalContext& ctx) {
    // Check for circular reference
    if (ctx.evaluatingCells->count(node->cellId)) {
        return EvalResult::Error(CellError::CIRCULAR);
    }

    Cell* cell = ctx.sheet->getCell(node->cellId);
    if (!cell) {
        return EvalResult::Error(CellError::REF);
    }

    // If cell has a formula that needs evaluation, evaluate it
    if (cell->formula && cell->formula->dirty) {
        // This triggers recursive evaluation
        evaluateCell(cell, ctx);
    }

    return cellValueToEvalResult(cell->value);
}
```

**Test**: Cell references resolve to their values

- [x] 1c: Implement cell reference evaluation with circular reference check

### 1d: Implement binary operators

```cpp
EvalResult evaluateBinaryOp(const BinaryOpNode* node, EvalContext& ctx) {
    EvalResult left = evaluate(node->left, ctx);
    EvalResult right = evaluate(node->right, ctx);

    // Error propagation
    if (left.isError()) return left;
    if (right.isError()) return right;

    switch (node->op) {
        case BinaryOp::ADD:
            return EvalResult::Number(left.toNumber() + right.toNumber());
        case BinaryOp::SUBTRACT:
            return EvalResult::Number(left.toNumber() - right.toNumber());
        case BinaryOp::MULTIPLY:
            return EvalResult::Number(left.toNumber() * right.toNumber());
        case BinaryOp::DIVIDE:
            if (right.toNumber() == 0) return EvalResult::Error(CellError::DIV);
            return EvalResult::Number(left.toNumber() / right.toNumber());
        case BinaryOp::POWER:
            return EvalResult::Number(std::pow(left.toNumber(), right.toNumber()));
        case BinaryOp::CONCAT:
            return EvalResult::String(left.toString() + right.toString());
        // Comparison operators...
    }
}
```

**Test**: All arithmetic and comparison operators work correctly

- [x] 1d: Implement binary operators (arithmetic, comparison, concat)

### 1e: Implement unary operators

```cpp
EvalResult evaluateUnaryOp(const UnaryOpNode* node, EvalContext& ctx) {
    EvalResult operand = evaluate(node->operand, ctx);
    if (operand.isError()) return operand;

    switch (node->op) {
        case UnaryOp::NEGATE:
            return EvalResult::Number(-operand.toNumber());
        case UnaryOp::POSITIVE:
            return EvalResult::Number(operand.toNumber());
    }
}
```

**Test**: Unary plus and minus work correctly

- [x] 1e: Implement unary operators (negate, positive)

### 1f: Add comprehensive evaluator tests

Create `core/cells/formula_eval_test.cc`:

**Literal Tests:**
- Integer: `=42` → 42
- Decimal: `=3.14` → 3.14
- Scientific: `=1.5e10` → 15000000000
- Negative: `=-5` → -5
- String: `="hello"` → "hello"
- Empty string: `=""` → ""
- Boolean TRUE: `=TRUE` → true
- Boolean FALSE: `=FALSE` → false

**Arithmetic Operator Tests:**
- Addition: `=2+3` → 5
- Subtraction: `=10-4` → 6
- Multiplication: `=3*4` → 12
- Division: `=15/3` → 5
- Division by zero: `=1/0` → #DIV/0!
- Power: `=2^10` → 1024
- Negative power: `=4^-1` → 0.25
- Zero power: `=5^0` → 1
- Operator precedence: `=2+3*4` → 14
- Parentheses: `=(2+3)*4` → 20
- Complex: `=2+3*4-6/2` → 11
- Unary minus: `=-5+3` → -2
- Double negative: `=--5` → 5

**Comparison Operator Tests:**
- Equal numbers: `=5=5` → true
- Not equal numbers: `=5<>3` → true
- Less than: `=3<5` → true
- Less equal: `=5<=5` → true
- Greater than: `=5>3` → true
- Greater equal: `=3>=5` → false
- String comparison: `="a"<"b"` → true
- Mixed type comparison: `="5"=5` → true (after coercion)

**Concatenation Tests:**
- String concat: `="hello"&"world"` → "helloworld"
- Number concat: `=1&2` → "12"
- Mixed concat: `="value: "&100` → "value: 100"

**Cell Reference Tests:**
- Simple ref: A1=10, `=A1` → 10
- Reference chain: A1=5, B1=A1, `=B1` → 5
- Empty cell ref: `=Z99` → 0 (empty cells are 0)
- Self reference: `=A1` in A1 → #CIRCULAR!

**Type Coercion Tests:**
- String to number: `="5"+3` → 8
- Invalid string to number: `="abc"+3` → #VALUE!
- Boolean to number: `=TRUE+1` → 2
- Number to string in concat: `=5&""` → "5"
- Boolean to string: `=TRUE&""` → "TRUE"

**Error Propagation Tests:**
- Error in left operand: `=#REF!+5` → #REF!
- Error in right operand: `=5+#DIV/0!` → #DIV/0!
- Error in nested expr: `=(1+#VALUE!)*2` → #VALUE!

**Test**: All evaluator tests pass (80+ tests)

- [x] 1f: Add comprehensive evaluator tests (80+ tests minimum) - **88 tests implemented**

---

## Phase 2: Range Evaluation

Implement evaluation of range references for use in aggregate functions.

### 2a: Implement range iteration

```cpp
// Iterate over all cells in a range
struct RangeIterator {
    Sheet* sheet;
    ID startCol, endCol, startRow, endRow;

    void forEach(std::function<void(Cell*, int col, int row)> callback);
    std::vector<EvalResult> toVector(EvalContext& ctx);
    size_t count() const;
};

EvalResult evaluateRangeRef(const RangeRefNode* node, EvalContext& ctx) {
    // Ranges don't evaluate to a single value directly
    // They're consumed by functions like SUM, AVERAGE, etc.
    // Return a special "range" result that functions can iterate
    return EvalResult::Range(node->topLeftCellId, node->bottomRightCellId);
}
```

**Test**: Range iteration visits all cells in order

- [x] 2a: Implement range reference evaluation and RangeIterator

### 2b: Implement whole column/row references

```cpp
EvalResult evaluateColumnRef(const ColumnRefNode* node, EvalContext& ctx);
EvalResult evaluateRowRef(const RowRefNode* node, EvalContext& ctx);
EvalResult evaluateColumnRangeRef(const ColumnRangeRefNode* node, EvalContext& ctx);
EvalResult evaluateRowRangeRef(const RowRangeRefNode* node, EvalContext& ctx);
```

Whole column/row refs iterate over all populated cells in that column/row.

**Test**: Whole column/row references include all populated cells

- [x] 2b: Implement whole column/row reference evaluation

### 2c: Add range evaluation tests

**Range Iteration Tests:**
- Single cell range: `A1:A1` → 1 cell
- Single row: `A1:C1` → 3 cells
- Single column: `A1:A3` → 3 cells
- Rectangle: `A1:C3` → 9 cells
- Iteration order: row-major (A1, B1, C1, A2, B2, C2...)

**Whole Column/Row Tests:**
- Empty column: `A:A` → 0 populated cells
- Populated column: put values in A1,A5,A10 → `A:A` includes all 3
- Whole row: `1:1` → all populated cells in row 1
- Column range: `A:C` → all populated cells in columns A, B, C
- Row range: `1:5` → all populated cells in rows 1-5

**Test**: All range tests pass (25+ tests)

- [x] 2c: Add range evaluation tests (25+ tests minimum) - **33 tests implemented**

---

## Phase 3: Core Functions - Math

Implement essential mathematical functions.

### 3a: Function registry and dispatch

Create `core/cells/formula_functions.h`:

```cpp
// Function signature
using FormulaFunction = std::function<EvalResult(
    const std::vector<const ASTNode*>& args,
    EvalContext& ctx
)>;

// Function registry
class FunctionRegistry {
    std::unordered_map<std::string, FormulaFunction> functions;
    std::unordered_set<std::string> volatileFunctions;

public:
    static FunctionRegistry& instance();

    void registerFunction(const std::string& name, FormulaFunction fn, bool isVolatile = false);
    EvalResult call(const std::string& name, const std::vector<const ASTNode*>& args, EvalContext& ctx);
    bool exists(const std::string& name) const;
    bool isVolatile(const std::string& name) const;
};
```

**Test**: Function registry can register and call functions

- [ ] 3a: Implement function registry and dispatch system

### 3b: Implement SUM function

```cpp
// SUM(value1, [value2], ...)
// Adds all numbers in the argument list
// Ranges are expanded, strings/bools converted, errors propagate
EvalResult fn_SUM(const std::vector<const ASTNode*>& args, EvalContext& ctx);
```

**Test cases:**
- `=SUM(1,2,3)` → 6
- `=SUM(A1:A3)` where A1=1,A2=2,A3=3 → 6
- `=SUM(A1:B2)` 2x2 range → sum of all 4 cells
- `=SUM(1,A1:A3,10)` mixed args → sum all
- `=SUM()` no args → 0
- `=SUM("5",3)` string coercion → 8
- `=SUM(TRUE,1)` boolean coercion → 2
- `=SUM(A1:A3)` with error cell → propagate error
- `=SUM(A:A)` whole column → sum all populated cells

- [ ] 3b: Implement SUM function with comprehensive tests

### 3c: Implement AVERAGE function

```cpp
// AVERAGE(value1, [value2], ...)
// Returns arithmetic mean of numbers
EvalResult fn_AVERAGE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
```

**Test cases:**
- `=AVERAGE(1,2,3)` → 2
- `=AVERAGE(A1:A4)` where values are 10,20,30,40 → 25
- `=AVERAGE(1,2,3,4,5)` → 3
- `=AVERAGE()` → #DIV/0! (no values)
- `=AVERAGE(A1)` single cell → that cell's value
- `=AVERAGE("abc")` non-numeric → #VALUE!
- Empty cells in range are ignored (not counted as 0)

- [ ] 3c: Implement AVERAGE function with comprehensive tests

### 3d: Implement COUNT and COUNTA functions

```cpp
// COUNT(value1, [value2], ...) - counts numbers only
// COUNTA(value1, [value2], ...) - counts non-empty values
EvalResult fn_COUNT(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_COUNTA(const std::vector<const ASTNode*>& args, EvalContext& ctx);
```

**Test cases:**
- `=COUNT(1,2,3)` → 3
- `=COUNT(1,"two",3)` → 2 (skips string)
- `=COUNT(A1:A5)` with mixed types → count only numbers
- `=COUNT()` → 0
- `=COUNTA(1,"two",TRUE)` → 3
- `=COUNTA(A1:A5)` with some empty → count non-empty
- `=COUNTA()` → 0
- `=COUNT(A1:A3)` with A2 empty → 2 (empty not counted)

- [ ] 3d: Implement COUNT and COUNTA functions with tests

### 3e: Implement MIN and MAX functions

```cpp
EvalResult fn_MIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_MAX(const std::vector<const ASTNode*>& args, EvalContext& ctx);
```

**Test cases:**
- `=MIN(5,2,8,1)` → 1
- `=MAX(5,2,8,1)` → 8
- `=MIN(A1:C3)` → smallest in range
- `=MAX(A1:C3)` → largest in range
- `=MIN()` → 0 (Excel behavior)
- `=MAX()` → 0
- `=MIN(-5,0,5)` → -5
- `=MAX(-5,-10,-1)` → -1
- With error in range → propagate error

- [ ] 3e: Implement MIN and MAX functions with tests

### 3f: Implement basic math functions

```cpp
EvalResult fn_ABS(args, ctx);     // Absolute value
EvalResult fn_SQRT(args, ctx);    // Square root
EvalResult fn_POWER(args, ctx);   // Power (same as ^ operator)
EvalResult fn_ROUND(args, ctx);   // Round to decimal places
EvalResult fn_FLOOR(args, ctx);   // Round down
EvalResult fn_CEILING(args, ctx); // Round up
EvalResult fn_MOD(args, ctx);     // Modulo
EvalResult fn_INT(args, ctx);     // Truncate to integer
```

**Test cases (per function):**
- `=ABS(-5)` → 5, `=ABS(5)` → 5, `=ABS(0)` → 0
- `=SQRT(16)` → 4, `=SQRT(2)` → 1.414..., `=SQRT(-1)` → #NUM!
- `=POWER(2,3)` → 8, `=POWER(4,0.5)` → 2
- `=ROUND(2.5)` → 3, `=ROUND(2.567,2)` → 2.57, `=ROUND(123,-1)` → 120
- `=FLOOR(2.9)` → 2, `=FLOOR(-2.9)` → -3
- `=CEILING(2.1)` → 3, `=CEILING(-2.1)` → -2
- `=MOD(10,3)` → 1, `=MOD(10,0)` → #DIV/0!
- `=INT(5.9)` → 5, `=INT(-5.9)` → -6

- [ ] 3f: Implement basic math functions (ABS, SQRT, POWER, ROUND, FLOOR, CEILING, MOD, INT)

### 3g: Add Phase 3 math function tests

Create `core/cells/formula_functions_math_test.cc`:

Consolidate and run all math function tests. Target: 100+ test cases covering:
- Normal operation
- Edge cases (zero, negative, large numbers)
- Type coercion
- Error handling
- Range inputs where applicable

**Test**: All math function tests pass (100+ tests)

- [ ] 3g: Consolidate math function tests (100+ tests minimum)

---

## Phase 4: Core Functions - Logic

Implement logical functions.

### 4a: Implement IF function

```cpp
// IF(condition, value_if_true, [value_if_false])
EvalResult fn_IF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
```

**Test cases:**
- `=IF(TRUE,1,2)` → 1
- `=IF(FALSE,1,2)` → 2
- `=IF(1>0,"yes","no")` → "yes"
- `=IF(A1>10,A1*2,0)` → depends on A1
- `=IF(TRUE,1)` → 1 (no else = FALSE)
- `=IF(FALSE,1)` → FALSE
- `=IF("",1,2)` → #VALUE! (empty string is not boolean)
- `=IF(0,1,2)` → 2 (0 is false)
- `=IF(1,1,2)` → 1 (non-zero is true)
- Nested: `=IF(A1>0,IF(A1>10,"big","small"),"negative")`

- [ ] 4a: Implement IF function with comprehensive tests

### 4b: Implement AND, OR, NOT functions

```cpp
// AND(logical1, [logical2], ...) - all true?
// OR(logical1, [logical2], ...) - any true?
// NOT(logical) - invert
EvalResult fn_AND(args, ctx);
EvalResult fn_OR(args, ctx);
EvalResult fn_NOT(args, ctx);
```

**Test cases:**
- `=AND(TRUE,TRUE)` → TRUE
- `=AND(TRUE,FALSE)` → FALSE
- `=AND(1,1,1)` → TRUE
- `=AND()` → TRUE (vacuous truth)
- `=OR(FALSE,FALSE)` → FALSE
- `=OR(FALSE,TRUE)` → TRUE
- `=OR()` → FALSE
- `=NOT(TRUE)` → FALSE
- `=NOT(FALSE)` → TRUE
- `=NOT(0)` → TRUE
- `=NOT(1)` → FALSE
- `=AND(A1>0,A1<10)` with A1=5 → TRUE

- [ ] 4b: Implement AND, OR, NOT functions with tests

### 4c: Implement IFERROR and IFNA functions

```cpp
// IFERROR(value, value_if_error) - catch any error
// IFNA(value, value_if_na) - catch only #N/A (future)
EvalResult fn_IFERROR(args, ctx);
EvalResult fn_IFNA(args, ctx);
```

**Test cases:**
- `=IFERROR(1/0,0)` → 0
- `=IFERROR(5,0)` → 5
- `=IFERROR(A1,0)` where A1=#REF! → 0
- `=IFERROR(SQRT(-1),"invalid")` → "invalid"

- [ ] 4c: Implement IFERROR and IFNA functions with tests

### 4d: Implement comparison functions

```cpp
// EXACT(text1, text2) - case-sensitive string comparison
// ISBLANK(value) - is cell empty?
// ISNUMBER(value) - is value a number?
// ISTEXT(value) - is value text?
// ISERROR(value) - is value an error?
EvalResult fn_EXACT(args, ctx);
EvalResult fn_ISBLANK(args, ctx);
EvalResult fn_ISNUMBER(args, ctx);
EvalResult fn_ISTEXT(args, ctx);
EvalResult fn_ISERROR(args, ctx);
```

**Test cases:**
- `=EXACT("abc","ABC")` → FALSE (case-sensitive)
- `=EXACT("abc","abc")` → TRUE
- `=ISBLANK(A1)` where A1 is empty → TRUE
- `=ISBLANK(0)` → FALSE
- `=ISNUMBER(5)` → TRUE
- `=ISNUMBER("5")` → FALSE
- `=ISTEXT("hello")` → TRUE
- `=ISTEXT(5)` → FALSE
- `=ISERROR(1/0)` → TRUE
- `=ISERROR(5)` → FALSE

- [ ] 4d: Implement comparison/type-checking functions with tests

### 4e: Add Phase 4 logic function tests

Create `core/cells/formula_functions_logic_test.cc`:

Consolidate all logic function tests. Target: 60+ test cases.

**Test**: All logic function tests pass (60+ tests)

- [ ] 4e: Consolidate logic function tests (60+ tests minimum)

---

## Phase 5: Core Functions - Text

Implement string manipulation functions.

### 5a: Implement basic text functions

```cpp
// LEN(text) - length of string
// LEFT(text, [num_chars]) - leftmost characters
// RIGHT(text, [num_chars]) - rightmost characters
// MID(text, start_num, num_chars) - substring
// TRIM(text) - remove extra spaces
EvalResult fn_LEN(args, ctx);
EvalResult fn_LEFT(args, ctx);
EvalResult fn_RIGHT(args, ctx);
EvalResult fn_MID(args, ctx);
EvalResult fn_TRIM(args, ctx);
```

**Test cases:**
- `=LEN("hello")` → 5
- `=LEN("")` → 0
- `=LEN(123)` → 3 (coerced to "123")
- `=LEFT("hello",2)` → "he"
- `=LEFT("hello")` → "h" (default 1)
- `=RIGHT("hello",2)` → "lo"
- `=MID("hello",2,3)` → "ell" (1-indexed)
- `=TRIM("  hello  world  ")` → "hello world"

- [ ] 5a: Implement basic text functions (LEN, LEFT, RIGHT, MID, TRIM)

### 5b: Implement case functions

```cpp
// UPPER(text) - convert to uppercase
// LOWER(text) - convert to lowercase
// PROPER(text) - capitalize each word
EvalResult fn_UPPER(args, ctx);
EvalResult fn_LOWER(args, ctx);
EvalResult fn_PROPER(args, ctx);
```

**Test cases:**
- `=UPPER("hello")` → "HELLO"
- `=LOWER("HELLO")` → "hello"
- `=PROPER("hello world")` → "Hello World"
- `=PROPER("mR. SMITH")` → "Mr. Smith"

- [ ] 5b: Implement case functions (UPPER, LOWER, PROPER)

### 5c: Implement search and replace functions

```cpp
// FIND(find_text, within_text, [start_num]) - case-sensitive search
// SEARCH(find_text, within_text, [start_num]) - case-insensitive search
// SUBSTITUTE(text, old_text, new_text, [instance_num])
// REPLACE(old_text, start_num, num_chars, new_text)
EvalResult fn_FIND(args, ctx);
EvalResult fn_SEARCH(args, ctx);
EvalResult fn_SUBSTITUTE(args, ctx);
EvalResult fn_REPLACE(args, ctx);
```

**Test cases:**
- `=FIND("l","hello")` → 3 (1-indexed)
- `=FIND("L","hello")` → #VALUE! (case-sensitive, not found)
- `=SEARCH("L","hello")` → 3 (case-insensitive)
- `=SUBSTITUTE("hello","l","L")` → "heLLo" (all instances)
- `=SUBSTITUTE("hello","l","L",1)` → "heLlo" (first only)
- `=REPLACE("hello",2,3,"i")` → "hio"

- [ ] 5c: Implement search/replace functions (FIND, SEARCH, SUBSTITUTE, REPLACE)

### 5d: Implement concatenation and conversion

```cpp
// CONCAT(text1, [text2], ...) - join strings (newer)
// CONCATENATE(text1, [text2], ...) - join strings (legacy)
// TEXT(value, format_text) - format number as text
// VALUE(text) - convert text to number
EvalResult fn_CONCAT(args, ctx);
EvalResult fn_CONCATENATE(args, ctx);
EvalResult fn_TEXT(args, ctx);
EvalResult fn_VALUE(args, ctx);
```

**Test cases:**
- `=CONCAT("a","b","c")` → "abc"
- `=CONCATENATE("hello"," ","world")` → "hello world"
- `=TEXT(1234.5,"$#,##0.00")` → "$1,234.50" (simplified format support)
- `=TEXT(0.5,"0%")` → "50%"
- `=VALUE("123")` → 123
- `=VALUE("$100")` → 100 (strip currency)
- `=VALUE("abc")` → #VALUE!

- [ ] 5d: Implement concatenation and conversion functions

### 5e: Add Phase 5 text function tests

Create `core/cells/formula_functions_text_test.cc`:

Target: 80+ test cases covering all text functions.

**Test**: All text function tests pass (80+ tests)

- [ ] 5e: Consolidate text function tests (80+ tests minimum)

---

## Phase 6: Core Functions - Date/Time

Implement date and time functions.

### 6a: Implement date/time volatile functions

```cpp
// NOW() - current date and time (volatile)
// TODAY() - current date (volatile)
EvalResult fn_NOW(args, ctx);
EvalResult fn_TODAY(args, ctx);
```

These are volatile functions - they trigger recalculation on every change.

**Test cases:**
- `=NOW()` → returns current datetime as number (Excel serial date)
- `=TODAY()` → returns current date (integer)
- Both functions are marked volatile in registry

- [ ] 6a: Implement NOW and TODAY volatile functions

### 6b: Implement date construction functions

```cpp
// DATE(year, month, day) - construct date
// TIME(hour, minute, second) - construct time
// DATEVALUE(date_text) - parse date string
// TIMEVALUE(time_text) - parse time string
EvalResult fn_DATE(args, ctx);
EvalResult fn_TIME(args, ctx);
EvalResult fn_DATEVALUE(args, ctx);
EvalResult fn_TIMEVALUE(args, ctx);
```

**Test cases:**
- `=DATE(2024,1,15)` → serial date for Jan 15, 2024
- `=TIME(14,30,0)` → 0.604166... (fraction of day)
- `=DATEVALUE("2024-01-15")` → serial date
- `=TIMEVALUE("14:30:00")` → time fraction

- [ ] 6b: Implement date construction functions

### 6c: Implement date extraction functions

```cpp
// YEAR(date) - extract year
// MONTH(date) - extract month (1-12)
// DAY(date) - extract day of month
// HOUR(time) - extract hour
// MINUTE(time) - extract minute
// SECOND(time) - extract second
// WEEKDAY(date, [type]) - day of week
EvalResult fn_YEAR(args, ctx);
EvalResult fn_MONTH(args, ctx);
EvalResult fn_DAY(args, ctx);
EvalResult fn_HOUR(args, ctx);
EvalResult fn_MINUTE(args, ctx);
EvalResult fn_SECOND(args, ctx);
EvalResult fn_WEEKDAY(args, ctx);
```

**Test cases:**
- `=YEAR(DATE(2024,6,15))` → 2024
- `=MONTH(DATE(2024,6,15))` → 6
- `=DAY(DATE(2024,6,15))` → 15
- `=WEEKDAY(DATE(2024,6,15))` → depends on type (1=Sunday)

- [ ] 6c: Implement date extraction functions

### 6d: Add Phase 6 date/time function tests

Create `core/cells/formula_functions_datetime_test.cc`:

Target: 50+ test cases.

**Test**: All date/time function tests pass (50+ tests)

- [ ] 6d: Consolidate date/time function tests (50+ tests minimum)

---

## Phase 7: Recalculation Engine

Integrate evaluation with the dependency graph for reactive updates.

### 7a: Implement single-cell evaluation trigger

```cpp
// In Sheet or Workbook
void evaluateCell(Cell* cell);

// Evaluates the cell's formula and stores result in cell->value
// Sets cell->formula->dirty = false after evaluation
```

**Test**: Single cell evaluation works

- [ ] 7a: Implement single-cell evaluation and result storage

### 7b: Implement batch recalculation

```cpp
void recalculate(const std::vector<ID>& changedCells);

// 1. Get recalc order from dependency graph (topological sort)
// 2. Evaluate each cell in order
// 3. Handle circular references (mark as error, break cycle)
// 4. Mark all evaluated cells as not dirty
```

**Test**: Batch recalculation respects dependencies

- [ ] 7b: Implement batch recalculation with dependency ordering

### 7c: Implement volatile cell handling

```cpp
void recalculateVolatile();

// 1. Get all volatile cells from dependency graph
// 2. Mark them dirty
// 3. Get all their dependents
// 4. Recalculate in order
```

Volatile cells (NOW, RAND, TODAY) must recalculate on any sheet change.

**Test**: Volatile cells and their dependents recalculate

- [ ] 7c: Implement volatile cell recalculation

### 7d: Wire formula entry to trigger recalc

When a formula is set via `Sheet::setCellFormula()`:
1. Parse and resolve the formula (already done)
2. Add to dependency graph (already done)
3. **NEW**: Evaluate the formula immediately
4. **NEW**: Trigger recalculation of dependent cells

When a cell value is changed directly:
1. Set the value
2. **NEW**: Trigger recalculation of dependent cells

**Test**: Setting formula triggers cascade recalculation

- [ ] 7d: Wire formula/value changes to trigger recalculation

### 7e: Add recalculation tests

Create `core/cells/formula_recalc_test.cc`:

**Dependency Chain Tests:**
- A1=5, B1=A1*2, C1=B1+1 → change A1 to 10 → B1=20, C1=21
- Diamond dependency: A1→B1, A1→C1, B1→D1, C1→D1 → D1 recalcs once
- Long chain: A1→A2→A3→...→A10 → change A1 → all update

**Circular Reference Tests:**
- A1=B1, B1=A1 → both show #CIRCULAR!
- A1=B1+1, B1=C1+1, C1=A1+1 → all show #CIRCULAR!
- Break cycle: change A1 to constant → B1, C1 calculate correctly

**Volatile Function Tests:**
- A1=NOW(), B1=A1+1 → any change triggers A1 and B1 recalc
- Multiple volatile cells → all recalc together

**Performance Tests:**
- 1000 cells in dependency chain → recalc completes <100ms
- Grid of 100x100 cells with formulas → recalc completes <1s

**Test**: All recalculation tests pass (50+ tests)

- [ ] 7e: Add comprehensive recalculation tests (50+ tests minimum)

---

## Phase 8: WASM Integration

Expose calculation engine to the TypeScript web UI.

### 8a: Add WASM bindings for evaluation

In `apps/wasm/cells_bindings.cpp`:

```cpp
// Evaluate a single cell and return the result
std::string evaluateCell(const std::string& cellId);

// Get display value for a cell (formatted result or error string)
std::string getCellDisplayValue(const std::string& cellId);

// Trigger recalculation of all dirty cells
void recalculate();

// Check if any cells need recalculation
bool hasDirtyCells();
```

**Test**: WASM bindings compile

- [ ] 8a: Add WASM bindings for cell evaluation

### 8b: Update TypeScript types

In `apps/wasm/cells.d.ts`:

```typescript
export interface CellsModule {
    // Existing...

    // Evaluation
    evaluateCell(cellId: string): string;
    getCellDisplayValue(cellId: string): string;
    recalculate(): void;
    hasDirtyCells(): boolean;
}
```

**Test**: TypeScript compiles

- [ ] 8b: Update TypeScript type definitions

### 8c: Wire grid to display calculated values

Update the grid renderer to:
1. Call `getCellDisplayValue()` for each visible cell
2. Display the calculated result (not the formula)
3. Show error values with appropriate styling (#DIV/0!, #REF!, etc.)
4. Maintain formula bar showing formula when cell is selected

**Test manually**: Enter `=1+1` → cell displays `2`, formula bar shows `=1+1`

- [ ] 8c: Wire grid to display calculated values

### 8d: Trigger recalculation on changes

When a cell is edited:
1. Set the new value/formula
2. Call `recalculate()`
3. Refresh the viewport

**Test manually**: A1=5, B1=A1*2 → displays 10 → change A1 to 10 → B1 displays 20

- [ ] 8d: Trigger recalculation on cell changes

### 8e: Add error styling

Style cells with errors differently:
- Red text for error values (#DIV/0!, #REF!, etc.)
- Tooltip showing full error message
- Formula bar shows formula, cell shows error

**Test manually**: Enter `=1/0` → cell displays red `#DIV/0!`

- [ ] 8e: Add error value styling in grid

---

## Phase 9: Additional Functions (Optional Extension)

These functions can be added incrementally after the core engine is working.

### Lookup Functions
- [ ] VLOOKUP(lookup_value, table_array, col_index, [range_lookup])
- [ ] HLOOKUP(lookup_value, table_array, row_index, [range_lookup])
- [ ] INDEX(array, row_num, [col_num])
- [ ] MATCH(lookup_value, lookup_array, [match_type])

### Statistical Functions
- [ ] MEDIAN(number1, [number2], ...)
- [ ] STDEV(number1, [number2], ...)
- [ ] VAR(number1, [number2], ...)
- [ ] PERCENTILE(array, k)

### Random Functions
- [ ] RAND() - random 0 to 1 (volatile)
- [ ] RANDBETWEEN(bottom, top) - random integer (volatile)

### Financial Functions (future)
- [ ] PMT, PV, FV, NPV, IRR

---

## Test Summary

| Phase | Test File | Test Count |
|-------|-----------|------------|
| 1 | formula_eval_test.cc | 80+ |
| 2 | (included in Phase 1) | 25+ |
| 3 | formula_functions_math_test.cc | 100+ |
| 4 | formula_functions_logic_test.cc | 60+ |
| 5 | formula_functions_text_test.cc | 80+ |
| 6 | formula_functions_datetime_test.cc | 50+ |
| 7 | formula_recalc_test.cc | 50+ |
| **Total** | | **445+** |

Run all tests: `bazel test //core/cells:all`

---

## UI Checkpoints (MANDATORY)

**⚠️ THESE CHECKPOINTS ARE NON-SKIPPABLE ⚠️**

Each checkpoint requires:
1. Manual testing in the web UI
2. Explicit user confirmation that it works
3. Cannot proceed to next phase until confirmed

Do NOT mark a checkpoint as complete without user approval.

| Phase | Checkpoint | Test Steps | Status |
|-------|------------|------------|--------|
| 1 | Basic arithmetic | Enter `=1+2` → should display `3` | - [ ] Awaiting confirmation |
| 3 | Aggregate functions | Set A1=1, A2=2, A3=3, then B1=`=SUM(A1:A3)` → should display `6` | - [ ] Awaiting confirmation |
| 4 | Conditional logic | Set A1=10, B1=`=IF(A1>5,"big","small")` → should display `"big"` | - [ ] Awaiting confirmation |
| 7 | Dependency cascade | Set A1=5, B1=`=A1*2` → displays `10`, then change A1 to `10` → B1 updates to `20` | - [ ] Awaiting confirmation |
| 8 | Full integration | All above + error styling (`=1/0` shows red `#DIV/0!`) | - [ ] Awaiting confirmation |

**Procedure**: At each checkpoint, pause execution and ask the user to verify the behavior manually. Only proceed after receiving explicit "confirmed" or equivalent response.

---

## Future Work (Separate Plans)

- **Array Formulas / Spill**: Dynamic arrays that expand results
- **Shared Formulas**: Optimized storage for copy/pasted formulas
- **Custom Functions**: User-defined functions
- **Performance Optimization**: Parallel evaluation, caching
