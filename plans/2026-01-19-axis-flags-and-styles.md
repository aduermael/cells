# Plan: Axis Flags Refactoring & Cross-Sheet Style Fixes

This plan addresses two related issues:
1. **Axis struct optimization**: Move `defaultStyleId` out of the Axis struct, using a workbook-level map (like cells), and introduce an `AxisFlags` byte combining `isColumn`, `hidden`, `hasStyle` and future flags.
2. **Cross-sheet style bug**: Fix the bug where applying background colors to ranges only works on the first sheet (functions use `_activeSheetIndex` without allowing explicit sheet specification).

## Current State

### Axis Struct (model.h:322-339)
```cpp
struct Axis {
    std::string name;         // Custom name (empty = compute from position)
    ID id;                    // Unique identifier (8-char base62)
    ID sheetId;               // ID of the sheet this axis belongs to
    uint32_t position;        // Visual position (0-indexed)
    uint32_t size;            // Width (column) or height (row) in pixels
    bool isColumn;            // true = column (x), false = row (y)
    bool hidden{false};       // Whether axis is hidden (default: false)
    ID defaultStyleId;        // Default style for cells in this axis
};
```

### Cell Pattern for Reference (model.h:180-181)
Cells store format/style IDs externally:
```cpp
// Note: formatId and styleId are stored at the Workbook level (see Workbook::_cellFormats,
// _cellStyles) to save memory - most cells don't have custom formats/styles.
```
With flags (model.h:148-157):
```cpp
enum class CellFlags : uint8_t {
    HAS_FORMAT = 1 << 4,  // bit 4: cell has custom format in workbook map
    HAS_STYLE = 1 << 5,   // bit 5: cell has custom style in workbook map
};
```

### Cross-Sheet Style Bug (bindings_format.cc:1451-1460)
```cpp
std::string CellsEngine::setRangeStyle(...) {
    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);  // Always uses active sheet!
    // ...
}
```

---

## Phase 1: Add AxisFlags enum and migrate bool fields

Create `AxisFlags` enum combining `isColumn`, `hidden`, and reserving bits for `hasStyle` and future use.

- [ ] 1a: Add `AxisFlags` enum in model.h with bits: `IS_COLUMN`, `HIDDEN`, `HAS_STYLE`, and operators
- [ ] 1b: Replace `bool isColumn` and `bool hidden` in Axis struct with `AxisFlags _flags`
- [ ] 1c: Add inline accessors: `isColumn()`, `hidden()`, `setHidden()`, `hasStyle()`, `setHasStyle()`
- [ ] 1d: Update all call sites reading/writing `isColumn` and `hidden` to use new accessors
- [ ] 1e: Update serializer (parser.cc, serializer.cc) to serialize flags byte
- [ ] 1f: Run tests to verify no regressions

## Phase 2: Move defaultStyleId to workbook-level map

Follow the same pattern as cells: store axis styles in a workbook-level `unordered_map<ID, ID>`.

- [ ] 2a: Add `_axisStyles: unordered_map<ID, ID>` to Workbook, with `getAxisStyleId()`, `setAxisStyleId()`, `clearAxisStyle()` methods
- [ ] 2b: Remove `defaultStyleId` field from Axis struct
- [ ] 2c: Update `HAS_STYLE` flag to be set/cleared when axis style is added/removed
- [ ] 2d: Update CRDT operations (crdt_axis.cc - AXIS_SET_STYLE) to use workbook map + flag
- [ ] 2e: Update XLSX reader/writer (xlsx_reader.cc, xlsx_writer.cc) to use new pattern
- [ ] 2f: Update bindings_viewport.cc style resolution to use new accessors
- [ ] 2g: Update parser/serializer to handle axis styles via workbook map
- [ ] 2h: Run tests to verify no regressions

## Phase 3: Fix cross-sheet style application

Add sheet ID parameter to style functions so styles can be applied to non-active sheets.

- [ ] 3a: Add `sheetId` parameter to `setRangeStyle()` in CellsEngine (optional, defaults to active sheet)
- [ ] 3b: Update TypeScript client/worker to pass sheet ID from selection state
- [ ] 3c: Add `sheetId` parameter to related functions: `removeRangeStyle()`, `setCellStyleAt()`, `getCellStyleAt()`
- [ ] 3d: Add `sheetId` parameter to effective style functions: `getEffectiveCellStyle()`, `getEffectiveStyleForRange()`
- [ ] 3e: Add E2E test for applying background color to range on non-active sheet
- [ ] 3f: Run full test suite

---

## Notes

- Phase 1 and 2 can be done independently of Phase 3 but all three contribute to cleaner style handling
- The `AxisFlags` pattern mirrors `CellFlags` for consistency
- Serialization format will change: need to handle backward compatibility (empty flags = defaults)
- The cross-sheet bug fix requires both C++ and TypeScript changes
