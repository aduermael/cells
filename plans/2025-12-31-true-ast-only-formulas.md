Status: IN_PROGRESS
Created At: 2025-12-31 20:25 UTC
Updated At: 2025-12-31 22:25 UTC
Following plan management guidelines defined in AGENTS.md

# True AST-Only Formula Storage

## Motivation

The previous "AST-first" plan was incomplete. While it added AST-based reference adjustment, formulas are still stored as strings in multiple places:

- `formula->text` - UUID-serialized string
- `cell->value.raw` - original user input (preserves whitespace like "= A1")
- CRDT `display` field - syncs original input to peers

This causes:
- "= A1" doesn't normalize to "=A1" when displayed
- Redundant storage and potential inconsistencies
- String manipulation still possible (bypassing AST)

## Goal

**AST is the ONLY storage format. Strings are generated on-demand.**

- Formulas parse to AST on input
- AST stored in memory and synced via CRDT (UUID-serialized)
- Display strings generated via `FormulaDisplayConverter` when needed
- UI may cache ephemeral typed string while editing (UX only)

## Key Decisions

1. **CRDT sync**: UUID-serialized AST only (no `display` field)
2. **ErrorNode**: Add `rawText` field to preserve unparseable input
3. **No caching**: Generate display strings on-demand (add cache later if needed)
4. **Normalization**: "= A1", "=a1", "=A1" all display as "=A1"

---

## Phase 1: Add rawText to ErrorNode ✅

Preserve unparseable formula text so users can fix syntax errors.

- [x] 1a: Add `std::string rawText` field to `ErrorNode` in formula_ast.h
- [x] 1b: Update `ErrorNode::clone()` to copy rawText
- [x] 1c: Update `FormulaParser` to populate rawText when creating ErrorNode
- [x] 1d: Update `FormulaDisplayConverter` to display rawText for ErrorNode
- [x] 1e: Add unit tests for ErrorNode rawText preservation

## Phase 2: Remove Formula.text field ✅

Formula struct should only contain AST.

**NOTE**: Audit shows this was ALREADY DONE:
- Formula struct (model.h:76-98) only has `ast` and `dirty` fields
- getCellFormulaText() (model.cc:872-890) already generates from AST
- setCellFormula() (model.cc:810-839) doesn't store text
- serializer.cc (line 207) already generates from AST
- xlsx_writer.cc (line 340) already generates from AST

- [x] 2a: Audit all usages of `formula->text` - NONE EXIST
- [x] 2b: Update `Sheet::getCellFormulaText()` to generate from AST via FormulaDisplayConverter - ALREADY DONE
- [x] 2c: Update `Sheet::setCellFormula()` to not store text - ALREADY DONE
- [x] 2d: Update serializer.cc to generate text from AST on save - ALREADY DONE
- [x] 2e: Update xlsx_writer.cc to generate text from AST - ALREADY DONE
- [x] 2f: Remove `text` field from Formula struct (types.h) - NEVER EXISTED IN CURRENT CODEBASE
- [x] 2g: Fix all compilation errors - N/A
- [x] 2h: Verify unit tests pass - DONE (fixed 14 sync_formula_test cases with wrong UUID format/expectations)

## Phase 3: Audit cell->value.raw for formulas ✅

**NOTE**: Audit revealed `cell->value.raw` is already used correctly:

**Finding**: For formula cells, `cell->value.raw` stores the **COMPUTED RESULT** (e.g., "42" for `=40+2`), NOT the formula text. This is the correct behavior.

Implementation details:
- `crdt.cc:318` - Sets `cell->value.raw = ""` initially for formula cells (result stored later via evaluation)
- `formula_recalc.cc:131-144` - Stores computed result via `CellValue(42.0)` which sets `raw = "42"`
- `model.cc:104-115` - `CellValue` constructor formats numbers, stripping trailing zeros
- Formula TEXT is generated on-demand from `cell->formula->ast` via `FormulaDisplayConverter`

The only special case is file parsing (`parser.cc:480, 593`) which temporarily uses `value.raw` to hold formula text during deserialization before parsing it to AST. This is an internal implementation detail, not long-term storage.

- [x] 3a: Audit all usages of `cell->value.raw` for formula cells - IMPLEMENTATION IS CORRECT
- [x] 3b: N/A - `cell->value.raw` stores computed result, not formula text
- [x] 3c: N/A - Code already uses FormulaDisplayConverter for formula text
- [x] 3d: N/A - No changes needed

## Phase 4: Clean up CRDT payload ✅

Remove the `display` field from CRDT formula operations.

- [x] 4a: Update `updateCell()` in bindings.cc to not include `display` field
- [x] 4b: Update `createCell()` in bindings.cc similarly
- [x] 4c: Update fill_range.cc buildFormulaPayload() - removed `display` field
- [x] 4d: Update CRDT apply - ALREADY ignores `display` field (backward compat preserved)
- [x] 4e: Update sync_formula_test.cc - removed `display` from all 30+ test payloads
- [x] 4f: Verify unit tests pass - sync_formula_test passes

## Phase 5: Verify normalization

Confirm that formulas are properly normalized.

- [ ] 5a: Add unit test: "= A1" -> displays as "=A1"
- [ ] 5b: Add unit test: "=a1" -> displays as "=A1" (uppercase columns)
- [ ] 5c: Add unit test: "=SUM( A1 , B2 )" -> displays as "=SUM(A1,B2)"
- [ ] 5d: Add E2E test: enter "= A1", verify formula bar shows "=A1"
- [ ] 5e: Final `make lint && make format && make test`

---

## Technical Details

### ErrorNode with rawText

```cpp
class ErrorNode : public ASTNode {
public:
    std::string message;      // Error description
    std::string rawText;      // Original unparseable text (for editing)
    std::vector<std::unique_ptr<ASTNode>> partialChildren;

    std::unique_ptr<ASTNode> clone() const override {
        auto copy = std::make_unique<ErrorNode>(message, position);
        copy->rawText = rawText;
        for (const auto& child : partialChildren) {
            if (child) copy->partialChildren.push_back(child->clone());
        }
        return copy;
    }
};
```

### Formula struct (after)

```cpp
struct Formula {
    ASTNode* ast;        // Only AST - source of truth
    bool dirty;          // Needs recalculation
    Value cachedResult;  // Last computed value

    // No more: char* text;
};
```

### getCellFormulaText() (after)

```cpp
std::string Sheet::getCellFormulaText(const ID& cellId) const {
    const Cell* cell = getCell(cellId);
    if (!cell || !cell->isFormula()) return "";

    const Formula* formula = cell->getFormula();
    if (!formula || !formula->ast) return "";

    FormulaDisplayConverter converter(*this);
    return converter.toDisplayString(formula->ast);
}
```

### CRDT payload (after)

```cpp
// Before: {"type":"f","value":"=cellId123","display":"=A1",...}
// After:  {"type":"f","value":"=cellId123",...}
// Display is generated locally from AST
```

---

## Backward Compatibility

- CRDT apply should gracefully ignore `display` field if present (for old operations in oplog)
- .cells file format unchanged (text generated from AST on save)
- XLSX format unchanged (text generated from AST on save)

## Testing Strategy

1. Unit tests for ErrorNode rawText
2. Unit tests for formula normalization
3. Integration tests for CRDT sync
4. E2E tests for formula bar display
5. Regression: all existing tests must pass
