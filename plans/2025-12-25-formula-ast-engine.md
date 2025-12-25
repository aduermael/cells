# Formula AST Engine

Status: READY
Created At: 2025-12-25 00:19 UTC
Updated At: 2025-12-25 01:45 UTC
Following plan management guidelines defined in AGENTS.md

## Overview

Implement the formula parsing engine with AST representation, UUID-based reference storage, and dependency tracking. This enables:
- Parsing formulas like `=C3+B4` into an AST
- Converting A1 references to UUID-based storage (stable across row/col moves)
- Building a dependency graph for reactive updates and UI highlighting
- Displaying formulas in A1 notation while storing with UUIDs

Execution (actually computing formula results) is deferred to a separate plan.

## Testing Philosophy

**Tests must be added and passing after each testable/verifiable step.** This prevents errors from accumulating and becoming harder to fix when detected later. Each phase includes specific test requirements that must pass before proceeding.

## Design Decisions

### Parser: Tree-sitter

Use tree-sitter for incremental, error-tolerant parsing:
- **Custom grammar**: Write a tree-sitter grammar for Excel-style formulas
- **AST conversion**: Convert tree-sitter's CST to our own AST types for internal use
- **Error recovery**: Parse errors create `ErrorNode` placeholders, enabling partial syntax highlighting while user is typing incomplete formulas like `=SUM(A1+`

### Reference Storage Format

UI shows: `=C3+B4`
Internal storage: `=xK7mNp2Q+fR3pK7wN` (cell UUIDs)

When parsing `=C3+B4`:
1. Look up column C (position 2) → get its UUID
2. Look up row 3 (position 2) → get its UUID
3. Find or create cell at (colId, rowId) → get cell UUID
4. Store cell UUID in AST node

Absolute references (`$A$1`) store as `$$cellId`, using existing `RefConverter` format.

### Range Representation

Ranges use **two-corner UUIDs** (spatial/bounds-based):
- `A1:B10` stores as `(topLeftCellId, bottomRightCellId)`
- "Anything within bounds" semantics—inserting rows/columns within the range automatically expands it
- Both corner cells must exist (auto-created if needed)

### Whole Column/Row References

Special reference types (not ranges):
- `A:A` → `ColumnRef(columnUUID)` - references entire column
- `1:1` → `RowRef(rowUUID)` - references entire row
- `A:C` → `ColumnRangeRef(startColUUID, endColUUID)`
- `1:5` → `RowRangeRef(startRowUUID, endRowUUID)`

### Cross-Sheet References

Supported: `Sheet2!A1`, `Sheet2!A1:B10`, `Sheet2!A:A`

**Future work**: External workbook references like `[Book.xlsx]Sheet1!A1` (standard Excel syntax) deferred to desktop version when local filesystem access is available.

### Named Ranges

Support both scopes (like Excel):
- **Workbook-scoped**: Global names accessible from any sheet
- **Sheet-scoped**: Local names that can shadow global names within a sheet

Parser must disambiguate named ranges from function names.

### Dependency Graph

**Custom R-tree** for efficient 2D range queries:
- When cell `A50` changes, query: "which ranges contain (col=A, row=50)?"
- Single-cell references stored as 1×1 rectangles for uniformity
- Supports high cell counts with sparse coordinates

Bidirectional tracking:
- `dependencies[cellId]` → what this formula reads from
- `dependents[cellId]` → formulas that read this cell (via R-tree query)

### Volatile Functions

Functions like `NOW()`, `RAND()`, `TODAY()` marked as **"always dirty"**:
- Cells containing volatile functions recalculate on every change
- Propagates through dependency chain (if B1=NOW() and A1=B1+1, both recalc)

### Circular Reference Detection

**Lazy detection** (at recalc time only):
- Formulas with circular references can be entered
- Error surfaces when recalculation attempts to resolve the cycle
- Allows user to fix by modifying other cells in the cycle

### AST Node Types

```
Literal (number, string, boolean)
CellRef (cellUUID, colAbsolute, rowAbsolute)
RangeRef (topLeftCellUUID, bottomRightCellUUID, absolute flags)
ColumnRef (columnUUID)
RowRef (rowUUID)
ColumnRangeRef (startColUUID, endColUUID)
RowRangeRef (startRowUUID, endRowUUID)
NamedRef (name, scope)
BinaryOp (op, left, right)
UnaryOp (op, operand)
FunctionCall (name, args[], isVolatile)
ErrorNode (errorType, position, partialChildren)
```

---

## Phase 1: Tree-sitter Grammar

Write the tree-sitter grammar for Excel-style formulas.

- [ ] 1a: Set up tree-sitter infrastructure
  - Create `core/cells/tree-sitter-formula/` directory
  - Initialize tree-sitter grammar project structure
  - Configure Bazel to build the generated C parser
  - **Test**: Build compiles successfully

- [ ] 1b: Define grammar in `grammar.js`
  - Formula: `=` followed by expression
  - Literals: numbers (int, decimal, scientific), strings (double-quoted), booleans (TRUE/FALSE)
  - Cell references: A1, $A$1, $A1, A$1 (all absolute/relative combinations)
  - Range references: A1:B2, $A$1:$B$2
  - Whole column/row: A:A, A:C, 1:1, 1:5
  - Cross-sheet: Sheet2!A1, 'Sheet Name'!A1 (quoted sheet names with spaces)
  - Operators: +, -, *, /, ^, &, =, <, >, <=, >=, <> with correct precedence
  - Functions: NAME(args...) with nested expressions
  - Named ranges: identifiers that aren't keywords
  - Parentheses for grouping
  - **Test**: `tree-sitter generate` succeeds

- [ ] 1c: Add tree-sitter test cases in `corpus/` directory
  - Test basic literals (numbers, strings, booleans)
  - Test cell references with all absolute/relative combinations
  - Test range references and whole column/row refs
  - Test cross-sheet references
  - Test operators with correct precedence
  - Test function calls with various arities
  - Test complex nested expressions
  - Test error recovery (partial formulas like `=SUM(A1+`)
  - **Test**: `tree-sitter test` passes all cases

---

## Phase 2: AST Types and Conversion

Define our AST types and convert from tree-sitter's CST.

- [ ] 2a: Define AST node types in `core/cells/formula_ast.h`
  - ASTNodeType enum (all types from Design Decisions)
  - Base ASTNode struct with type, source position, virtual destructor
  - LiteralNode (number, string, boolean values)
  - CellRefNode (cellUUID placeholder, colAbsolute, rowAbsolute, original A1 text)
  - RangeRefNode (topLeft/bottomRight UUID placeholders, absolute flags)
  - ColumnRefNode, RowRefNode, ColumnRangeRefNode, RowRangeRefNode
  - NamedRefNode (name string, scope enum)
  - BinaryOpNode (operator, left, right children)
  - UnaryOpNode (operator, operand child)
  - FunctionCallNode (name, args vector, isVolatile flag)
  - ErrorNode (errorType, position, partialChildren for error recovery)
  - Helper to clone AST trees
  - **Test**: Header compiles, basic node creation works

- [ ] 2b: Implement CST-to-AST converter in `core/cells/formula_parser.h/.cc`
  - Wrap tree-sitter parser initialization
  - `parse(string) -> unique_ptr<ASTNode>` - parse and convert
  - Walk tree-sitter nodes, create corresponding ASTNode types
  - Handle ERROR nodes from tree-sitter → create ErrorNode with partial children
  - Preserve source positions for error reporting and highlighting
  - **Test**: Basic conversion works for simple formulas

- [ ] 2c: Add parser tests in `core/cells/formula_parser_test.cc`
  - Test literal parsing and type inference
  - Test cell reference parsing (UUID fields empty until resolution)
  - Test binary operations with correct precedence
  - Test unary operations
  - Test function calls with various arities
  - Test cross-sheet references
  - Test whole column/row references
  - Test complex nested expressions
  - Test error recovery (ErrorNode created for invalid syntax)
  - **Test**: All tests pass before proceeding

---

## Phase 3: Reference Resolution

Convert A1 references in AST to UUID-based references, auto-creating cells as needed.

- [ ] 3a: Add cell/axis auto-creation to Sheet in `core/cells/model.h` and `model.cc`
  - `getOrCreateCellAt(colId, rowId)` - returns existing cell or creates new one
  - `getOrCreateAxisByPosition(position, isColumn)` - returns existing axis or creates new one
  - `getAxisByName(name)` - look up column by letter (A, B, ..., Z, AA, AB, ...)
  - These are needed when parsing formulas that reference non-existent cells
  - **Test**: Auto-creation works, lookup by name works

- [ ] 3b: Add named range registry in `core/cells/named_ranges.h` and `named_ranges.cc`
  - `NamedRangeRegistry` class
  - Store workbook-scoped and sheet-scoped named ranges
  - `define(name, scope, rangeOrCell)` - create named range
  - `resolve(name, currentSheet)` - look up, respecting scope shadowing
  - `remove(name, scope)` - delete named range
  - **Test**: Define/resolve/remove works, sheet scope shadows workbook scope

- [ ] 3c: Implement reference resolver in `core/cells/formula_resolver.h` and `formula_resolver.cc`
  - `resolveReferences(ASTNode*, Sheet&, NamedRangeRegistry&)` - walk AST, convert A1 to UUIDs
  - For CellRefNode: look up col by letter, row by number, get/create cell, store UUID
  - For RangeRefNode: resolve both corner cells, store UUIDs
  - For ColumnRefNode/RowRefNode: resolve axis UUID
  - For NamedRefNode: look up in registry, replace with resolved reference
  - For cross-sheet refs: look up sheet by name first
  - Preserve absolute/relative flags from original A1 notation
  - Error handling for invalid references (create ErrorNode)
  - **Test**: Basic resolution works

- [ ] 3d: Add A1 display conversion in `core/cells/formula_resolver.cc`
  - `toDisplayString(ASTNode*, const Sheet&)` - convert AST back to A1 notation for display
  - Walk AST, convert UUID refs back to A1 using axis positions
  - Handle absolute/relative markers ($)
  - Rebuild formula string with operators and functions
  - **Test**: Display conversion works

- [ ] 3e: Add resolver tests in `core/cells/formula_resolver_test.cc`
  - Test A1 to UUID resolution
  - Test auto-creation of cells and axes
  - Test round-trip (parse -> resolve -> display)
  - Test absolute/relative preservation
  - Test range resolution (both corners)
  - Test whole column/row resolution
  - Test cross-sheet reference resolution
  - Test named range resolution (both scopes)
  - **Test**: All tests pass before proceeding

---

## Phase 4: R-tree Dependency Graph

Track which cells depend on which for reactive updates, using R-tree for efficient range queries.

- [ ] 4a: Implement R-tree in `core/cells/rtree.h` and `rtree.cc`
  - Custom 2D R-tree implementation optimized for sparse cell coordinates
  - `RTree<T>` template class
  - `insert(minCol, minRow, maxCol, maxRow, value)` - insert rectangle
  - `remove(minCol, minRow, maxCol, maxRow, value)` - remove rectangle
  - `query(col, row)` - find all rectangles containing point
  - `queryRange(minCol, minRow, maxCol, maxRow)` - find all rectangles intersecting range
  - Handle whole-column refs (row bounds = 0 to MAX) and whole-row refs (col bounds = 0 to MAX)
  - **Test**: Basic insert/remove/query works

- [ ] 4b: Add R-tree tests in `core/cells/rtree_test.cc`
  - Test point insertion (1×1 rectangles)
  - Test range insertion
  - Test point queries
  - Test range queries
  - Test removal
  - Test large-scale performance (10k+ entries)
  - Test sparse coordinates (non-contiguous positions)
  - **Test**: All tests pass before proceeding

- [ ] 4c: Define dependency graph in `core/cells/dependency_graph.h`
  - `DependencyGraph` class wrapping R-tree
  - `dependencies[cellId]` - set of references this formula reads (for display)
  - R-tree stores: rectangle → set of dependent cell IDs
  - Methods:
    - `addFormula(cellId, AST*)` - extract refs, add to R-tree
    - `removeFormula(cellId)` - remove all entries for this cell
    - `getDependents(cellId)` - R-tree query for cells affected by this cell changing
    - `getDependencies(cellId)` - direct lookup of what this formula reads
    - `markVolatile(cellId)` - flag cell as always-dirty
    - `getVolatileCells()` - get all volatile cells
  - **Test**: Basic add/remove/query works

- [ ] 4d: Implement dependency extraction in `core/cells/dependency_graph.cc`
  - Walk AST to extract all references
  - For CellRef: insert 1×1 rectangle at (col, row)
  - For RangeRef: insert rectangle from topLeft to bottomRight corners
  - For ColumnRef: insert rectangle (col, 0) to (col, MAX)
  - For RowRef: insert rectangle (0, row) to (MAX, row)
  - Track volatile functions (NOW, RAND, etc.) and mark cell
  - **Test**: Extraction works for all reference types

- [ ] 4e: Add circular reference detection (lazy)
  - `detectCycle(startCellId)` - DFS from cell, return cycle path if found
  - `getRecalcOrder(changedCells)` - topological sort of affected cells
  - Called at recalc time, not on formula entry
  - Return error info for UI display when cycle detected
  - **Test**: Cycle detection works

- [ ] 4f: Add dependency graph tests in `core/cells/dependency_graph_test.cc`
  - Test single cell dependencies
  - Test range dependencies
  - Test whole column/row dependencies
  - Test transitive dependencies
  - Test circular reference detection
  - Test dependency removal
  - Test volatile cell tracking
  - Test recalc ordering
  - **Test**: All tests pass before proceeding

---

## Phase 5: Integration with Model

Wire the parser and dependency graph into the Cell/Sheet model.

- [ ] 5a: Update Cell/Formula structs in `core/cells/model.h`
  - Add `unique_ptr<ASTNode> ast` field to Formula
  - Add `parse()` method to Formula that uses the parser
  - Add `isValid()` helper to check if AST has no ErrorNodes
  - Add `hasVolatile()` helper to check for volatile functions
  - **Test**: Formula struct works with AST

- [ ] 5b: Add formula management to Sheet
  - `setCellFormula(cellId, formulaText)` - parse, resolve, update deps
  - `getCellDisplayFormula(cellId)` - get A1 display string
  - `clearCellFormula(cellId)` - remove formula and deps
  - Sheet owns one `DependencyGraph` instance
  - Sheet owns one `NamedRangeRegistry` instance
  - **Test**: Set/get/clear formula works

- [ ] 5c: Add serialization support
  - Update `.cells` parser to read formulas with UUID refs
  - Update serializer to write formulas with UUID refs
  - AST is reparsed on load (don't serialize AST itself)
  - Ensure round-trip preserves formula exactly
  - **Test**: Serialization round-trip works

- [ ] 5d: Add integration tests in `core/cells/formula_integration_test.cc`
  - Test setting formulas via Sheet API
  - Test display conversion
  - Test serialization round-trip
  - Test dependency graph integration
  - Test named ranges through Sheet API
  - Test volatile function tracking
  - **Test**: All tests pass before proceeding

---

## Phase 6: Column/Row Move Stability

Test that formulas remain stable when columns/rows are moved.

- [ ] 6a: Add axis move operations to Sheet (if not already present)
  - `moveColumn(colId, newPosition)` - update column position
  - `moveRow(rowId, newPosition)` - update row position
  - Formulas should NOT need updating (they use UUIDs)
  - **Test**: Move operations work

- [ ] 6b: Add move stability tests in `core/cells/formula_move_test.cc`
  - Create formula `=B2+C3`
  - Move column B to position 5 (becomes F)
  - Verify formula display updates to `=F2+C3`
  - Verify stored formula still uses same UUIDs
  - Verify dependencies unchanged
  - **Test**: Move stability works

- [ ] 6c: Add insert/delete stability tests
  - Test inserting column before referenced column (display updates, UUIDs unchanged)
  - Test inserting row within a range (range expands automatically via two-corner representation)
  - Test deleting referenced cell (formula should error at eval time, not parse time)
  - Test deleting row outside a range (range unchanged)
  - Test that unaffected formulas don't change
  - **Test**: All insert/delete tests pass

- [ ] 6d: Add range expansion tests
  - Create formula `=SUM(A1:A10)`
  - Insert row between rows 5 and 6
  - Verify range now includes the new row (bounds-based semantics)
  - Verify display shows `=SUM(A1:A11)`
  - **Test**: Range expansion works

---

## Phase 7: UI Integration (WASM bindings)

Expose formula functionality to the web UI.

- [ ] 7a: Add WASM bindings for formula parsing in `apps/wasm/`
  - `parseFormula(sheetId, cellId, text)` - parse, resolve, update deps, return success/error
  - `getFormulaDisplay(sheetId, cellId)` - get A1 display string
  - `getCellDependencies(sheetId, cellId)` - get list of cells this formula reads (for UI highlighting)
  - `validateFormula(text)` - parse without side effects, return errors for live feedback
  - **Test**: WASM bindings work from JS

- [ ] 7b: Add WASM bindings for dependency visualization
  - `getCellDependents(sheetId, cellId)` - cells that depend on this cell
  - `getFormulaReferences(sheetId, cellId)` - get refs with source positions (for colored highlighting)
  - These enable the colored reference boxes shown in Numbers UI
  - **Test**: Dependency queries work from JS

- [ ] 7c: Add WASM bindings for incremental parsing (live highlighting)
  - `parseFormulaIncremental(text)` - parse partial formula, return AST with ErrorNodes
  - `getReferencesFromPartial(text)` - extract valid references from incomplete formula
  - Enables highlighting while user types `=SUM(A1+` (A1 highlighted even though formula incomplete)
  - **Test**: Incremental parsing works

- [ ] 7d: Update web UI to display formula dependencies
  - When editing a formula, highlight referenced cells with colors
  - Show different colors for different refs (like Numbers UI)
  - Update highlights in real-time as formula is typed
  - Show error indicators for invalid syntax
  - **Test**: Manual UI testing, visual verification

---

## Future Work (Separate Plans)

The following are explicitly deferred:

- **Formula Execution**: Evaluating formulas to compute results (Luau integration)
- **Function Library**: SUM, IF, VLOOKUP, etc.
- **Recalculation Engine**: Propagating changes through dependency graph using topological sort
- **Shared Formulas**: Master cell with offset-based subscribers for copy/paste with relative reference adjustment
- **External Workbook References**: `[Book.xlsx]Sheet1!A1` syntax for desktop version
- **Array Formulas / Spill**: Dynamic arrays that expand results to multiple cells

---

## Testing Strategy

**Tests must pass after each step before proceeding to the next.**

Each phase includes its own test file. Tests should cover:
1. Happy path
2. Edge cases (empty formulas, single refs, deeply nested)
3. Error cases (invalid syntax, circular refs, #REF!)
4. Round-trip (parse -> serialize -> parse)
5. Performance (where applicable, e.g., R-tree with 10k+ entries)

Run all tests: `bazel test //core/cells:all`

### Test File Summary

| Phase | Test File |
|-------|-----------|
| 1 | `core/cells/tree-sitter-formula/corpus/*.txt` (tree-sitter tests) |
| 2 | `core/cells/formula_parser_test.cc` |
| 3 | `core/cells/formula_resolver_test.cc` |
| 4 | `core/cells/rtree_test.cc`, `core/cells/dependency_graph_test.cc` |
| 5 | `core/cells/formula_integration_test.cc` |
| 6 | `core/cells/formula_move_test.cc` |
| 7 | Manual testing + JS integration tests |
