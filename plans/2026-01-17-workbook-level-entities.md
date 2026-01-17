# Workbook-Level Entities Architecture

Move cells, ranges, dependency graph, shared formulas, and spill tracking from Sheet-level to Workbook-level ownership. This enables simpler formula addressing (no need for sheet prefixes), unified calculation engine, and dynamic address display (UUID_SHEET2_B2 displayed as `Sheet2!B2` in Sheet1 vs just `B2` in Sheet2).

## Process Notes

**Working approach:**
- Commit every 5-10 files/tests fixed
- Take breaks after commits to allow resuming later
- Update this plan after each commit to track exactly where we left off
- Run `bazel build //core/cells/...` after each batch to check progress

**Current status:** Phase 1 COMPLETE - all tests passing.

**Progress Jan 17 (session 2):**
- Updated all `sheet->cells` iterations in test and app files
- Fixed FormulaDisplayConverter usage in fill_range_test.cc (needs workbook param)
- fill_range_test.cc now passes
- xlsx_reader_test.cc and xlsx_writer_test.cc still failing - need to ensure XLSX reader sets sheet's workbook pointer before adding cells
- Identified issue: XLSX reader adds cells before sheet is added to workbook, so sheet's `_workbook` is null

**Progress Jan 17 (session 3):**
- Fixed XLSX reader to call `sheet->setWorkbook(workbook.get())` early before adding cells
- Fixed CSV reader with the same pattern
- Fixed all test files that add cells to sheets before calling addSheet() - 42+ occurrences in xlsx_writer_test.cc, plus serializer_test.cc, csv_writer_test.cc, formula_integration_test.cc, formula_move_test.cc, crdt_test.cc, sync_formula_test.cc, sync_manager_test.cc, viewport_index_test.cc, ref_converter_test.cc, luau_sandbox_test.cc
- Fixed FormulaDisplayConverter calls in formula_integration_test.cc and formula_move_test.cc to pass workbook parameter for proper cell lookup
- Fixed lint errors (braces around single statements, const correctness)
- All 47 core/cells unit tests pass
- All 184 E2E tests pass
- bazel run :check passes all checks

**Next steps:**
1. Commit Phase 1 completion
2. Begin Phase 2: Workbook-Level Dependency Graph

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

**Core implementation (done):**
- [x] 1a: Add `Workbook::_cells` map (ID → unique_ptr<Cell>) as primary cell storage
- [x] 1b: Add `Workbook::getCell(cellId)` method for O(1) lookup
- [x] 1c: Add `Workbook::addCell(cell)` that takes ownership and adds to map
- [x] 1d: Add `Workbook::removeCell(cellId)` that removes and returns ownership
- [x] 1e: Removed `Sheet::cells` - cells now stored only at Workbook level. Sheet uses `_cellIndex` (position→ID) only.
- [x] 1f: Update `Sheet::getCell()` to delegate to `Workbook::getCell()`
- [x] 1g: Update `Sheet::addCell()` to add to Workbook storage, then update `_cellIndex`
- [x] 1h: Update `Sheet::getCellAt()` to use `_cellIndex` → cellId → `Workbook::getCell()`
- [x] 1i-core: Added `Sheet::getCellIds()` helper to iterate over cell IDs in a sheet
- [x] 1i-core: Added `Sheet::removeCellFromIndex()` helper for CRDT operations

**Files updated (core implementation):**
- [x] core/cells/model.h - Added Workbook cell storage, removed Sheet::cells
- [x] core/cells/model.cc - Implemented Workbook::getCell/addCell/removeCell, updated findCell
- [x] core/cells/sheet.cc - Updated all Sheet cell methods to use Workbook storage
- [x] core/cells/crdt_axis.cc - Updated deleteColumn/deleteRow to use new pattern
- [x] core/cells/crdt_cell.cc - Updated applyCellClear to use new pattern
- [x] core/cells/formula_eval.cc - Updated range iteration functions
- [x] core/cells/formula_recalc.cc - Updated hasDirtyCells/getDirtyCells
- [x] core/cells/formula_display.cc - Updated cell lookup
- [x] core/cells/serializer.cc - Updated serializeCells
- [x] core/cells/crdt.cc - Updated generateInitialOps cell iteration
- [x] core/cells/ref_converter.cc - Updated setContext and cell lookups
- [x] core/cells/viewport_index.cc - Updated build() cell iteration

**Test files updated:**
- [x] core/cells/csv_reader_test.cc - 4 occurrences fixed
- [x] core/cells/crdt_test.cc - 3 occurrences fixed
- [x] core/cells/ref_converter_test.cc - Refactored to use createTestWorkbook()
- [x] core/cells/large_file_test.cc - 1 occurrence fixed

**Test files updated (session 2):**
- [x] core/cells/xlsx_reader_test.cc - 14 occurrences updated
- [x] core/cells/xlsx_writer_test.cc - 10 occurrences updated
- [x] core/cells/fill_range_test.cc - fixed FormulaDisplayConverter to use workbook param
- [x] core/cells/xlsx_writer.cc - 5 occurrences updated (plus function signature change)

**App files updated (session 2):**
- [x] apps/wasm/bindings_format.cc - 5 occurrences
- [x] apps/wasm/bindings_viewport.cc - 1 occurrence
- [x] apps/wasm/bindings_formula.cc - 3 occurrences
- [x] apps/wasm/bindings_file.cc - 3 occurrences
- [x] apps/wasm/bindings_core.cc - 6 occurrences
- [x] apps/cli/converter.cc - 4 occurrences
- [x] apps/cli/main.cc - 1 occurrence

**Session 3 completed issues:**
- [x] Fixed XLSX reader: call `sheet->setWorkbook()` early before adding cells
- [x] Fixed CSV reader with same pattern
- [x] Fixed all test files with cell storage patterns
- [x] Fixed FormulaDisplayConverter calls to pass workbook parameter
- [x] Fixed lint errors (braces around single statements, const correctness)

- [x] 1i: Run all tests to verify cell operations work correctly - ALL PASS

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
1. Get cell from `Workbook::getCell(cellId)`
2. Get column axis and read its `sheetId` field (`Axis::sheetId` already stores this)
3. Look up sheet via `Workbook::getSheet(sheetId)`

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
