Status: IN PROGRESS
Created At: 2026-01-07 01:46 UTC
Updated At: 2026-01-06 18:22 UTC
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

## Overview

This plan addresses three interconnected issues with the cell formatting system:

1. **Decimal places limited to 4**: Currently decimals are capped at 4 because we only have format variants like `PERCENTAGE_0` through `PERCENTAGE_4`. Users need higher precision or "auto" decimals.

2. **Copy/paste doesn't preserve format**: The clipboard data includes value, formula, and type but not `formatId`, so pasted cells always get GENERAL format.

3. **No custom format support**: The current system only supports predefined format IDs. Excel-style custom format codes (e.g., `#,##0.00`, `0.00%`, `mm/dd/yy`) should be supported.

## Architecture Decision: Dynamic Format IDs + Format Codes

### Key Insight: Dynamic Format ID Parsing

Instead of pre-registering `PERCENTAGE_0` through `PERCENTAGE_15`, format IDs are **parsed dynamically**:

```
PERCENTAGE_7  → parse → { category: PERCENTAGE, decimals: 7 }  → code: "0.0000000%"
NUMBER_12     → parse → { category: NUMBER, decimals: 12 }     → code: "0.000000000000"
CUSD_008      → parse → { currency: USD, decimals: 8 }         → code: "$#,##0.00000000"
```

The registry lookup becomes:
1. Check if it's a cached/known format
2. If not, parse the ID pattern (e.g., `PERCENTAGE_(\d+)`, `C([A-Z]{3})_(\d+)`)
3. Generate the format code on-the-fly
4. Cache for future lookups

This eliminates the need to pre-register variants and allows unlimited decimal precision.

### Format Codes as First-Class Citizens

For custom formats beyond the dynamic ID patterns, we support **format code strings** (Excel-compatible):

**Excel format code syntax (subset to support):**
- `0` - Digit placeholder (shows 0 if no digit)
- `#` - Digit placeholder (omits leading zeros)
- `.` - Decimal separator
- `,` - Thousands separator (when placed in integer portion, e.g., `#,##0`)
- `%` - Percentage display (**multiplies stored value by 100**, so `0.15` → `15%`)
- `"%"` - Literal percent sign (no multiplication, for displaying already-multiplied values)
- `$`, `€`, `£`, `¥` - Currency symbols
- `@` - Text placeholder
- Sections: `positive;negative;zero;text`

**Note on `%` behavior:** Per Excel convention, `%` in format codes means the stored value is the decimal (0.15 for 15%). The formatter multiplies by 100 for display. This matches our current implementation.

---

## Phase 1: Dynamic Format ID Parsing (Unlimited Decimals) ✅

Replace hardcoded format variants with dynamic ID parsing, allowing any decimal precision.

- [x] 1a: Implement dynamic format ID parser in `number_format.cc`
  - Add `parseFormatId(id)` function that extracts category + decimals from ID pattern
  - Support patterns: `FMT_P0XX`, `FMT_N0XX`, `FMT_NS0X`, `CXXX_0YY`
  - Return parsed struct: `{ category, decimals, currency?, hasThousandsSep? }`
  - Fall back to existing hardcoded lookups for legacy IDs

- [x] 1b: Generate format codes from parsed IDs
  - Add `generateFormatCode(parsedId)` function
  - `FMT_P007` → `"0.0000000%"`
  - `FMT_N012` → `"0.000000000000"`
  - `FMT_NS05` → `"#,##0.00000"`
  - `CUSD_008` → `"$#,##0.00000000"`

- [x] 1c: Update `formatNumber()` to use dynamic lookup
  - First try registry, then parse ID dynamically, then generate code
  - Use generated format code to format the value
  - Added unit tests for decimals 0-15

- [x] 1d: Update UI decimal controls for extended range
  - Modified `format-controls.ts` `handleDecimalChange()` to allow 0-15
  - Generate format ID dynamically based on category
  - Removed hardcoded decimal limit of 4

- [x] 1e: Update GENERAL format precision
  - In `number_formatter.cc`, adjusted `formatGeneral()` to show up to 15 significant digits
  - Trailing zeros are still trimmed

---

## Phase 2: Copy/Paste Preserves Format ✅

Add formatId to clipboard data so paste restores cell formatting.

- [x] 2a: Update clipboard data structure in `clipboard.ts`
  - Add `formatId?: string` to `ClipboardCell` interface
  - In `serializeSelection()`, include `formatId` from cell data (skip if GENERAL/`~`)
  - Store in internal clipboard JSON format

- [x] 2b: Update paste logic to apply format
  - In `pasteClipboardData()`, after creating cell, call `setCellFormatAt()` if formatId exists
  - Ensure formatId is only applied for internal paste (detected by checking `sourceCol !== undefined`)

- [x] 2c: Add E2E tests for format preservation
  - Copy cell with percentage format, paste elsewhere, verify format preserved
  - Copy cell with currency format, paste elsewhere, verify format preserved
  - Test clipboard serialization includes formatId for formatted cells

---

## Phase 3: Custom Format Code Support ✅

Implement Excel-compatible format code strings.

### Phase 3a: Format Code Parser ✅

- [x] 3a-1: Create `format_code_parser.h/.cc` with format code parsing
  - Parse Excel format code syntax: `#`, `0`, `.`, `,`, `%`, currency symbols
  - Support section separators (`;`) for positive/negative/zero/text
  - Return parsed structure with: decimal places, has thousands sep, has percent, currency symbol, etc.

- [x] 3a-2: Add unit tests for format code parser
  - Test `0.00` → 2 decimals, no thousands
  - Test `#,##0.00` → 2 decimals, thousands separator
  - Test `0.00%` → 2 decimals, percentage
  - Test `$#,##0.00` → currency, 2 decimals, thousands
  - Test `#,##0.00;(#,##0.00)` → positive/negative sections

### Phase 3b: Format Code Formatter ✅

- [x] 3b-1: Create `format_code_formatter.h/.cc` to render values using format codes
  - Take parsed format code + numeric value → formatted string
  - Handle positive/negative/zero sections
  - Support all placeholders: `#`, `0`, `.`, `,`, `%`, currency, `@`

- [x] 3b-2: Add unit tests for format code formatter
  - Test various inputs with different format codes
  - Test negative number formatting with sections
  - Test percentage multiplication
  - Test thousands separator insertion

### Phase 3c: Integrate Custom Formats into Registry

- [ ] 3c-1: Update `NumberFormat` struct to use formatCode as primary
  - Keep `decimalPlaces`, `useThousandsSeparator`, etc. as derived/cached
  - Add `isCustom` flag to distinguish from built-in formats
  - Update `formatNumber()` to use format code formatter

- [ ] 3c-2: Add API to create custom formats
  - Add `createCustomFormat(formatCode)` to registry
  - Return new format ID for the custom format
  - Store custom formats in the workbook/CRDT

- [ ] 3c-3: CRDT support for custom format definitions
  - Add new operation type `FORMAT_DEFINE` to persist custom formats
  - Custom formats need to sync across peers
  - Format ID generation for custom formats

### Phase 3d: Custom Format UI

- [ ] 3d-1: Add "Custom" category to format dropdown
  - When selected, show format code input field
  - Show live preview of current cell value with entered format code
  - Show common format code templates like Excel does

- [ ] 3d-2: Format code validation and error display
  - Parse format code as user types
  - Show validation errors inline
  - Prevent applying invalid format codes

- [ ] 3d-3: Save and apply custom formats
  - Call `createCustomFormat()` API
  - Apply returned format ID to selected cells
  - Add to "recent custom formats" list

---

## Phase 4: Polish and Edge Cases

- [ ] 4a: Handle format display in formula bar
  - Show raw value or formatted value based on user preference
  - Currently shows formula or raw value

- [ ] 4b: Format code auto-detection from input
  - When user types `$1,234.56`, detect and create format code `$#,##0.00`
  - Update input parser to generate format codes instead of predefined IDs

- [ ] 4c: Migration from old format IDs
  - Map old `CUSD_002` style IDs to format codes on load
  - Ensure backward compatibility with existing files

---

## File Changes Summary

**Phase 1 (modified files):**
- `core/cells/number_format.h/.cc` - Add `parseFormatId()`, `generateFormatCode()`, caching
- `core/cells/number_format_test.cc` - Tests for dynamic ID parsing
- `core/cells/number_formatter.cc` - Use dynamic lookup, increase GENERAL precision
- `apps/wasm/src/format-controls.ts` - Remove decimal limit, generate IDs dynamically

**Phase 2 (modified files):**
- `apps/wasm/src/clipboard.ts` - Add formatId to clipboard data
- `apps/wasm/tests/clipboard.test.mjs` - E2E tests for format preservation

**Phase 3 (new + modified files):**
- `core/cells/format_code_parser.h/.cc` - Parse Excel format code strings (NEW)
- `core/cells/format_code_parser_test.cc` - Parser tests (NEW)
- `core/cells/format_code_formatter.h/.cc` - Format values using codes (NEW)
- `core/cells/format_code_formatter_test.cc` - Formatter tests (NEW)
- `core/cells/crdt.cc` - Add FORMAT_DEFINE operation for custom formats
- `apps/wasm/src/format-controls.ts` - Add Custom format UI
- `apps/wasm/bindings.cc` - Expose custom format APIs

---

## Dependencies

- **Phase 1 and Phase 2 are independent** - can be done in parallel
- **Phase 3 depends on Phase 1** - dynamic ID system lays groundwork for format codes
- **Phase 4 depends on Phase 3** - polish after custom formats work

## Risks

1. **Excel format code complexity**: Excel's format code syntax is complex. We implement a useful subset, not 100% compatibility.
2. **CRDT changes**: Adding FORMAT_DEFINE operation requires protocol changes. Ensure backward compatibility.
3. **Performance**: Custom format parsing should be cached, not repeated per render.

---

## Success Criteria

1. **Decimals**: Users can set decimal places from 0-15+ (not just 0-4) via UI controls
2. **Copy/paste**: Pasting a cell preserves its format (percentage stays percentage, currency stays currency)
3. **Custom formats**: Users can enter Excel-style format codes like `#,##0.00` or `0.00%`
4. **Architecture**: Format IDs are parsed dynamically, no need to pre-register every variant
5. **Backward compatibility**: Existing files with old format IDs continue to work
