Status: COMPLETED
Created At: 2026-01-03 05:26 UTC
Updated At: 2026-01-03 17:59 UTC
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
| TypeScript check | `make check-types` |

---

## Overview

This plan implements five interconnected features to improve the formula editing and cell formatting experience:

1. **Range text highlighting** - Color ranges (A1:B5) in formula bar text
2. **Hover interaction** - Bidirectional hover highlighting between formula bar and grid
3. **Number formats** - Full Excel-style formatting (%, $, date, etc.)
4. **Formula bar UI** - Taller bar with format controls
5. **Formula autocomplete** - Function dropdown when editing formulas

---

## Phase 1: Range Text Highlighting Fix ✅ COMPLETED

**Goal:** Ensure range references (A1:B5) are colored in the formula bar text.

**Root cause found:** The `RangeRefNode` constructor in the parser was being called without a `SourcePosition` argument, leaving the position field unset (defaulting to {0, 0}). This caused the colorizer to skip range highlights since `sourceEnd <= sourceStart`.

### Files modified:
- `core/cells/formula_parser.cc` - Fixed 5 locations where `RangeRefNode` was created without position
- `core/cells/formula_parser_test.cc` - Added unit tests for range source positions
- `apps/wasm/tests/formula.test.mjs` - Added E2E tests for range highlighting

### Tasks:
- [x] 1a: Debug range highlighting - identified parser wasn't setting sourcePosition
- [x] 1b: Fix the root cause - pass computed `SourcePosition{start, end}` to `RangeRefNode` constructor
- [x] 1c: Add E2E test for range highlighting verification

**Build:** Use `make wasm-dist` to rebuild WASM (not direct bazel commands).

---

## Phase 2: Formula ↔ Grid Hover Interaction ✅ COMPLETED

**Goal:** Hovering a cell ref in the formula bar emphasizes the grid frame; hovering grid frame emphasizes formula text.

### Files to modify:
- `apps/wasm/src/formula-colorizer.ts` - Add `data-ref-index` to spans
- `apps/wasm/src/init.ts` - Hover event handlers, state tracking
- `apps/wasm/src/grid-formula-renderer.ts` - Draw hovered highlight with emphasis
- `apps/wasm/src/app-events.ts` - Grid hover detection
- `apps/wasm/static/shared/styles.css` - Hover styles

### Tasks:
- [x] 2a: Add `data-ref-index` attribute to formula-ref spans in colorizeFormula()
- [x] 2b: Track hovered reference state in app.ts
- [x] 2c: Add mouseenter/mouseleave handlers to formula display for refs
- [x] 2d: Emphasize grid highlight when formula ref is hovered (thicker border)
- [x] 2e: Detect grid hover over formula highlights
- [x] 2f: Emphasize formula bar text when grid highlight is hovered
- [x] 2g: Add CSS transitions for smooth hover effects

---

## Phase 3: Number Format Data Model (C++)

**Goal:** Add format storage and CRDT operations for cell formatting.

### Files to create:
- `core/cells/number_format.h` - NumberFormat struct, NumberFormatCategory enum
- `core/cells/number_format.cc` - Format registry with built-in formats

### Files to modify:
- `core/cells/model.h` - Add `formatId` to Cell struct
- `core/cells/operation.h` - Add CELL_SET_FORMAT operation type
- `core/cells/operation.cc` - Implement format operation
- `core/cells/serializer.cc` - Serialize T section (formats) and formatId on cells
- `core/cells/parser.cc` - Parse T section

### Data structures:
```cpp
enum class NumberFormatCategory : uint8_t {
    GENERAL, NUMBER, CURRENCY, ACCOUNTING, PERCENTAGE,
    DATE, TIME, DATE_TIME, SCIENTIFIC, FRACTION, TEXT
};

struct NumberFormat {
    ID id;
    NumberFormatCategory category;
    std::string formatCode;  // e.g., "#,##0.00"
    uint8_t decimalPlaces;
    bool useThousandsSeparator;
    std::string currencySymbol;
    bool isAccounting;
};
```

### Tasks:
- [x] 3a: Create NumberFormat struct and NumberFormatCategory enum
- [x] 3b: Create NumberFormatRegistry with built-in formats (General, Number, %, $, Date, etc.)
- [x] 3c: Add formatId field to Cell struct in model.h
- [x] 3d: Add CELL_SET_FORMAT operation type and implementation
- [x] 3e: Add T section serialization for formats (fmt:<id> property on cells)
- [x] 3f: Add T section parsing (fmt:<id> property on cells)
- [x] 3g: Add unit tests for format operations and serialization

---

## Phase 4: Input Parsing & Number Formatting (C++) ✅ COMPLETED

**Goal:** Auto-detect input formats (15% → 0.15) and format numbers for display.

### Files to create:
- `core/cells/input_parser.h` - ParsedInput struct, parseUserInput()
- `core/cells/input_parser.cc` - Auto-detection for %, $, dates, times
- `core/cells/number_formatter.h` - formatNumber() function
- `core/cells/number_formatter.cc` - Format numbers per NumberFormat

### Auto-detection rules:
- `15%` or `15 %` → value=0.15, format=PERCENTAGE
- `$1,234.56` → value=1234.56, format=CURRENCY
- `1/15/2024` → value=serial_date, format=DATE
- `12:30 PM` → value=0.520833..., format=TIME
- `1.5E+10` → value=15000000000, format=SCIENTIFIC

### Tasks:
- [x] 4a: Create input_parser.h with ParsedInput struct
- [x] 4b: Implement percentage parsing (15% → 0.15)
- [x] 4c: Implement currency parsing ($1,234.56)
- [x] 4d: Implement date parsing (multiple formats: MM/DD/YYYY, YYYY-MM-DD, etc.)
- [x] 4e: Implement time parsing (12:30 PM, 14:30)
- [x] 4f: Implement scientific notation detection
- [x] 4g: Create number_formatter.h
- [x] 4h: Implement number formatting (decimal places, thousands separator)
- [x] 4i: Implement percentage display (0.15 → "15%")
- [x] 4j: Implement currency display ($1,234.56)
- [x] 4k: Implement date/time display with locale awareness
- [x] 4l: Add unit tests for input parsing
- [x] 4m: Add unit tests for number formatting

---

## Phase 5: WASM API & TypeScript Integration ✅ COMPLETED

**Goal:** Expose format APIs to TypeScript and wire up cell value flow.

### Files modified:
- `apps/wasm/bindings.cc` - Added format-related WASM exports
- `apps/wasm/src/client.ts` - Added format client methods
- `apps/wasm/src/types.ts` - Added NumberFormat TypeScript types
- `apps/wasm/src/worker.ts` - Added worker handlers for format operations
- `apps/wasm/src/wasm-data-source.ts` - Added WasmDataSource wrapper methods
- `apps/wasm/src/cell-editor.ts` - Integrated format detection on cell edit commit

### WASM API additions:
```cpp
// bindings.cc - Format operations
setCellFormat(cellId, formatId)
setCellFormatAt(col, row, formatId)
getAvailableFormats()
getCellFormatId(cellId)
parseUserInputValue(input)
formatCellValue(value, formatId)
formatCellById(cellId)
updateCellWithFormatDetection(cellId, value)
```

### Tasks:
- [x] 5a: Add WASM bindings for setCellFormat, getAvailableFormats
- [x] 5b: Add WASM bindings for parseUserInput, formatCellValue
- [x] 5c: Add TypeScript types for NumberFormat
- [x] 5d: Add client.ts methods for format operations
- [x] 5e: Integrate input parsing on cell edit commit (auto-detect format)
- [x] 5f: Update cell display to use formatted value from C++
- [x] 5g: Add E2E tests for format auto-detection

---

## Phase 6: Formula Bar UI Redesign ✅ COMPLETED

**Goal:** Taller formula bar with format controls (dropdown, accounting toggle, decimal +/-).

### Files created:
- `apps/wasm/src/format-controls.ts` - FormatControls class

### Files modified:
- `apps/wasm/static/index.html` - Added format controls HTML structure
- `apps/wasm/static/shared/styles.css` - Added format controls styling
- `apps/wasm/src/app.ts` - Added format control DOM element references
- `apps/wasm/src/init.ts` - Created and wired FormatControls instance

### UI layout:
```
+------------------------------------------------------------------+
| A1  | [Format v] [$] [%] | [+.0] [-.0] |  =SUM(A1:A10)        [<>]|
+------------------------------------------------------------------+
```

### Tasks:
- [x] 6a: Add format-controls div with dropdown, currency, percent, decimal buttons to HTML
- [x] 6b: Style format controls (dropdown menu, buttons)
- [x] 6c: Create FormatControls class in format-controls.ts
- [x] 6d: Wire FormatControls to cell selection (update dropdown on select)
- [x] 6e: Implement format change handler (apply format to selection)
- [x] 6f: Implement decimal +/- handlers
- [x] 6g: Show/hide accounting toggle based on currency selection (simplified - no toggle, just Currency button)
- [x] 6h: Add keyboard shortcuts for format controls (Ctrl/Cmd+Shift+1-6)

---

## Phase 7: Formula Function Autocomplete ✅ COMPLETED

**Goal:** Show function dropdown when editing formulas (typing 'S' shows SUM, SIN, etc.).

### Files modified:
- `core/cells/formula_functions.h` - Added FunctionInfo struct, getFunctionList()
- `core/cells/formula_functions.cc` - Implemented getFunctionList() with name/signature/description
- `core/cells/functions/fn_math.cc` - Added metadata to all math functions
- `core/cells/functions/fn_logic.cc` - Added metadata to all logic functions
- `core/cells/functions/fn_text.cc` - Added metadata to all text functions
- `core/cells/functions/fn_datetime.cc` - Added metadata to all date/time functions
- `core/cells/functions/fn_stats.cc` - Added metadata to all statistics functions
- `core/cells/functions/fn_lookup.cc` - Added metadata to all lookup functions
- `core/cells/functions/fn_rand.cc` - Added metadata to random functions
- `apps/wasm/bindings.cc` - Exposed getFormulaFunctions()
- `apps/wasm/src/header-editor.ts` - Integrated autocomplete
- `apps/wasm/src/types.ts` - Added FunctionInfo TypeScript type
- `apps/wasm/src/client.ts` - Added getFormulaFunctions() method
- `apps/wasm/src/worker.ts` - Added worker handler
- `apps/wasm/src/wasm-data-source.ts` - Added wrapper method
- `apps/wasm/src/init.ts` - Wired FormulaBarContainer
- `apps/wasm/static/shared/styles.css` - Added .autocomplete-signature style

### Files created:
- `apps/wasm/src/formula-autocomplete.ts` - FormulaAutocomplete class

### FunctionInfo structure:
```cpp
struct FunctionInfo {
    std::string name;        // "SUM"
    std::string signature;   // "(number1, [number2], ...)"
    std::string description; // "Adds all numbers in a range"
    std::string category;    // "Math"
};
```

### Trigger conditions:
- After `=` at formula start
- After `(` or `,` in arguments
- While typing letters (filter by prefix)

### Tasks:
- [x] 7a: Add FunctionInfo struct to formula_functions.h
- [x] 7b: Implement getFunctionList() with all registered functions + metadata
- [x] 7c: Add WASM binding for getFormulaFunctions()
- [x] 7d: Add client.ts method getFormulaFunctions()
- [x] 7e: Create FormulaAutocomplete class (popup, filtering, keyboard nav)
- [x] 7f: Integrate autocomplete into header-editor.ts
- [x] 7g: Add trigger detection (=, (, , and letter typing)
- [x] 7h: Style autocomplete popup (reuse existing .autocomplete-* classes)

---

## Critical Files Summary

### C++ Core:
- `core/cells/model.h` - Cell formatId
- `core/cells/number_format.h/cc` (new) - Format types & registry
- `core/cells/input_parser.h/cc` (new) - Auto-detection
- `core/cells/number_formatter.h/cc` (new) - Display formatting
- `core/cells/formula_functions.h/cc` - FunctionInfo, getFunctionList()
- `core/cells/operation.h/cc` - CELL_SET_FORMAT
- `core/cells/serializer.cc` - T section
- `apps/wasm/bindings.cc` - WASM exports

### TypeScript:
- `apps/wasm/src/init.ts` - Wiring, hover state
- `apps/wasm/src/formula-colorizer.ts` - Range highlighting, data attributes
- `apps/wasm/src/grid-formula-renderer.ts` - Hover emphasis
- `apps/wasm/src/header-editor.ts` - Formula autocomplete integration
- `apps/wasm/src/format-controls.ts` (new) - UI controls
- `apps/wasm/src/formula-autocomplete.ts` (new) - Function dropdown
- `apps/wasm/src/client.ts` - Format APIs

### HTML/CSS:
- `apps/wasm/static/index.html` - Formula bar structure
- `apps/wasm/static/shared/styles.css` - Format controls, hover styles
