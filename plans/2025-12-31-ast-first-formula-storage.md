Status: COMPLETED
Created At: 2025-12-31 18:06 UTC
Updated At: 2025-12-31 22:45 UTC
Following plan management guidelines defined in AGENTS.md

# AST-First Formula Storage

## Key Clarifications

1. **AST is the ONLY source of truth** - UI formula display issues will be resolved deterministically later; skip E2E tests failing due to UI display
2. **No collab/offline distinction** - Always apply CRDT operations to update the workbook (remove `isCollaborating()` checks)
3. **Build commands**: Use `make wasm-dist` to build, `make wasm-serve` to test in browser
4. **Focus on core contract**, not E2E tests in early phases
5. **Consider storing formulas in Operations in AST form** (future optimization)

## Overview

Refactor formula storage to use AST representation as the source of truth instead of raw formula strings. This improves robustness, performance, and provides automatic normalization.

### Current State

Formulas are currently stored in multiple forms:
1. **Raw string** (`formula->raw`) - Original user input (e.g., "= A1", "=a1")
2. **UUID string** (`formula->value`) - With cell IDs replacing A1 refs
3. **AST** (`formula->ast`) - Parsed tree representation

This leads to:
- Redundant storage and parsing
- No normalization ("=A1" vs "= A1" vs "=a1" stored differently)
- String manipulation for reference adjustment (fragile)
- Re-parsing when adjusting references (inefficient)

### Target State

Store only the AST, generate display strings on-demand:
1. **AST** (`formula->ast`) - Single source of truth
2. Display string generated from AST when needed (always normalized)
3. Reference adjustment by walking/modifying AST nodes directly

Benefits:
- **Normalization**: "=A1", "= A1", "=a1" all become same AST
- **Efficiency**: No re-parsing for reference adjustment
- **Robustness**: AST manipulation is type-safe vs string regex
- **Simplicity**: Single source of truth, less state to maintain

### Functions to Update

The following functions will need modification:

1. **`RefConverter::adjustFormulaReferences()`** (core/cells/ref_converter.cc)
   - Currently: Parses formula string, manipulates text, returns new string
   - Target: Accept AST, walk nodes, adjust CellRefNode/RangeRefNode, return new AST

2. **`Formula` struct** (core/cells/types.h or model.h)
   - Remove `raw` and `value` string fields
   - Keep only `ast` field

3. **`FormulaDisplayConverter`** (core/cells/formula_display.cc)
   - Already exists for AST → A1 string conversion
   - May need optimization for frequent calls

4. **`Sheet::setCellFormula()`** (core/cells/model.cc)
   - Currently stores both strings and AST
   - Target: Store only AST

5. **Fill range formula handling** (core/cells/fill_range.cc)
   - Currently gets formula display string, calls adjustFormulaReferences
   - Target: Clone AST, call adjustASTReferences directly

6. **CRDT formula operations** (core/cells/crdt.cc)
   - Serialization format may need update
   - Must remain backward compatible for sync

---

## Phase 1: AST Reference Adjustment ✅

Add AST-based reference adjustment alongside existing string-based approach.

- [x] 1a: Add `ASTNode::clone()` method to deep-copy AST nodes (already existed)
- [x] 1b: Add `adjustASTReferences(ASTNode*, colOffset, rowOffset)` function
- [x] 1c: Add unit tests for AST reference adjustment
- [x] 1d: Verify existing adjustFormulaReferences tests still pass

## Phase 2: Update Fill Operations to Use AST ✅

Modify fill_range.cc to use AST-based adjustment.

- [x] 2a: Store AST pointers in DetectedPattern instead of formula strings
- [x] 2b: Use adjustASTReferences + FormulaDisplayConverter in fill
- [x] 2c: Remove direct mutation path - always use CRDT operations (no isCollaborating check)
- [x] 2d: Remove any remaining string-based formula paths in fill_range.cc
- [x] 2e: Build with `make wasm-dist` and verify unit tests pass

## Phase 3: Simplify Formula Storage ✅

Remove redundant string storage from Formula struct.

Note: The plan originally mentioned `raw` and `value` fields, but the actual codebase
has only `text` (UUID-format formula string). The goal remains: make AST the sole source
of truth and generate text on demand via `FormulaSerializer::serialize(ast)`.

### Audit Results (3a)

Places using `formula->text`:
1. **model.cc:858** - `setCellFormula()` creates Formula with text from `FormulaSerializer::serialize(ast)` ✅ already uses AST
2. **model.cc:897** - `setCellFormulaUnresolved()` stores original text (needed for unresolved formulas)
3. **model.cc:920-924** - `getCellFormulaText()` returns `formula->text`
4. **model.cc:390** - Clone master formula using `formula->text`
5. **crdt.cc:1375-1378** - Serializing formula to oplog uses `formula->text`
6. **crdt.cc:292** - Creating formula from CRDT operation
7. **serializer.cc:204-205** - Serializing formula to .cells file uses `formula->text`
8. **xlsx_writer.cc:349,363** - Writing formula to XLSX uses `formula->text`
9. **xlsx_reader.cc:451,476** - Reading formula from XLSX creates Formula with text
10. **parser.cc:604** - Parsing .cells file creates Formula with text

Decision: Keep `text` field for now since it's generated from AST anyway.
The key win was achieved in Phase 1-2: AST-based reference adjustment.

**Important insight**: If we eventually remove `text`, we need ErrorNode to preserve the
original unparsable text. Currently ErrorNode stores partialChildren and a message, but
NOT the raw text that couldn't be parsed. This would cause data loss for invalid formulas.

Future improvement (not in scope for this phase):
- Add `rawText` field to ErrorNode to store the unparsable portion
- This enables full round-trip: even invalid formulas can be reconstructed from AST

- [x] 3a: Audit all usages of `formula->text` (see above)
- [x] 3b: Serialization already generates strings from AST in `setCellFormula()` ✅
- [x] 3c: Keep `text` field (needed for error cases where AST parsing fails)
- [x] 3d: CRDT operations already use the AST-generated text ✅
- [x] 3e: Verify all formula tests pass (43 tests pass)

## Phase 4: Deprecate String-Based adjustFormulaReferences ✅

- [x] 4a: clipboard.ts uses its own TS implementation (appropriate for browser clipboard API input)
- [x] 4b: Removed `RefConverter::adjustFormulaReferences(string)` and `adjustSingleRef` from C++
- [x] 4c: Final test pass to ensure no regressions

---

## Technical Details

### AST Node Clone

Need deep clone for modifying copies without affecting original:

```cpp
// In formula_ast.h
class ASTNode {
public:
    virtual std::unique_ptr<ASTNode> clone() const = 0;
    // ...
};

class CellRefNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> clone() const override {
        auto copy = std::make_unique<CellRefNode>();
        copy->column = column;
        copy->row = row;
        copy->colAbsolute = colAbsolute;
        copy->rowAbsolute = rowAbsolute;
        copy->cellId = cellId;
        return copy;
    }
};
```

### AST Reference Adjustment

```cpp
// In ref_converter.h or new file
std::unique_ptr<ASTNode> adjustASTReferences(
    const ASTNode* ast,
    int colOffset,
    int rowOffset);

// Implementation walks tree, clones nodes, adjusts CellRefNode/RangeRefNode
```

### Backward Compatibility

The .cells file format and CRDT sync protocol may need consideration:
- Current format stores formula strings
- During transition, generate string from AST for serialization
- Future format could store AST directly (more compact)

---

## Testing Strategy

1. **Unit tests**: Test AST clone and adjustment functions
2. **Round-trip tests**: Parse → Adjust → Display should produce expected output
3. **E2E tests**: Existing formula fill tests should continue passing
4. **Regression tests**: All existing formula tests must pass

---

## Dependencies

- This plan should be executed after the current grid-selection-fill-features plan
- No external dependencies
