# Content-Addressed Style System

Replace the current entity-based style system with content-addressed styles using compact binary encoding and hash-based identity.

## Problem Statement

The current style system has fundamental sync issues:

1. **Style ID Deduplication Bug**: Styles are created with UUIDs, synced via `STYLE_DEFINE` operations, then referenced by `RANGE_SET_STYLE`. When operations are deduplicated or pruned, style definitions can be lost while references remain.

2. **Two-Operation Overhead**: Setting a style requires two CRDT operations:
   - `STYLE_DEFINE <style_id> {"bgColor":"#FBBF24"}`
   - `RANGE_SET_STYLE <range_id> {"style_id":"<style_id>"}`

3. **Fragile References**: Style IDs create indirect references that can become dangling.

4. **Inefficient Style Merging**: Computing effective cell style requires lookups and JSON parsing.

## Proposed Solution

**Content-addressed styles**: The style's content IS its identity. No separate definition step needed.

### New Operation Format

Instead of:
```
STYLE_DEFINE VuoKkGtv {"bgColor":"#FBBF24"}
RANGE_SET_STYLE JwzH5A51 {"style_id":"VuoKkGtv"}
```

New format:
```
RANGE_SET_STYLE JwzH5A51 <BASE64_STYLE_HASH>
```

Where `<BASE64_STYLE_HASH>` is a base64-encoded binary representation of the style that:
- Contains all style properties in a compact binary format
- Serves as both the style data AND its unique identifier
- Is deterministic (same properties = same hash)

### Binary Style Format

```
+--------+--------+--------+--------+--------+--------+...
| Flags  | Flags  | Prop1  | Prop2  | Prop3  | ...    |
| Byte 0 | Byte 1 | Data   | Data   | Data   |        |
+--------+--------+--------+--------+--------+--------+...
```

**Flag bytes** (2-4 bytes):
- Bit 0-1 of byte 0: Number of flag bytes (0=2, 1=3, 2=4, 3=reserved)
- Remaining bits: Property presence flags (presence only, NOT values)

**First flag byte** (bits 2-7):
- Bit 2: bold present
- Bit 3: italic present
- Bit 4: underline present
- Bit 5: strikethrough present
- Bit 6: bgColor present
- Bit 7: textColor present

**Second flag byte** (bits 0-7):
- Bit 0: fontSize present
- Bit 1: fontFamily present
- Bit 2: horizontalAlign present
- Bit 3: verticalAlign present
- Bit 4: textWrap present
- Bit 5: numberFormat present
- Bit 6: border present
- Bit 7: (reserved for flag byte 3)

**Property data** follows flags in order of flag bits:
- Boolean properties (bold, italic, underline, strikethrough, textWrap): 1 byte packed
  - Bit 0: bold value (if bold present)
  - Bit 1: italic value (if italic present)
  - Bit 2: underline value (if underline present)
  - Bit 3: strikethrough value (if strikethrough present)
  - Bit 4: textWrap value (if textWrap present)
  - Bits 5-7: reserved
  - Note: This byte only present if ANY boolean flag is set
- Colors: 3 bytes RGB each (no alpha needed for cell styles)
- fontSize: 1 byte (6-72pt range, encoded as value-6)
- fontFamily: length-prefixed string (1 byte length + UTF-8 bytes)
- Alignment: 1 byte (3 bits h-align, 3 bits v-align, 2 bits reserved)
- numberFormat: 8 bytes format ID (keep separate registry for now)
- Borders: variable length (see border encoding below)

**Important**: Flags indicate PRESENCE, not value. A style with `bold: false` explicitly set is different from a style with no bold property. The former overrides inherited bold; the latter inherits.

### Border Encoding

Borders are the most complex property. Encoding:
```
+--------+--------+--------+--------+...
| Sides  | Style1 | Color1 | Style2 |...
| Mask   | (1b)   | (3b)   | (1b)   |
+--------+--------+--------+--------+...
```

- Sides mask: 4 bits for top/right/bottom/left
- For each side present: 1 byte style + 3 bytes color

### Style Hash Identity

The binary representation IS the identity:
- Same properties in same order = identical bytes = same style
- No separate "style ID" needed
- Natural deduplication at storage level
- Base64 encoding for JSON/text transport

### Efficient Style Merging

To compute effective cell style (merging column, row, range, and cell styles):

```cpp
// Pseudo-code for style merging
StyleBuffer effectiveStyle;
for (const auto& style : {colStyle, rowStyle, rangeStyle, cellStyle}) {
    // OR the flags together (later styles override)
    effectiveStyle.flags |= style.flags;
    // Copy property data for newly set flags
    copyPropertiesWhere(effectiveStyle, style, style.flags & ~effectiveStyle.flags);
}
```

Flag-based merging is O(1) for detecting which properties are set, enabling efficient cascading.

### Collision Detection for Range Splits

When ranges overlap, flags enable instant collision detection:

```cpp
bool hasPropertyCollision(const StyleBuffer& a, const StyleBuffer& b) {
    return (a.flags & b.flags) != 0;  // Any overlapping properties?
}
```

## Migration Strategy

1. **Phase A**: Add new binary style infrastructure alongside existing system
2. **Phase B**: Migrate internal storage to use binary styles
3. **Phase C**: Update CRDT operations to use new format
4. **Phase D**: Remove old style registry and STYLE_DEFINE operations
5. **Phase E**: Clean up and optimize

## Implementation Phases

### Phase 1: Design Binary Style Format

- [x] 1a: Document complete flag layout for all supported properties (current + Excel compatibility)

**Complete Flag Layout:**

```
Flag Byte 0 (bits 0-7):
  Bits 0-1: Flag byte count indicator (0b00 = 2 bytes, 0b01 = 3, 0b10 = 4, 0b11 = reserved)
  Bit 2: bold present
  Bit 3: italic present
  Bit 4: underline present
  Bit 5: strikethrough present (new, for Excel compatibility)
  Bit 6: bgColor present
  Bit 7: textColor present

Flag Byte 1 (bits 8-15):
  Bit 0 (8): fontSize present
  Bit 1 (9): fontFamily present
  Bit 2 (10): horizontalAlign present
  Bit 3 (11): verticalAlign present
  Bit 4 (12): textWrap present
  Bit 5 (13): numberFormat present (reference to format registry)
  Bit 6 (14): border present
  Bit 7 (15): reserved (for flag byte 3 indicator if needed)

Future Flag Bytes 2-3 (if needed):
  - indent level
  - rotation angle
  - shrink to fit
  - locked/protected
  - hidden
  - etc.
```

**Current properties (14 total, plus borders which use 4 flags internally):**
1. bold, italic, underline, strikethrough (4 booleans → packed into 1 byte)
2. bgColor, textColor (2 colors → 6 bytes total)
3. fontSize (1 byte)
4. fontFamily (length-prefixed string)
5. hAlign, vAlign (packed into 1 byte: 3+3 bits)
6. textWrap (packed with other booleans)
7. numberFormat (8 bytes format ID reference)
8. border (variable: 1 sides-mask byte + 4 bytes per side present)

- [x] 1b: Define property encoding for each type (colors, fonts, alignments, borders)

**Property Encoding Specification:**

```
Property Data (in order of flag bits, only present if flag is set):

1. BOOLEAN BYTE (if any of bold/italic/underline/strikethrough/textWrap flags set):
   +--------+
   | B I U S W _ _ _ |  (1 byte)
   +--------+
   Bit 0: bold value
   Bit 1: italic value
   Bit 2: underline value
   Bit 3: strikethrough value
   Bit 4: textWrap value
   Bits 5-7: reserved

2. BGCOLOR (if flag set): 3 bytes RGB
   +--------+--------+--------+
   |   R    |   G    |   B    |
   +--------+--------+--------+

3. TEXTCOLOR (if flag set): 3 bytes RGB (same as bgColor)

4. FONTSIZE (if flag set): 1 byte
   +--------+
   | size-6 |  (supports 6-261pt, but practical range is 6-72pt)
   +--------+
   Encoding: stored as (size - 6), so 11pt → 5, 6pt → 0, 72pt → 66

5. FONTFAMILY (if flag set): length-prefixed string
   +--------+--------+--------+...
   | length | UTF-8 bytes...   |
   +--------+--------+--------+...
   Length: 1 byte (max 255 chars, practical font names are <64)

6. ALIGNMENT (if either hAlign or vAlign flag set): 1 byte
   +--------+
   | HHH VVV _ _ |  (1 byte)
   +--------+
   Bits 0-2: hAlign (0=LEFT, 1=CENTER, 2=RIGHT, 3=JUSTIFY, 4=GENERAL)
   Bits 3-5: vAlign (0=TOP, 1=MIDDLE, 2=BOTTOM)
   Bits 6-7: reserved

7. NUMBERFORMAT (if flag set): 8 bytes format ID
   +--------+--------+--------+--------+--------+--------+--------+--------+
   |                    format_id (64-bit)                                  |
   +--------+--------+--------+--------+--------+--------+--------+--------+
   Note: Format IDs are from a separate registry (content-address formats later)

8. BORDER (if flag set): variable length
   +--------+--------+--------+--------+--------+...
   | sides  | T_style| T_R    | T_G    | T_B    | R_style | R_R | ...
   +--------+--------+--------+--------+--------+...

   Sides mask (1 byte):
     Bit 0: top present
     Bit 1: right present
     Bit 2: bottom present
     Bit 3: left present
     Bits 4-7: reserved

   For each side present (4 bytes each):
     Byte 0: BorderStyle enum (0-13)
     Bytes 1-3: RGB color

   Total border size: 1 + (4 × number_of_sides_present) bytes
   Min: 1 byte (no sides), Max: 17 bytes (all 4 sides)
```

**Example Encodings:**

```
Style: { bold: true, bgColor: "#FBBF24" }
Flags: 0b00_0100_0100 (bold=bit2, bgColor=bit6)
Data:  [0x01] [0xFB, 0xBF, 0x24]
       ^^^^   ^^^^^^^^^^^^^^^^
       bools  RGB color
Total: 2 flag bytes + 1 bool byte + 3 color bytes = 6 bytes
Base64: ~8 characters

Style: { fontSize: 14, hAlign: "center" }
Flags: 0b00_0000_0101_0000_0000 (fontSize=bit8, hAlign=bit10)
Data:  [0x08] [0x01]
       ^^^^   ^^^^
       14-6   CENTER|BOTTOM
Total: 2 flag bytes + 1 size byte + 1 align byte = 4 bytes
```

- [x] 1c: Create test cases for encoding/decoding round-trips

**Test Cases for StyleBuffer:**

```cpp
// 1. Empty style
TEST(StyleBuffer, EmptyStyle) {
  StyleBuffer s;
  EXPECT_EQ(s.toBase64(), "AAA=");  // Just flag bytes, all zeros
  auto decoded = StyleBuffer::fromBase64("AAA=");
  EXPECT_TRUE(decoded.isEmpty());
}

// 2. Single boolean property
TEST(StyleBuffer, BoldOnly) {
  StyleBuffer s;
  s.setBold(true);
  auto b64 = s.toBase64();
  auto decoded = StyleBuffer::fromBase64(b64);
  EXPECT_TRUE(decoded.getBold());
  EXPECT_FALSE(decoded.hasItalic());
}

// 3. Boolean with false value (presence matters, not value)
TEST(StyleBuffer, BoldFalseExplicit) {
  StyleBuffer s;
  s.setBold(false);  // Explicitly set to false
  EXPECT_TRUE(s.hasBold());  // Flag IS set
  EXPECT_FALSE(s.getBold()); // But value is false
}

// 4. Single color
TEST(StyleBuffer, BgColorOnly) {
  StyleBuffer s;
  s.setBgColor(0xFB, 0xBF, 0x24);  // #FBBF24
  auto decoded = StyleBuffer::fromBase64(s.toBase64());
  uint8_t r, g, b;
  decoded.getBgColor(r, g, b);
  EXPECT_EQ(r, 0xFB);
  EXPECT_EQ(g, 0xBF);
  EXPECT_EQ(b, 0x24);
}

// 5. Font size edge cases
TEST(StyleBuffer, FontSizeRange) {
  StyleBuffer s1, s2, s3;
  s1.setFontSize(6);   // Minimum
  s2.setFontSize(11);  // Default
  s3.setFontSize(72);  // Max practical

  EXPECT_EQ(StyleBuffer::fromBase64(s1.toBase64()).getFontSize(), 6);
  EXPECT_EQ(StyleBuffer::fromBase64(s2.toBase64()).getFontSize(), 11);
  EXPECT_EQ(StyleBuffer::fromBase64(s3.toBase64()).getFontSize(), 72);
}

// 6. Font family with special characters
TEST(StyleBuffer, FontFamilyUnicode) {
  StyleBuffer s;
  s.setFontFamily("Arial Unicode™");
  auto decoded = StyleBuffer::fromBase64(s.toBase64());
  EXPECT_EQ(decoded.getFontFamily(), "Arial Unicode™");
}

// 7. All alignments
TEST(StyleBuffer, AllAlignments) {
  for (auto h : {TextAlign::LEFT, TextAlign::CENTER, TextAlign::RIGHT,
                 TextAlign::JUSTIFY, TextAlign::GENERAL}) {
    for (auto v : {VerticalAlign::TOP, VerticalAlign::MIDDLE, VerticalAlign::BOTTOM}) {
      StyleBuffer s;
      s.setHAlign(h);
      s.setVAlign(v);
      auto decoded = StyleBuffer::fromBase64(s.toBase64());
      EXPECT_EQ(decoded.getHAlign(), h);
      EXPECT_EQ(decoded.getVAlign(), v);
    }
  }
}

// 8. Border with single side
TEST(StyleBuffer, BorderSingleSide) {
  StyleBuffer s;
  s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);
  auto decoded = StyleBuffer::fromBase64(s.toBase64());
  EXPECT_TRUE(decoded.hasBorderTop());
  EXPECT_FALSE(decoded.hasBorderRight());
  EXPECT_EQ(decoded.getBorderTopStyle(), BorderStyle::THIN);
}

// 9. Border with all sides
TEST(StyleBuffer, BorderAllSides) {
  StyleBuffer s;
  s.setBorderTop(BorderStyle::THIN, 0xFF, 0x00, 0x00);
  s.setBorderRight(BorderStyle::MEDIUM, 0x00, 0xFF, 0x00);
  s.setBorderBottom(BorderStyle::THICK, 0x00, 0x00, 0xFF);
  s.setBorderLeft(BorderStyle::DASHED, 0xFF, 0xFF, 0x00);

  auto decoded = StyleBuffer::fromBase64(s.toBase64());
  EXPECT_EQ(decoded.getBorderTopStyle(), BorderStyle::THIN);
  EXPECT_EQ(decoded.getBorderRightStyle(), BorderStyle::MEDIUM);
  EXPECT_EQ(decoded.getBorderBottomStyle(), BorderStyle::THICK);
  EXPECT_EQ(decoded.getBorderLeftStyle(), BorderStyle::DASHED);
}

// 10. Complex style with multiple properties
TEST(StyleBuffer, ComplexStyle) {
  StyleBuffer s;
  s.setBold(true);
  s.setItalic(true);
  s.setBgColor(0xFF, 0xFF, 0x00);
  s.setTextColor(0x00, 0x00, 0x00);
  s.setFontSize(14);
  s.setFontFamily("Helvetica");
  s.setHAlign(TextAlign::CENTER);
  s.setVAlign(VerticalAlign::MIDDLE);
  s.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);

  auto decoded = StyleBuffer::fromBase64(s.toBase64());
  EXPECT_TRUE(decoded.getBold());
  EXPECT_TRUE(decoded.getItalic());
  EXPECT_FALSE(decoded.hasUnderline());
  EXPECT_EQ(decoded.getFontSize(), 14);
  EXPECT_EQ(decoded.getFontFamily(), "Helvetica");
  EXPECT_EQ(decoded.getHAlign(), TextAlign::CENTER);
  EXPECT_TRUE(decoded.hasBorderTop());
  EXPECT_FALSE(decoded.hasBorderBottom());
}

// 11. Determinism: same input = same output
TEST(StyleBuffer, Deterministic) {
  StyleBuffer s1, s2;
  s1.setBold(true);
  s1.setBgColor(0xFB, 0xBF, 0x24);

  s2.setBgColor(0xFB, 0xBF, 0x24);  // Different order
  s2.setBold(true);

  EXPECT_EQ(s1.toBase64(), s2.toBase64());  // Same result regardless of order
}

// 12. Identity: style content IS its identity
TEST(StyleBuffer, ContentIdentity) {
  StyleBuffer s1, s2;
  s1.setBold(true);
  s2.setBold(true);

  // Same content = same base64 = same identity
  EXPECT_EQ(s1.toBase64(), s2.toBase64());

  StyleBuffer s3;
  s3.setBold(false);  // Explicit false is different from no bold

  StyleBuffer s4;  // No bold set at all

  EXPECT_NE(s3.toBase64(), s4.toBase64());
}
```

- [x] 1d: Decide on base64 variant (standard vs URL-safe)

**Decision: Standard Base64 (RFC 4648)**

Rationale:
- Style data is never used in URLs directly (it's embedded in CRDT operations which are already JSON-encoded)
- Standard base64 is more widely supported and recognized
- No special URL encoding needed since styles are transported in JSON string fields
- Padding (`=`) is fine since we're not concatenating or splitting

Implementation: Use standard base64 alphabet `A-Za-z0-9+/` with `=` padding.

Alternative considered: URL-safe base64 (`-_` instead of `+/`) was rejected because:
- Adds complexity without benefit for our use case
- Would require custom encoder since many libs default to standard

### Phase 2: Implement StyleBuffer Class

Create a new `StyleBuffer` class that represents styles as binary data.

- [ ] 2a: Create `style_buffer.h` with StyleBuffer struct and flag constants
- [ ] 2b: Implement property setters (setBold, setBgColor, etc.) that update flags and data
- [ ] 2c: Implement property getters that read from binary data based on flags
- [ ] 2d: Implement `toBase64()` and `fromBase64()` serialization
- [ ] 2e: Implement `toJSON()` for debugging/export and `fromJSON()` for import
- [ ] 2f: Add unit tests for all properties and edge cases

### Phase 3: Implement Style Merging

- [ ] 3a: Implement `merge(const StyleBuffer& other)` method
- [ ] 3b: Implement `hasCollision(const StyleBuffer& other)` method
- [ ] 3c: Implement `getEffectiveStyle(col, row, ranges, cell)` helper
- [ ] 3d: Add unit tests for merging scenarios

### Phase 4: Integrate with Range System

Update ranges to use StyleBuffer instead of style_id reference.

- [ ] 4a: Add `StyleBuffer` field to Range struct (replace style_id reference)
- [ ] 4b: Update `setRangeStyle()` to accept StyleBuffer
- [ ] 4c: Update range serialization to use base64 style
- [ ] 4d: Update `getViewportStyles()` to compute effective styles using merging

### Phase 5: Update CRDT Operations

- [ ] 5a: Modify `RANGE_SET_STYLE` payload to contain base64 style instead of style_id
- [ ] 5b: Update `applyRangeSetStyle()` to parse base64 style
- [ ] 5c: Remove `STYLE_DEFINE` operation type (or deprecate)
- [ ] 5d: Update bootstrap to not emit STYLE_DEFINE operations
- [ ] 5e: Add backward compatibility: parse old format during transition

### Phase 6: Update Bindings and TypeScript

- [ ] 6a: Update `setRangeStyle()` binding to accept style properties directly
- [ ] 6b: Remove `defineStyle()` binding (no longer needed)
- [ ] 6c: Update TypeScript types for new style format
- [ ] 6d: Update style toolbar to work with new system

### Phase 7: Remove Old Style System

- [ ] 7a: Remove StyleRegistry class
- [ ] 7b: Remove style_id from workbook model
- [ ] 7c: Remove STYLE_DEFINE from operation types
- [ ] 7d: Clean up any remaining style ID references

### Phase 8: File Format Migration

- [ ] 8a: Update ZCD serializer to write new style format
- [ ] 8b: Add ZCD deserializer support for both old and new formats
- [ ] 8c: Update XLSX import to create StyleBuffer directly
- [ ] 8d: Update XLSX export to read from StyleBuffer

### Phase 9: Testing and Validation

- [ ] 9a: Run all existing style-related E2E tests
- [ ] 9b: Add new E2E tests for style sync scenarios
- [ ] 9c: Test collaboration with mixed old/new clients (if needed)
- [ ] 9d: Performance benchmarks for style merging

### Phase 10: Documentation and Cleanup

- [ ] 10a: Update architecture documentation
- [ ] 10b: Remove debug logging added during development
- [ ] 10c: Final code review and cleanup

## File Changes Summary

**New files:**
- `core/cells/style_buffer.h` - StyleBuffer class definition
- `core/cells/style_buffer.cc` - StyleBuffer implementation
- `core/cells/style_buffer_test.cc` - Unit tests

**Modified files:**
- `core/cells/model.h` - Remove StyleRegistry, update Range
- `core/cells/range.h` - Add StyleBuffer field
- `core/cells/crdt_range.cc` - Update RANGE_SET_STYLE handling
- `core/cells/crdt.cc` - Remove STYLE_DEFINE handling
- `core/cells/operation.h` - Remove STYLE_DEFINE type
- `core/cells/serializer.cc` - Update ZCD format
- `apps/wasm/bindings_format.cc` - Update style bindings
- `apps/wasm/src/types.ts` - Update TypeScript types

**Removed files:**
- `core/cells/style_registry.h` (eventually)
- `core/cells/style_registry.cc` (eventually)

## Benefits Summary

1. **No more sync bugs**: Style identity is deterministic from content
2. **Simpler CRDT**: One operation instead of two
3. **Efficient merging**: O(1) flag-based property detection
4. **Compact storage**: Binary encoding vs JSON
5. **Easy collision detection**: Flag AND operation
6. **Future-proof**: Extensible flag system for 40+ properties
7. **No dangling references**: Style data is always present with the operation

## Risks and Mitigations

1. **Migration complexity**: Mitigate with backward-compatible parsing
2. **Binary format versioning**: Reserve bits for version indicator if needed
3. **Large styles (many properties)**: Still smaller than JSON; can compress if needed
4. **Debugging difficulty**: Keep `toJSON()` for human-readable output

## Design Decisions

1. **Style inheritance**: Computed live at display time only. Cells don't store inherited properties - they inherit from col/row/range when rendering. Moving cells may absorb container styles as a future action-based feature.

2. **Custom fonts**: Font family name stored directly in binary style (length-prefixed string). No predefined font index needed.

3. **Number formats**: Keep separate for now. FORMAT_DEFINE operations have the same sync issue but format codes are variable-length strings. Store format_id reference in binary style for now; content-address formats in a future iteration.
