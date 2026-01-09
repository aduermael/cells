Status: READY
Created At: 2026-01-08 23:14 UTC
Updated At: 2026-01-09 00:00 UTC
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

The formula bar and cell editor show raw underlying values instead of human-readable "edit values" like Excel does:

| Format | Cell Display | Excel Formula Bar | Current App Formula Bar |
|--------|-------------|-------------------|------------------------|
| Date | 12/12/2025 | 12/12/2025 | 46003 ❌ |
| Time | 3:30 PM | 3:30 PM | 0.645833 ❌ |
| Percentage | 15% | 15% | 0.15 ❌ |
| Currency | $1,000.56 | 1000.56 | 1000.56 ✅ |
| Number+Sep | 1,456,789 | 1456789 | 1456789 ✅ |

**Key insight**: Excel distinguishes between:
- **Display value**: Fully formatted for cell display (e.g., "$1,000.56")
- **Edit value**: Human-readable for editing (e.g., "1000.56" for currency, "15%" for percentage, "12/12/2025" for date)
- **Raw value**: The underlying stored number (e.g., 1000.56, 0.15, 46003)

## Architecture

All logic will be implemented in C++. TypeScript is UI-only.

```
Cell data structure:
- value: raw numeric/string value (stored)
- display: fully formatted string for cell rendering (computed)
- editValue: human-readable string for formula bar/editing (NEW, computed)
```

**Edit value rules by format category:**
| Category | Edit Value Format | Example |
|----------|------------------|---------|
| DATE | Regional date format | "12/12/2025" |
| TIME | Regional time format | "3:30 PM" |
| DATETIME | Date + Time | "12/12/2025 3:30 PM" |
| PERCENTAGE | Value×100 + "%" | "15%" |
| CURRENCY | Raw number | "1000.56" |
| NUMBER | Raw number | "1456789" |
| GENERAL | Raw number | "123.456" |
| TEXT | String value | "hello" |

---

## Phase 1: Add Edit Value Computation in C++

Add a new function to compute the "edit value" for a cell based on its format.

- [x] 1a: Add `formatEditValue()` function in `core/cells/number_formatter.h/.cc`
  - Takes: `double value`, `const ID& formatId`, `const FormatLocale& locale`
  - Returns: `std::string` - the edit representation
  - For DATE: call `formatDate()` with short format
  - For TIME: call `formatTime()` with 12h format
  - For DATETIME: combine date + time
  - For PERCENTAGE: `value * 100` + "%" with appropriate decimals
  - For others: return raw value as string (current behavior)

- [x] 1b: Add unit tests for `formatEditValue()` in `number_formatter_test.cc`
  - Test DATE returns "12/12/2025" not "46003"
  - Test TIME returns "3:30 PM" not "0.645833"
  - Test PERCENTAGE returns "15%" not "0.15"
  - Test CURRENCY returns "1000.56" not "$1,000.56"
  - Test NUMBER returns "1456789" not "1,456,789"

**Implementation details for 1a:**
```cpp
std::string formatEditValue(NumberFormatRegistry& registry, double value,
                            const ID& formatId, const FormatLocale& locale) {
    if (formatId.isNull()) {
        return formatGeneral(value, locale).text;  // Raw value
    }

    const NumberFormat* format = registry.getOrCreateFormat(formatId);
    if (!format) {
        return formatGeneral(value, locale).text;
    }

    switch (format->category) {
        case NumberFormatCategory::DATE:
            return formatDate(value, BuiltInFormats::DATE_SHORT, locale).text;
        case NumberFormatCategory::TIME:
            return formatTime(value, BuiltInFormats::TIME_12H, locale).text;
        case NumberFormatCategory::DATE_TIME:
            return formatDateTime(value, formatId, locale).text;
        case NumberFormatCategory::PERCENTAGE:
            // Show as "15%" not "0.15"
            return formatPercentage(value, format->decimalPlaces, locale).text;
        default:
            // Currency, Number, etc: return raw value
            return formatGeneral(value, locale).text;
    }
}
```

---

## Phase 2: Expose Edit Value via WASM API

Update the viewport query to include editValue alongside value and display.

- [x] 2a: Update `CellData` in viewport response to include `editValue` field in `bindings_viewport.cc`
  - In `queryViewport()`, compute and include `editValue` for each cell
  - Call new `formatEditValue()` function

- [x] 2b: Update TypeScript types in `apps/wasm/src/types.ts`
  - Add `editValue?: string` to `CellData` interface

- [x] 2c: Add E2E test verifying editValue is returned correctly in viewport

**Implementation for 2a (in queryViewport):**
```cpp
// After computing display value:
cellJson += ",\"editValue\":\"";
cellJson += escapeJsonString(formatEditValue(registry, numValue, formatId, locale));
cellJson += "\"";
```

---

## Phase 3: Use Edit Value in Formula Bar and Cell Editor

Update TypeScript to use editValue instead of raw value for display.

- [x] 3a: Update `getFormulaBarValue()` in `apps/wasm/src/init-components.ts`
  - Return `cell.editValue` instead of `cell.value` (when not a formula)
  - Formula cells still show the formula text

- [x] 3b: Update `cell-editor.ts` to use editValue when starting edit
  - When double-clicking to edit, show editValue not raw value
  - C++ `getOrCreateCellAt` updated to return editValue field

- [x] 3c: Updated E2E tests for formula bar showing correct edit values
  - Percentage cell: formula bar shows "15%" ✅
  - Percentage cell: in-cell editor shows "15%" ✅
  - Currency cell: formula bar shows raw value "1234.5" ✅ (by design)

---

## Phase 4: Enhance Date Parsing ✅

Excel accepts many date input formats. Enhance date parsing to match.

- [x] 4a: Add support for 2-digit years in `parseDate()` in `input_parser.cc`
  - "12/12/25" → 12/12/2025 (years 00-29 → 2000-2029, 30-99 → 1930-1999)
  - Update existing MM/DD/YYYY regex to also match MM/DD/YY

- [x] 4b: Add support for text month names
  - "Jan 15, 2025" or "January 15, 2025"
  - "15 Jan 2025" or "15-Jan-2025"

- [x] 4c: Add support for short date formats
  - "1/15" → January 15 of current year
  - "Jan 15" → January 15 of current year

- [x] 4d: Add unit tests for all new date parsing formats

**Note on date libraries:**
The current implementation is adequate for basic date handling. Howard Hinnant's date library (https://github.com/HowardHinnant/date) is excellent but adds external dependencies. The current approach using std::regex and manual calculation is sufficient and has no dependencies. We can revisit if timezone support is needed.

---

## Phase 5: Enhance Time Parsing ✅

Add support for more time input formats.

- [x] 5a: Support time without seconds: "9:30" → 9:30:00 (already implemented)
- [x] 5b: Support lowercase am/pm: "9:30am", "9:30 pm" (already implemented)
- [x] 5c: Support period notation: "9:30 a.m.", "9:30 p.m."
- [x] 5d: Add unit tests for new time formats

---

## Phase 6: Percentage Parsing Enhancement ✅

When user types "15%", parsing already works. But when EDITING a percentage cell, they should be able to type "15%" and have it work correctly.

- [x] 6a: Ensure percentage parsing handles decimal places correctly
  - "15%" → 0.15 (stored value)
  - "15.5%" → 0.155 (stored value)
  - Preserve decimal places from input in format
  - (Already implemented in input_parser.cc with comprehensive unit tests)

- [x] 6b: Add E2E test for editing percentage cell
  - Edit existing 15% cell, change to "20%", verify stored as 0.20
  - Added tests in format.test.mjs: "Editing percentage cell preserves format" and "Editing percentage cell with decimal places"

---

## Phase 7: Final Review and Documentation

- [ ] 7a: Review all changes for consistency
- [ ] 7b: Ensure all format categories work correctly in round-trip (display → edit → save)
- [ ] 7c: Run full test suite and fix any regressions

---

## Files to Modify

**C++ (core logic):**
- `core/cells/number_formatter.h` - Add `formatEditValue()` declaration
- `core/cells/number_formatter.cc` - Implement `formatEditValue()`
- `core/cells/number_formatter_test.cc` - Add tests
- `core/cells/input_parser.cc` - Enhance date/time parsing
- `core/cells/input_parser_test.cc` - Add parsing tests
- `apps/wasm/bindings_format.cc` - Include editValue in viewport response

**TypeScript (UI only):**
- `apps/wasm/src/types.ts` - Add editValue to CellData
- `apps/wasm/src/init.ts` - Use editValue in getFormulaBarValue()
- `apps/wasm/src/cell-editor.ts` - Use editValue when starting edit

**Tests:**
- `apps/wasm/tests/format.test.mjs` - E2E tests for edit value behavior

---

## Summary

This plan adds an "edit value" concept that matches Excel's behavior:
- Dates and times show human-readable format when editing
- Percentages show with % sign when editing
- Currency and numbers show raw values when editing

All logic is implemented in C++, TypeScript is UI-only.
