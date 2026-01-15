# Range System Design

A unified Range primitive for all range-based operations in the spreadsheet CRDT.

## Problem Statement

The spreadsheet needs to handle various range-based operations:
- Merged cells
- Background colors / styles applied to ranges
- Borders
- Conditional formatting
- Data validation
- Named ranges
- Print areas
- Filters

Currently, each is implemented separately (e.g., MergeRange with anchor + span). This leads to:
- Duplicate code for range containment, corner deletion, etc.
- Inefficient storage (cell-by-cell styles)
- No unified indexing
- Inconsistent CRDT handling

## Chosen Design: Range as First-Class Primitive

### Core Structure

```cpp
// Bitmask flags - a range can have multiple purposes
enum RangeFlags : uint8_t {
    RANGE_MERGE             = 1 << 0,  // Cells are merged
    RANGE_STYLE             = 1 << 1,  // Has style metadata
    RANGE_CONDITIONAL_FORMAT = 1 << 2,  // Has conditional format rules
    RANGE_DATA_VALIDATION   = 1 << 3,  // Has validation rules
    RANGE_NAMED             = 1 << 4,  // Is a named range
    RANGE_PRINT_AREA        = 1 << 5,  // Defines print area
    RANGE_FILTER            = 1 << 6,  // Has auto-filter
    // 1 bit reserved
};

struct Range {
    ID id;            // Range's own UUID (for CRDT operations)
    ID startColId;    // Column UUID for left edge
    ID startRowId;    // Row UUID for top edge
    ID endColId;      // Column UUID for right edge
    ID endRowId;      // Row UUID for bottom edge
    uint8_t flags;    // Bitmask of RangeFlags
};
```

**Key insight:** A single range can serve multiple purposes. For example:
- Merged cells with red background: `flags = RANGE_MERGE | RANGE_STYLE`
- The Range struct stays small (just coordinates + flags)

### Metadata Storage

Metadata stored in separate hash maps, keyed by range UUID:

```cpp
// Only populated if RANGE_STYLE flag is set
std::unordered_map<ID, CellStyle> rangeStyles;

// Only populated if RANGE_CONDITIONAL_FORMAT flag is set
std::unordered_map<ID, ConditionalFormatRules> rangeConditionalFormats;

// Only populated if RANGE_DATA_VALIDATION flag is set
std::unordered_map<ID, DataValidationRules> rangeDataValidation;

// Only populated if RANGE_NAMED flag is set
std::unordered_map<ID, std::string> rangeNames;
```

**Benefits:**
- Range struct is compact (5 UUIDs + 1 byte = ~81 bytes)
- No wasted space for ranges without metadata
- Easy to extend with new metadata types

### Column/Row UUID Corners

Ranges reference column and row UUIDs, not positions or cell UUIDs:

```
Range "A1:C3" = {
    startColId: colA_UUID,
    startRowId: row1_UUID,
    endColId: colC_UUID,
    endRowId: row3_UUID
}
```

**Why this works:**

1. **Column insertion expands ranges automatically:**
   - Insert column B' between B and C
   - Range A1:C3 still spans colA to colC
   - B' is automatically included (matches Excel behavior)

2. **Cell movement is clean:**
   - Cell at B2 moved to E5
   - Cell leaves the range, loses range styles
   - Range unchanged - no "hole" to track

3. **Corner deletion shrinks range:**
   - Delete colC from range (colA:colC)
   - Range shrinks to (colA:colB)
   - If all columns deleted, range becomes invalid/deleted

### R-tree Spatial Index

Single R-tree index for ALL ranges:

```cpp
// Each range is a rectangle in (col_position, row_position) space
RTree<Range*, double, 2> rangeIndex;

// Query: find all ranges containing cell at position (col, row)
std::vector<Range*> findRangesAt(uint32_t colPos, uint32_t rowPos);

// Query: find all ranges of specific type(s)
std::vector<Range*> findRangesAt(uint32_t colPos, uint32_t rowPos, uint8_t flagMask);
```

**Index maintenance:**
- Insert/remove ranges: O(log n)
- Query ranges at position: O(log n + k) where k = results
- Column/row position changes: update affected range rectangles

### Style Inheritance (CSS-like)

When rendering a cell, compute effective style:

```
effectiveStyle = defaultStyle
for range in rangesContainingCell (ordered by creation time or priority):
    if range.flags & RANGE_STYLE:
        effectiveStyle = merge(effectiveStyle, rangeStyles[range.id])
effectiveStyle = merge(effectiveStyle, cell.style)  // Cell overrides win
```

### CRDT Operations

```cpp
// Add a new range
Operation makeAddRangeOp(Range range);

// Remove a range by UUID
Operation makeRemoveRangeOp(ID rangeId);

// Update range corners (e.g., resize)
Operation makeUpdateRangeCornersOp(ID rangeId, ID newStartCol, ID newStartRow, ID newEndCol, ID newEndRow);

// Update range flags
Operation makeUpdateRangeFlagsOp(ID rangeId, uint8_t newFlags);

// Update range metadata (style, conditional format, etc.)
Operation makeUpdateRangeMetadataOp(ID rangeId, MetadataType type, Metadata data);
```

### Merge Semantics

For ranges with `RANGE_MERGE` flag:
- Anchor cell is at (startCol, startRow) intersection
- Anchor cell displays content and is editable
- Other cells in range are visually hidden/absorbed
- Moving cells out of merge: disallowed (must unmerge first)
- "Moving merged cells" = move content + delete old merge + create new merge

## Design Questions to Resolve

1. **Overlapping style ranges:** How to handle precedence? Creation order? Explicit priority field?

2. **Range resize on corner deletion:** If colC deleted from (colA:colC), should range shrink to colB or stay "invalid" until colC is restored?

3. **CRDT conflicts:** Two users create overlapping merges simultaneously - how to resolve?

4. **Named ranges:** Are these just ranges with RANGE_NAMED flag, or a separate concept?

5. **Column/row-wide ranges:** Special case where startCol=endCol (single column) or entire column (startRow=null, endRow=null)?

## Implementation Phases

### Phase A: Core Range Infrastructure
- [x] A1: Create Range struct with UUID corners and flags - Created `range.h` with Range struct using 5 UUIDs (id, startColId, startRowId, endColId, endRowId) + flags byte
- [x] A2: Create RangeFlags enum/bitmask - 7 flags: MERGE, STYLE, CONDITIONAL_FORMAT, DATA_VALIDATION, NAMED, PRINT_AREA, FILTER
- [x] A3: Implement range containment check - Added `rangeContainsPosition()`, `rangeIsAnchorPosition()`, and `rangesOverlap()` helper functions
- [x] A4: Add R-tree index for range lookup - Created `RangeIndex` class wrapping R-tree for O(log n + k) spatial queries with flag filtering
- [x] A5: Implement corner deletion (range shrinking) - Added `adjustRangeForColumnDeletion()` and `adjustRangeForRowDeletion()` template functions

### Phase B: CRDT Operations
- [x] B1: Design AddRange operation - Added RANGE_ADD OpType, `makeRangeAddOp()`, and `applyRangeAdd()` in crdt_range.cc
- [x] B2: Design RemoveRange operation - Added RANGE_REMOVE OpType, `makeRangeRemoveOp()`, and `applyRangeRemove()` (idempotent deletion)
- [x] B3: Design UpdateRangeCorners operation - Added RANGE_UPDATE_CORNERS OpType for resizing; resurrects deleted ranges (no data loss)
- [x] B4: Design UpdateRangeFlags operation - Added RANGE_UPDATE_FLAGS OpType for changing range purposes
- [x] B5: Design UpdateRangeMetadata operation - Added RANGE_SET_STYLE OpType for style association
- [x] B6: Handle CRDT conflicts for overlapping ranges - LWW for same ID; overlapping ranges coexist (UI resolves display conflicts)

### Phase C: Migrate Existing Features
- [x] C1: Migrate MergeRange to Range (flags = RANGE_MERGE) - Updated `addMergeRange`/`removeMergeRange` in bindings_core.cc to use CRDT operations; updated XLSX reader/writer to use Range system; updated viewport query to use `getRangesAt()` for merge detection
- [x] C2: Update merge UI to use new Range system - No changes needed; viewport query provides same JSON fields (isMergeAnchor, isMergedCell, mergeColSpan, mergeRowSpan, mergeAnchorCol, mergeAnchorRow) that UI already consumes

### Phase D: Viewport & Rendering
- [x] D1: Update viewport query to find ranges for each cell - Done via C1; viewport uses `getRangesAt()` for MERGE flag queries
- [x] D2: Implement style inheritance from ranges - Done via F3; `getEffectiveStyle()` queries RANGE_STYLE ranges (range-to-styleId mapping)
- [x] D3: Update merge rendering - Done via C1/C2; merge info included in viewport JSON
- [x] D4: Optimize for large numbers of ranges - Added flag-specific R-tree indices (`_mergeTrees`, `_styleTrees`) so queries with MERGE or STYLE flag use dedicated trees directly instead of filtering all ranges; eliminates O(n) post-query filtering for viewport rendering

### Phase E: Advanced Features
- [ ] E1: Conditional formatting ranges - Deferred
- [ ] E2: Data validation ranges - Deferred
- [x] E3: Named ranges - Already implemented via NamedRangeRegistry with full CRDT support; kept separate from Range system since it uses cell UUIDs for corners (appropriate for formula references)
- [x] E4: Column/row-wide styling - Already implemented via `Axis::defaultStyleId` with `AXIS_SET_STYLE` CRDT operation; more efficient than Range-based approach (O(1) vs R-tree query); Luau API `setColumnStyle()`/`setRowStyle()` available

### Phase F: Range-Based Styles (Integration Test)
- [x] F1: Add range-to-styleId mapping in Sheet - Added `_rangeStyles` map (rangeId → styleId), `getRangeStyleId()`/`setRangeStyleId()` methods, and cleanup in `removeRange()`/`clearAllRanges()`
- [x] F2: Update RANGE_SET_STYLE to store style association - `applyRangeSetStyle()` now calls `sheet->setRangeStyleId()` instead of just setting the flag
- [x] F3: Update viewport to apply range styles (CSS-like inheritance) - Extended `getEffectiveStyle()` to query RANGE_STYLE ranges between cell and column priorities; added `fromRange` field and `"inheritedFrom":"range"` JSON output
- [x] F4: Add UI for applying styles to ranges - Added `setRangeStyle()`/`removeRangeStyle()` bindings in C++, TypeScript, and client; updated `setStyleForRange()` to use Range system

### Phase G: Range Serialization/Parsing
Add ZCD format support for the Range system, enabling persistence of range-based styles and other range features.

- [x] G1: Add Range serialization to ZCD format - `RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [sty:<styleId>]`
- [x] G2: Add Range parsing from ZCD format - Parses RG lines and recreates Range objects with style associations

### Phase H: UI Range Style Integration
The toolbar still applies styles cell-by-cell via `setCellStyleAt()`. To use range-based styling for empty cells, the frontend renderer needs to be updated to query and render style ranges (currently it only renders styles from existing cell objects).

- [x] H1: Update viewport query to return style ranges covering the visible area - Added `styleRanges` array to viewport JSON with range bounds and styles
- [x] H2: Update GridRenderer to render backgrounds for style ranges (not just cells) - Added `_drawStyleRangeBackgrounds()` method called before cell backgrounds
- [x] H3: Update StyleControls.applyStyleToSelection() to use setStyleForRange() - Multi-cell selections now use Range system
- [x] H4: Add E2E test verifying range styles render correctly and don't create empty cell entries - Created `range-styles.test.mjs` with 4 tests: verifies styleRanges in viewport data, background rendering for empty cells, no wasteful cell object creation, and proper range bounds

### Phase I: Range Modification Behaviors
Complex behaviors for how ranges interact with cell operations and each other.

- [x] I1: Range edge adjustment on column/row deletion - When deleting a column/row that is a range's corner, shrink the range to the adjacent column/row; if the range becomes invalid (single-col/row), remove it. Integrated `adjustRangeForColumnDeletion` and `adjustRangeForRowDeletion` into `applyColDelete`/`applyRowDelete` CRDT operations. Added 7 unit tests in `crdt_test.cc`.
- [x] I2: Range style clears cell styles - Added `stripMatchingStyleProperties()` helper and integrated into `setRangeStyle()` in bindings_format.cc. When applying a range style, cells within the range have matching style properties cleared (or entire cell style removed if all properties match). This avoids redundant cell-level styles.
- [x] I3: Overlapping ranges combine styles - Added `mergeStyles()` helper in bindings_viewport.cc and updated `getEffectiveStyle()` to combine styles from all overlapping ranges. When a cell is covered by multiple style ranges, properties from all ranges are merged (first range's non-default properties win, subsequent ranges fill in missing properties).
- [x] I4: E2E tests for range modification behaviors - Added 3 tests to `range-styles.test.mjs`: "Range style provides correct rendering" (I2), "Overlapping ranges combine styles" (I3), "Range creation and rendering works" (I1). Also updated `deleteColumnById`/`deleteRowById` to use CRDT operations so range adjustment is triggered.

**Phase I Complete** - All range modification behaviors implemented with passing tests.

## Testing Strategy

- Unit tests for range containment with various corner positions
- Unit tests for R-tree queries
- Unit tests for corner deletion shrinking
- CRDT conflict resolution tests
- E2E tests for merge with column insertion
- E2E tests for style ranges
- Performance tests with 10k+ ranges

## References

- Current MergeRange implementation: `core/cells/model.h`
- R-tree library options: boost::geometry, custom implementation
- CRDT operation framework: `core/cells/operation.h`
