# Style Defined Flags and Border UI

Status: READY
Created At: 2026-01-19

## Overview

The current style merge system treats default values (e.g., `bold=false`) as "unset", which makes it impossible for a cell to explicitly override a parent range's property back to the default value. For example, if a range sets `bold=true`, a cell inside that range cannot set `bold=false` to override it.

**Solution**: Add a `defined` bitfield (2 bytes) to `CellStyle` to track which properties have been explicitly set. The `defined` flag is the source of truth - not whether a value equals its default. Only defined properties participate in merges.

Additionally:
1. Reorder CellStyle struct fields for better memory alignment
2. Expose existing border styles (thin/medium/thick/dashed/etc.) in the UI

## Current Property Count

Properties requiring "defined" tracking:
1. bold
2. italic
3. underline
4. wrapText
5. bgColor
6. textColor
7. fontFamily
8. fontSize
9. hAlign
10. vAlign
11. border.top
12. border.right
13. border.bottom
14. border.left

**14 properties** = need 14 bits, so 2 bytes (uint16_t) suffices.

## Current CellStyle Layout (before optimization)

```cpp
struct CellStyle {
    bool bold{false};            // 1 byte
    bool italic{false};          // 1 byte
    bool underline{false};       // 1 byte
    bool wrapText{false};        // 1 byte
    std::string bgColor;         // 24-32 bytes (SSO dependent)
    std::string textColor;       // 24-32 bytes
    std::string fontFamily;      // 24-32 bytes
    uint8_t fontSize{0};         // 1 byte + 7 padding
    TextAlign hAlign{GENERAL};   // 1 byte
    VerticalAlign vAlign{BOTTOM}; // 1 byte + padding
    CellBorder border;           // ~200+ bytes (4 strings + 4 enums)
};
```

## Proposed CellStyle Layout (optimized)

```cpp
struct CellStyle {
    // Group fixed-size fields together for better packing
    uint16_t defined{0};              // 2 bytes - bitfield for which props are set
    uint8_t fontSize{0};              // 1 byte
    TextAlign hAlign{GENERAL};        // 1 byte
    VerticalAlign vAlign{BOTTOM};     // 1 byte
    bool bold{false};                 // 1 byte
    bool italic{false};               // 1 byte
    bool underline{false};            // 1 byte
    bool wrapText{false};             // 1 byte + padding to 8-byte boundary

    // Variable-size strings (each ~24-32 bytes with SSO)
    std::string bgColor;
    std::string textColor;
    std::string fontFamily;

    // Border (nested struct)
    CellBorder border;
};
```

## Existing Border Styles (already implemented in C++)

The `BorderStyle` enum already supports all Excel border styles:
- NONE, THIN, MEDIUM, THICK
- DASHED, DOTTED, DOUBLE, HAIR
- MEDIUM_DASHED, DASH_DOT, MEDIUM_DASH_DOT
- DASH_DOT_DOT, MEDIUM_DASH_DOT_DOT, SLANT_DASH_DOT

These just need to be exposed in the UI - no C++ changes needed for border styles.

---

## Phase 1: Add Defined Flags to CellStyle

- [x] 1a: Add `defined` bitfield and constants to `style_types.h`
  - Add `uint16_t defined{0}` field to CellStyle
  - Define bit constants: `DEFINED_BOLD = 1 << 0`, `DEFINED_ITALIC = 1 << 1`, etc.
  - Add helper methods: `bool isDefined(uint16_t prop) const`, `void setDefined(uint16_t prop)`, `void clearDefined(uint16_t prop)`

- [x] 1b: Reorder CellStyle fields for better alignment
  - Move `defined`, `fontSize`, `hAlign`, `vAlign`, booleans together
  - Keep strings and border at end

- [x] 1c: Update `isEmpty()` to check `defined == 0`
  - An empty style has no defined properties (flag is source of truth)

- [x] 1d: Update `hash()` to include `defined` field
  - Hash should differentiate between defined vs undefined properties
  - Returns 0 for empty styles (defined == 0)

- [x] 1e: Update `operator==` to compare `defined` field

**Additional work done in Phase 1** (required for tests to pass):
- Updated `applyStyleDefine()` in `crdt_axis.cc` to set defined flags when parsing JSON
- Updated `luaSetStyle()` in `luau_api.cc` to set defined flags when building styles from Lua tables
- Updated `luaCellSet()` style handler in `luau_types.cc` to set defined flags
- Updated unit tests in `style_registry_test.cc` and `crdt_test.cc` to use defined flags

---

## Phase 2: Update Style Merge Logic

- [ ] 2a: Update `mergeEffectiveStyles()` in `bindings_format.cc`
  - Only merge properties where overlay has them defined
  - Example: `if ((overlay.defined & DEFINED_BOLD) && !(result.defined & DEFINED_BOLD)) { result.bold = overlay.bold; result.defined |= DEFINED_BOLD; }`

- [ ] 2b: Update style application to set defined flags
  - When user sets a property via UI, mark it as defined
  - When parsing JSON style updates, mark properties as defined

- [ ] 2c: Add unit tests for override behavior
  - Test: range has `bold=true`, cell sets `bold=false` (defined), effective style should be `bold=false`
  - Test: range has `bgColor="#FF0000"`, cell sets `bgColor=""` with DEFINED_BGCOLOR, effective should be no background

---

## Phase 3: Update Serialization

- [ ] 3a: Update `styleToJson()` to serialize defined properties
  - Serialize all properties that have their defined flag set, even if value equals default
  - Example: `bold=false` with `DEFINED_BOLD` set should serialize as `"bold": false`

- [ ] 3b: Update `jsonToStyle()` to set defined flags
  - When parsing JSON, any property present in JSON sets its defined flag

- [ ] 3c: Update ZCD serialization to preserve defined flags
  - Ensure round-trip preserves which properties are defined

- [ ] 3d: Update XLSX import to set appropriate defined flags
  - Properties explicitly set in XLSX should be marked defined

- [ ] 3e: Update testdata files if needed
  - Fix any test files that break due to serialization changes

---

## Phase 4: Expose Border Styles in UI

- [ ] 4a: Add border style selector to border dropdown
  - Show style options: Thin, Medium, Thick, Dashed, Dotted, Double
  - Group less common styles (Hair, Medium Dashed, etc.) in submenu or advanced section

- [ ] 4b: Update border button to show current style
  - Display style name or icon indicating current border style

- [ ] 4c: Update grid renderer border drawing for different styles
  - Implement dashed/dotted line patterns in canvas
  - Map style to appropriate line width (thin=1px, medium=2px, thick=3px)

---

## Phase 5: Testing and Verification

- [ ] 5a: Add unit tests for defined flags
  - Test merge with defined vs undefined properties
  - Test serialization round-trip of defined flags

- [ ] 5b: Add E2E tests for style override behavior
  - Create range style, create cell inside with override, verify effective style

- [ ] 5c: Run full test suite
  - `make test`
  - `cd apps/wasm && npm run test:parallel -- stable`

---

## Technical Notes

### Defined Flags Bit Layout

```cpp
constexpr uint16_t DEFINED_BOLD        = 1 << 0;   // bit 0
constexpr uint16_t DEFINED_ITALIC      = 1 << 1;   // bit 1
constexpr uint16_t DEFINED_UNDERLINE   = 1 << 2;   // bit 2
constexpr uint16_t DEFINED_WRAPTEXT    = 1 << 3;   // bit 3
constexpr uint16_t DEFINED_BGCOLOR     = 1 << 4;   // bit 4
constexpr uint16_t DEFINED_TEXTCOLOR   = 1 << 5;   // bit 5
constexpr uint16_t DEFINED_FONTFAMILY  = 1 << 6;   // bit 6
constexpr uint16_t DEFINED_FONTSIZE    = 1 << 7;   // bit 7
constexpr uint16_t DEFINED_HALIGN      = 1 << 8;   // bit 8
constexpr uint16_t DEFINED_VALIGN      = 1 << 9;   // bit 9
constexpr uint16_t DEFINED_BORDER_TOP  = 1 << 10;  // bit 10
constexpr uint16_t DEFINED_BORDER_RIGHT= 1 << 11;  // bit 11
constexpr uint16_t DEFINED_BORDER_BOTTOM=1 << 12;  // bit 12
constexpr uint16_t DEFINED_BORDER_LEFT = 1 << 13;  // bit 13
// bits 14-15 reserved for future use
```

### Merge Logic Example

```cpp
CellStyle mergeEffectiveStyles(const CellStyle& base, const CellStyle& overlay) {
    CellStyle result = base;

    // Only apply overlay properties that are defined and not already defined in result
    // The defined flag is the source of truth, not the value
    if ((overlay.defined & DEFINED_BOLD) && !(result.defined & DEFINED_BOLD)) {
        result.bold = overlay.bold;
        result.defined |= DEFINED_BOLD;
    }
    // ... same pattern for all properties

    return result;
}
```

### Key Principle: Defined Flag is Source of Truth

- `bold=false` with `DEFINED_BOLD=0`: property not set, inherit from parent
- `bold=false` with `DEFINED_BOLD=1`: explicitly set to false, overrides parent
- `bgColor=""` with `DEFINED_BGCOLOR=0`: no background set, inherit from parent
- `bgColor=""` with `DEFINED_BGCOLOR=1`: explicitly cleared background, overrides parent

---

Execute with: `/execute-plan plans/2026-01-19-style-defined-flags.md`
