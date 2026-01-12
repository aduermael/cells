Status: DONE
Created At: 2026-01-12 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build WASM | `bazel run :wasm` |
| Unit tests | `bazel run :test` |
| E2E tests | `bazel run :e2e` |
| All checks | `bazel run :check` |

---

# Fix #REF! Errors on XLSX Import - Named Reference Dependency Tracking

## Problem

When importing `testdata/xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx`, some cells show #REF! errors. However, if you manually trigger a recalculation (e.g., edit cell K8), the formula evaluates correctly.

## Root Cause Analysis

The dependency graph (`dependency_graph.cc`) **does not track named reference dependencies**. At line 132-134:

```cpp
default:
    // Literals, named refs (need resolution first), etc.
    break;
```

The `ReferenceExtractor` explicitly skips `NAMED_REF` nodes. This means:

1. Formulas like `=LTM_EBITDA * 2` don't register any dependencies in the graph
2. During import, the topological sort for recalculation order is incomplete
3. Cells referencing named ranges may be evaluated before their dependencies
4. Manual recalculation works because it uses **demand-driven evaluation** - dependencies are resolved recursively at evaluation time

## Solution

Track named references in the dependency graph by resolving them to their underlying cell/range targets. This requires:

1. Access to the `NamedRangeRegistry` during dependency extraction
2. Resolving named references to their target cells/ranges
3. Adding those resolved references to the dependency graph
4. Handling potential circular references through named ranges

---

## Phase 1: Extend Dependency Graph API

Update the dependency graph to accept a `NamedRangeRegistry` for resolving named references.

- [x] 1a: Add `NamedRangeRegistry*` parameter to `ReferenceExtractor::extract()` (optional, nullable for backwards compat)
- [x] 1b: Add `NamedRangeRegistry*` and `sheetId` parameters to `addFormula()` overload
- [x] 1c: Update `addFormula()` callers to pass registry when available

**Files:** `core/cells/dependency_graph.h`, `core/cells/dependency_graph.cc`, `apps/wasm/bindings_file.cc`, `core/cells/BUILD`

---

## Phase 2: Implement Named Reference Resolution

Extract dependencies from named references by resolving them to their target cells/ranges.

- [x] 2a: Add `NAMED_REF` case to `ReferenceExtractor::extract()` that resolves the named reference
- [x] 2b: Convert resolved target (CELL, RANGE, COLUMN, ROW, etc.) to appropriate `DependencyRef` entries
- [x] 2c: Handle sheet-scoped vs workbook-scoped named ranges correctly (via `resolve()` which already handles scope)
- [x] 2d: Handle recursive named references - N/A, current architecture stores resolved cell IDs not formulas
- [x] 2e: Add recursion depth limit to prevent infinite loops from circular named references (`kMaxNamedRefDepth = 32`)

**Files:** `core/cells/dependency_graph.cc`

---

## Phase 3: Update Import Workflow

Ensure the import path provides the named range registry to the dependency graph.

- [x] 3a: Update `bindings_file.cc` to pass `NamedRangeRegistry*` when building dependency graph during import
- [x] 3b: Ensure named ranges are fully loaded before dependency graph is built (XLSX reader loads them first)

**Files:** `apps/wasm/bindings_file.cc`

---

## Phase 4: Unit Tests

Add comprehensive tests for named reference dependency tracking.

- [x] 4a: Test basic named reference dependency extraction (CELL target) - `NamedRefCell` test
- [x] 4b: Test range named reference dependency extraction (RANGE target) - `NamedRefRange` test
- [x] 4c: Test column/row named reference dependency extraction - `NamedRefColumn`, `NamedRefRow`, `NamedRefColumnRange`, `NamedRefRowRange` tests
- [x] 4d: Test sheet-scoped vs workbook-scoped resolution - `NamedRefSheetScoped`, `NamedRefWorkbookScopedFallback` tests
- [x] 4e: Test recursive named references - N/A (architecture stores resolved cell IDs, not formulas pointing to other named ranges)
- [x] 4f: Test circular named reference protection - N/A (not possible with current architecture; depth limit exists as defensive measure)

Also added: `NamedRefUnresolved`, `NamedRefWithoutRegistry`, `NamedRefMixedWithCellRef`, `NamedRefSourcePositionPreserved`, `NamedRefMultipleInFormula` tests

**Files:** `core/cells/dependency_graph_test.cc`

---

## Phase 5: Integration Test

Verify the specific bug is fixed.

- [x] 5a: Add e2e test that imports `init_lbo_model_60min_is_revenue_cf_only.xlsx` and verifies no #REF! errors
- [x] 5b: Verify cells like K8 evaluate correctly on initial load (no manual recalc needed)

**Fix:** Added formula resolution step to XLSX import in `bindings_file.cc`. XLSX formulas are parsed with A1 notation but weren't resolved to UUIDs before evaluation, causing `#REF!` errors. Now `FormulaResolver::resolve()` is called for each formula after import.

**Files:** `apps/wasm/bindings_file.cc`, `apps/wasm/tests/named-ref-import.test.mjs`, `apps/wasm/tests/run-parallel.mjs`

---

## Key Files

| File | Purpose |
|------|---------|
| `core/cells/dependency_graph.h` | API changes for named reference support |
| `core/cells/dependency_graph.cc` | Named reference resolution in ReferenceExtractor |
| `apps/wasm/bindings_file.cc` | Pass registry during XLSX import |
| `core/cells/dependency_graph_test.cc` | Unit tests |

---

## Notes

- This is a real architectural fix, not a patch
- The fix ensures correct recalculation order for all formulas, including those using named references
- Backwards compatible: `NamedRangeRegistry*` is nullable, existing code continues to work
- The demand-driven evaluation (manual recalc) continues to work as a fallback
