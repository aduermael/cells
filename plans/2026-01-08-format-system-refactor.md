Status: COMPLETED
Created At: 2026-01-08 01:09 UTC
Updated At: 2026-01-08 03:02 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

---

## Problem Statement

The current format system has several architectural issues:

1. **TypeScript caches format data** - `format-controls.ts` parses/generates format IDs independently, duplicating C++ logic
2. **Formula bar shows formatted values** - Should show raw values (1.654987654 not 1.65)
3. **Built-in vs Dynamic formats are separate paths** - Should unify: Built-in uses Dynamic under the hood
4. **Inconsistent decimal support** - `FMT_NS0X` only supports 0-9 decimals, others support 0-15
5. **Default Number format shows no decimals** - Should show 2 decimals by default

## Architecture Changes

### Current Flow (problematic):
```
Cell.formatId → [C++ Registry OR TS parseCurrentFormat()] → display
                     ↑ duplicated logic
```

### Target Flow:
```
Cell.formatId → C++ Registry (single source of truth) → display

UI queries C++ for:
- Available formats list
- Current cell's format details
- Format previews
```

### Key Decisions:
- **Source of truth**: C++ `NumberFormatRegistry` only
- **TypeScript role**: Display UI, call C++ APIs, no format logic
- **display field**: Computed in C++ viewport query, never cached
- **Raw value**: Always stored, always shown in formula bar & in-cell editing
- **Dynamic format caching**: First request generates and caches in registry

---

## Phase 1: Formula Bar Shows Raw Value ✅

Fix the formula bar to show raw values instead of formatted display.

- [x] 1a: Update `getFormulaBarValue()` in init.ts to return `cell.value` not `cell.display`
- [x] 1b: Add E2E test verifying formula bar shows raw value for formatted cell

**Details for 1a:**
```typescript
// Before (wrong):
if (cell.display) return cell.display;
return cell.value || "";

// After (correct):
// Formula cells: show formula
if (cell.formula) return cell.formula;
// All other cells: show raw value
return cell.value || "";
```

---

## Phase 2: In-Cell Editing Shows Raw Value ✅

Ensure double-clicking a formatted cell shows the raw value for editing.

- [x] 2a: Verify cell-editor.ts uses raw value when starting edit (confirmed working)
- [x] 2b: Add E2E test verifying in-cell editing shows raw value

**Details:**
When user double-clicks a cell showing "$1,234.57", the editor should display "1234.567" (the raw value), not the formatted string.

---

## Phase 3: Unify Dynamic Format ID Patterns ✅

Change all dynamic format patterns to support 00-15 decimals consistently.

- [x] 3a: Update `FMT_NS` pattern to `FMT_NSXX` (2-digit decimals) in C++ number_format.cc
- [x] 3b: Update `parseFormatId()` in number_format.cc to handle new pattern
- [x] 3c: Update `generateFormatCode()` to generate codes for new pattern
- [x] 3d: Add/update unit tests for new pattern
- [x] 3e: Remove old `FMT_NS0X` pattern handling (breaking change, but no external deps)

**New unified patterns:**
| Pattern | Example | Description |
|---------|---------|-------------|
| `FMT_P0XX` | `FMT_P007` | Percentage, XX decimals (00-15) |
| `FMT_N0XX` | `FMT_N012` | Number, XX decimals (00-15) |
| `FMT_NSXX` | `FMT_NS12` | Number+separator, XX decimals (00-15) |
| `CXXX_0YY` | `CUSD_015` | Currency XXX, YY decimals (00-15) |

---

## Phase 4: Unify Built-in and Dynamic Formats ✅

Make built-in formats use the dynamic system under the hood.

- [x] 4a: Add `getOrCreateFormat()` method to NumberFormatRegistry
- [x] 4b: Update `initBuiltInFormats()` to use `getOrCreateFormat()` for parseable formats
- [x] 4c: Update `formatNumber()` to use `getOrCreateFormat()` (caching on first use)
- [x] 4d: Add tests for getOrCreateFormat and verify built-in formats use dynamic system

**Implementation:**
- Added `getOrCreateFormat(const ID& id)` that parses dynamic format IDs and caches them
- `initBuiltInFormats()` now calls `getOrCreateFormat()` for NUMBER, NUMBER_SEP, PERCENTAGE, and CURRENCY formats
- Non-parseable formats (GENERAL, ACCOUNTING, DATE, TIME, DATETIME, SCIENTIFIC, TEXT) remain hardcoded
- Legacy currency formats (FMT_C0XX) remain hardcoded (don't follow CXXX_0YY pattern)
- `formatNumber()` now uses `getOrCreateFormat()` instead of parsing on every call

---

## Phase 5: Remove Format Logic from TypeScript ✅

Remove all format parsing/generation from TypeScript, replace with C++ API calls.

- [x] 5a: Add C++ API `getFormatDetails(formatId)` → returns JSON with category, decimals, etc.
- [x] 5b: Add C++ API `makeFormatId(category, decimals, separator, currency)` → returns format ID
- [x] 5c: Remove `parseCurrentFormat()` from format-controls.ts, call C++ instead
- [x] 5d: Remove `generateFormatId()` from format-controls.ts, call C++ instead
- [x] 5e: Remove `availableFormats` caching, query C++ directly via getFormatDetails()

**New C++ APIs:**
```cpp
// Get details about a format ID
std::string getFormatDetails(const std::string& formatId);
// Returns: {"category":"NUMBER","decimals":2,"separator":true,"currency":null}

// Generate a format ID for given parameters
std::string makeFormatId(const std::string& category, int decimals,
                         bool separator, const std::string& currency);
// Returns: "FMT_NS02" or "CUSD_002" etc.
```

---

## Phase 6: NUMBER Format Default = 2 Decimals ✅

Change the default NUMBER format (when user selects "Number" category) to show 2 decimal places.

- [x] 6a: Change default NUMBER format ID from `FMT_N000` to `FMT_N002` in format selection UI
- [x] 6b: Update any tests that expect 0-decimal NUMBER as default

**Details:**
- GENERAL stays unchanged (full precision like Excel)
- NUMBER category default changes: `1.5` → `"1.50"` (was `"2"`)
- This matches Excel's Number format behavior

---

## Phase 7: Formula Format Inheritance

When a formula is entered into a cell with GENERAL format, Excel automatically inherits the format from referenced cells. This is a one-time automatic change that happens only when the destination cell has GENERAL format.

**Excel's behavior (from research):**
- Only applies when destination cell has GENERAL format
- Once changed from GENERAL to something else, it won't change again
- If multiple cells with different formats are referenced, Excel uses priority rules
- Numeric literals (like `*2`, `+100`) don't affect format inheritance - only cell references matter
- Format inheritance happens at formula entry time, not on recalculation

**Priority rules (when multiple formats conflict):**
1. DATE/TIME formats have highest priority (dates are special)
2. CURRENCY > PERCENTAGE > NUMBER_SEP > NUMBER
3. More specific formats win over less specific (e.g., 4 decimals > 2 decimals)
4. If all referenced cells have GENERAL, result stays GENERAL

**Implementation approach (all in C++):**

- [x] 7a: Add `inferFormatFromFormula(ast, formatLookup)` function in C++ (`core/cells/number_format.cc`)
  - Walk the AST to find all cell references
  - Look up each referenced cell's formatId via a lookup callback
  - Apply priority rules to determine "winning" format
  - Return the format ID (or empty string for GENERAL/no inheritance)

- [x] 7b: Integrate format inference into CRDT cell operations (`core/cells/crdt.cc`)
  - When `applyCellSetValue()` processes a formula (type == FORMULA)
  - AND the cell's current formatId is null/GENERAL
  - Call `inferFormatFromFormula()` with sheet-based format lookup
  - Apply the inherited format ID to the cell
  - This happens automatically at formula entry time, transparent to UI

- [x] 7c: Add unit tests for format inheritance (26 tests in number_format_test.cc)
  - Format priority ordering tests (6 tests)
  - Single cell reference inheritance (currency, percentage)
  - GENERAL/empty/~ formats are not inherited
  - Binary operations with same/different formats
  - Currency wins over percentage regardless of order
  - More decimals wins in same category
  - Separator beats no separator at same decimals
  - Literals don't affect inheritance
  - Function calls inherit from args
  - Range references inherit from corners
  - Unary operations inherit from operand
  - Nested formulas use cell's format (not underlying formula's)

- [x] 7d: Add E2E tests for format inheritance (6 tests in format.test.mjs)
  - Formula inherits currency format from referenced cell (=A1*2 → $200.00)
  - Formula inherits percentage format from referenced cell (=A1+0.1 → 25%)
  - Explicit format on cell is not overridden by formula inheritance
  - Currency format wins over percentage in multi-ref formula
  - Formula with no formatted references stays General
  - SUM function inherits format from range

**Example scenarios:**

| A1 Format | B1 Format | Formula in C1 | C1 Result Format |
|-----------|-----------|---------------|------------------|
| Currency  | GENERAL   | `=A1*2`       | Currency         |
| GENERAL   | Currency  | `=A1+B1`      | Currency         |
| Currency  | Percent   | `=A1+B1`      | Currency (higher priority) |
| NUMBER(2) | NUMBER(4) | `=A1+B1`      | NUMBER(4) (more specific) |
| Date      | Currency  | `=A1+B1`      | Date (highest priority) |
| GENERAL   | GENERAL   | `=A1+B1`      | GENERAL          |

**Important notes:**
- **All logic in C++** - Format inference happens in the core engine, not UI layer (consistent with Phase 5)
- This only affects cells with GENERAL format - explicit user formatting is never overridden
- Format is inherited at formula entry time, not on recalc (matches Excel)
- If referenced cells later change format, the formula cell's format does NOT update
- This is purely a UX convenience feature - users can always manually change format
- UI (TypeScript) simply calls `setCellValue()` - the C++ layer handles format inference automatically

---

## Summary of Changes

1. `FMT_NS0X` pattern replaced with `FMT_NSXX` (no backwards compat needed - product not live)
2. NUMBER format default changes from 0 to 2 decimals (matches Excel)
3. Formula bar shows raw value instead of formatted display
4. All format logic moves to C++ (TypeScript becomes pure UI)
5. Formula cells with GENERAL format inherit format from referenced cells (Phase 7)
