# Formula Engine

## Implementation Status

**Current state (July 2026):** Fully implemented.

| Component | Status |
|-----------|--------|
| Lexer/Tokenizer | ✅ Implemented |
| AST Parser | ✅ Implemented |
| Execution Engine | ✅ Implemented |
| Dependency Graph | ✅ Implemented |
| Function Library | ✅ 120 functions |
| Dynamic Arrays (Spill) | ✅ Implemented |

For the full function inventory, missing Excel functions, and product/feature parity, see [Excel Parity](./excel-parity.md).

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

**120 functions** registered in `core/cells/functions/fn_*.cc`.

| Category | Count |
|----------|------:|
| Math (incl. trig + random) | 47 |
| Logic | 21 |
| Text | 19 |
| Date / time | 14 |
| Statistics | 10 |
| Array / dynamic | 5 |
| Lookup | 4 |
| **Total** | **120** |

Full inventory, Excel aliases (`CEILING.MATH` → `CEILING_MATH`, etc.), and the missing-function backlog live in [Excel Parity](./excel-parity.md).

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
