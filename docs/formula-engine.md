# Formula Engine

## Implementation Status

**Current state (January 2026):** Fully implemented.

| Component | Status |
|-----------|--------|
| Lexer/Tokenizer | ✅ Implemented |
| AST Parser | ✅ Implemented |
| Execution Engine | ✅ Implemented |
| Dependency Graph | ✅ Implemented |
| Function Library | ✅ 83 functions |
| Dynamic Arrays (Spill) | ✅ Implemented |

---

## Overview

The formula engine parses Excel-style formulas into an AST and executes them natively in C++, managing the dependency graph for reactive updates.

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Excel        │    │     AST      │    │   Native     │
│ Formula      │───►│    Parser    │───►│  Execution   │
│ "=SUM(A1:B2)"│    │              │    │  (C++)       │
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

Instead of transpiling to a scripting language, we execute the AST directly:

| Benefit | Reason |
|---------|--------|
| Simpler architecture | No codegen step, no runtime embedding |
| Better performance | No interpreter overhead |
| Easier debugging | Native stack traces |
| Smaller binary | No embedded runtime |
| Full control | Custom memory management, error handling |

## Lexer

Tokenize Excel formula syntax into:

| Token Type | Examples |
|------------|----------|
| Literals | `42`, `3.14`, `15%`, `"hello"`, `TRUE`, `#REF!` |
| References | `A1`, `$A$1`, `Sheet1!A1`, `A1:B2`, `A:A`, `1:1` |
| Operators | `+`, `-`, `*`, `/`, `^`, `&`, `=`, `<>`, `<`, `<=`, `>`, `>=` |
| Functions | `SUM`, `IF`, `VLOOKUP` |
| Punctuation | `(`, `)`, `,`, `:`, `!`, `$`, `#` |

UUID-based internal storage format uses special prefixes:
- `$$`, `$~`, `~$`, `~~` for cell references with absolute/relative markers
- `@$`, `@~` for column references
- `#$`, `#~` for row references
- `!<uuid>` for cross-sheet references

## Parser (AST)

Build an abstract syntax tree with node types:

| Node Type | Example |
|-----------|---------|
| NUMBER_LITERAL | `42`, `3.14` |
| STRING_LITERAL | `"hello"` |
| BOOLEAN_LITERAL | `TRUE`, `FALSE` |
| CELL_REF | `A1`, `$A$1`, `Sheet1!B2` |
| RANGE_REF | `A1:B10` |
| COLUMN_REF | `A:A` (whole column) |
| ROW_REF | `1:1` (whole row) |
| COLUMN_RANGE_REF | `A:C` (column range) |
| ROW_RANGE_REF | `1:5` (row range) |
| SPILL_RANGE_REF | `A1#` (dynamic array range) |
| NAMED_REF | `myRange`, `Sales_Total` |
| BINARY_OP | `A1 + B1` |
| UNARY_OP | `-A1`, `+A1` |
| FUNCTION_CALL | `SUM(A1:A10)` |
| ERROR_NODE | Parse errors with partial recovery |

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

## Semantic Analysis

### Reference Resolution

Convert A1-style references to UUID-based references for stability.

**Cell Reference Format**: `[col_flag][row_flag]cellUUID`
- `$` = absolute (position stays fixed when formula is copied)
- `~` = relative (position adjusts when formula is copied)

| A1 Notation | Internal Format | Meaning |
|-------------|-----------------|---------|
| `$A$1` | `$$kR7pN2wQ` | Both absolute |
| `$A1` | `$~kR7pN2wQ` | Col absolute, row relative |
| `A$1` | `~$kR7pN2wQ` | Col relative, row absolute |
| `A1` | `~~kR7pN2wQ` | Both relative |

**Column/Row Reference Formats**:
- Column: `@$colUUID` (absolute) or `@~colUUID` (relative)
- Row: `#$rowUUID` (absolute) or `#~rowUUID` (relative)
- Sheet prefix: `!sheetUUID` for cross-sheet references

Resolution: `cellUUID` → `Cell` → `(cell.colId, cell.rowId)` → axis positions

Benefits:
- Moving columns/rows doesn't break formulas
- 8-10 chars vs 18 chars (col+row UUIDs)
- Cell UUID directly references the target cell

### Dependency Extraction

Extract all cells a formula depends on for the dependency graph.
Track volatile functions (NOW, RAND) that need recalc every time.

## Execution

Tree-walking interpreter evaluates AST nodes:
- Literals: return value directly
- Cell refs: lookup cell, return its value
- Binary ops: evaluate operands, apply operator
- Functions: evaluate args, call function implementation

### Type Coercion

Excel-compatible coercion rules:
- Numbers in string context → formatted text
- Text in number context → parse or error
- Booleans: TRUE=1, FALSE=0

## Function Library

83 functions organized by category. Functions are registered in `core/cells/functions/fn_*.cc`.

### Math (14 functions)

| Function | Description |
|----------|-------------|
| SUM | Adds all numbers in a range |
| AVERAGE | Returns the arithmetic mean |
| COUNT | Counts cells containing numbers |
| COUNTA | Counts non-empty cells |
| MIN | Returns the smallest value |
| MAX | Returns the largest value |
| ABS | Returns the absolute value |
| SQRT | Returns the square root |
| POWER | Returns number raised to a power |
| ROUND | Rounds to specified digits |
| FLOOR | Rounds down to nearest integer |
| CEILING | Rounds up to nearest integer |
| MOD | Returns remainder after division |
| INT | Truncates to an integer |

### Logic (15 functions)

| Function | Description |
|----------|-------------|
| IF | Conditional evaluation |
| AND | TRUE if all arguments are true |
| OR | TRUE if any argument is true |
| NOT | Reverses boolean value |
| IFERROR | Returns alternate value if error |
| IFNA | Returns alternate value if #N/A |
| EXACT | Case-sensitive string comparison |
| ISBLANK | TRUE if cell is empty |
| ISNUMBER | TRUE if value is a number |
| ISTEXT | TRUE if value is text |
| ISERROR | TRUE if value is any error |
| ISLOGICAL | TRUE if value is boolean |
| ISNA | TRUE if value is #N/A |
| TRUE | Returns TRUE |
| FALSE | Returns FALSE |

### Text (19 functions)

| Function | Description |
|----------|-------------|
| LEN | Returns number of characters |
| LEFT | Returns leftmost characters |
| RIGHT | Returns rightmost characters |
| MID | Returns characters from middle |
| TRIM | Removes extra spaces |
| UPPER | Converts to uppercase |
| LOWER | Converts to lowercase |
| PROPER | Capitalizes first letter of each word |
| FIND | Case-sensitive text search |
| SEARCH | Case-insensitive text search |
| SUBSTITUTE | Replaces text occurrences |
| REPLACE | Replaces characters by position |
| CONCAT | Joins text strings |
| CONCATENATE | Joins text strings (legacy) |
| REPT | Repeats text N times |
| TEXT | Formats number as text |
| VALUE | Converts text to number |
| CHAR | Returns character for ASCII code |
| CODE | Returns ASCII code of first character |

### Lookup (4 functions)

| Function | Description |
|----------|-------------|
| INDEX | Returns value at position in range |
| MATCH | Returns position of value in range |
| VLOOKUP | Vertical lookup in first column |
| HLOOKUP | Horizontal lookup in first row |

### Date/Time (14 functions)

| Function | Description | Volatile |
|----------|-------------|----------|
| NOW | Current date and time | ✅ |
| TODAY | Current date | ✅ |
| DATE | Creates date from year/month/day | |
| TIME | Creates time from hour/min/sec | |
| DATEVALUE | Converts text to date | |
| TIMEVALUE | Converts text to time | |
| YEAR | Extracts year from date | |
| MONTH | Extracts month (1-12) | |
| DAY | Extracts day of month | |
| HOUR | Extracts hour (0-23) | |
| MINUTE | Extracts minute (0-59) | |
| SECOND | Extracts second (0-59) | |
| WEEKDAY | Returns day of week | |
| EOMONTH | Last day of month N months away | |

### Statistics (10 functions)

| Function | Description |
|----------|-------------|
| MEDIAN | Returns the median value |
| STDEV | Sample standard deviation |
| STDEVS | Sample standard deviation (alias) |
| STDEVP | Population standard deviation |
| VAR | Sample variance |
| VARS | Sample variance (alias) |
| VARP | Population variance |
| PERCENTILE | Returns k-th percentile (inclusive) |
| PERCENTILEINC | Inclusive percentile |
| PERCENTILEEXC | Exclusive percentile |

### Random (2 functions)

| Function | Description | Volatile |
|----------|-------------|----------|
| RAND | Random number 0-1 | ✅ |
| RANDBETWEEN | Random integer in range | ✅ |

### Array (5 functions)

| Function | Description |
|----------|-------------|
| UNIQUE | Returns unique values from range |
| SORT | Sorts a range of data |
| FILTER | Filters range based on criteria |
| SEQUENCE | Generates number sequence |
| TRANSPOSE | Transposes rows and columns |

## Dependency Graph

Managed by `formula_recalc.h/.cc` for reactive updates.

### Recalculation Strategy

1. Mark changed cells as dirty
2. Propagate dirty flags to dependents
3. Topologically sort dirty cells
4. Evaluate in dependency order

### Volatile Functions

Functions that must recalculate on every sheet change:
- `NOW()`, `TODAY()` - current date/time
- `RAND()`, `RANDBETWEEN()` - random numbers

Volatile cells are tracked separately and recalculated on any edit.

### Circular Reference Detection

DFS from each dependency to detect cycles. Cells in cycles are flagged with `#CIRCULAR!` error.

## Dynamic Arrays (Spill Behavior)

Excel-compatible dynamic array functionality where formulas can return multiple values that automatically "spill" into neighboring cells. Managed by `formula_recalc.h/.cc`.

### Key Behaviors

- Formula results that return arrays automatically populate adjacent cells
- Only the "master cell" (top-left) contains the actual formula
- Spilled cells show the formula grayed out in the formula bar (non-editable)
- If any cell in the spill range is blocked (has data), show `#SPILL!` error
- Selecting any cell in the spill range highlights the entire spill boundary
- Spilled values are runtime-only (not persisted) - recomputed on recalculation

### Spill-Capable Functions

| Function | Description | Example |
|----------|-------------|---------|
| UNIQUE | Extract unique values | `=UNIQUE(A1:A10)` |
| SORT | Sort array by column | `=SORT(A1:B10,1,-1)` |
| FILTER | Filter rows by criteria | `=FILTER(A1:B10,A1:A10>50)` |
| SEQUENCE | Generate number sequence | `=SEQUENCE(5,3,1,1)` |
| TRANSPOSE | Flip rows/columns | `=TRANSPOSE(A1:C3)` |

### Spill Range Operator (#)

The `#` operator references the entire spill range of a cell (parsed as `SPILL_RANGE_REF` AST node):

```
=SUM(A1#)     // Sum all spilled values starting from A1
=AVERAGE(D2#) // Average of spill range at D2
```

This is useful for referencing dynamic arrays whose size may change.

### Spill Blocking

A spill is blocked when the target cells contain:
- Existing values (non-empty cells)
- Other formulas (cells with their own formula)
- Spilled values from a different formula

When blocked, the master cell displays `#SPILL!` error.

### Limits

- Maximum spill size: 1,000,000 cells (`MAX_SPILL_CELLS` in `formula_recalc.h`)
- Circular spill dependencies are detected and show `#CIRCULAR!` error

## Performance Optimizations

1. **AST caching**: Parse formula once, reuse AST
2. **Batch recalc**: Group changes, recalc once
3. **Parallel recalc**: Independent branches run in parallel
4. **Lazy evaluation**: Only calc visible cells first

## WASM Considerations

Native C++ compiles cleanly to WebAssembly:
- Same code runs native and WASM
- Long calculations yield via asyncify
- Pre-allocate memory pools
