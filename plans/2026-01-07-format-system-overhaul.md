Status: READY
Created At: 2026-01-07 01:46 UTC
Updated At: 2026-01-07 01:46 UTC
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

## Architecture Decision: Format Codes as First-Class Citizens

Instead of hardcoded format variants with embedded decimal counts, we move to **format code strings** (Excel-compatible):

**Current approach:**
- Cell stores: `formatId = "CUSD_002"` (2 decimals, USD)
- Registry has 25+ predefined variants

**New approach:**
- Cell stores: `formatId` (ID) + format has `formatCode` string
- Format registry stores format codes: `"$#,##0.00"`, `"0.0000%"`, etc.
- Built-in formats still exist but custom formats can be created
- Decimal precision is encoded in the format code, not limited to 0-4

**Excel format code syntax (subset to support):**
- `0` - Digit placeholder (shows 0 if no digit)
- `#` - Digit placeholder (omits leading zeros)
- `.` - Decimal separator
- `,` - Thousands separator
- `%` - Percentage (multiplies by 100)
- `$`, `€`, `£`, `¥` - Currency symbols
- `@` - Text placeholder
- Sections: `positive;negative;zero;text`

---

## Phase 1: Increase Decimal Limit (Quick Win)

Immediate fix: extend the existing variant system from 0-4 to 0-15 decimals.

- [ ] 1a: Add format variants for 5-15 decimals in `number_format.cc`
  - Add `PERCENTAGE_5` through `PERCENTAGE_15`
  - Add `CURRENCY_USD_5` through `CURRENCY_USD_15` (and other currencies)
  - Add `NUMBER_5` through `NUMBER_15` variants
  - Update `getFormatIdForCategory()` to support decimals 0-15

- [ ] 1b: Update UI decimal controls to allow 0-15 range
  - Modify `format-controls.ts` `handleDecimalChange()` to cap at 15 instead of 4
  - Update button labels/tooltips if needed

- [ ] 1c: Update GENERAL format to show up to 15 significant decimals
  - In `number_formatter.cc`, adjust `formatGeneral()` to show more precision
  - Ensure trailing zeros are still trimmed for GENERAL

---

## Phase 2: Copy/Paste Preserves Format

Add formatId to clipboard data so paste restores cell formatting.

- [ ] 2a: Update clipboard data structure in `clipboard.ts`
  - Add `formatId?: string` to `ClipboardCell` interface
  - In `copySelection()`, include `formatId` from cell data
  - Store in internal clipboard JSON format

- [ ] 2b: Update paste logic to apply format
  - In `pasteClipboard()`, after creating cell, call `setCellFormatAt()` if formatId exists
  - Ensure formatId is only applied for internal paste (not TSV from external apps)

- [ ] 2c: Add E2E tests for format preservation
  - Copy cell with percentage format, paste elsewhere, verify format preserved
  - Copy cell with currency format, paste elsewhere, verify format preserved
  - Copy from external TSV, verify no format applied (uses input detection)

---

## Phase 3: Custom Format Code Support

Implement Excel-compatible format code strings.

### Phase 3a: Format Code Parser

- [ ] 3a-1: Create `format_code_parser.h/.cc` with format code parsing
  - Parse Excel format code syntax: `#`, `0`, `.`, `,`, `%`, currency symbols
  - Support section separators (`;`) for positive/negative/zero/text
  - Return parsed structure with: decimal places, has thousands sep, has percent, currency symbol, etc.

- [ ] 3a-2: Add unit tests for format code parser
  - Test `0.00` → 2 decimals, no thousands
  - Test `#,##0.00` → 2 decimals, thousands separator
  - Test `0.00%` → 2 decimals, percentage
  - Test `$#,##0.00` → currency, 2 decimals, thousands
  - Test `#,##0.00;(#,##0.00)` → positive/negative sections

### Phase 3b: Format Code Formatter

- [ ] 3b-1: Create `format_code_formatter.h/.cc` to render values using format codes
  - Take parsed format code + numeric value → formatted string
  - Handle positive/negative/zero sections
  - Support all placeholders: `#`, `0`, `.`, `,`, `%`, currency, `@`

- [ ] 3b-2: Add unit tests for format code formatter
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

**New files:**
- `core/cells/format_code_parser.h/.cc` - Parse Excel format codes
- `core/cells/format_code_parser_test.cc` - Parser tests
- `core/cells/format_code_formatter.h/.cc` - Format values using codes
- `core/cells/format_code_formatter_test.cc` - Formatter tests

**Modified files:**
- `core/cells/number_format.h/.cc` - Add 5-15 decimal variants, custom format support
- `core/cells/number_formatter.cc` - Use format code formatter
- `core/cells/crdt.cc` - Add FORMAT_DEFINE operation
- `apps/wasm/src/clipboard.ts` - Add formatId to clipboard
- `apps/wasm/src/format-controls.ts` - Extend decimal range, add Custom UI
- `apps/wasm/bindings.cc` - Expose new APIs

---

## Dependencies

- Phase 2 is independent and can be done in parallel with Phase 1
- Phase 3 depends on Phase 1 being complete (decimal extension proves the need)
- Phase 4 depends on Phase 3 being complete

## Risks

1. **Excel format code complexity**: Excel's format code syntax is complex. We implement a useful subset, not 100% compatibility.
2. **CRDT changes**: Adding FORMAT_DEFINE operation requires protocol changes. Ensure backward compatibility.
3. **Performance**: Custom format parsing should be cached, not repeated per render.

---

## Success Criteria

1. Users can set decimal places from 0-15 (not just 0-4)
2. Copy/paste preserves cell format
3. Users can enter custom format codes like `#,##0.00` or `0.00%`
4. Format system is extensible for future format types
