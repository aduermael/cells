# Formula Engine

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
| Literals | `42`, `3.14`, `"hello"`, `TRUE` |
| References | `A1`, `$A$1`, `Sheet1!A1`, `A1:B2` |
| Operators | `+`, `-`, `*`, `/`, `^`, `&`, `=`, `<>` |
| Functions | `SUM`, `IF`, `VLOOKUP` |
| Punctuation | `(`, `)`, `,`, `:` |

## Parser (AST)

Build an abstract syntax tree with node types:

| Node Type | Example |
|-----------|---------|
| Number | `42` |
| String | `"hello"` |
| Boolean | `TRUE` |
| CellRef | `A1`, `$A$1` |
| RangeRef | `A1:B10` |
| BinaryOp | `A1 + B1` |
| UnaryOp | `-A1` |
| FunctionCall | `SUM(A1:A10)` |

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

Format: `[col_flag][row_flag]cellUUID` where `$`=absolute, `~`=relative

| A1 Notation | Internal Format | Meaning |
|-------------|-----------------|---------|
| `$A$1` | `$$kR7pN2wQ` | Both absolute |
| `$A1` | `$~kR7pN2wQ` | Col absolute, row relative |
| `A$1` | `~$kR7pN2wQ` | Col relative, row absolute |
| `A1` | `kR7pN2wQ` | Both relative (`~~` omitted) |

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

### Priority Categories

| Priority | Category | Examples |
|----------|----------|----------|
| P0 | Math | SUM, AVERAGE, MIN, MAX, COUNT |
| P0 | Logic | IF, AND, OR, NOT, IFS |
| P0 | Text | CONCATENATE, LEFT, RIGHT, LEN |
| P0 | Lookup | VLOOKUP, HLOOKUP, INDEX, MATCH |
| P1 | Date/Time | NOW, TODAY, DATE, YEAR |
| P1 | Statistical | STDEV, MEDIAN, PERCENTILE |
| P2 | Financial | PMT, NPV, IRR |
| P2 | Array | FILTER, SORT, UNIQUE |

## Dependency Graph

### Structure

- `dependents[cell_id]` → list of cells that depend on it
- `dependencies[cell_id]` → list of cells it depends on
- `volatile_cells` → cells needing recalc every time

### Recalculation

When cell X changes:
1. Find all affected cells (transitive dependents)
2. Topological sort for correct order
3. Recalculate in order

### Circular Reference Detection

DFS from each dependency to detect cycles. Flag as `#CIRCULAR` error.

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
