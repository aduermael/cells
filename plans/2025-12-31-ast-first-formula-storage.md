Status: READY
Created At: 2025-12-31 18:06 UTC
Updated At: 2025-12-31 18:06 UTC
Following plan management guidelines defined in AGENTS.md

# AST-First Formula Storage

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

## Phase 1: AST Reference Adjustment

Add AST-based reference adjustment alongside existing string-based approach.

- [ ] 1a: Add `ASTNode::clone()` method to deep-copy AST nodes
- [ ] 1b: Add `adjustASTReferences(ASTNode*, colOffset, rowOffset)` function
- [ ] 1c: Add unit tests for AST reference adjustment
- [ ] 1d: Verify existing adjustFormulaReferences tests still pass

## Phase 2: Update Fill Operations to Use AST

Modify fill_range.cc to use AST-based adjustment.

- [ ] 2a: Store AST pointers in DetectedPattern instead of formula strings
- [ ] 2b: Use adjustASTReferences + FormulaDisplayConverter in fill
- [ ] 2c: Remove string-based path in fill_range.cc
- [ ] 2d: Verify fill E2E tests pass

## Phase 3: Simplify Formula Storage

Remove redundant string storage from Formula struct.

- [ ] 3a: Audit all usages of `formula->raw` and `formula->value`
- [ ] 3b: Update serialization to generate strings from AST
- [ ] 3c: Remove `raw` and `value` fields from Formula struct
- [ ] 3d: Update CRDT operations to use AST
- [ ] 3e: Verify all formula tests pass

## Phase 4: Deprecate String-Based adjustFormulaReferences

- [ ] 4a: Update any remaining callers to use AST version
- [ ] 4b: Remove `RefConverter::adjustFormulaReferences(string)`
- [ ] 4c: Final test pass to ensure no regressions

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
