# Cell Format Optimization - Remove formatId/styleId from Cell Struct

## Overview

Move `formatId` and `styleId` out of the Cell struct into a workbook-level hash map. This reduces the default Cell size significantly, benefiting memory usage when dealing with millions of cells where most don't have custom formats or styles.

### Current State

```cpp
struct Cell {
    ID id;             // 8 bytes
    ID colId;          // 8 bytes
    ID rowId;          // 8 bytes
    CellValue value;   // ~24 bytes (type enum + union + string capacity)
    Formula* formula;  // 8 bytes (pointer)
    ID formatId;       // 8 bytes  ← REMOVE
    ID styleId;        // 8 bytes  ← REMOVE
    uint8_t _flags;    // 1 byte (+ padding)
};
// Total: ~73 bytes + padding → ~80 bytes per cell
```

### Proposed Change

Remove `formatId` and `styleId` from Cell, storing them in a workbook-level hash map keyed by cell ID. Add a `HAS_FORMAT` flag to CellFlags for quick lookup avoidance.

**Memory savings**: 16 bytes per cell = 16MB per million cells.

### Design Decision: Merge Styles and Formats

Rather than having separate `formatId` and `styleId`, merge them into a unified **format** concept at the workbook level. A "format" encompasses both:
- Number formatting (decimals, currency, percentage, date display)
- Visual styling (bold, italic, colors, alignment, borders)

This simplifies:
- Storage: One hash map instead of two
- API: `cell.format` instead of `cell.format` + `cell.style`
- CRDT operations: One operation type instead of two

**Note**: The underlying `NumberFormat` and `CellStyle` structs remain separate internally (they serve different purposes in rendering), but cells reference a combined "Format" that may include both.

---

## Phase 1: Add HAS_FORMAT Flag to CellFlags

Add a flag to quickly check if a cell has a custom format without hash map lookup.

- [ ] 1a: Add `HAS_FORMAT = 1 << 4` to CellFlags enum in `model.h`
- [ ] 1b: Add helper methods to Cell: `hasFormat()`, `markHasFormat()`, `clearHasFormat()`
- [ ] 1c: Update CellFlags documentation comment

---

## Phase 2: Add Workbook-Level Format Storage

Create the hash map and accessors at the Workbook level.

- [ ] 2a: Add `std::unordered_map<ID, ID, IDHash> _cellFormats` to Workbook (cellId → formatId)
- [ ] 2b: Add `getCellFormatId(cellId)` method - returns formatId or null ID
- [ ] 2c: Add `setCellFormatId(cellId, formatId)` method - updates map and returns old formatId
- [ ] 2d: Add `clearCellFormat(cellId)` method - removes from map, returns true if existed
- [ ] 2e: Add same for styles: `_cellStyles` map, `getCellStyleId()`, `setCellStyleId()`, `clearCellStyle()`

---

## Phase 3: Migrate CRDT Operations

Update CRDT operation handlers to use workbook-level storage.

- [ ] 3a: Update `applyCellSetFormat()` in `crdt_cell.cc`:
  - Get cell, set HAS_FORMAT flag
  - Store formatId in workbook map instead of cell
  - Handle null formatId (clear format case)
- [ ] 3b: Update `applyCellSetStyle()` in `crdt_cell.cc`:
  - Get cell, update workbook map
  - Update StyleRegistry ref counting (already done, keep)
  - Handle null styleId (clear style case)
- [ ] 3c: Update any code reading `cell->formatId` directly to use `workbook.getCellFormatId(cell->id)`
- [ ] 3d: Update any code reading `cell->styleId` directly to use `workbook.getCellStyleId(cell->id)`

---

## Phase 4: Update Serialization

Ensure the workbook-level format map is properly saved and loaded.

- [ ] 4a: Update ZCD serializer to write cell formats from workbook map
  - Currently: cell line includes formatId/styleId inline
  - Change: emit separate format assignment lines or include in cell metadata
- [ ] 4b: Update ZCD deserializer to populate workbook map when reading cell formats
- [ ] 4c: Update XLSX reader to store formats in workbook map (not cell)
- [ ] 4d: Update XLSX writer to read formats from workbook map
- [ ] 4e: Add round-trip tests verifying format preservation

---

## Phase 5: Update WASM Bindings

Update the TypeScript/WASM bridge to use the new storage.

- [ ] 5a: Update `bindings_format.cc` - viewport cell data fetching to read from workbook map
- [ ] 5b: Update `bindings_core.cc` - any direct cell format/style access
- [ ] 5c: Update `computeEffectiveStyleAt()` to read from workbook map
- [ ] 5d: Update TypeScript `CellData` type if needed (should be unchanged - it receives formatId/styleId, just from different source)
- [ ] 5e: Run E2E tests to verify rendering unchanged

---

## Phase 6: Update Luau API

Ensure scripting API continues to work.

- [ ] 6a: Update `luaCellIndex` in `luau_cell_api.cc` for `cell.format` property - read from workbook map
- [ ] 6b: Update `luaCellNewindex` for `cell.format = ...` - write to workbook map
- [ ] 6c: Update `luaCellIndex` for `cell.style` property - read from workbook map
- [ ] 6d: Update `luaCellNewindex` for `cell.style = ...` - write to workbook map
- [ ] 6e: Update `setFormat()` and `setStyle()` functions - write to workbook map
- [ ] 6f: Run Luau unit tests

---

## Phase 7: Remove Fields from Cell Struct

Final cleanup - remove the now-unused fields.

- [ ] 7a: Remove `ID formatId` from Cell struct
- [ ] 7b: Remove `ID styleId` from Cell struct
- [ ] 7c: Update Cell constructors (no formatId/styleId params needed)
- [ ] 7d: Fix any remaining compilation errors from removed fields
- [ ] 7e: Run full test suite (unit + E2E)

---

## Phase 8: Optional - Unify Format and Style

If desired, merge the concepts further.

- [ ] 8a: Create unified `CellFormat` struct containing both NumberFormat reference and CellStyle
- [ ] 8b: Single `_cellFormats` map storing combined format
- [ ] 8c: Update all APIs to use unified format concept
- [ ] 8d: Deprecate separate `CELL_SET_FORMAT` and `CELL_SET_STYLE` ops in favor of single `CELL_SET_FORMAT` that handles both

**Note**: This phase is optional. The current dual-map approach (Phase 2-7) works well and matches how rendering treats them separately. Unification is a simplification that can be done later if desired.

---

## Technical Notes

### Memory Impact

Current Cell size with formatId + styleId: ~80 bytes
After removal: ~64 bytes
Savings: 16 bytes per cell = 16MB per million cells

### Hash Map Overhead

The workbook-level `unordered_map<ID, ID>` has overhead per entry:
- Key: 8 bytes (ID)
- Value: 8 bytes (ID)
- Hash bucket pointer: 8 bytes
- Total: ~24 bytes per cell WITH a format

Break-even point: If more than 67% of cells have custom formats, the old approach uses less memory.
In practice, typically <5% of cells have custom formats, so this is a big win.

### Lookup Performance

- Old: Direct field access O(1)
- New: Hash map lookup O(1) average, but with hash computation overhead

Mitigation: The `HAS_FORMAT` flag allows skipping the hash lookup for cells without formats (the common case). This makes the common path (no format) actually faster since we just check a bit.

### CRDT Considerations

Format/style changes are still tracked via CRDT operations. The operation payloads remain the same - only the storage location changes (workbook map vs cell field).

### Migration

No file format migration needed:
- ZCD files already serialize formats separately (F lines for format definitions)
- Cell format references will simply be stored differently at runtime
- On save, the serializer reads from the new location
- On load, the deserializer writes to the new location
