Status: IN_PROGRESS
Created At: 2026-01-06 01:03 UTC
Updated At: 2026-01-06 04:20 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| WASM Build | `make wasm-dist` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

---

# Format Fixes Phase 2 - Decimal Controls, Empty Cells, and UI Polish

## Problem Summary

Issues discovered after completing the format-refresh-architecture plan:

### Functional Bugs
1. **Decimal +/- buttons don't work** - Clicking .0- and .00 buttons does nothing
2. **Empty cell format doesn't persist** - Selecting format on empty cell reverts to "General" (no cell UUID to store format)
3. **Formula with percentage selector** - `=0.15` doesn't become percentage when using dropdown selector
4. **Inconsistent decimal places** - Typing "15%" shows "15%", but 0.15→Percentage shows "15.00%" (2 decimals)

### Formula/AST Issues
5. **Percentage literal lost in formula bar** - Typing "=15%" shows "=0.15" when re-selected (AST loses format info)

### Enhancement Requests
6. **Currency format inheritance** - `=A1*10` where A1 is currency should default to currency format
7. **Currency selector dropdown** - $ button should show dropdown to select different currencies (USD, EUR, GBP, etc.)

### UI Polish
8. **Spacing inconsistency** - Space around vertical separator differs from space at end of formula bar

---

## Root Cause Analysis

### Issue 1: Decimal buttons
The `handleDecimalChange()` function compares `f.category === "NUMBER"` but C++ returns lowercase `"number"`. The case conversion is done in `getCategoryForFormatId()` but not in `handleDecimalChange()`.

### Issue 2: Empty cell format
`setCellFormatAt()` in bindings.cc returns `{"error":"Cell not found at position"}` when no cell exists. We need to create a cell first (or create the cell inline).

### Issue 3: Formula with format selector
When a cell contains a formula, `setCellFormatAt()` should still work - the format is stored on the cell, not the value. Need to verify this code path.

### Issue 4: Inconsistent decimal places
When input parser detects "15%", it stores 0.15 with `FMT_P000` (0 decimal places). But when user selects Percentage from dropdown for a value like 0.15, it uses `FMT_P002` (2 decimal places). The input parser should detect and preserve the input's decimal precision.

### Issue 5: Percentage in formula bar
The formula AST stores `NumberNode(0.15)` for `15%`. To show the original notation, we'd need either:
- Store a "display hint" on the AST node (complex)
- Recognize when a number could have come from a percentage and show it that way (heuristic)
- Accept current behavior (matches Excel - shows `=0.15`)

**Decision**: Accept current behavior. This matches Excel. The formula `=15%` is semantically equivalent to `=0.15`. Documenting this as intentional behavior, not a bug.

### Issue 6: Currency inheritance
Formula result format inheritance requires tracking which inputs had formats and propagating the "dominant" format. Common rules:
- If any input is currency, result is currency
- If any input is percentage, result is percentage
- Otherwise, general

This is a significant feature that requires careful design.

### Issue 7: Currency dropdown
UI change to replace single $ button with a dropdown containing USD, EUR, GBP, JPY, CNY options.

### Issue 8: Spacing
CSS adjustment to make spacing consistent.

---

## Phase 1: Fix Decimal +/- Buttons

The comparison in `handleDecimalChange()` uses exact string match against lowercase C++ categories.

- [x] 1a: Fix category comparison in handleDecimalChange to be case-insensitive
  - Convert `currentFormat.category` and `f.category` to uppercase before comparison
  - Same pattern as `getCategoryForFormatId()`

- [x] 1b: Add E2E test for decimal buttons
  - Enter "1234.5678", select Number format
  - Click decimal decrease, verify "1234.568" (3 decimals)
  - Click decimal decrease again, verify "1234.57" (2 decimals)
  - Click decimal increase, verify "1234.568" (3 decimals)

## Phase 2: Fix Empty Cell Format Selection ✅

When selecting a format on an empty cell, the cell doesn't exist so the format can't be applied.

- [x] 2a: Create cell if not exists when setting format (C++)
  - In `setCellFormatAt()`, if cell not found but column and row exist, create an empty cell
  - Empty cell: value type = STRING, raw value = ""
  - Apply format to the new cell

- [x] 2b: Create column/row if not exists when setting format (C++)
  - If column doesn't exist at position, create it via `getOrCreateColumnByPosition()`
  - If row doesn't exist at position, create it via `getOrCreateRowByPosition()`
  - Then create cell and apply format

- [x] 2c: Add E2E test for empty cell format
  - Select empty cell B5 (where B and 5 don't exist)
  - Select Currency from dropdown
  - Verify dropdown shows "Currency"
  - Type "100", verify displays as "$100.00"

## Phase 3: Preserve Decimal Places from Input ✅

When typing "15%", preserve the 0-decimal format. When typing "15.5%", use 1-decimal format.

**Architecture Decision**: Rather than adding `decimalPlacesOverride` to Cell struct (which would require
CRDT operation changes, serialization updates, etc.), we chose a simpler approach: add format variants for
0-4 decimal places for each category that supports decimals (percentage, currency, number with separator).

Approach implemented:
- Added format variants: PERCENTAGE_0-4, CURRENCY_0-4, NUMBER_SEP-SEP4
- Input parser counts exact decimal places and returns matching format ID
- Decimal +/- buttons cycle through available format IDs by decimal count
- No changes needed to Cell struct, CRDT operations, or file format

- [x] 3a: Add format variants for 0-4 decimal places
  - Added PERCENTAGE_1, PERCENTAGE_3, PERCENTAGE_4
  - Added CURRENCY_1, CURRENCY_3, CURRENCY_4
  - Added NUMBER_SEP1, NUMBER_SEP3, NUMBER_SEP4
  - Updated number_format.h and number_format.cc

- [x] 3b: Update input parser to count exact decimal places
  - Added `countDecimalPlaces()` helper function
  - Added `getPercentageFormatId()` and `getCurrencyFormatId()` helpers
  - Updated parsePercentage() and parseCurrency() to use exact decimal count

- [x] 3c: Update decimal +/- buttons to cycle format IDs
  - Format dropdown max decimal changed from 10 to 4
  - handleDecimalChange() already searches for formats by category and decimal places

- [x] 3d: Add unit tests for decimal place preservation
  - Added tests for 1, 2, 3 decimal percentages
  - Added tests for 1, 3 decimal currencies
  - Updated format count expectations in number_format_test.cc

- [x] 3e: Add E2E tests for preserved decimal places
  - Updated "Percentage with decimals" test to expect 12.5% (not 12.50%)
  - Added "Percentage with 2 decimals" test for 12.50%
  - Added "Currency with 1 decimal" test for $99.9

## Phase 4: Formula Cell Format Selection ✅

Verify and fix format application to formula cells.

- [x] 4a: Debug and fix formula cell format selection
  - Root cause: bindings.cc getCellsInRange() wasn't applying formatId to formula cell numeric results
  - Fixed by checking entry.cell->formatId and calling formatNumber() in the result.isNumber() branch
  - Also improved getFormatIdForCategory() to prefer 0-decimal formats as default for dropdown selection

- [x] 4b: Add E2E test for formula cell format
  - Added "Formula cell can have percentage format applied" test
  - Added "Formula cell can have currency format applied" test
  - Both verify format changes display correctly while formula bar preserves the formula

## Phase 5: Currency Dropdown ✅

Replace the $ button with a dropdown to select currency type.

- [x] 5a: Update HTML for currency dropdown
  - Replaced `<button>$</button>` with dropdown structure
  - Options: USD ($), EUR (€), GBP (£), JPY (¥), CNY (¥)
  - Show currently selected currency symbol on button

- [x] 5b: Add CSS for currency dropdown
  - Style similar to format dropdown
  - Compact dropdown menu with currency symbols

- [x] 5c: Update TypeScript for currency selection
  - Added state for selected currency
  - On selection, applies appropriate format (CUSD_002, CEUR_002, etc.)
  - Updates button to show selected currency symbol

- [x] 5d: Add format IDs for different currencies (C++)
  - Registered formats: USD_0-4, EUR_0-4, GBP_0-4, JPY_0-4, CNY_0-4
  - Format ID pattern: C{CURRENCY}_0{DECIMALS} (8 chars, e.g., CUSD_002)
  - Legacy USD formats (FMT_C0XX) remain for backward compatibility

- [x] 5e: Add E2E test for currency selection
  - Tests for EUR, GBP, JPY currency selection
  - Verifies correct formatting and symbol display

## Phase 6: UI Spacing Fix

Fix inconsistent spacing in formula bar.

- [ ] 6a: Audit and fix formula bar spacing
  - Measure current spacing around separator vs right edge
  - Adjust CSS to make spacing consistent
  - Use consistent spacing variable (--bar-gap or --spacing-sm)

- [ ] 6b: Visual review of formula bar layout
  - Verify alignment looks correct
  - Check in both light and dark modes

## Phase 7: Currency Format Inheritance (Future Enhancement)

**Note**: This is a larger feature that may warrant its own plan. Documenting design here for future reference.

Design considerations:
- Track format "source" during formula evaluation
- When result is computed, check if any operand had a format
- Priority: Currency > Percentage > Number > General
- Store "inherited format" hint on result cell
- Allow manual override

Deferred to future plan due to complexity.

---

## Files to Modify

### TypeScript (Phase 1, 2c, 4-5)
- `apps/wasm/src/format-controls.ts` - Fix category comparison, add currency dropdown logic
- `apps/wasm/src/types.ts` - Add currency type if needed

### C++ (Phase 2a-b, 3a-c, 5d)
- `apps/wasm/bindings.cc` - Create cell on format set, add currency formats
- `core/cells/input_parser.cc` - Detect decimal places from input
- `core/cells/input_parser_test.cc` - Tests for decimal detection
- `core/cells/format_registry.cc` - Register additional currency formats

### HTML/CSS (Phase 5a-b, 6)
- `apps/wasm/static/index.html` - Currency dropdown structure
- `apps/wasm/static/shared/styles.css` - Currency dropdown styles, spacing fix

### Tests (Phase 1b, 2c, 3d, 4b, 5e)
- `apps/wasm/tests/format.test.mjs` - E2E tests for all fixes

---

## Success Criteria

1. Decimal +/- buttons change decimal places correctly
2. Empty cells can have format applied (cell created automatically)
3. Percentage/currency format from dropdown works on formula cells
4. Typed "15%" stays as "15%" (0 decimals), not "15.00%"
5. Currency dropdown allows selecting USD, EUR, GBP, JPY, CNY
6. Formula bar spacing is consistent
7. All existing tests continue to pass

---

## File Format Compatibility Requirements

**Important**: All format changes must be correctly serialized and deserialized in both file formats:

### Excel (.xlsx)
- Number formats use Excel-style format codes (e.g., `"0.00%"`, `"$#,##0.00"`)
- `decimalPlacesOverride` should generate appropriate format code on export
- On import, parse format codes to extract decimal places and set override

### ZCD (.zcd / .cells)
- Add `decimalPlaces` field to cell serialization if override is set
- Format: `F <cellId> <formatId> [decimalPlaces]`
- Example: `F cA1b2c3d PERCENTAGE 3` (3 decimal places)
- Backwards compatible: missing decimal places means use format default

### Test Coverage
- Round-trip tests: save file with custom decimal places, reload, verify preserved
- Import tests: Excel files with various decimal formats should display correctly
- Export tests: verify generated Excel files have correct format codes
