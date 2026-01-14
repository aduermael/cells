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
- [ ] B1: Design AddRange operation
- [ ] B2: Design RemoveRange operation
- [ ] B3: Design UpdateRangeCorners operation
- [ ] B4: Design UpdateRangeFlags operation
- [ ] B5: Design UpdateRangeMetadata operation
- [ ] B6: Handle CRDT conflicts for overlapping ranges

### Phase C: Migrate Existing Features
- [ ] C1: Migrate MergeRange to Range (flags = RANGE_MERGE)
- [ ] C2: Update merge UI to use new Range system
- [ ] C3: Migrate cell-by-cell styles to Range (flags = RANGE_STYLE)
- [ ] C4: Update style UI to create StyleRanges

### Phase D: Viewport & Rendering
- [ ] D1: Update viewport query to find ranges for each cell
- [ ] D2: Implement style inheritance from ranges
- [ ] D3: Update merge rendering
- [ ] D4: Optimize for large numbers of ranges

### Phase E: Advanced Features
- [ ] E1: Conditional formatting ranges
- [ ] E2: Data validation ranges
- [ ] E3: Named ranges
- [ ] E4: Column/row-wide styling

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
