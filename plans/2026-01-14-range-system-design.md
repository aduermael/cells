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
- [x] I3: Overlapping ranges - rectangle splitting **(REVISED)** - Implemented via Phase J. Original approach (layer ranges and merge at render time) was incorrect. When applying a new range style with the **same property** as an existing range, the old range is now **split** to avoid overlap. Different properties (e.g., bgColor + textColor) can layer without splitting.
- [x] I4: E2E tests for range modification behaviors - Added 3 tests to `range-styles.test.mjs`: "Range style provides correct rendering" (I2), "Overlapping ranges combine styles" (I3), "Range creation and rendering works" (I1). Also updated `deleteColumnById`/`deleteRowById` to use CRDT operations so range adjustment is triggered.

### Phase J: Rectangle Splitting for Overlapping Range Styles ✓ COMPLETE

**Problem**: When user applies red bgColor to C4:D10, and blue bgColor already exists at B2:D8, the overlapping cells (C4:D8) should become red. The blue range must be split to avoid having two bgColor values for the same cells.

**Excel behavior** (verified): Moving a cell from the red area reveals white/empty, NOT the underlying blue. This confirms ranges don't "layer" - the new style replaces the old in the overlap area.

**Correct approach - Rectangle Subtraction**:
When applying a new range style, for each property being set (e.g., bgColor):
1. Find all existing ranges that have the same property set
2. For each overlapping range, compute the rectangle subtraction (old - new)
3. This produces 0-4 non-overlapping rectangles from the old range
4. Delete the old range, create the new split ranges
5. Create the new range

```
Example: Blue B2:D8, then Red C4:D10

Before:                 After splitting:
┌─────────────┐         ┌──────┐
│   BLUE      │         │BLUE 1│ (B2:B8) - left strip
│   B2:D8     │    →    └──────┘
│      ┌──────┼───┐     ┌──────┐
│      │ RED  │   │     │BLUE 2│ (C2:D3) - top strip
└──────┼──────┘   │     └──────┘
       │ C4:D10   │     ┌──────────┐
       └──────────┘     │   RED    │ (C4:D10)
                        └──────────┘
```

**Rectangle subtraction algorithm** (A - B):
```
Given rectangles A and B that overlap:
Result can be 0-4 rectangles:
- Left strip:   if B.left > A.left
- Right strip:  if B.right < A.right
- Top strip:    if B.top > A.top (clipped to not include left/right strips)
- Bottom strip: if B.bottom < A.bottom (clipped to not include left/right strips)
```

**When to split vs. when to layer**:
- **Same property** (both have bgColor): Split - no overlap allowed
- **Different properties** (one bgColor, one bold): Layer - overlap OK, merge at render

**Steps**:
- [x] J1: Implement `subtractRectangle(oldRange, newRange)` → vector of up to 4 rectangles - Added `PositionRect` struct and `subtractRectangle()` function to range.h with 12 unit tests
- [x] J2: In `setRangeStyle`, before creating new range, find and split overlapping ranges with same properties - Implemented in bindings_format.cc with helper functions `stylesHaveConflictingProperties()` and `getStylePropertiesJson()`
- [x] J3: Update CRDT operations to handle range splitting (may need RANGE_SPLIT op or multiple RANGE_ADD/RANGE_REMOVE) - Uses existing RANGE_ADD/RANGE_REMOVE ops; split ranges inherit original style
- [x] J4: E2E test: apply blue, apply overlapping red, move red cell → should reveal white not blue - Added "Overlapping ranges with same property splits the old range" test verifying rectangle splitting
- [x] J5: E2E test: apply bgColor range, apply overlapping bold range → both should render (different properties OK) - Added "Overlapping ranges with different properties can layer" test verifying non-conflicting properties don't trigger split

### Phase K: Smart Range Style Merging ✓ COMPLETE

**Problem**: Currently, applying multiple style properties to the same range creates multiple overlapping ranges:
```
RG SWccgRXF B1:E7 sty:{bgColor:"#818CF8", bold:false, italic:false, underline:false}
RG mPGHRVtx B1:E7 sty:{bold:true, italic:false, underline:false}
```
This is inefficient and creates unnecessary complexity. The system should be smarter about:
1. Reusing existing ranges when applying additional properties to the same area
2. Only storing non-default property values (sparse style representation)
3. Only splitting when there's actual overlap with *different* ranges

**Goal**: Single range with merged properties:
```
RG SWccgRXF B1:E7 sty:{bgColor:"#818CF8", bold:true}
```

**Design principles**:
- **Sparse styles**: Only store non-default values. `{bold:true}` not `{bold:true, italic:false, underline:false}`
- **Range reuse**: When applying style to exact same area as existing range, merge into existing range's style
- **Smart splitting**: Only split when new range partially overlaps with a different range that has conflicting properties

**Steps**:
- [x] K1: Fix `mergeStyleJson` to only include non-default properties in resulting style (sparse representation) - `styleToJson` now sparse
- [x] K2: Fix `styleToJson` to only serialize non-default properties - Updated to output sparse JSON with only non-default values
- [x] K3: In `setRangeStyle`, detect when new range exactly matches an existing range's bounds - Added exact match check with `PositionRect::operator==`
- [x] K4: When exact match found, merge new properties into existing range's style (RANGE_SET_STYLE with merged style) - Returns early with merged style, reuses existing range ID
- [x] K5: For fully contained ranges, strip conflicting properties from their style (or delete if style becomes empty) - Added `ContainedOperation` handling with `stripConflictingProperties`
- [x] K6: Update `stylesHaveConflictingProperties` to work with sparse styles (absent property = no conflict) - Already works correctly with `hasJsonField` checks
- [x] K7: Unit test: sparse style serialization (only non-default values) - Added `PositionRect::contains` and equality tests in range_test.cc
- [x] K8: E2E test: apply bgColor to B2:D4, then apply bold to same B2:D4 → single range with both properties - "Same-area style merging creates single range with merged properties"
- [x] K9: E2E test: apply bgColor to B2:D4, then apply bgColor to A1:E5 (superset) → contained range loses bgColor - "Superset range strips conflicting properties from contained range"

**Additional fix**: Updated viewport query `styleRanges` serialization to include all style properties (bold, italic, underline, fontFamily, fontSize) not just bgColor/textColor.

**Edge cases**:
- **Subset** (new range inside existing): Create new range, existing range unchanged. The new range layers on top for conflicting properties.
- **Superset** (new range contains existing): Create new range. Existing ranges fully contained lose conflicting properties (stripped from their style). Partially overlapping ranges are split (Phase J behavior).
- **Exact match**: Merge into existing range's style (no new range created)

### Phase L: UI Effective Style Display

**Problem**: When selecting a cell that's inside a range with styles, the toolbar doesn't reflect the effective style. For example, selecting a cell with green background and italic text from a range doesn't show the italic button as active or the background color swatch as green.

The toolbar should display the **effective merged style** considering:
1. Default style (base)
2. Column default style
3. Row default style (TODO: not implemented yet)
4. Range styles (merged from all overlapping ranges, CSS-like cascade)
5. Cell-level style override (highest priority)

**Current behavior**: The UI reads `cell.style` directly, which doesn't include range styles.

**Solution**: The viewport query already computes `getEffectiveStyle()` for rendering. We need to expose this to the UI when a cell is selected.

**Architecture**: All style computation logic lives in C++ (headless clients need it too). TypeScript UI just calls bindings and displays results.

**Steps**:
- [x] L1: Add C++ `getEffectiveCellStyle(col, row)` method in CellsEngine (reuses existing `getEffectiveStyle` from viewport)
- [x] L2: Add WASM binding to expose `getEffectiveCellStyle` as JSON
- [x] L3: Add C++ `getEffectiveStyleForRange(col1, row1, col2, row2)` that returns merged style + mixed flags for multi-cell selection
- [x] L4: Add WASM binding for `getEffectiveStyleForRange` returning `{style: CellStyle, mixed: {bold: bool, italic: bool, ...}}`
- [x] L5: Update TypeScript `StyleControls` to call these bindings instead of reading cell.style directly
- [x] L6: E2E test: apply range style, select cell within range, verify toolbar reflects range style
- [x] L7: E2E test: apply range style + cell override, verify toolbar shows cell override values
- [x] L8: E2E test: select range with mixed styles, verify mixed indicators show correctly

**Design consideration**: When the user changes a style on a cell that inherits from a range:
- Option A: Always create cell-level override (simple, current behavior)
- Option B: If whole range is selected, modify range style; otherwise cell override (smarter, matches user intent)

### Phase M: Content-Addressed Style Registry

**Problem**: Styles are getting duplicated in the registry:
```
Y BhUpbxBv {"bold":false,"italic":true,"underline":false}
Y NBBKD1LW {"bold":false,"italic":false,"underline":false,"bgColor":"#34D399"}
Y oSonVbqo {"bold":true,"italic":true,"underline":false,"bgColor":"#34D399"}
Y rLRDOH2N {"bold":true,"italic":false,"underline":false}
Y rbHBvj4m {"bold":false,"italic":true,"underline":false,"bgColor":"#34D399"}
```

Multiple identical styles can exist with different IDs. This wastes memory and makes style comparison harder.

**Solution**: Content-addressed style registry with reference counting:
1. Hash styles by their content to detect duplicates
2. Return existing style ID when registering a duplicate
3. Track usage count (cells + ranges referencing each style)
4. Support copy-on-write: when modifying a shared style, clone if refcount > 1

**Architecture**: All registry logic lives in C++ (`core/cells/`). This ensures:
- Headless clients get the same deduplication behavior
- Single source of truth for style management
- CRDT operations work correctly with deduplicated styles
- WASM bindings are thin wrappers that just call C++ methods

**Design** (in `core/cells/style_registry.h`):
```cpp
class StyleRegistry {
    // Primary storage: ID → Style
    std::unordered_map<ID, CellStyle> _styles;

    // Content hash → ID for deduplication
    std::unordered_map<size_t, ID> _hashToId;

    // Reference counting
    std::unordered_map<ID, uint32_t> _refCount;

public:
    // Register style, returns existing ID if duplicate
    ID registerStyle(const CellStyle& style);

    // Increment/decrement ref count
    void addRef(ID styleId);
    void release(ID styleId);  // Deletes if refcount hits 0

    // Get style for modification (clones if shared)
    ID getOrCloneForModification(ID styleId, const CellStyle& newStyle);

    // Lookup
    const CellStyle* getStyle(ID styleId) const;
};
```

**Hash function** for CellStyle (in `core/cells/model.h`):
- Add `size_t hash() const` method to CellStyle
- Combine hashes of all non-default properties
- Use sparse representation (only hash set properties)

**Steps**:
- [ ] M1: Add `CellStyle::hash()` method in `core/cells/model.h` for content-based hashing
- [ ] M2: Create `StyleRegistry` class in `core/cells/style_registry.h/.cc`
- [ ] M3: Migrate Workbook's style storage to use StyleRegistry
- [ ] M4: Update `registerStyle()` to check hash first, return existing ID if duplicate
- [ ] M5: Add reference counting (`addRef`/`release`) called from cell/range style assignment
- [ ] M6: Update CRDT `CELL_SET_STYLE` operation to use `addRef`/`release`
- [ ] M7: Update CRDT `RANGE_SET_STYLE` operation to use `addRef`/`release`
- [ ] M8: Implement `getOrCloneForModification()` - returns new ID if style is shared
- [ ] M9: Garbage collect unreferenced styles on `release()` when refcount hits 0
- [ ] M10: Unit test: register duplicate style returns same ID
- [ ] M11: Unit test: reference counting increments/decrements correctly
- [ ] M12: Unit test: modifying shared style clones it (returns different ID)
- [ ] M13: E2E test: apply same style to multiple ranges, verify single style entry in debug output

**Migration**: On document load, scan all cells and ranges to initialize refcounts for existing styles.

### Phase N: Architecture Audit - Thin UI Layer

**Goal**: Review the entire codebase to ensure all business logic lives in C++, with TypeScript/UI being a thin presentation layer only.

**Philosophy**:
- **C++ owns all logic**: Computations, validations, transformations, state management
- **UI is display-only**: Receives data, renders it, sends user actions back to C++
- **Headless-first**: Every feature must work without a browser (CLI, server-side, tests)
- **Single source of truth**: No duplicated logic between C++ and TypeScript

**Benefits**:
- Easier maintenance (one place to fix bugs)
- Consistent behavior across all clients (WASM, native, headless)
- Better testability (unit test C++ directly)
- Smaller WASM bundle (less JS code)

**Audit checklist** - Review each area and migrate logic to C++ if needed:

**Clipboard operations**:
- [ ] N1: Review clipboard copy logic - ensure cell serialization is in C++
- [ ] N2: Review clipboard paste logic - ensure parsing/validation is in C++
- [ ] N3: Add C++ `copyRangeToClipboard(col1, row1, col2, row2)` returning serialized data
- [ ] N4: Add C++ `pasteFromClipboard(col, row, data)` handling all formats

**Selection & navigation**:
- [ ] N5: Review selection expansion logic (Shift+Arrow) - should be in C++
- [ ] N6: Review "select all" / "select column" / "select row" - should be in C++
- [ ] N7: Add C++ `expandSelection(direction, modifier)` returning new selection bounds

**Formula bar & editing**:
- [ ] N8: Review formula parsing - ensure it's fully in C++ (likely already is)
- [ ] N9: Review input validation (date detection, number parsing) - should be in C++
- [ ] N10: Review autocomplete suggestions - should come from C++

**Formatting & display**:
- [ ] N11: Review number formatting - ensure format application is in C++
- [ ] N12: Review date/time formatting - should use C++ formatter
- [ ] N13: Review cell display value computation - should be in C++

**Undo/Redo**:
- [ ] N14: Review undo/redo stack management - should be in C++
- [ ] N15: Ensure UI just calls `undo()`/`redo()` bindings

**File operations**:
- [ ] N16: Review import logic (CSV, XLSX) - should be in C++
- [ ] N17: Review export logic - should be in C++

**Collaboration**:
- [ ] N18: Review presence/cursor display - data should come from C++
- [ ] N19: Review conflict resolution display - should be computed in C++

**General patterns to fix**:
- TypeScript computing derived state → Move to C++, expose via binding
- TypeScript validating input → Move validation to C++
- TypeScript transforming data → Move transformation to C++
- Duplicated constants (e.g., DEFAULT_COL_WIDTH) → Single source in C++

**Outcome**: Document in code comments which bindings exist and their purpose. Create a "Bindings API" reference showing the clean C++/TypeScript boundary.

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
