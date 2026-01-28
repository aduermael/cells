# Content-Addressed Format System

Replace the current registry-based format system with content-addressed formats using binary encoding, mirroring the style system architecture.

## Problem Statement

The current format system has inconsistencies with the style system:

1. **FORMAT_DEFINE operations required**: Custom formats need separate `F <id> "<code>"` definition operations, while styles are fully content-addressed.

2. **Hardcoded format IDs**: Built-in formats like `CUSD_002` are hardcoded strings that get parsed dynamically. This is fragile and inconsistent.

3. **Registry complexity**: FormatRegistry manages IDs, reference counting, and deduplication - complexity that styles avoid via content-addressing.

4. **Two different systems**: Styles use `sty:<base64>` (content IS identity), while formats use `fmt:<id>` (reference to definition). This is confusing and adds cognitive overhead.

## Current State

**Styles (content-addressed):**
```
X aiY10D0p USPVkpUi FftuXhIw s "Hello" sty:QAD4cXE=
```
- No STYLE_DEFINE operation needed
- Base64 encodes the style properties directly
- Same properties = same base64 = same identity

**Formats (registry-based):**
```
F V5bzfKy6 "# BANANA"
X baNM2mxw qDN3bdI6 FftuXhIw n 100 fmt:V5bzfKy6
```
- FORMAT_DEFINE (`F`) line required for custom formats
- Built-in formats use hardcoded IDs like `CUSD_002`, `FMT_P002`
- These IDs get dynamically parsed to generate format codes

## Proposed Solution

**Content-addressed formats**: The format's encoded properties ARE its identity.

### New Format

**Before:**
```
F V5bzfKy6 "# BANANA"
X baNM2mxw qDN3bdI6 FftuXhIw n 100 fmt:V5bzfKy6 sty:DAAD
```

**After:**
```
X baNM2mxw qDN3bdI6 FftuXhIw n 100 fmt:AQNVU0QC sty:DAAD
```

Where `AQNVU0QC` is a base64-encoded binary format specification.

### Binary Format Encoding

```
+--------+--------+--------+--------+...
| Flags  | Prop   | Prop   | Prop   |
| Byte   | Data   | Data   | Data   |
+--------+--------+--------+--------+
```

**Flag byte (1 byte):**
- Bit 0: category present (otherwise GENERAL)
- Bit 1: decimals present (otherwise 0)
- Bit 2: thousands separator flag (if set, use separator)
- Bit 3: currency symbol present
- Bit 4: custom format code present (raw Excel-style string)
- Bits 5-7: reserved

**Property data (in flag bit order, only if flag set):**

1. **Category** (1 byte): NumberFormatCategory enum value
   - 0=GENERAL, 1=NUMBER, 2=CURRENCY, 3=ACCOUNTING, 4=PERCENTAGE
   - 5=DATE, 6=TIME, 7=DATE_TIME, 8=SCIENTIFIC, 9=FRACTION, 10=TEXT

2. **Decimals** (1 byte): 0-15 decimal places

3. **Currency symbol** (length-prefixed UTF-8): 1 byte length + symbol chars
   - "$", "€", "£", "¥" etc.

4. **Custom format code** (length-prefixed UTF-8): 2 bytes length + format string
   - For complex formats like `"# BANANA"`, `"_($* #,##0.00_)"`

### Examples

**Percentage with 2 decimals (FMT_P002):**
```
Flags: 0b00000011 (category + decimals)
Data:  [0x04] [0x02]
       ^^^^   ^^^^
       PERCENTAGE  2 decimals
Total: 3 bytes
Base64: ~4 characters (e.g., "AQQI")
```

**USD Currency with 2 decimals (CUSD_002):**
```
Flags: 0b00001111 (category + decimals + separator + currency)
Data:  [0x02] [0x02] [0x01, '$']
       ^^^^   ^^^^   ^^^^^^^^^^^
       CURRENCY  2 dec  1-char symbol
Total: 5 bytes
Base64: ~7 characters
```

**Custom format "# BANANA":**
```
Flags: 0b00010001 (category + custom code)
Data:  [0x01] [0x00, 0x08, '#', ' ', 'B', 'A', 'N', 'A', 'N', 'A']
       ^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       NUMBER  8-char format code
Total: 11 bytes
Base64: ~15 characters
```

### Benefits

1. **No FORMAT_DEFINE operations**: Custom formats embedded directly
2. **No registry needed**: Format identity is deterministic from content
3. **Consistent with styles**: Same `<prop>:<base64>` pattern
4. **Simpler CRDT**: One less operation type
5. **No sync issues**: Format data always present with the cell

### Migration Notes

**No backward compatibility needed** - app not released. Simply:

- Remove `F` (FORMAT_DEFINE) line parsing entirely (parser should error on unknown line type)
- Remove FormatRegistry class completely
- Update parser to only accept base64-encoded `fmt:` values
- Update any testdata ZCD files if they contain format references
- Update unit tests to use new format encoding

## Implementation Phases

### Phase 1: Create FormatBuffer Class

Analogous to StyleBuffer, but for number format encoding.

- [x] 1a: Create `format_buffer.h` with FormatBuffer struct and flag constants
- [x] 1b: Implement property setters (setCategory, setDecimals, setCurrencySymbol, setCustomFormatCode)
- [x] 1c: Implement property getters that read from binary data based on flags
- [x] 1d: Implement `toBase64()` and `fromBase64()` serialization
- [x] 1e: Implement `toFormatCode()` to generate Excel-style format code from properties
- [x] 1f: Implement `fromFormatCode()` to parse Excel-style format code into properties
- [x] 1g: Add unit tests for all properties and edge cases
- [x] 1h: Add `merge()` method to merge another format into this one (like StyleBuffer)
- [x] 1i: Add `hasCollision()` to check if two formats define the same property
- [x] 1j: Add `getEffectiveFormat()` static method to compute effective format from multiple sources
- [x] 1k: Add unit tests for merge and effective format computation

### Phase 2: Integrate FormatBuffer with Model

- [ ] 2a: Add `_entityFormats` map (ID → FormatBuffer) to Workbook alongside `_entityStyles`
- [ ] 2b: Add `setEntityFormat(entityId, FormatBuffer)`, `getEntityFormat(entityId)`, `clearEntityFormat()`, `hasEntityFormat()` methods
- [ ] 2c: Update Cell struct to optionally store FormatBuffer instead of format ID
- [ ] 2d: Add `fmt:` field to Range struct for range-level formats (like `sty:`)

### Phase 3: Update CRDT Operations

- [ ] 3a: Modify `CELL_SET_FORMAT` payload to contain base64 format instead of format_id
- [ ] 3b: Update `applyCellSetFormat()` to parse base64 format
- [ ] 3c: Add `RANGE_SET_FORMAT` operation for range-level formats (similar to RANGE_SET_STYLE)
- [ ] 3d: Add `AXIS_SET_FORMAT` operation update for content-addressed formats
- [ ] 3e: Remove `FORMAT_DEFINE` operation type

### Phase 4: Update Serialization

- [ ] 4a: Update ZCD serializer to write base64 formats directly (no more `F` lines)
- [ ] 4b: Update ZCD parser to only accept base64 `fmt:` values (remove `F` line parsing entirely)
- [ ] 4c: Update XLSX import to create FormatBuffer directly
- [ ] 4d: Update XLSX export to read from FormatBuffer
- [ ] 4e: Update bootstrap to emit content-addressed formats
- [ ] 4f: Update any testdata ZCD files that contain format references

### Phase 5: Update Bindings and TypeScript

- [ ] 5a: Update `setCellFormat()` binding to accept format properties directly
- [ ] 5b: Update `getCellFormat()` to return decoded format properties
- [ ] 5c: Remove format registry APIs from bindings
- [ ] 5d: Update TypeScript types for new format system
- [ ] 5e: Update format dropdown/toolbar to work with new system

### Phase 6: Effective Format Computation and Range Splitting

Mirror the style system's approach for computing effective formats at display time.

- [ ] 6a: Implement `getEffectiveCellFormat()` in model (column < row < range < cell priority)
- [ ] 6b: Update range splitting logic to handle format inheritance (like styles)
- [ ] 6c: Update viewport/rendering to use effective format computation
- [ ] 6d: Add unit tests for effective format with overlapping ranges
- [ ] 6e: Add E2E tests for format inheritance across columns, rows, ranges, and cells

### Phase 7: Remove Old Format System

- [ ] 7a: Remove FormatRegistry class
- [ ] 7b: Remove NumberFormatRegistry (or simplify to just formatting logic)
- [ ] 7c: Remove FORMAT_DEFINE from operation types
- [ ] 7d: Remove built-in format ID constants (CUSD_002, FMT_P002, etc.)
- [ ] 7e: Clean up any remaining format ID references

### Phase 8: Testing and Documentation

- [ ] 8a: Update unit tests to use new FormatBuffer APIs (no legacy format ID tests)
- [ ] 8b: Run all existing format-related E2E tests
- [ ] 8c: Fix any failing tests
- [ ] 8d: Update docs/file-format.md with new format encoding
- [ ] 8e: Update docs/persistence.md to remove `F` line documentation

## File Changes Summary

**New files:**
- `core/cells/format_buffer.h` - FormatBuffer class definition
- `core/cells/format_buffer.cc` - FormatBuffer implementation
- `core/cells/format_buffer_test.cc` - Unit tests

**Modified files:**
- `core/cells/model.h` - Add _entityFormats, remove format registry
- `core/cells/model.cc` - Implement new format methods
- `core/cells/crdt.cc` - Update format operations
- `core/cells/crdt.h` - Remove FORMAT_DEFINE
- `core/cells/operation.h` - Remove FORMAT_DEFINE type
- `core/cells/serializer.cc` - Update ZCD format
- `core/cells/parser.cc` - Update ZCD parsing
- `apps/wasm/bindings_format.cc` - Update bindings
- `apps/wasm/src/types.ts` - Update TypeScript types

**Removed files:**
- `core/cells/format_registry.h`
- `core/cells/format_registry.cc`
- `core/cells/format_registry_test.cc`

## Design Decisions

1. **Keep NumberFormat struct for display logic**: The `NumberFormat` struct and `NumberFormatRegistry` can remain for runtime formatting (converting numbers to display strings). Only the storage/sync mechanism changes.

2. **Custom format codes as fallback**: For complex Excel formats that don't fit the structured properties (accounting formats with special alignment, etc.), store the raw format code string directly.

3. **Category enum in binary**: Store the category as a single byte enum rather than inferring it from other properties. This is explicit and avoids ambiguity.

4. **No reference counting needed**: Content-addressed formats don't need garbage collection - they're self-contained.

## Risks and Mitigations

1. **Larger cell data for custom formats**: Long format codes increase cell size. Mitigation: Most cells use simple formats (few bytes). Custom formats are rare.

2. **Format code parsing complexity**: Need to parse format codes to extract properties. Mitigation: Already have `parseFormatCode()` function; just use it in reverse.
