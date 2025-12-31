Status: READY
Created At: 2025-12-31 20:25 UTC
Updated At: 2025-12-31 20:25 UTC
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

## Phase 2: Remove Formula.text field

Formula struct should only contain AST.

- [ ] 2a: Audit all usages of `formula->text`
- [ ] 2b: Update `Sheet::getCellFormulaText()` to generate from AST via FormulaDisplayConverter
- [ ] 2c: Update `Sheet::setCellFormula()` to not store text
- [ ] 2d: Update serializer.cc to generate text from AST on save
- [ ] 2e: Update xlsx_writer.cc to generate text from AST
- [ ] 2f: Remove `text` field from Formula struct (types.h)
- [ ] 2g: Fix all compilation errors
- [ ] 2h: Verify unit tests pass

## Phase 3: Remove cell->value.raw for formulas

Display string should not be stored on Cell.

- [ ] 3a: Audit all usages of `cell->value.raw` for formula cells
- [ ] 3b: Update CRDT apply to not set `cell->value.raw` for formulas
- [ ] 3c: Update any code reading `cell->value.raw` for formulas to use FormulaDisplayConverter
- [ ] 3d: Verify unit tests pass

## Phase 4: Clean up CRDT payload

Remove the `display` field from CRDT formula operations.

- [ ] 4a: Update `updateCell()` in bindings.cc to not include `display` field
- [ ] 4b: Update `createCell()` in bindings.cc similarly
- [ ] 4c: Update CRDT serialization (for oplog/sync) to not include `display`
- [ ] 4d: Update CRDT apply to not expect `display` field (backward compat: ignore if present)
- [ ] 4e: Verify sync between peers still works (UUID formula syncs, display generated locally)
- [ ] 4f: Verify unit tests pass

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
