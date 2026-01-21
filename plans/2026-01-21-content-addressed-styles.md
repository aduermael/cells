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

- [ ] 1a: Document complete flag layout for all supported properties (current + Excel compatibility)
- [ ] 1b: Define property encoding for each type (colors, fonts, alignments, borders)
- [ ] 1c: Create test cases for encoding/decoding round-trips
- [ ] 1d: Decide on base64 variant (standard vs URL-safe)

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
