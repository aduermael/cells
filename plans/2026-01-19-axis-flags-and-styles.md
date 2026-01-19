# Plan: Axis Flags Refactoring & Unified Style/Format Index

This plan addresses several related issues:
1. **Axis struct optimization**: Move `defaultStyleId` out of the Axis struct, introduce an `AxisFlags` byte combining `isColumn`, `hidden`, `hasStyle` and future flags.
2. **Unified style indexing**: Rename `_cellStyles` to `_styles` as a single entity-to-style map for cells, ranges, and axes. UUIDs are unique across resource types, so one map suffices.
3. **Cross-sheet style bug**: Fix the bug where applying background colors to ranges only works on the first sheet.
4. **Unified format indexing**: Similar to styles, rename `_cellFormats` to `_formats` as a unified entity-to-format map for cells, ranges, and axes.
5. **Format reference counting**: Add `FormatRegistry` with reference counting and automatic garbage collection (like `StyleRegistry` for styles).
6. **Style reference counting consistency**: Ensure style refcounting is consistently applied across all code paths.

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

- [x] 2a: Rename `_cellStyles` to `_styles` in Workbook
- [x] 2b: Rename accessor methods: `getCellStyleId()` → `getStyleId()`, `setCellStyleId()` → `setStyleId()`, `clearCellStyle()` → `clearStyle()`
- [x] 2c: Update all call sites to use the new method names
- [x] 2d: Remove `defaultStyleId` field from Axis struct - axis styles now stored in workbook._styles via axis ID
- [x] 2e: Update `HAS_STYLE` flag in AxisFlags to be set/cleared when axis style is added/removed via `_styles`
- [x] 2f: Update CRDT operations (crdt_axis.cc - AXIS_SET_STYLE) to use `_styles` map + flag
- [x] 2g: Update XLSX reader/writer (xlsx_reader.cc, xlsx_writer.cc) to use new pattern
- [x] 2h: Update bindings_viewport.cc style resolution to use `getStyleId(axisId)` and fixed hidden() accessor calls
- [x] 2i: Update parser/serializer to handle axis styles via unified `_styles` map with optional style ID output
- [x] 2j: Run tests to verify no regressions (all 55 core tests pass)

## Phase 3: Fix cross-sheet style application

The current `setRangeStyle()` takes position coordinates and always operates on the active sheet. This breaks when styling ranges on non-active sheets.

**Approach**: Add a sheet index parameter to allow explicit sheet targeting while keeping TypeScript working with positions only (no UUIDs exposed to the UI layer).

- [x] 3a: Add `setRangeStyleOnSheet(sheetIndex, startCol, startRow, endCol, endRow, styleJson)` that targets a specific sheet. Refactored `setRangeStyle()` to delegate to this function with `_activeSheetIndex`.
- [x] 3b: Update TypeScript client/worker to use `setRangeStyleOnSheet` when styling non-active sheets. Added `setRangeStyleOnSheet()` method to CellsClient and WasmDataSource. Fixed bug in C++ where axis insert operations used wrong sheet ID.
- [x] 3c: Add E2E test for applying background color to range on non-active sheet. Created `cross-sheet-style.test.mjs` with 2 tests verifying cross-sheet style application works correctly.
- [x] 3d: Run full test suite (all 188 E2E tests pass, 55 unit tests pass)

---

## Phase 4: Unify format indexing (like styles)

Currently `_cellFormats` only maps cells to format IDs. Ranges and axes can also have formats, so we need a unified `_formats` map like we did for `_styles`.

**Current State**:
```cpp
// Workbook
std::unordered_map<ID, ID, IDHash> _cellFormats;    // Cell ID → Format ID (cells only)
std::unordered_map<ID, std::string, IDHash> _customFormats;  // Format ID → Format code
```

- [x] 4a: Rename `_cellFormats` to `_formats` in Workbook (unified entity-to-format map)
- [x] 4b: Rename accessor methods: `getCellFormatId()` → `getFormatId()`, `setCellFormatId()` → `setFormatId()`, `clearCellFormat()` → `clearFormat()`
- [x] 4c: Update all call sites to use the new method names. TypeScript API keeps `getCellFormatId` for backward compatibility.
- [x] 4d: Add `HAS_FORMAT` flag to AxisFlags enum (bit 3) and hasFormat()/setHasFormat() accessors
- [x] 4e: Add AXIS_SET_FORMAT CRDT operation (OpType 53) with applyAxisSetFormat() and makeAxisSetFormatOp()
- [x] 4f: Update serializer/parser for axis formats - added fmt: property support for columns and rows
- [x] 4g: Run tests to verify no regressions (all 55 unit tests, 188 E2E tests pass)

## Phase 5: Add FormatRegistry with reference counting

Formats currently have no garbage collection - `_customFormats` stores format definitions indefinitely. Add a `FormatRegistry` class similar to `StyleRegistry` with reference counting.

**Current Problem**: Custom formats accumulate without cleanup. When a format is no longer used by any entity, it should be garbage collected.

**Approach**: Create `FormatRegistry` class with:
- Content deduplication (same format code → same ID)
- Reference counting (addRef/release)
- Automatic garbage collection when refcount hits 0

- [x] 5a: Create `FormatRegistry` class in `format_registry.h/.cc` with:
  - `_formats: map<formatId, formatCode>` - Format definitions
  - `_codeToId: map<formatCode, formatId>` - Deduplication by code string
  - `_refCount: map<formatId, count>` - Reference counting
  - Methods: `registerFormat()`, `findOrRegisterFormat()`, `addRef()`, `release()`, `getFormatCode()`
- [x] 5b: Replace `_customFormats` in Workbook with `_formatRegistry` (unique_ptr<FormatRegistry>)
- [x] 5c: Update `setFormatId()` to call `addRef`/`release` on the registry (like `setRangeStyleId` does for styles). Also updated `clearFormat()` to release references.
- [x] 5d: Update CRDT operations (`CELL_SET_FORMAT`, `AXIS_SET_FORMAT`) to use registry reference counting. Both operations already call `workbook.setFormatId()` which now includes addRef/release logic.
- [x] 5e: Update serializer/parser to work with FormatRegistry. Already compatible - serializer uses getCustomFormats() which delegates to registry, parser uses registerCustomFormat() which delegates to registry.
- [x] 5f: Add unit tests for FormatRegistry (reference counting, deduplication, GC). Created format_registry_test.cc with 17 tests covering all functionality.
- [x] 5g: Run full test suite (56 unit tests pass, 188 E2E tests pass)

## Phase 6: Ensure style reference counting is consistent

Currently style reference counting is handled differently in different places:
- `setRangeStyleId()` includes addRef/release calls internally
- `setStyleId()` does NOT include addRef/release (relies on CRDT ops)
- CRDT operations (crdt_cell.cc, crdt_axis.cc) call addRef/release

This inconsistency could lead to bugs. Consider unifying the approach.

- [ ] 6a: Audit all places where styles are set/cleared and verify refcounting is correct
- [ ] 6b: Option A: Move addRef/release into `setStyleId()` (like `setRangeStyleId()`)
- [ ] 6c: Option B: Keep addRef/release in CRDT ops but document clearly
- [ ] 6d: Add assertions or debug logging to detect refcount mismatches
- [ ] 6e: Run full test suite with refcount validation

---

## Architecture After This Plan

```
StyleRegistry (already exists):
  - _styles: map<styleId, CellStyle>     // Style objects
  - _hashToId: map<hash, styleId>        // Props → ID deduplication
  - _refCount: map<styleId, count>       // Reference counting

FormatRegistry (NEW - Phase 5):
  - _formats: map<formatId, formatCode>  // Format code strings
  - _codeToId: map<code, formatId>       // Code → ID deduplication
  - _refCount: map<formatId, count>      // Reference counting

Workbook:
  - _styles: map<entityId, styleId>      // Unified: cell/range/axis → style
  - _formats: map<entityId, formatId>    // Unified: cell/range/axis → format
  - _styleRegistry: StyleRegistry*       // Style definitions with refcount
  - _formatRegistry: FormatRegistry*     // Format definitions with refcount

Axis:
  - _flags: AxisFlags                    // IS_COLUMN | HIDDEN | HAS_STYLE | HAS_FORMAT
  - (no defaultStyleId field)            // Style looked up via _styles[axisId]
  - (no defaultFormatId field)           // Format looked up via _formats[axisId]
```

## Notes

- Phase 1-3 are complete
- Phase 4 mirrors Phase 2 but for formats instead of styles
- Phase 5 mirrors StyleRegistry but for formats
- Phase 6 addresses potential refcount inconsistencies discovered during analysis
- Reference counting enables automatic garbage collection of unused styles/formats
- Deduplication reduces memory when multiple entities share the same style/format
- The same style/format ID can be referenced by multiple entities - that's intentional
