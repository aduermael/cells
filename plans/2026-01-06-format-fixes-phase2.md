Status: READY
Created At: 2026-01-06 01:03 UTC
Updated At: 2026-01-06 01:03 UTC
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

- [ ] 1a: Fix category comparison in handleDecimalChange to be case-insensitive
  - Convert `currentFormat.category` and `f.category` to uppercase before comparison
  - Same pattern as `getCategoryForFormatId()`

- [ ] 1b: Add E2E test for decimal buttons
  - Enter "1234.5678", select Number format
  - Click decimal decrease, verify "1234.568" (3 decimals)
  - Click decimal decrease again, verify "1234.57" (2 decimals)
  - Click decimal increase, verify "1234.568" (3 decimals)

## Phase 2: Fix Empty Cell Format Selection

When selecting a format on an empty cell, the cell doesn't exist so the format can't be applied.

- [ ] 2a: Create cell if not exists when setting format (C++)
  - In `setCellFormatAt()`, if cell not found but column and row exist, create an empty cell
  - Empty cell: value type = STRING, raw value = ""
  - Apply format to the new cell

- [ ] 2b: Create column/row if not exists when setting format (C++)
  - If column doesn't exist at position, create it via `getOrCreateColumnByPosition()`
  - If row doesn't exist at position, create it via `getOrCreateRowByPosition()`
  - Then create cell and apply format

- [ ] 2c: Add E2E test for empty cell format
  - Select empty cell B5 (where B and 5 don't exist)
  - Select Currency from dropdown
  - Verify dropdown shows "Currency"
  - Type "100", verify displays as "$100.00"

## Phase 3: Preserve Decimal Places from Input

When typing "15%", preserve the 0-decimal format. When typing "15.5%", use 1-decimal format.

- [ ] 3a: Update input parser to detect decimal places from percentage input
  - Parse "15%" → 0 decimals, "15.5%" → 1 decimal, "15.50%" → 2 decimals
  - Select appropriate format ID: FMT_P000, FMT_P001, FMT_P002, etc.

- [ ] 3b: Update input parser to detect decimal places from currency input
  - Parse "$100" → 0 decimals, "$100.5" → 1 decimal, "$100.50" → 2 decimals
  - Select appropriate format ID: FMT_C000, FMT_C001, FMT_C002, etc.

- [ ] 3c: Add unit tests for decimal place detection
  - "15%" → FMT_P000, "15.5%" → FMT_P001, "15.50%" → FMT_P002
  - "$100" → FMT_C000, "$100.5" → FMT_C001, "$100.50" → FMT_C002
  - "1.5E6" → scientific with 1 decimal

- [ ] 3d: Add E2E tests for preserved decimal places
  - Type "15%", verify shows "15%" (not "15.00%")
  - Type "15.5%", verify shows "15.5%"
  - Type "$100", verify shows "$100" (not "$100.00")
  - Type "$99.9", verify shows "$99.9" (not "$99.90")

## Phase 4: Formula Cell Format Selection

Verify and fix format application to formula cells.

- [ ] 4a: Debug and fix formula cell format selection
  - Test: enter "=0.5", select Percentage → should show "50%"
  - Verify `setCellFormatAt()` works for formula cells
  - Issue may be in UI not triggering or C++ not applying

- [ ] 4b: Add E2E test for formula cell format
  - Enter "=0.15" in A1
  - Select Percentage from dropdown
  - Verify A1 displays "15%" (or "15.00%" depending on selected format)
  - Verify formula bar still shows "=0.15"

## Phase 5: Currency Dropdown

Replace the $ button with a dropdown to select currency type.

- [ ] 5a: Update HTML for currency dropdown
  - Replace `<button>$</button>` with dropdown structure
  - Options: USD ($), EUR (€), GBP (£), JPY (¥), CNY (¥)
  - Show currently selected currency symbol on button

- [ ] 5b: Add CSS for currency dropdown
  - Style similar to format dropdown
  - Compact dropdown menu with currency symbols

- [ ] 5c: Update TypeScript for currency selection
  - Add state for selected currency
  - On selection, apply appropriate format (FMT_C_USD_002, FMT_C_EUR_002, etc.)
  - Update button to show selected currency symbol

- [ ] 5d: Add format IDs for different currencies (C++)
  - Register formats: USD_0-4, EUR_0-4, GBP_0-4, JPY_0-4, CNY_0-4
  - Update `getAvailableFormats()` to include currency symbol info

- [ ] 5e: Add E2E test for currency selection
  - Select cell, choose EUR from currency dropdown
  - Type "100", verify displays "€100.00"
  - Choose GBP, verify displays "£100.00"

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
