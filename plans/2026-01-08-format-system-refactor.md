Status: IN_PROGRESS
Created At: 2026-01-08 01:09 UTC
Updated At: 2026-01-08 02:16 UTC
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

## Phase 4: Unify Built-in and Dynamic Formats

Make built-in formats use the dynamic system under the hood.

- [ ] 4a: Remove hardcoded built-in format constants (FMT_GEN0, etc.) from number_format.cc
- [ ] 4b: Add `initializeBuiltinFormats()` that registers built-ins via dynamic system
- [ ] 4c: Add caching to `getFormat()` - first request generates, subsequent requests return cached
- [ ] 4d: Update tests to work with new unified system

**Details:**
```cpp
// Before: hardcoded
const ID GENERAL("FMT_GEN0");
const ID NUMBER_0("FMT_N000");

// After: registered at startup via dynamic system
void NumberFormatRegistry::initializeBuiltinFormats() {
    // These just ensure the common IDs are pre-cached
    getOrCreateFormat("FMT_GEN0");  // GENERAL
    getOrCreateFormat("FMT_N000");  // NUMBER 0 decimals
    getOrCreateFormat("FMT_N002");  // NUMBER 2 decimals
    // etc.
}
```

---

## Phase 5: Remove Format Logic from TypeScript

Remove all format parsing/generation from TypeScript, replace with C++ API calls.

- [ ] 5a: Add C++ API `getFormatDetails(formatId)` → returns JSON with category, decimals, etc.
- [ ] 5b: Add C++ API `generateFormatId(category, decimals, options)` → returns format ID
- [ ] 5c: Remove `parseCurrentFormat()` from format-controls.ts, call C++ instead
- [ ] 5d: Remove `generateFormatId()` from format-controls.ts, call C++ instead
- [ ] 5e: Remove `availableFormats` caching, fetch fresh each time (or invalidate on format change)

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

## Phase 6: NUMBER Format Default = 2 Decimals

Change the default NUMBER format (when user selects "Number" category) to show 2 decimal places.

- [ ] 6a: Change default NUMBER format ID from `FMT_N000` to `FMT_N002` in format selection UI
- [ ] 6b: Update any tests that expect 0-decimal NUMBER as default

**Details:**
- GENERAL stays unchanged (full precision like Excel)
- NUMBER category default changes: `1.5` → `"1.50"` (was `"2"`)
- This matches Excel's Number format behavior

---

## Summary of Changes

1. `FMT_NS0X` pattern replaced with `FMT_NSXX` (no backwards compat needed - product not live)
2. NUMBER format default changes from 0 to 2 decimals (matches Excel)
3. Formula bar shows raw value instead of formatted display
4. All format logic moves to C++ (TypeScript becomes pure UI)
