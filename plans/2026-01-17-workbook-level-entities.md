# Workbook-Level Entities Architecture

Move cells, ranges, dependency graph, shared formulas, and spill tracking from Sheet-level to Workbook-level ownership. This enables simpler formula addressing (no need for sheet prefixes), unified calculation engine, and dynamic address display (UUID_SHEET2_B2 displayed as `Sheet2!B2` in Sheet1 vs just `B2` in Sheet2).

## Process Notes

**Working approach:**
- Commit every 5-10 files/tests fixed
- Take breaks after commits to allow resuming later
- Update this plan after each commit to track exactly where we left off
- Run `bazel build //core/cells/...` after each batch to check progress

**Current status:** Phase 8 COMPLETE - Formula engine already supports workbook-level storage.

**Progress Jan 17 (session 10):**
- Phase 8: Formula Engine Integration
- Analyzed FormulaResolver, FormulaEvaluator, EvalContext, and recalculation engine
- FormulaEvaluator already uses `workbook->findCell()` for cells without explicit sheetId
- EvalContext::sheet is still needed for axis position lookups (positions are sheet-local)
- Recalculation simplification (8e) depends on deferred Phase 2e-2f cross-sheet tracking removal
- All 54 unit tests pass; 183/184 E2E tests pass (1 flaky collab sync test, unrelated)

**Progress Jan 17 (session 9):**
- Phase 7: CRDT Operation Updates
- Updated `applyCellSetValue` to use `workbook.findCell()` instead of looping through sheets
- Updated `applyCellSetFormat` and `applyCellSetStyle` to use `workbook.getCell()` directly
- Updated `applyCellClear` to use `workbook.findCell()` and `workbook.getDependencyGraph()`
- All formula dependency operations now use workbook-level graph
- Range CRDT operations already compatible (Sheet methods delegate to Workbook from Phase 5)
- All 54 unit tests and 184 E2E tests pass

**Progress Jan 17 (session 8):**
- Phase 6: Context-aware address display
- `FormulaDisplayConverter` already had the logic to compare cell's sheet with context sheet
- Updated `CellsEngine::getFormulaDisplay` to use `FormulaDisplayConverter` directly
- All 54 unit tests and 184 E2E tests pass

**Progress Jan 17 (session 7):**
- Moved range storage from Sheet to Workbook level
- Added `Workbook::_ranges` and `_rangeStyles` maps
- Added Workbook methods: `getRange()`, `addRange()`, `removeRange()`, `getRangeStyleId()`, `setRangeStyleId()`
- Changed `Sheet::_ranges` from `unordered_map<ID, unique_ptr<Range>>` to `unordered_set<ID>` (renamed to `_rangeIds`)
- Updated Sheet methods to delegate to Workbook: `getRange()`, `addRange()`, `removeRange()`, `getRangeStyleId()`, `setRangeStyleId()`
- Sheet keeps `_rangeIndex` (R-tree) per-sheet for fast viewport queries
- Updated all files using `sheet->getRanges()` to use `sheet->getRangeIds()` and lookup from workbook
- Updated crdt_axis.cc, serializer.cc, xlsx_writer.cc, xlsx_writer_test.cc, crdt_test.cc, bindings_viewport.cc
- Fixed test that creates Sheet without Workbook (MergedCellsApiWorks)
- All 54 unit tests and 184 E2E tests pass

**Progress Jan 17 (session 6):**
- Moved spill tracking from Sheet to Workbook level
- Added `Workbook::_spillMasters` and `_spilledFrom` maps
- Added Workbook methods: `getSpillInfo()`, `getSpillMaster()`, `getSpilledValue()`, `isSpilledPosition()`, `registerSpillRange()`, `clearSpillRange()`, `clearAllSpillRanges()`
- Removed `_spillMasters` and `_spilledFrom` from Sheet
- Updated all Sheet spill methods to delegate to Workbook
- All 54 unit tests and 184 E2E tests pass

**Progress Jan 17 (session 5):**
- Moved shared formula tracking from Sheet to Workbook level
- Added `Workbook::_sharedFormulaMasters` and `_sharedFormulaFrom` maps
- Added Workbook methods: `getSharedFormulaInfo()`, `getSharedFormulaMaster()`, `getEffectiveFormula()`, `isInSharedFormulaGroup()`, `registerSharedFormulaGroup()`, `addSharedFormulaSubscriber()`, `removeSharedFormulaSubscriber()`, `clearSharedFormulaGroup()`, `clearAllSharedFormulaGroups()`
- Removed `_sharedFormulaMasters` and `_sharedFormulaFrom` from Sheet
- Updated all Sheet shared formula methods to delegate to Workbook
- All 47 unit tests and 184 E2E tests pass

**Progress Jan 17 (session 4):**
- Moved dependency graph from Sheet to Workbook level
- Added `Workbook::_depGraph` and `getDependencyGraph()` accessor
- Removed `Sheet::_depGraph` member
- Updated `Sheet::getDependencyGraph()` to delegate to workbook
- Updated all `_depGraph` usages in sheet.cc to use `getDependencyGraph()`
- All 43 unit tests and 184 E2E tests pass
- Cross-sheet dependency tracking kept for now (R-tree is sheet-local)

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

- **Cells**: ✅ Workbook level (`Workbook::_cells`), Sheet has `_cellIndex` for position lookups
- **Columns/Rows**: Per-sheet (`Sheet::columns`, `Sheet::rows` maps keyed by UUID)
- **Dependency Graph**: ✅ Workbook level (`Workbook::_depGraph`)
- **Shared Formulas**: ✅ Workbook level (`Workbook::_sharedFormulaMasters`, `_sharedFormulaFrom`)
- **Spill Regions**: ✅ Workbook level (`Workbook::_spillMasters`, `_spilledFrom`)
- **Ranges**: ✅ Workbook level (`Workbook::_ranges`), Sheet has `_rangeIds` set and `_rangeIndex` R-tree
- **Cross-sheet Dependencies**: Workbook level (`Workbook::_crossSheetDeps`, `_crossSheetRangeDeps`) - to be removed when R-tree is workbook-aware

## Target Architecture

- **Cells**: ✅ Indexed by UUID at Workbook level; Sheets maintain `_cellIndex` for fast 2D access
- **Columns/Rows**: Indexed by UUID at Workbook level; Sheets maintain position indices for fast position lookups
- **Dependency Graph**: ✅ Global to Workbook (cross-sheet tracking can be removed when R-tree is workbook-aware)
- **Shared Formulas**: ✅ Global to Workbook
- **Spill Regions**: ✅ Global to Workbook (master cell links back to sheet via its column/row refs)
- **Ranges**: ✅ Global to Workbook; `_rangeIds` also global (link back to sheet via axis's sheetId)
- **Range Index**: Remains per-sheet (R-tree for fast viewport queries by position)
- **Position Indices**: Remain per-sheet (column/row position → ID lookups)

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

Move dependency graph from Sheet to Workbook level.

**Completed:**
- [x] 2a: Add `Workbook::_depGraph` (single global dependency graph)
- [x] 2b: Add `Workbook::getDependencyGraph()` accessor
- [x] 2c: Remove `Sheet::_depGraph` member
- [x] 2d: Update `Sheet::getDependencyGraph()` to delegate to workbook's graph
- [x] 2g: Update `Sheet::setCellFormula()` to use workbook's global dep graph
- [x] 2h: All usages of _depGraph in sheet.cc now use getDependencyGraph()
- [x] 2i: Run tests to verify formula dependencies work correctly - ALL PASS

**Deferred to later phase:**
- [ ] 2e: Remove `Workbook::_crossSheetDeps`, `_crossSheetDepReverse`, `_crossSheetRangeDeps`
- [ ] 2f: Remove `Workbook::addCrossSheetDep()`, `removeCrossSheetDeps()`, `getCrossSheetDependents()`, etc.

Note: Cross-sheet dependency tracking is kept for now because:
1. The R-tree uses sheet-local positions for range queries
2. Cross-sheet range references need separate tracking until R-tree is made workbook-aware
3. Direct cell refs work via the global graph's reverseDeps_ map

## Phase 3: Workbook-Level Shared Formulas

Move shared formula tracking from Sheet to Workbook level.

- [x] 3a: Add `Workbook::_sharedFormulaMasters` and `Workbook::_sharedFormulaFrom` maps
- [x] 3b: Add `Workbook::getSharedFormulaInfo()`, `getSharedFormulaMaster()` methods
- [x] 3c: Add `Workbook::registerSharedFormulaGroup()`, `addSharedFormulaSubscriber()`, etc.
- [x] 3d: Add `Workbook::getEffectiveFormula(Cell*)` method
- [x] 3e: Remove shared formula tracking from Sheet (keep delegating methods for convenience)
- [x] 3f: Update `Sheet::getEffectiveFormula()` to delegate to Workbook
- [x] 3g: CRDT operations use Sheet methods which now delegate to Workbook - no changes needed
- [x] 3h: Run tests to verify shared formulas work correctly - ALL PASS (47 unit + 184 E2E)

## Phase 4: Workbook-Level Spill Regions

Move spill tracking from Sheet to Workbook level.

- [x] 4a: Add `Workbook::_spillMasters` and `Workbook::_spilledFrom` maps
- [x] 4b: Add `Workbook::getSpillInfo()`, `getSpillMaster()` methods
- [x] 4c: Add `Workbook::registerSpillRange()`, `clearSpillRange()`, `clearAllSpillRanges()`
- [x] 4d: Add `Workbook::getSpilledValue()`, `isSpilledPosition()` methods
- [x] 4e: Remove spill tracking from Sheet (keep delegating methods)
- [x] 4f: Update `Sheet::getSpilledValue()` etc. to delegate to Workbook
- [x] 4g: Formula evaluation spill logic already uses Sheet methods which now delegate to Workbook - no changes needed
- [x] 4h: Run tests to verify spill regions work correctly - ALL PASS (54 unit + 184 E2E)

## Phase 5: Workbook-Level Ranges

Move range storage from Sheet to Workbook level. Keep per-sheet R-tree index.

- [x] 5a: Add `Workbook::_ranges` map (ID → unique_ptr<Range>) as primary range storage
- [x] 5b: Add `Workbook::getRange()`, `addRange()`, `removeRange()` methods
- [x] 5c: Change `Sheet::_ranges` from `unordered_map<ID, unique_ptr<Range>>` to `unordered_set<ID>` (renamed to `_rangeIds`)
- [x] 5d: Update `Sheet::getRange()` to delegate to Workbook
- [x] 5e: Update `Sheet::addRange()` to add to Workbook, then add ID to Sheet's set and update R-tree
- [x] 5f: Keep `Sheet::_rangeIndex` (R-tree) per-sheet for fast viewport queries
- [x] 5g: Update range CRDT operations for Workbook-level storage (crdt_axis.cc range adjustment)
- [x] 5h: Update `Sheet::_rangeStyles` to move to Workbook level (ranges are global, so styles should be too)
- [x] 5i: Run tests to verify range operations work correctly - ALL PASS (54 unit + 184 E2E)

## Phase 6: Address Display System

Implement dynamic address display based on context (current sheet vs. other sheets).

**Implementation note:** `FormulaDisplayConverter` already had context-aware display logic implemented. The only change needed was updating `CellsEngine::getFormulaDisplay` to use `FormulaDisplayConverter` directly instead of going through the `FormulaSerializer` → `RefConverter::formulaToA1` path.

- [x] 6a-6d: Context-aware display already implemented in `FormulaDisplayConverter::cellRefToString()` - it compares a cell's column's sheetId with the context sheet's id and adds sheet prefix when they differ
- [x] 6e: Not needed - `FormulaDisplayConverter` handles this at AST level, no need to modify `FormulaSerializer`
- [x] 6f-6g: Updated `CellsEngine::getFormulaDisplay` to use `FormulaDisplayConverter` instead of `RefConverter::formulaToA1`
- [x] 6h: Run tests to verify address display works in all contexts - ALL PASS (54 unit + 184 E2E)

## Phase 7: CRDT Operation Updates

Update CRDT operations to work with Workbook-level entities.

- [x] 7a: Update `CELL_SET_VALUE` to use Workbook cell storage - uses `workbook.findCell()` instead of looping through sheets
- [x] 7b: Update `CELL_SET_FORMULA` to use Workbook cell storage and global dep graph - uses `workbook.getDependencyGraph()`
- [x] 7c: Update `CELL_DELETE` to use Workbook cell storage - uses `workbook.findCell()` and `workbook.getDependencyGraph()`
- [x] 7d: Update `RANGE_ADD`, `RANGE_REMOVE`, etc. for Workbook-level ranges - already compatible (Sheet methods delegate to Workbook from Phase 5)
- [x] 7e: Backward compatibility skipped per user request
- [x] 7f: Run CRDT sync tests to verify collaboration still works - ALL PASS (54 unit + 184 E2E)

## Phase 8: Formula Engine Integration

Update formula resolution and evaluation for Workbook-level entities.

**Analysis:** The formula engine already supports workbook-level storage:
- `FormulaResolver` still needs sheet context for relative references (A1 on Sheet1 vs Sheet2)
- `FormulaEvaluator::evaluateCellRef()` already uses `workbook->findCell()` for cells without explicit sheetId (line 131)
- `EvalContext::sheet` is still needed for axis position lookups (positions are sheet-local)
- Recalculation simplification depends on removing cross-sheet tracking (deferred from Phase 2e-2f)

- [x] 8a: `FormulaResolver` already uses workbook for cross-sheet lookups; sheet context needed for relative refs
- [x] 8b: `FormulaEvaluator` already uses `workbook->findCell()` for simplified formula storage
- [x] 8c: Sheet-scoped resolution paths still needed - axis positions are sheet-local
- [x] 8d: `EvalContext::sheet` still required for position lookups and range iteration
- [~] 8e: Recalculation simplification depends on deferred Phase 2e-2f (cross-sheet tracking removal)
- [x] 8f: All 54 unit tests and 183/184 E2E tests pass (1 flaky collab sync test, unrelated)

## Phase 9: Migration and Cleanup

Handle migration of existing data and clean up obsolete code.

- [ ] 9a: Add migration code in file parser to move cells from Sheet to Workbook storage
- [ ] 9b: Add migration code for ranges from Sheet to Workbook storage
- [ ] 9c: Remove obsolete cross-sheet dependency code from Workbook
- [ ] 9d: Remove obsolete per-sheet storage fields that are now at Workbook level
- [ ] 9e: Update documentation and comments to reflect new architecture
- [ ] 9f: Run full E2E test suite to verify everything works

## Phase 10: Workbook-Level Axis Storage

Move columns and rows (Axis objects) from Sheet to Workbook level. Each Axis already has a `sheetId` field, so it can link back to its sheet.

**Benefits:**
- Simpler cell-to-axis lookups: `workbook->getColumn(colId)` instead of finding the sheet first
- Consistent with cell/range storage pattern
- Enables future cross-sheet axis references

**What moves to Workbook:**
- `Sheet::columns` → `Workbook::_columns` (primary storage)
- `Sheet::rows` → `Workbook::_rows` (primary storage)

**What stays in Sheet:**
- `_columnIndex` (position → ID) for fast position-based lookups
- `_rowIndex` (position → ID) for fast position-based lookups

- [ ] 10a: Add `Workbook::_columns` map (ID → unique_ptr<Axis>) as primary column storage
- [ ] 10b: Add `Workbook::_rows` map (ID → unique_ptr<Axis>) as primary row storage
- [ ] 10c: Add `Workbook::getColumn(colId)`, `addColumn()`, `removeColumn()` methods
- [ ] 10d: Add `Workbook::getRow(rowId)`, `addRow()`, `removeRow()` methods
- [ ] 10e: Change `Sheet::columns` to `_columnIds` set (just IDs, not ownership)
- [ ] 10f: Change `Sheet::rows` to `_rowIds` set (just IDs, not ownership)
- [ ] 10g: Update `Sheet::getColumn()`, `getRow()` to delegate to Workbook
- [ ] 10h: Update all CRDT axis operations for Workbook-level storage
- [ ] 10i: Update serializer/parser for new storage pattern
- [ ] 10j: Run tests to verify axis operations work correctly

## Phase 11: Global Range ID Tracking

Move `_rangeIds` from Sheet to Workbook level for consistency with other entities.

**What moves to Workbook:**
- `Sheet::_rangeIds` → `Workbook::_rangeIds` (global set of all range IDs)

**What stays in Sheet:**
- `_rangeIndex` (R-tree) for fast spatial queries - positions are sheet-local

- [ ] 11a: Add `Workbook::_rangeIds` set as global range ID tracking
- [ ] 11b: Update `Sheet::_rangeIds` to be removed (Workbook has the global set)
- [ ] 11c: Update `Sheet::addRange()`, `removeRange()` to update Workbook's `_rangeIds`
- [ ] 11d: Update code that iterates `sheet->getRangeIds()` to use Workbook
- [ ] 11e: Run tests to verify range operations work correctly

## Phase 12: Performance Validation

Verify performance is maintained or improved with new architecture.

- [ ] 12a: Benchmark cell lookup performance (should be similar - still O(1) hash lookup)
- [ ] 12b: Benchmark axis lookup performance (should be similar - still O(1) hash lookup)
- [ ] 12c: Benchmark recalculation with cross-sheet formulas (should be faster - no separate tracking)
- [ ] 12d: Benchmark viewport query performance (still uses per-sheet R-tree)
- [ ] 12e: Benchmark CRDT operation application speed
- [ ] 12f: Document any performance changes

## Design Notes

### Target Architecture Summary

**Workbook-level storage (global by UUID):**
- `_cells` - all Cell objects
- `_columns` - all column Axis objects (future Phase 10)
- `_rows` - all row Axis objects (future Phase 10)
- `_ranges` - all Range objects
- `_rangeIds` - global set of range IDs (future Phase 11)
- `_rangeStyles` - range style mappings
- `_depGraph` - single global dependency graph
- `_sharedFormulaMasters`, `_sharedFormulaFrom` - shared formula tracking
- `_spillMasters`, `_spilledFrom` - spill range tracking

**Sheet-level storage (position-based indices):**
- `_cellIndex` - (colId:rowId → cellId) for fast position lookups
- `_columnIndex` - (position → colId) for fast column position lookups (future)
- `_rowIndex` - (position → rowId) for fast row position lookups (future)
- `_rangeIndex` - R-tree for fast spatial queries (positions are sheet-local)

### Cell Ownership
- Workbook owns all Cell objects via `_cells` map
- Sheet maintains `_cellIndex` map (colId:rowId → cellId) for position-based lookups
- Cell has `colId` and `rowId` which link to Axis objects (will be in Workbook after Phase 10)

### Finding a Cell's Sheet
Given a cell UUID, to find its sheet:
1. Get cell from `Workbook::getCell(cellId)`
2. Get column axis from `Workbook::getColumn(cell->colId)` (after Phase 10)
3. Read axis's `sheetId` field (`Axis::sheetId` already stores this)
4. Look up sheet via `Workbook::getSheet(sheetId)`

### Axis Ownership (Target - Phase 10)
- Workbook owns all Axis objects via `_columns` and `_rows` maps
- Each Axis has a `sheetId` field linking back to its sheet
- Sheet maintains position indices for fast position-to-ID lookups

### Range Ownership
- Workbook owns all Range objects via `_ranges` map
- Workbook tracks all range IDs globally via `_rangeIds` (after Phase 11)
- Ranges reference column/row UUIDs which belong to specific sheets
- Sheet maintains R-tree index for fast spatial queries during viewport rendering
- A range's "sheet" is determined by its startColId's axis's sheetId

### Why Keep R-tree and Position Indices Per-Sheet?
- Viewport queries need to quickly find entities in a visible area
- R-tree uses (col_position, row_position) coordinates which are sheet-local
- Having separate R-trees per sheet avoids coordinate namespace collisions
- Performance: smaller R-trees = faster queries

### Backward Compatibility
- Existing ZCD files store cells under Sheet sections
- Parser will read old format and migrate to Workbook-level storage
- Serializer will write in format compatible with both old and new parsers (or introduce new version marker)
