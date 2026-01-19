# Plan: Axis Flags Refactoring & Unified Style Index

This plan addresses two related issues:
1. **Axis struct optimization**: Move `defaultStyleId` out of the Axis struct, introduce an `AxisFlags` byte combining `isColumn`, `hidden`, `hasStyle` and future flags.
2. **Unified style indexing**: Rename `_cellStyles` to `_styles` as a single entity-to-style map for cells, ranges, and axes. UUIDs are unique across resource types, so one map suffices.
3. **Cross-sheet style bug**: Fix the bug where applying background colors to ranges only works on the first sheet.

## Current State

### Style Architecture
```
StyleRegistry (already exists with deduplication):
  - _styles: map<styleId, CellStyle>     // Style objects
  - _hashToId: map<hash, styleId>        // Props → ID deduplication
  - _refCount: map<styleId, count>       // Reference counting

Workbook:
  - _cellStyles: map<cellId, styleId>    // Cell → style references (to be renamed)
```

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
    ID defaultStyleId;        // Default style for cells in this axis (TO BE REMOVED)
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

- [x] 1a: Add `AxisFlags` enum in model.h with bits: `IS_COLUMN`, `HIDDEN`, `HAS_STYLE`, and operators
- [x] 1b: Replace `bool isColumn` and `bool hidden` in Axis struct with `AxisFlags _flags`
- [x] 1c: Add inline accessors: `isColumn()`, `hidden()`, `setHidden()`, `hasStyle()`, `setHasStyle()`, `setIsColumn()`
- [x] 1d: Update all call sites reading/writing `isColumn` and `hidden` to use new accessors
- [x] 1e: Update serializer (parser.cc, serializer.cc) to use accessor methods
- [x] 1f: Run tests to verify no regressions

## Phase 2: Unify style index and move axis styles

Rename `_cellStyles` to `_styles` as a unified entity-to-style map. Since UUIDs are unique across resource types (cells, ranges, axes), one map can serve all.

- [ ] 2a: Rename `_cellStyles` to `_styles` in Workbook
- [ ] 2b: Rename accessor methods: `getCellStyleId()` → `getStyleId()`, `setCellStyleId()` → `setStyleId()`, `clearCellStyle()` → `clearStyle()`
- [ ] 2c: Update all call sites to use the new method names
- [ ] 2d: Remove `defaultStyleId` field from Axis struct
- [ ] 2e: Update `HAS_STYLE` flag in AxisFlags to be set/cleared when axis style is added/removed via `_styles`
- [ ] 2f: Update CRDT operations (crdt_axis.cc - AXIS_SET_STYLE) to use `_styles` map + flag
- [ ] 2g: Update XLSX reader/writer (xlsx_reader.cc, xlsx_writer.cc) to use new pattern
- [ ] 2h: Update bindings_viewport.cc style resolution to use `getStyleId(axisId)`
- [ ] 2i: Update parser/serializer to handle axis styles via unified `_styles` map
- [ ] 2j: Run tests to verify no regressions

## Phase 3: Fix cross-sheet style application

Add sheet ID parameter to style functions so styles can be applied to non-active sheets.

- [ ] 3a: Add `sheetId` parameter to `setRangeStyle()` in CellsEngine (optional, defaults to active sheet)
- [ ] 3b: Update TypeScript client/worker to pass sheet ID from selection state
- [ ] 3c: Add `sheetId` parameter to related functions: `removeRangeStyle()`, `setCellStyleAt()`, `getCellStyleAt()`
- [ ] 3d: Add `sheetId` parameter to effective style functions: `getEffectiveCellStyle()`, `getEffectiveStyleForRange()`
- [ ] 3e: Add E2E test for applying background color to range on non-active sheet
- [ ] 3f: Run full test suite

---

## Architecture After This Plan

```
StyleRegistry (unchanged):
  - _styles: map<styleId, CellStyle>     // Style objects
  - _hashToId: map<hash, styleId>        // Props → ID deduplication
  - _refCount: map<styleId, count>       // Reference counting

Workbook:
  - _styles: map<entityId, styleId>      // Unified: cell/range/axis → style references

Axis:
  - _flags: AxisFlags                    // IS_COLUMN | HIDDEN | HAS_STYLE
  - (no defaultStyleId field)            // Style looked up via _styles[axisId]
```

## Notes

- Phase 1 and 2 can be done independently of Phase 3 but all three contribute to cleaner style handling
- The `AxisFlags` pattern mirrors `CellFlags` for consistency
- Serialization format will change: need to handle backward compatibility (empty flags = defaults)
- The cross-sheet bug fix requires both C++ and TypeScript changes
- The same style ID can be referenced by multiple entities (cell, axis, range) - that's intentional and enables style sharing
