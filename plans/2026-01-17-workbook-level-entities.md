# Workbook-Level Entities Architecture

Move cells, ranges, dependency graph, shared formulas, and spill tracking from Sheet-level to Workbook-level ownership. This enables simpler formula addressing (no need for sheet prefixes), unified calculation engine, and dynamic address display (UUID_SHEET2_B2 displayed as `Sheet2!B2` in Sheet1 vs just `B2` in Sheet2).

## Current Architecture

- **Cells**: Owned by Sheet (`Sheet::cells` map keyed by UUID)
- **Dependency Graph**: Per-sheet (`Sheet::_depGraph`)
- **Shared Formulas**: Per-sheet (`Sheet::_sharedFormulaMasters`, `Sheet::_sharedFormulaFrom`)
- **Spill Regions**: Per-sheet (`Sheet::_spillMasters`, `Sheet::_spilledFrom`)
- **Ranges**: Per-sheet (`Sheet::_ranges`) with per-sheet R-tree index (`Sheet::_rangeIndex`)
- **Cross-sheet Dependencies**: Already at Workbook level (`Workbook::_crossSheetDeps`, `_crossSheetRangeDeps`)

## Target Architecture

- **Cells**: Indexed by UUID at Workbook level; Sheets maintain secondary (colId, rowId) index for fast 2D access
- **Dependency Graph**: Global to Workbook (no more cross-sheet tracking needed - it's all one graph)
- **Shared Formulas**: Global to Workbook
- **Spill Regions**: Global to Workbook (master cell links back to sheet via its column/row refs)
- **Ranges**: Global to Workbook (link back to sheet via column/row UUIDs)
- **Range Index**: Remains per-sheet (R-tree for fast viewport queries by position)

## Benefits

1. **Simpler formula engine**: No special cross-sheet dependency tracking - all deps in one graph
2. **Dynamic address display**: `=UUID_B2` displays as `Sheet2!B2` or just `B2` depending on context
3. **Cleaner CRDT operations**: Cell ops don't need sheetId - cell UUID is unique globally
4. **Future-proof**: Enables cells to move between sheets without breaking references

## Phase 1: Workbook-Level Cell Storage

Add primary cell storage at Workbook level. Sheets keep a lightweight secondary index.

- [ ] 1a: Add `Workbook::_cells` map (ID → unique_ptr<Cell>) as primary cell storage
- [ ] 1b: Add `Workbook::getCell(cellId)` method for O(1) lookup
- [ ] 1c: Add `Workbook::addCell(cell)` that takes ownership and adds to map
- [ ] 1d: Add `Workbook::removeCell(cellId)` that removes and returns ownership
- [ ] 1e: Change `Sheet::cells` from `unordered_map<ID, unique_ptr<Cell>>` to `unordered_set<ID>` (just cell IDs)
- [ ] 1f: Update `Sheet::getCell()` to delegate to `Workbook::getCell()`
- [ ] 1g: Update `Sheet::addCell()` to add to Workbook storage, then add ID to Sheet's set
- [ ] 1h: Update `Sheet::getCellAt()` to use `_cellIndex` → cellId → `Workbook::getCell()`
- [ ] 1i: Run tests to verify cell operations work correctly

## Phase 2: Workbook-Level Dependency Graph

Move dependency graph from Sheet to Workbook level. Remove cross-sheet dependency tracking (now redundant).

- [ ] 2a: Add `Workbook::_depGraph` (single global dependency graph)
- [ ] 2b: Add `Workbook::getDependencyGraph()` accessor
- [ ] 2c: Remove `Sheet::_depGraph` member
- [ ] 2d: Update `Sheet::getDependencyGraph()` to return `_workbook->getDependencyGraph()`
- [ ] 2e: Remove `Workbook::_crossSheetDeps`, `_crossSheetDepReverse`, `_crossSheetRangeDeps` (redundant now)
- [ ] 2f: Remove `Workbook::addCrossSheetDep()`, `removeCrossSheetDeps()`, `getCrossSheetDependents()`, etc.
- [ ] 2g: Update `Sheet::setCellFormula()` to use workbook's global dep graph
- [ ] 2h: Update formula evaluation's dirty propagation to use global dep graph
- [ ] 2i: Run tests to verify formula dependencies work correctly

## Phase 3: Workbook-Level Shared Formulas

Move shared formula tracking from Sheet to Workbook level.

- [ ] 3a: Add `Workbook::_sharedFormulaMasters` and `Workbook::_sharedFormulaFrom` maps
- [ ] 3b: Add `Workbook::getSharedFormulaInfo()`, `getSharedFormulaMaster()` methods
- [ ] 3c: Add `Workbook::registerSharedFormulaGroup()`, `addSharedFormulaSubscriber()`, etc.
- [ ] 3d: Add `Workbook::getEffectiveFormula(Cell*)` method
- [ ] 3e: Remove shared formula tracking from Sheet (keep delegating methods for convenience)
- [ ] 3f: Update `Sheet::getEffectiveFormula()` to delegate to Workbook
- [ ] 3g: Update shared formula CRDT operations to use Workbook-level tracking
- [ ] 3h: Run tests to verify shared formulas work correctly

## Phase 4: Workbook-Level Spill Regions

Move spill tracking from Sheet to Workbook level.

- [ ] 4a: Add `Workbook::_spillMasters` and `Workbook::_spilledFrom` maps
- [ ] 4b: Add `Workbook::getSpillInfo()`, `getSpillMaster()` methods
- [ ] 4c: Add `Workbook::registerSpillRange()`, `clearSpillRange()`, `clearAllSpillRanges()`
- [ ] 4d: Add `Workbook::getSpilledValue()`, `isSpilledPosition()` methods
- [ ] 4e: Remove spill tracking from Sheet (keep delegating methods)
- [ ] 4f: Update `Sheet::getSpilledValue()` etc. to delegate to Workbook
- [ ] 4g: Update formula evaluation spill logic to use Workbook-level tracking
- [ ] 4h: Run tests to verify spill regions work correctly

## Phase 5: Workbook-Level Ranges

Move range storage from Sheet to Workbook level. Keep per-sheet R-tree index.

- [ ] 5a: Add `Workbook::_ranges` map (ID → unique_ptr<Range>) as primary range storage
- [ ] 5b: Add `Workbook::getRange()`, `addRange()`, `removeRange()` methods
- [ ] 5c: Change `Sheet::_ranges` from `unordered_map<ID, unique_ptr<Range>>` to `unordered_set<ID>`
- [ ] 5d: Update `Sheet::getRange()` to delegate to Workbook
- [ ] 5e: Update `Sheet::addRange()` to add to Workbook, then add ID to Sheet's set and update R-tree
- [ ] 5f: Keep `Sheet::_rangeIndex` (R-tree) per-sheet for fast viewport queries
- [ ] 5g: Update range CRDT operations for Workbook-level storage
- [ ] 5h: Update `Sheet::_rangeStyles` to move to Workbook level (ranges are global, so styles should be too)
- [ ] 5i: Run tests to verify range operations work correctly

## Phase 6: Address Display System

Implement dynamic address display based on context (current sheet vs. other sheets).

- [ ] 6a: Create `AddressDisplayContext` struct with `currentSheetId` field
- [ ] 6b: Add `getDisplayAddress(cellId, context)` method to Workbook
- [ ] 6c: For cells on current sheet: return just `B2` (column + row position)
- [ ] 6d: For cells on other sheets: return `Sheet2!B2` (sheet name + position)
- [ ] 6e: Update `FormulaSerializer` to accept display context and use dynamic addressing
- [ ] 6f: Update formula bar display to use context-aware address formatting
- [ ] 6g: Update UI formula display in cells to use context-aware formatting
- [ ] 6h: Run tests to verify address display works in all contexts

## Phase 7: CRDT Operation Updates

Update CRDT operations to work with Workbook-level entities.

- [ ] 7a: Update `CELL_SET_VALUE` to use Workbook cell storage (no sheetId needed in op)
- [ ] 7b: Update `CELL_SET_FORMULA` to use Workbook cell storage and global dep graph
- [ ] 7c: Update `CELL_DELETE` to use Workbook cell storage
- [ ] 7d: Update `RANGE_ADD`, `RANGE_REMOVE`, etc. for Workbook-level ranges
- [ ] 7e: Verify backward compatibility with existing saved files (migration if needed)
- [ ] 7f: Run CRDT sync tests to verify collaboration still works

## Phase 8: Formula Engine Integration

Update formula resolution and evaluation for Workbook-level entities.

- [ ] 8a: Update `FormulaResolver` to resolve cell refs without requiring sheet context
- [ ] 8b: Update `FormulaEvaluator` to use Workbook-level cell lookup
- [ ] 8c: Remove sheet-scoped cell resolution paths (now all cells are global)
- [ ] 8d: Update `EvalContext` to remove sheet-specific cell lookup
- [ ] 8e: Simplify recalculation engine (no cross-sheet dirty tracking needed)
- [ ] 8f: Run comprehensive formula evaluation tests

## Phase 9: Migration and Cleanup

Handle migration of existing data and clean up obsolete code.

- [ ] 9a: Add migration code in file parser to move cells from Sheet to Workbook storage
- [ ] 9b: Add migration code for ranges from Sheet to Workbook storage
- [ ] 9c: Remove obsolete cross-sheet dependency code from Workbook
- [ ] 9d: Remove obsolete per-sheet storage fields that are now at Workbook level
- [ ] 9e: Update documentation and comments to reflect new architecture
- [ ] 9f: Run full E2E test suite to verify everything works

## Phase 10: Performance Validation

Verify performance is maintained or improved with new architecture.

- [ ] 10a: Benchmark cell lookup performance (should be similar - still O(1) hash lookup)
- [ ] 10b: Benchmark recalculation with cross-sheet formulas (should be faster - no separate tracking)
- [ ] 10c: Benchmark viewport query performance (still uses per-sheet R-tree)
- [ ] 10d: Benchmark CRDT operation application speed
- [ ] 10e: Document any performance changes

## Design Notes

### Cell Ownership
- Workbook owns all Cell objects via `_cells` map
- Sheet maintains `_cellIds` set (just IDs) and `_cellIndex` map (colId:rowId → cellId)
- Cell still has `colId` and `rowId` which implicitly link it to a Sheet via those Axis UUIDs

### Finding a Cell's Sheet
Given a cell UUID, to find its sheet:
1. Get cell's `colId` from `Workbook::getCell(cellId)`
2. Look up `Axis* col = findAxisSheet(colId)` → returns the Sheet that owns that column
3. The sheet owns the column, so the cell "belongs" to that sheet for display purposes

### Range Ownership
- Workbook owns all Range objects
- Ranges reference column/row UUIDs which belong to specific sheets
- Sheet maintains R-tree index for fast spatial queries during viewport rendering
- A range's "sheet" is determined by its startColId's sheet

### Why Keep R-tree Per-Sheet?
- Viewport queries need to quickly find ranges in a visible area
- R-tree uses (col_position, row_position) coordinates which are sheet-local
- Having separate R-trees per sheet avoids coordinate namespace collisions
- Performance: smaller R-trees = faster queries

### Backward Compatibility
- Existing ZCD files store cells under Sheet sections
- Parser will read old format and migrate to Workbook-level storage
- Serializer will write in format compatible with both old and new parsers (or introduce new version marker)
