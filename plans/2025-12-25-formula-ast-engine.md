# Formula AST Engine

Status: READY
Created At: 2025-12-25 00:19 UTC
Updated At: 2025-12-25 21:09 UTC
Following plan management guidelines defined in AGENTS.md

## Overview

Implement the formula parsing engine with AST representation, UUID-based reference storage, and dependency tracking. This enables:
- Parsing formulas like `=C3+B4` into an AST
- Converting A1 references to UUID-based storage (stable across row/col moves)
- Building a dependency graph for reactive updates and UI highlighting
- Displaying formulas in A1 notation while storing with UUIDs

Execution (actually computing formula results) is deferred to a separate plan.

### Architecture Boundaries

| Layer | Language | Notes |
|-------|----------|-------|
| **Core engine** | C++ | All parsing, AST, resolution, dependency tracking. Compiles to native (desktop/mobile) and WASM (web). |
| **Web UI** | TypeScript | Calls C++ via WASM exports. All web code is TypeScript (not JavaScript). |
| **Native UI** | SwiftUI, etc. | Calls C++ directly. No JS/TS involved. |

## Testing Philosophy

**Tests must be added and passing after each testable/verifiable step.** This prevents errors from accumulating and becoming harder to fix when detected later. Each phase includes specific test requirements that must pass before proceeding.

**UI Testing Philosophy**: Whenever something can be wired to the UI for manual/visual testing, it should be done—even if the feature is partially implemented. Each phase includes a "UI Checkpoint" step that exposes the new functionality in the web UI. This allows:
- Visual verification that the feature works as expected
- Early detection of integration issues
- User-facing feedback on the implementation direction

UI checkpoints should clearly indicate:
1. What to test (specific actions to perform)
2. Expected results (what the user should see)
3. Known limitations (what won't work yet)

## Design Decisions

### Parser: Hand-written Recursive Descent

Use a hand-written C++ recursive descent parser:
- **Zero dependencies**: No external libraries required, compiles cleanly to native and WASM
- **Full control**: Custom error recovery, easy to debug and extend
- **Sufficient for formulas**: Excel formulas are short (<100 chars typically); incremental parsing unnecessary
- **Error recovery**: Parse errors create `ErrorNode` placeholders, enabling partial syntax highlighting while user is typing incomplete formulas like `=SUM(A1+`
- **Single implementation**: Same C++ code serves native clients and web (via WASM)—no reimplementation needed

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

## Phase 1: Lexer and Parser Foundation ✅ COMPLETE

Build a hand-written recursive descent parser in C++ for Excel-style formulas. This gives us zero external dependencies and full control over error recovery.

### Grammar (EBNF-style reference)

```
formula     = "=" expression
expression  = comparison
comparison  = concat (("=" | "<>" | "<" | "<=" | ">" | ">=") concat)*
concat      = additive ("&" additive)*
additive    = multiplicative (("+" | "-") multiplicative)*
multiplicative = power (("*" | "/") power)*
power       = unary ("^" unary)*
unary       = ("-" | "+")? primary
primary     = literal | reference | function_call | "(" expression ")" | error_recovery

literal     = NUMBER | STRING | BOOLEAN
reference   = [sheet_prefix] (cell_ref | range_ref | column_ref | row_ref | named_ref)
sheet_prefix = (IDENTIFIER | QUOTED_STRING) "!"
cell_ref    = "$"? COLUMN "$"? ROW
range_ref   = cell_ref ":" cell_ref
column_ref  = "$"? COLUMN (":" "$"? COLUMN)?
row_ref     = "$"? ROW (":" "$"? ROW)?
named_ref   = IDENTIFIER

function_call = IDENTIFIER "(" [arg_list] ")"
arg_list    = expression ("," expression)*
```

### Implementation Tasks

- [x] 1a: Implement lexer (tokenizer) in `core/cells/formula_lexer.h/.cc`
  - Token types: NUMBER, STRING, BOOLEAN, IDENTIFIER, COLUMN (A-ZZ), ROW (digits)
  - Operators: `+`, `-`, `*`, `/`, `^`, `&`, `=`, `<>`, `<`, `<=`, `>`, `>=`
  - Punctuation: `(`, `)`, `,`, `:`, `!`, `$`
  - Handle scientific notation (1.5e10), negative numbers, quoted strings
  - Track source position (start, end) for each token for error reporting
  - **Test**: Lexer correctly tokenizes sample formulas

- [x] 1b: Add lexer tests in `core/cells/formula_lexer_test.cc`
  - Test number tokens (integers, decimals, scientific notation)
  - Test string tokens (double-quoted, with escapes)
  - Test boolean tokens (TRUE, FALSE, case-insensitive)
  - Test cell reference tokens (A1, AA100, $A$1)
  - Test operators and punctuation
  - Test position tracking
  - Test error tokens for invalid input
  - **Test**: All lexer tests pass (75 tests)

- [x] 1c: Implement recursive descent parser in `core/cells/formula_parser.h/.cc`
  - `FormulaParser` class with `parse(string) -> unique_ptr<ASTNode>`
  - Implement precedence via grammar structure (comparison < concat < additive < mult < power < unary)
  - Handle all reference types: cell, range, whole column/row, cross-sheet
  - Distinguish named ranges from function calls (lookahead for `(`)
  - Support function calls with arbitrary arity
  - **Test**: Basic parsing works

- [x] 1d: Implement error recovery in parser
  - On unexpected token, create `ErrorNode` with partial children
  - Continue parsing to capture as much structure as possible
  - Example: `=SUM(A1+` → FunctionCall with ErrorNode as second arg
  - Example: `=A1++B2` → BinaryOp with ErrorNode between operands
  - Store error message and position in ErrorNode
  - **Test**: Error recovery produces useful partial ASTs

- [x] 1e: Add parser tests in `core/cells/formula_parser_test.cc`
  - Test literals (all numeric formats, strings, booleans)
  - Test operators with correct precedence: `=1+2*3` → `1+(2*3)` not `(1+2)*3`
  - Test all reference types (cell, range, column, row, cross-sheet)
  - Test absolute/relative markers ($A$1, A$1, $A1, A1)
  - Test function calls with 0, 1, many arguments
  - Test nested expressions
  - Test error recovery for various malformed inputs
  - **Test**: All parser tests pass (55 tests)

- [x] 1f: **UI Checkpoint** - Parse tree visualization
  - Add WASM binding: `debugParseFormula(text)` → returns JSON AST representation
  - Add debug panel in web UI (hidden by default, toggle with keyboard shortcut Ctrl+Shift+D)
  - Display AST tree when typing in formula bar
  - **Test manually**:
    - Type `=1+2` → see AST with BinaryOp, Literal nodes
    - Type `=SUM(A1:B2)` → see FunctionCall, RangeRef nodes
    - Type `=SUM(A1+` → see ErrorNode in tree (error recovery working)
  - **Expected**: Tree updates live as you type; ErrorNodes appear for invalid syntax
  - **Limitations**: No UUID resolution yet, just syntax structure

---

## Phase 2: AST Types ✅ COMPLETE

Define the AST node types used by the parser. With a hand-written parser, the parser directly produces AST nodes (no CST-to-AST conversion needed).

- [x] 2a: Define AST node types in `core/cells/formula_ast.h`
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
  - `toJson()` method for debug visualization
  - **Test**: Header compiles, basic node creation works

- [x] 2b: Add AST tests in `core/cells/formula_ast_test.cc`
  - Test node construction and destruction
  - Test AST cloning (deep copy)
  - Test JSON serialization for debug panel
  - Test source position preservation
  - **Test**: All AST tests pass (65 tests)

**Note**: Parser tests (2c, 2d from previous plan) are now in Phase 1 since the parser directly produces AST nodes.

---

## Phase 3: Reference Resolution

Convert A1 references in AST to UUID-based references, auto-creating cells as needed.

- [x] 3a: Add cell/axis auto-creation to Sheet in `core/cells/model.h` and `model.cc`
  - `getOrCreateCellAt(colId, rowId)` - returns existing cell or creates new one
  - `getOrCreateAxisByPosition(position, isColumn)` - returns existing axis or creates new one
  - `getAxisByName(name)` - look up column by letter (A, B, ..., Z, AA, AB, ...)
  - These are needed when parsing formulas that reference non-existent cells
  - **Test**: Auto-creation works, lookup by name works

- [x] 3b: Add named range registry in `core/cells/named_ranges.h` and `named_ranges.cc`
  - `NamedRangeRegistry` class
  - Store workbook-scoped and sheet-scoped named ranges
  - `define(name, scope, rangeOrCell)` - create named range
  - `resolve(name, currentSheet)` - look up, respecting scope shadowing
  - `remove(name, scope)` - delete named range
  - **Test**: Define/resolve/remove works, sheet scope shadows workbook scope (28 tests)

- [x] 3c: Implement reference resolver in `core/cells/formula_resolver.h` and `formula_resolver.cc`
  - `resolveReferences(ASTNode*, Sheet&, NamedRangeRegistry&)` - walk AST, convert A1 to UUIDs
  - For CellRefNode: look up col by letter, row by number, get/create cell, store UUID
  - For RangeRefNode: resolve both corner cells, store UUIDs
  - For ColumnRefNode/RowRefNode: resolve axis UUID
  - For NamedRefNode: look up in registry, replace with resolved reference
  - For cross-sheet refs: look up sheet by name first
  - Preserve absolute/relative flags from original A1 notation
  - Error handling for invalid references (create ErrorNode)
  - **Test**: Basic resolution works

- [x] 3d: Add A1 display conversion in `core/cells/formula_resolver.cc`
  - `toDisplayString(ASTNode*, const Sheet&)` - convert AST back to A1 notation for display
  - Walk AST, convert UUID refs back to A1 using axis positions
  - Handle absolute/relative markers ($)
  - Rebuild formula string with operators and functions
  - **Test**: Display conversion works

- [x] 3e: Add resolver tests in `core/cells/formula_resolver_test.cc`
  - Test A1 to UUID resolution (29 tests)
  - Test auto-creation of cells and axes
  - Test round-trip (parse -> resolve -> display)
  - Test absolute/relative preservation
  - Test range resolution (both corners)
  - Test whole column/row resolution
  - Test named range resolution (both scopes)
  - Test reference extraction for UI highlighting
  - **Test**: All tests pass

- [ ] 3f: **UI Checkpoint** - Reference highlighting (basic) - DEFERRED (backend complete)
  - Wire formula bar to use parser + resolver
  - When editing a formula, highlight referenced cells on the grid
  - Use different colors for different references (like Numbers/Excel)
  - Show both A1 text in formula bar and highlight cells in grid
  - **Test manually**:
    - Type `=A1` → cell A1 highlights with color
    - Type `=A1+B2` → cells A1 and B2 highlight with different colors
    - Type `=A1:C3` → range A1:C3 highlights as a block
    - Type `=SUM(A1,B2,C3)` → three cells highlight with three colors
    - Click on non-existent cell reference (e.g., `=ZZ999`) → cell auto-created, highlighted
  - **Expected**: Referenced cells visually highlighted; colors match formula segments
  - **Limitations**: Formula doesn't execute yet (no computed values); no dependency tracking

---

## Phase 4: R-tree Dependency Graph

Track which cells depend on which for reactive updates, using R-tree for efficient range queries.

- [x] 4a: Implement R-tree in `core/cells/rtree.h` and `rtree.cc`
  - Custom 2D R-tree implementation optimized for sparse cell coordinates
  - `RTree<T>` template class
  - `insert(minCol, minRow, maxCol, maxRow, value)` - insert rectangle
  - `remove(minCol, minRow, maxCol, maxRow, value)` - remove rectangle
  - `query(col, row)` - find all rectangles containing point
  - `queryRange(minCol, minRow, maxCol, maxRow)` - find all rectangles intersecting range
  - Handle whole-column refs (row bounds = 0 to MAX) and whole-row refs (col bounds = 0 to MAX)
  - **Test**: Basic insert/remove/query works

- [x] 4b: Add R-tree tests in `core/cells/rtree_test.cc`
  - Test point insertion (1×1 rectangles)
  - Test range insertion
  - Test point queries
  - Test range queries
  - Test removal
  - Test large-scale performance (10k+ entries)
  - Test sparse coordinates (non-contiguous positions)
  - **Test**: All tests pass before proceeding

- [x] 4c: Define dependency graph in `core/cells/dependency_graph.h`
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

- [x] 4d: Implement dependency extraction in `core/cells/dependency_graph.cc`
  - Walk AST to extract all references
  - For CellRef: insert 1×1 rectangle at (col, row)
  - For RangeRef: insert rectangle from topLeft to bottomRight corners
  - For ColumnRef: insert rectangle (col, 0) to (col, MAX)
  - For RowRef: insert rectangle (0, row) to (MAX, row)
  - Track volatile functions (NOW, RAND, etc.) and mark cell
  - **Test**: Extraction works for all reference types

- [x] 4e: Add circular reference detection (lazy)
  - `detectCycle(startCellId)` - DFS from cell, return cycle path if found
  - `getRecalcOrder(changedCells)` - topological sort of affected cells
  - Called at recalc time, not on formula entry
  - Return error info for UI display when cycle detected
  - **Test**: Cycle detection works

- [x] 4f: Add dependency graph tests in `core/cells/dependency_graph_test.cc`
  - Test single cell dependencies
  - Test range dependencies
  - Test whole column/row dependencies
  - Test transitive dependencies
  - Test circular reference detection
  - Test dependency removal
  - Test volatile cell tracking
  - Test recalc ordering
  - **Test**: All tests pass before proceeding

- [ ] 4g: **UI Checkpoint** - Dependency visualization
  - Add "Show Dependencies" mode (toggle with keyboard shortcut or toolbar button)
  - When a cell with a formula is selected, show:
    - **Precedents**: cells this formula reads from (highlight in blue)
    - **Dependents**: cells that read from this cell (highlight in green)
  - Draw arrows or lines connecting dependent cells (optional, can be simple highlights)
  - **Test manually**:
    - Create formula `=A1+B1` in C1, select C1 → A1 and B1 highlight as precedents
    - Select A1 → C1 highlights as dependent
    - Create `=C1*2` in D1, select C1 → D1 shows as dependent, A1/B1 as precedents
    - Create circular ref `=D1` in A1 → warning indicator shown (cycle detected)
  - **Expected**: Clear visual of what feeds into and out of each formula cell
  - **Limitations**: No execution yet; values won't update when dependencies change

---

## Phase 5: Integration with Model

Wire the parser and dependency graph into the Cell/Sheet model.

- [x] 5a: Update Cell/Formula structs in `core/cells/model.h`
  - Add `unique_ptr<ASTNode> ast` field to Formula (already present)
  - Add `parse()` method to Formula that uses the parser
  - Add `isValid()` helper to check if AST has no ErrorNodes
  - Add `hasVolatile()` helper to check for volatile functions
  - **Test**: Formula struct works with AST

- [x] 5b: Add formula management to Sheet
  - `setCellFormula(cellId, formulaText, ast)` - set formula with pre-resolved AST, update deps
  - `setCellFormulaUnresolved(cellId, formulaText)` - parse only, no resolution
  - `getCellFormulaText(cellId)` - get raw formula text
  - `clearCellFormula(cellId)` - remove formula and deps
  - Sheet owns one `DependencyGraph` instance
  - Workbook owns one `NamedRangeRegistry` instance
  - Added `getSheetByName()` to Workbook for cross-sheet references
  - **Test**: Set/get/clear formula works

- [x] 5c: Add serialization support (PARTIAL - needs 5f)
  - Formulas currently stored as A1 notation text in formula.text
  - Serializer writes formula.text directly (already working)
  - Parser reads formula.text and creates Formula object (already working)
  - **ISSUE**: A1 storage breaks move stability - see Phase 5f for fix
  - **Test**: Serialization round-trip works (but with A1, not UUID)

- [x] 5d: Add integration tests in `core/cells/formula_integration_test.cc`
  - Test setting formulas via Sheet API
  - Test serialization round-trip
  - Test dependency graph integration
  - Test named ranges through Sheet API
  - Test volatile function tracking
  - Test Workbook::getSheetByName
  - **Test**: All 19 tests pass

- [ ] 5e: **UI Checkpoint** - Formula persistence and display
  - Formulas should now persist: enter formula, refresh page, formula still there
  - Formula bar shows A1 notation when cell selected
  - Grid cell shows formula text (prefixed with `=`) since execution not implemented yet
  - **Test manually**:
    - Enter `=A1+B2` in C1 → formula bar shows `=A1+B2`, cell shows `=A1+B2`
    - Refresh page → formula still present, references still highlighted when editing
    - Save file, reload → formula preserved exactly
    - Create named range "total" → use `=total` in formula → resolves correctly
  - **Expected**: Formulas round-trip through save/load; display matches input
  - **Limitations**: Cell shows formula text, not computed value (execution deferred)

- [ ] 5f: **FIX** - Implement UUID-based formula storage (REQUIRED for Phase 6)

  **Problem**: Current implementation stores A1 notation in formula.text, which breaks
  move stability. When column A becomes column B, stored `=A1` is now wrong.

  **Correct architecture**:
  ```
  User enters: =A1+B1 (A1 notation)
       ↓
  Parse to AST (CellRefNode with column="A", row=1)
       ↓
  Resolve: Fill in cellId UUIDs for each CellRefNode
       ↓
  Generate UUID text: =cellId1+cellId2 (using RefConverter format)
       ↓
  Store in formula.text (UUID format, e.g., "=$$xK7mNp2Q+fR3pK7wN")
       ↓
  Serialize to .zcd file (UUID format)

  For display:
  Read formula.text → parse → for each CellRefNode:
    cellId → lookup cell → get col/row positions → format as A1
  ```

  **UUID ref format** (updated for parsing clarity):
  - `$$cellId` → `$A$1` (both absolute)
  - `$~cellId` → `$A1` (col absolute, row relative)
  - `~$cellId` → `A$1` (col relative, row absolute)
  - `~~cellId` → `A1` (both relative)

  All refs are exactly 10 chars (2-char prefix + 8-char cell UUID).
  This makes lexer/parser simpler - no ambiguity with function names.
  Note: Binary format will use 2-bit flag, text format uses `~~` for clarity.
  Note: Existing RefConverter uses bare ID for relative - update to use `~~`.

  **Sub-tasks**:

  - [x] 5f.1: Add `FormulaSerializer` class to convert resolved AST → UUID text
    - Walk AST, for each CellRefNode: format using UUID ref format
    - Handle ranges: `~~cellId1:~~cellId2` (or with appropriate abs markers)
    - Handle named ranges: pass through as-is
    - Handle all operators and functions
    - **Test**: `=A1+B2` with resolved UUIDs → `=~~xK7mNp2Q+~~fR3pK7wN`

  - [x] 5f.2: Update FormulaLexer to tokenize UUID refs
    - Recognize patterns: `$$`, `$~`, `~$`, `~~` followed by 8 alphanumeric chars
    - All UUID refs are exactly 10 chars - simple pattern matching
    - New token type UUID_CELL_REF or extend CELL_REF
    - **Test**: Lexer tokenizes `=$$xK7mNp2Q+~~fR3pK7wN` correctly

  - [x] 5f.3: Update FormulaParser to handle UUID ref tokens
    - CellRefNode stores cellId directly (no column/row needed for UUID refs)
    - Add `CellRefNode::isUuidFormat` flag or detect from fields
    - **Test**: Parser produces correct AST from UUID-format formula

  - [x] 5f.4: Update Sheet::setCellFormula to store UUID format
    - After resolution, call FormulaSerializer to generate UUID text
    - Store UUID text in formula.text (NOT the original A1 input)
    - Keep AST for immediate use
    - **Test**: After setCellFormula("=A1"), formula.text contains UUID format (3 new tests)

  - [x] 5f.5: Update display to convert UUID → A1 for user
    - FormulaDisplayConverter extracted to formula_display.h/.cc
    - Walk AST, convert each UUID ref to A1 based on current cell positions
    - **Test**: Display shows "=A1+B2" from stored UUID format (4 new tests)

  - [x] 5f.6: Update parser.cc to handle UUID format on load
    - FormulaParser already handles UUID refs via parseUuidCellRef()
    - Formula::parse() uses FormulaParser which can parse UUID format
    - When .zcd file is loaded, UUID text is preserved in Formula.text
    - **Test**: Load .zcd with UUID formulas, display shows correct A1 (4 new tests)

  - [x] 5f.7: Update integration tests
    - Added UuidFormatSerializationRoundTrip test
    - Added UuidFormatAbsoluteRefRoundTrip test
    - Added UuidFormatRangeRefRoundTrip test
    - Added UuidFormatFunctionCallRoundTrip test
    - **Test**: All 27 integration tests pass (4 new tests for UUID format)

  - [x] 5f.8: Comprehensive UUID round-trip test suite
    Formula handling is critical infrastructure - requires aggressive testing.
    All tests verify: A1 input → UUID storage → serialize → load → parse → A1 display

    **Cell Reference Variations**:
    - [x] Mixed absolute `$A1` (col absolute, row relative)
    - [x] Mixed absolute `A$1` (col relative, row absolute)
    - [x] Multiple refs in one formula `=A1+B1+C1+A2+B2+C2`
    - [x] Same cell referenced multiple times `=A1+A1+A1`
    - [x] References to cells in different quadrants of grid

    **Range Reference Variations**:
    - [x] Fully absolute range `=$A$1:$B$2`
    - [x] Mixed absolute range `=$A1:B$2`
    - [x] Single-row range `=SUM(A1:C1)`
    - [x] Single-column range `=SUM(A1:A3)`
    - [x] Large range spanning many cells `=SUM(A1:C3)`

    **All Operators**:
    - [x] Arithmetic: `+`, `-`, `*`, `/`, `^` each with cell refs
    - [x] Comparison: `=`, `<>`, `<`, `<=`, `>`, `>=` each with cell refs
    - [x] Concatenation: `=A1&B1`
    - [x] Unary minus: `=-A1`
    - [x] Unary plus: `=+A1`
    - [x] Operator precedence: `=A1+B1*C1` (verify structure preserved)

    **Complex Nested Expressions**:
    - [x] Deeply nested parens: `=((A1+B1)*C1)/A2` (extra parens optimized away)
    - [x] Nested function calls: `=SUM(A1,MAX(B1,C1))`
    - [x] Function with expression args: `=IF(A1+B1>10,C1*2,0)`
    - [x] Multiple nested levels: `=IF(AND(A1>0,B1>0),SUM(C1:C3),0)`

    **Literals Mixed with Refs**:
    - [x] Number literals: `=A1+10`, `=A1*3.14`, `=A1/100`
    - [x] String literals: `=A1&"hello"`, `="prefix"&A1&"suffix"`
    - [x] Boolean literals: `=IF(TRUE,A1,B1)`, `=AND(A1>0,FALSE)`
    - [x] Scientific notation: `=A1*1.5e10` (formatted as 1.5e+10)

    **Function Variations**:
    - [x] Zero-arg functions: `=NOW()`, `=TODAY()`, `=RAND()`
    - [x] Single-arg functions: `=ABS(A1)`, `=SQRT(B1)`
    - [x] Multi-arg functions: `=IF(A1,B1,C1)`, `=ROUND(A1,2)`
    - [x] Variadic functions: `=SUM(A1,B1,C1)`, `=MAX(A1,B1,C1,A2)`
    - [x] Nested functions: `=ROUND(SUM(A1:B2)/COUNT(A1:B2),2)`

    **Edge Cases**:
    - [x] Very long formula (10+ refs)
    - [x] Formula referencing same cell as it's in (circular - should store OK)
    - [x] Pure literal formulas (no UUID format needed)
    - [x] Complex combined formula with all features
    - [x] All absolute marker combinations in one formula
    - [x] Range with all absolute marker combinations

    **Test**: All 83 integration tests pass (53 new round-trip tests)

---

## Phase 6: Column/Row Move Stability ✅ COMPLETE

Test that formulas remain stable when columns/rows are moved.

- [x] 6a: Add axis move operations to Sheet (if not already present)
  - `moveColumn(colId, newPosition)` - update column position
  - `moveRow(rowId, newPosition)` - update row position
  - `insertColumnAt(position)` - insert new column, shift others right
  - `insertRowAt(position)` - insert new row, shift others down
  - `deleteColumn(colId)` - delete column, shift others left
  - `deleteRow(rowId)` - delete row, shift others up
  - Formulas should NOT need updating (they use UUIDs)
  - **Test**: Move operations work (15 tests)

- [x] 6b: Add move stability tests in `core/cells/formula_move_test.cc`
  - Create formula `=B2+C3`
  - Move column B to position 5 (becomes F)
  - Verify formula display updates to `=F2+C3`
  - Verify stored formula still uses same UUIDs
  - Verify dependencies unchanged
  - **Test**: Move stability works (15 tests)

- [x] 6c: Add insert/delete stability tests
  - Test inserting column before referenced column (display updates, UUIDs unchanged)
  - Test inserting row within a range (range expands automatically via two-corner representation)
  - Test deleting row outside a range (range unchanged)
  - Test that unaffected formulas don't change
  - **Test**: All insert/delete tests pass (13 tests)

- [x] 6d: Add range expansion tests
  - Create formula `=SUM(A1:A5)`
  - Insert row between rows 2 and 3
  - Verify range now includes the new row (bounds-based semantics)
  - Verify display shows `=SUM(A1:A6)`
  - **Test**: Range expansion works (10 tests)

  Total: 38 tests in formula_move_test.cc

- [x] 6e: **UI Checkpoint** - Move stability visualization ✅
  - This is a key demonstration of the UUID-based reference system
  - Provide UI controls to move columns/rows (drag-and-drop or menu)
  - **Test manually**:
    - Enter value `10` in B2, formula `=B2*2` in C2
    - Move column B to position D (columns now: A, C, D, ...)
    - Observe: formula in C2 now displays `=D2*2` (auto-updated!)
    - References still highlight correctly when editing
    - Insert new column between A and C → formula updates to `=E2*2`
    - Insert row above row 2 → formula updates to `=E3*2`
  - **Expected**: Formula references auto-update on structural changes; highlighting stays correct
  - **Limitations**: Still no execution; values don't compute yet
  - **Bug fixed**: RefConverter used vectors indexed by loop counter instead of maps indexed by position.
    Added sparse position tests to catch this regression.

---

## Phase 7: UI Integration (WASM bindings) ✅ COMPLETE

Expose C++ formula functionality to the TypeScript web UI via WASM bindings. The architecture is:
- **Core logic**: C++ (parser, resolver, dependency graph) compiled to WASM
- **Web UI**: TypeScript calling WASM exports
- **Native clients**: C++ directly (SwiftUI, etc.) - no JS/TS involved

- [x] 7a: Add WASM bindings for formula parsing in `apps/wasm/`
  - C++ functions exposed via Emscripten bindings
  - `validateFormula(text)` - parse without side effects, return errors for live feedback
  - `getFormulaDisplay(cellId)` - get A1 display string
  - `getCellDependencies(cellId)` - get list of cells this formula reads (for UI highlighting)
  - Added dependency_graph, formula_ast, formula_display, formula_resolver deps to BUILD
  - **Test**: WASM bindings compile and build successfully

- [x] 7b: Add WASM bindings for dependency visualization
  - `getCellDependents(cellId)` - cells that depend on this cell
  - `getFormulaReferences(formulaText)` - get refs with source positions (for colored highlighting)
  - `detectCircularRef(cellId)` - detect circular reference cycles
  - `getVolatileCells()` - get cells with volatile functions (NOW, RAND, etc.)
  - TypeScript type definitions added to cells.d.ts
  - **Test**: WASM bindings compile and build successfully

- [x] 7c: Add WASM bindings for live formula editing
  - `getReferencesFromPartial(text)` - extract valid references from incomplete formula
  - Parser has error recovery, so partial parsing extracts valid refs even from incomplete formulas
  - Note: With hand-written parser, we re-parse the entire formula on each keystroke (fast enough for short formulas)
  - **Test**: WASM bindings compile and build successfully

- [x] 7d: Update web UI to display formula dependencies
  - When editing a formula, highlight referenced cells with colors
  - Show different colors for different refs (like Numbers UI) - 8 colors in palette
  - Update highlights in real-time as formula is typed via `getReferencesFromPartial()`
  - Added TypeScript types in client-types.ts (ReferenceInfo, FormulaReferencesResult, etc.)
  - Added worker.ts handlers for formula API methods
  - Added client.ts methods for calling formula API
  - Added grid-constants.ts colors (FORMULA_REF_COLORS, FORMULA_ERROR_COLOR)
  - Added FormulaHighlight type and GridRendererState.formulaHighlights
  - Created grid-formula-renderer.ts for drawing highlights
  - Integrated highlights into GridRenderer.render()
  - Added FormulaBarEditor.onUpdateFormulaHighlights callback
  - Highlights cleared on commit/cancel formula edit
  - **Test**: TypeScript compiles, all C++ tests pass. Manual UI testing needed.
  - **Note**: Cell reference resolution requires cells to be in viewport; cells outside viewport not highlighted yet

---

## Phase 8: Polish & Hardening - PENDING

Address issues found during Phase 7d testing and ensure rock-solid foundation before moving to calculation engine.

### 8a: Show highlights when selecting a formula cell (not just while editing) ✅
- [x] When a cell with a formula is selected (not editing), show dependency highlights
- [x] Parse the formula and highlight referenced cells immediately on selection
- [x] Clear highlights when selecting a non-formula cell
- [x] **Test**: Select formula cell → highlights visible; select empty cell → no highlights
- Implementation: Modified `updateFormulaBar()` in `init.ts` to call `updateFormulaHighlights()`
  with the cell's formula when a formula cell is selected (not in editing mode)

### 8b: Create cells on-demand when referenced in formulas ✅
- [x] When typing a formula reference like `=A2`, create the referenced cell UUID immediately if it doesn't exist
- [x] This allows highlights to work for empty cell references
- [x] Currently cells may be created but too late for the first highlight pass
- [x] Ensure this happens during `getReferencesFromPartial()` or as a separate "ensure cells exist" step
- [x] **Test**: Type `=A1` in empty sheet → A1 cell created and highlighted
- Implementation: Cells ARE created during `getReferencesFromPartial()` via C++ `FormulaResolver::resolve()`.
  The issue was that TypeScript's viewport data wasn't refreshed to include newly created cells.
  Modified `updateFormulaHighlights()` to detect unresolved references and refresh viewport once.

### 8c: Track and cleanup orphan empty cells - DEFERRED
- [ ] Add a flag or mechanism to identify "auto-created" empty cells
- [ ] Implement cleanup logic: remove cells that are empty AND not referenced by any formula
- [ ] Cleanup should happen at appropriate times (e.g., after formula edit commit, on file save)
- [ ] **Test**: Create formula `=A1`, delete formula → A1 should be cleaned up if still empty
- **Deferred**: Empty cells don't cause functional issues - they just consume minimal storage.
  This optimization can be done in a later polish pass or when file size becomes a concern.

### 8d: In-cell editing should show formula highlights ✅
- [x] Wire CellEditor input events to trigger formula highlighting (like FormulaBarEditor)
- [x] Sync highlights between cell editor and formula bar editing
- [x] **Test**: Double-click cell, type `=B1+C1` → both B1 and C1 highlighted
- Implementation: Added `onUpdateFormulaHighlights` callback to CellEditor, wired to
  call on input events and clear on commit/cancel

### 8e: Fix state machine for column/row operations during editing ✅
- [x] Allow starting column/row drag while editing a cell
- [x] This should commit the current edit (like pressing Enter) and start the drag
- [x] Review UIStateMachine transitions for CELL_EDITING → COLUMN_DRAGGING/ROW_DRAGGING
- [x] **Test**: Edit cell, drag column header → edit commits, column moves
- Implementation:
  - Added state transitions from CELL_EDITING and FORMULA_BAR_EDITING to dragging/resizing states
  - Modified handleMouseDown to commit edits before starting resize operations
  - Modified handleMouseMove to commit edits before starting drag operations

### 8f: Comprehensive test coverage for formula parsing & AST
- [ ] Review and expand `formula_lexer_test.cc` - cover all token types, edge cases
- [ ] Review and expand `formula_parser_test.cc` - complex nested expressions, all operators
- [ ] Review and expand `formula_ast_test.cc` - visitor patterns, node types
- [ ] Add tests for partial/incomplete formula parsing (error recovery)
- [ ] Add tests for formula reference extraction from partial formulas
- [ ] Add tests for formula display (A1 notation generation)
- [ ] Ensure 100% coverage of critical paths
- [ ] **Test**: All C++ tests pass with comprehensive coverage

### 8g: UI Integration Tests (TypeScript)
- [ ] Add tests for formula highlighting logic in TypeScript
- [ ] Test reference→highlight conversion
- [ ] Test color assignment
- [ ] Test highlight clearing on edit commit/cancel
- [ ] Consider adding Playwright e2e tests for visual verification
- [ ] **Test**: TypeScript tests pass, optional e2e tests for visual verification

### 8h: Final UI Polish Checkpoint
- [ ] Manual testing of all formula editing scenarios
- [ ] Verify highlights work for: cell refs, range refs, mixed refs
- [ ] Verify highlights update in real-time while typing
- [ ] Verify highlights clear appropriately
- [ ] Verify formula bar and in-cell editing behave identically
- [ ] Verify state machine allows all expected transitions
- [ ] **Checkpoint**: Demo video or screenshots showing polished behavior

---

## Future Work (Separate Plans)

The following are explicitly deferred to separate plans:

- **Calculation Engine**: Evaluating formulas to compute results using a pure C++ AST interpreter
  - No scripting language (Luau/Lua) - keeps the stack simple and performant
  - Direct AST traversal with C++ function implementations
  - Function library: SUM, IF, VLOOKUP, AVERAGE, COUNT, etc.
  - Recalculation engine using topological sort on dependency graph
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

| Phase | Test File | UI Checkpoint |
|-------|-----------|---------------|
| 1 | `core/cells/formula_lexer_test.cc`, `core/cells/formula_parser_test.cc` | 1f: Debug panel with AST visualization |
| 2 | `core/cells/formula_ast_test.cc` | (covered by Phase 1 UI checkpoint) |
| 3 | `core/cells/formula_resolver_test.cc` | 3f: Reference highlighting in grid |
| 4 | `core/cells/rtree_test.cc`, `core/cells/dependency_graph_test.cc` | 4g: Precedent/dependent visualization |
| 5 | `core/cells/formula_integration_test.cc` | 5e: Formula persistence and display |
| 6 | `core/cells/formula_move_test.cc` | 6e: Move stability demonstration |
| 7 | Manual testing + TypeScript integration tests | 7d: Full formula editing UI |
| 8 | Expanded C++ tests + TypeScript tests + e2e | 8h: Final polish checkpoint |
