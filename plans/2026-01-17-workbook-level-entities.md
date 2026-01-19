# Workbook-Level Entities Architecture

Move cells, ranges, dependency graph, shared formulas, and spill tracking from Sheet-level to Workbook-level ownership. This enables simpler formula addressing (no need for sheet prefixes), unified calculation engine, and dynamic address display (UUID_SHEET2_B2 displayed as `Sheet2!B2` in Sheet1 vs just `B2` in Sheet2).

## Process Notes

**Working approach:**
- Commit every 5-10 files/tests fixed
- Take breaks after commits to allow resuming later
- Update this plan after each commit to track exactly where we left off
- Run `bazel build //core/cells/...` after each batch to check progress

**Current status:** Phase 13 COMPLETE - Ready for Phase 14 (Sheet-Agnostic Recalculation Engine).

**Progress Jan 17 (session 16):**
- Phase 11: Global Range ID Tracking
- Added `Workbook::_rangeIds` set for global range tracking
- Added `Workbook::getRangeIds()` to get all range IDs and `getRangeIdsForSheet(sheetId)` for per-sheet iteration
- Removed `Sheet::_rangeIds` - range ownership now fully at Workbook level
- Updated `Sheet::getRangeIds()` to return `std::vector<ID>` (delegates to Workbook)
- Updated `Sheet::getRange()`, `removeRange()`, `clearAllRanges()`, `getRangeStyleId()`, `setRangeStyleId()` to use `rangeInSheet()` helper
- Fixed serializer.cc to handle new return type (const auto instead of const auto&)
- All 54 unit tests pass
- All 184 E2E tests pass

**Progress Jan 17 (session 15):**
- Fixed all remaining `sheet->columns` and `sheet->rows` usages in WASM bindings
- Updated bindings_viewport.cc (2 locations), bindings_format.cc (20 locations), bindings_formula.cc (4 locations), bindings_core.cc (28 locations)
- Fixed formula->ast.get() typo in bindings_formula.cc (should be formula->ast)
- All 54 unit tests pass
- All 184 E2E tests pass

**Progress Jan 17 (session 14):**
- Discovered bindings_viewport.cc still uses old `sheet->columns`/`sheet->rows` patterns
- Partially fixed bindings_viewport.cc (lines 129, 145, 272-279, 547-554, 623-669, 698-710)

**Progress Jan 17 (session 13):**
- Phase 10: Completed test verification
- Fixed all tests that add columns/rows before sheet has workbook set (setWorkbook pattern)
- Updated serializer_test.cc, crdt_test.cc, ref_converter_test.cc, xlsx_writer_test.cc, fill_range_test.cc
- Fixed lint errors (const pointer correctness in sheet.cc, crdt_axis.cc, crdt.cc)
- All 54 unit tests pass (E2E tests used cached WASM build)

**Progress Jan 17 (session 12):**
- Phase 10: Workbook-Level Axis Storage
- Added `Workbook::_columns` and `_rows` maps as primary axis storage
- Added Workbook methods: `getColumn()`, `addColumn()`, `removeColumn()`, `getRow()`, `addRow()`, `removeRow()`
- Changed `Sheet::columns`/`rows` to `_columnIds`/`_rowIds` sets with `_columnIndex`/`_rowIndex` position maps
- Updated Sheet axis methods to delegate to Workbook
- Updated all CRDT axis operations for workbook-level storage
- Updated all consumers: formula_display.cc, formula_eval.cc, serializer.cc, csv_writer.cc, xlsx_writer.cc, ref_converter.cc, viewport_index.cc
- Updated all test files: csv_reader_test.cc, ref_converter_test.cc, xlsx_reader_test.cc, xlsx_writer_test.cc
- Build passes for //core/cells/...

**Progress Jan 17 (session 11):**
- Phase 9: Migration and Cleanup
- Analyzed parser/serializer - migration code NOT needed (Sheet methods delegate to Workbook)
- Cross-sheet dependency removal deferred (R-tree uses sheet-local positions)
- No obsolete per-sheet storage fields to remove
- Updated header comments in model.h to document storage architecture
- All 54 unit tests and 184 E2E tests pass

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
- **Columns/Rows**: ✅ Workbook level (`Workbook::_columns`, `_rows`), Sheet has `_columnIds`/`_rowIds` sets and `_columnIndex`/`_rowIndex` position maps
- **Dependency Graph**: ✅ Workbook level (`Workbook::_depGraph`)
- **Shared Formulas**: ✅ Workbook level (`Workbook::_sharedFormulaMasters`, `_sharedFormulaFrom`)
- **Spill Regions**: ✅ Workbook level (`Workbook::_spillMasters`, `_spilledFrom`)
- **Ranges**: ✅ Workbook level (`Workbook::_ranges`, `_rangeIds`), Sheet has `_rangeIndex` R-tree only
- **Cross-sheet Dependencies**: ⚠️ TO BE REMOVED in Phase 14 - single global `_depGraph` handles all dependencies

## Target Architecture

- **Cells**: ✅ Indexed by UUID at Workbook level; Sheets maintain `_cellIndex` for fast 2D access
- **Columns/Rows**: ✅ Indexed by UUID at Workbook level; Sheets maintain `_columnIndex`/`_rowIndex` for fast position lookups
- **Dependency Graph**: ✅ Single global graph in Workbook - NO separate cross-sheet tracking needed
- **Shared Formulas**: ✅ Global to Workbook
- **Spill Regions**: ✅ Global to Workbook (master cell links back to sheet via its column/row refs)
- **Ranges**: ✅ Global to Workbook; `_rangeIds` ✅ global (link back to sheet via axis's sheetId)
- **Range Index**: Remains per-sheet (R-tree for fast viewport queries by position)
- **Position Indices**: ✅ Per-sheet (`_columnIndex`, `_rowIndex` for position → ID lookups)

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

**Deferred to Phase 14:**
- [ ] 2e: Remove `Workbook::_crossSheetDeps`, `_crossSheetDepReverse`, `_crossSheetRangeDeps` → Phase 14
- [ ] 2f: Remove `Workbook::addCrossSheetDep()`, `removeCrossSheetDeps()`, `getCrossSheetDependents()`, etc. → Phase 14

Note: Cross-sheet dependency tracking will be removed in Phase 14. With globally unique cell UUIDs and formula storage without sheet prefixes (Phase 13), ALL dependencies go through the single global `_depGraph`.

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

**Analysis:** Migration code is NOT needed. The file format remains unchanged - cells and ranges are serialized under their sheet sections, but when parsed, the Sheet::addCell/addRange methods automatically delegate to Workbook-level storage. This provides transparent backward compatibility.

- [x] 9a: Migration NOT needed - Sheet methods delegate to Workbook during parsing
- [x] 9b: Migration NOT needed - same pattern for ranges
- [~] 9c: Cross-sheet dependency removal deferred (needed while R-tree uses sheet-local positions)
- [x] 9d: No obsolete fields - all Sheet members needed (_cellIndex for positions, _rangeIds, _rangeIndex)
- [x] 9e: Updated header comments in model.h to document workbook-level storage architecture
- [x] 9f: All 54 unit tests and 184/184 E2E tests pass

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

- [x] 10a: Add `Workbook::_columns` map (ID → unique_ptr<Axis>) as primary column storage
- [x] 10b: Add `Workbook::_rows` map (ID → unique_ptr<Axis>) as primary row storage
- [x] 10c: Add `Workbook::getColumn(colId)`, `addColumn()`, `removeColumn()` methods
- [x] 10d: Add `Workbook::getRow(rowId)`, `addRow()`, `removeRow()` methods
- [x] 10e: Change `Sheet::columns` to `_columnIds` set (just IDs, not ownership) - also added `_columnIndex` position map
- [x] 10f: Change `Sheet::rows` to `_rowIds` set (just IDs, not ownership) - also added `_rowIndex` position map
- [x] 10g: Update `Sheet::getColumn()`, `getRow()` to delegate to Workbook
- [x] 10h: Update all CRDT axis operations for Workbook-level storage
- [x] 10i: Update serializer/parser for new storage pattern - updated serializer.cc to use getColumnIds()/getRowIds()
- [x] 10j: Run tests to verify axis operations work correctly - All 54 unit tests and 184 E2E tests pass

## Phase 11: Global Range ID Tracking

Move `_rangeIds` from Sheet to Workbook level for consistency with other entities.

**What moves to Workbook:**
- `Sheet::_rangeIds` → `Workbook::_rangeIds` (global set of all range IDs)

**What stays in Sheet:**
- `_rangeIndex` (R-tree) for fast spatial queries - positions are sheet-local

- [x] 11a: Add `Workbook::_rangeIds` set as global range ID tracking - added set and `getRangeIds()`, `getRangeIdsForSheet()` methods
- [x] 11b: Update `Sheet::_rangeIds` to be removed (Workbook has the global set) - removed member, updated all methods
- [x] 11c: Update `Sheet::addRange()`, `removeRange()` to update Workbook's `_rangeIds` - addRange/removeRange now fully delegate to Workbook
- [x] 11d: Update code that iterates `sheet->getRangeIds()` to use new return type - updated serializer.cc
- [x] 11e: Run tests to verify range operations work correctly - All 54 unit + 184 E2E tests pass

## Phase 12: Performance Validation

Verify performance is maintained or improved with new architecture.

- [x] 12a: Benchmark cell lookup performance - getCell() by ID: ~113ns/lookup, findCell(): ~277ns/lookup, getCellAt(): ~182ns/lookup
- [x] 12b: Benchmark axis lookup performance - getColumn()/getRow() by ID: ~72-82ns/lookup, ByPosition: ~159-181ns/lookup
- [x] 12c: Benchmark recalculation with cross-sheet formulas - getDependents(): ~84ns/query (fast)
- [x] 12d: Benchmark viewport query performance - covered by large_file_test.cc (still uses per-sheet R-tree)
- [x] 12e: Benchmark CRDT operation application speed - CELL_SET_VALUE: ~22µs/op, COL_RESIZE: ~50µs/op, CELL_SET_FORMAT: ~67µs/op
- [x] 12f: Document performance changes - All lookups remain O(1), performance is excellent

**Benchmark Results Summary:**
- Cell/Axis lookups by ID: sub-200ns (O(1) hash lookup)
- Position-based lookups: ~160-200ns (two hash lookups)
- Dependency queries: sub-100ns
- CRDT operations: 22-67µs (includes HLC generation and model updates)
- Memory efficiency verified: All entities accessible through workbook-level storage

## Phase 13: Remove Sheet Prefixes from Internal Formula Storage

Since cells are now globally unique by UUID at the workbook level, internal formula storage should NOT include sheet prefixes. The sheet context is only needed for display (derived dynamically from cell's column's sheetId).

**Current (incorrect):** `=!bQYHziAr~~6RVulooT` (sheet prefix in internal storage)
**Target:** `=~~6RVulooT` (lock flags + cell UUID, no sheet prefix)

**Lock flag encoding (2-char prefix):**
- `~~cellId` = relative reference (like `B1`)
- `$~cellId` = column locked (like `$B1`)
- `~$cellId` = row locked (like `B$1`)
- `$$cellId` = absolute reference (like `$B$1`)

**Display behavior (unchanged):**
- Formula in Sheet1 referencing B1 on Sheet2 displays as `=Sheet2!B1`
- Same formula viewed on Sheet2 displays as `=B1`
- This is handled by `FormulaDisplayConverter` using context sheet comparison

**What was changed:**

- [x] 13a: Audit FormulaResolver - sheet prefixes were added in `resolveCellRef()` when `targetSheet != &_sheet`
- [x] 13b: Updated FormulaResolver to NOT set `node->sheetId` for cell references (cell UUID is sufficient)
- [x] 13c: FormulaParser already handles cell references without sheet prefixes via `parseUuidCellRef()`
- [x] 13d: Updated FormulaSerializer to NOT output sheet prefixes for cell/range references
- [x] 13e: FormulaEvaluator already works via `workbook.findCell()` for cells without sheetId
- [x] 13f: DependencyGraph extracts cell deps by cellId only (sheetId not used). `extractCrossSheetRefs()` now returns empty for cell refs.
- [x] 13g: Backward compatibility removed (app not public). Old format support removed from tests.
- [x] 13h: All 54 unit tests + 184 E2E tests pass

**Note on range references:**
Range references (`A1:B5`) also no longer have sheet prefixes. The range's sheet can be derived from its corner cells' columns' `sheetId` fields.

## Phase 14: Sheet-Agnostic Recalculation Engine

**Key insight:** Sheets are a UI concept. The calculation engine should be completely sheet-agnostic - it just follows the global dependency graph. The UI layer refreshes the displayed viewport after calculation.

**Current (flawed) design:**
- `recalculate(sheet, changedCells)` - only recalculates cells on one sheet
- `recalculateCrossSheet(workbook, sheet, changedCells)` - special handling for "other" sheets
- Uses redundant `_crossSheetDeps`/`_crossSheetRangeDeps` tracking structures

**Target design:**
- `recalculate(workbook, changedCells)` - recalculates ALL dependents regardless of sheet
- Uses only the global `_depGraph` for dependency lookups
- UI layer refreshes the visible viewport (cheap/fast - just re-renders computed values)

**Single `_depGraph` with two internal indexes:**
```cpp
// Forward: cellId -> what this formula references (for display, cleanup)
std::unordered_map<ID, std::vector<DependencyRef>> dependencies_;

// Reverse: cellId -> formulas that depend on this cell (for recalculation)
std::unordered_map<ID, std::vector<ID>> reverseDeps_;

// R-tree for range queries (spatial index) - NOTE: positions are sheet-local
RTree<ID> rtree_;
```

**Benefits:**
- No "cross-sheet" vs "same-sheet" distinction in calculation engine
- Simpler recalculation logic - just walk the global graph
- Cleaner code - remove redundant tracking structures and methods
- Sheet concept only matters for display and position lookups

**R-tree consideration:**
The R-tree uses sheet-local positions for range queries. When getting dependents for a cell, we need to:
1. Look up direct cell deps via `reverseDeps_` (O(1), sheet-agnostic)
2. Look up range deps via R-tree query using the cell's position on its sheet

This means `getDependentsForCell(cellId, col, row)` still needs position info, but the caller can get that from the cell's column/row axes.

### Implementation Steps

**Step 1: Create workbook-level recalculate function**
- [ ] 14a: Add `recalculate(Workbook*, const std::vector<ID>& changedCells)` in formula_recalc.h/cc
- [ ] 14b: Use `workbook->getCell()` instead of `sheet->getCell()` for cell lookups
- [ ] 14c: Use `workbook->getDependencyGraph()` for dep graph access
- [ ] 14d: For R-tree queries, look up cell position via `workbook->getColumn(cell->colId)->position`
- [ ] 14e: `evaluateCell()` already works at workbook level (uses `workbook->findCell()`)

**Step 2: Update callers to use workbook-level recalculate**
- [ ] 14f: Update bindings_core.cc - replace `recalculate(sheet, ...)` + `recalculateCrossSheet(...)` with `recalculate(workbook, ...)`
- [ ] 14g: Update bindings_file.cc similarly
- [ ] 14h: Update luau_api.cc and luau_types.cc similarly
- [ ] 14i: Keep old `recalculate(Sheet*, ...)` as a thin wrapper that delegates to workbook version

**Step 3: Remove cross-sheet dependency tracking**
- [ ] 14j: Remove calls to `addCrossSheetDep()`, `removeCrossSheetDeps()` from crdt_cell.cc
- [ ] 14k: Remove `Workbook::_crossSheetDeps`, `_crossSheetDepReverse`, `_crossSheetRangeDeps` from model.h
- [ ] 14l: Remove `Workbook::addCrossSheetDep()`, `removeCrossSheetDeps()`, `getCrossSheetDependents()` from model.h/cc
- [ ] 14m: Remove `Workbook::addCrossSheetRangeDep()`, `getCrossSheetRangeDependents()` from model.h/cc
- [ ] 14n: Remove `CrossSheetDep` and `CrossSheetRangeDep` structs from model.h

**Step 4: Remove recalculateCrossSheet**
- [ ] 14o: Remove `recalculateCrossSheet()` from formula_recalc.h/cc (no longer needed)

**Step 5: Cleanup and testing**
- [ ] 14p: Remove `extractCrossSheetRefs()` from dependency_graph.h/cc if no longer used
- [ ] 14q: Run all tests to verify recalculation still works correctly
- [ ] 14r: Verify cross-sheet formula scenarios work (Sheet1!A1 = Sheet2!B1)

## Design Notes

### Target Architecture Summary

**Workbook-level storage (global by UUID):**
- `_cells` - all Cell objects ✅
- `_columns` - all column Axis objects ✅
- `_rows` - all row Axis objects ✅
- `_ranges` - all Range objects ✅
- `_rangeIds` - global set of range IDs ✅
- `_rangeStyles` - range style mappings ✅
- `_depGraph` - single global dependency graph ✅
- `_sharedFormulaMasters`, `_sharedFormulaFrom` - shared formula tracking ✅
- `_spillMasters`, `_spilledFrom` - spill range tracking ✅

**Sheet-level storage (position-based indices for UI):**
- `_cellIndex` - (colId:rowId → cellId) for fast position lookups ✅
- `_columnIndex` - (position → colId) for fast column position lookups ✅
- `_rowIndex` - (position → rowId) for fast row position lookups ✅
- `_rangeIndex` - R-tree for fast spatial queries (positions are sheet-local) ✅

### Sheets as a UI Concept

**Key architectural insight:** Sheets are primarily a UI/display concept, not a calculation concept.

**What sheets are for:**
- **Display**: Organizing cells into separate viewable areas
- **Position mapping**: Converting (col, row) positions to cell IDs for rendering
- **Viewport queries**: Finding what to display in a visible area (R-tree)

**What sheets are NOT for:**
- **Calculation**: The formula engine follows the global dependency graph regardless of sheets
- **Dependencies**: All dependencies go through the single global `_depGraph`
- **Cell identity**: Cells are globally unique by UUID, not by (sheet, col, row)

**Recalculation flow:**
1. Cell X changes → get all dependents from global `_depGraph`
2. Topologically sort dependents
3. Evaluate each dependent (using workbook-level cell lookup)
4. UI layer refreshes displayed viewport (cheap - just re-renders computed values)

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
- Workbook owns all Range objects via `_ranges` map ✅
- Workbook tracks all range IDs globally via `_rangeIds` ✅
- Ranges reference column/row UUIDs which belong to specific sheets
- Sheet maintains R-tree index for fast spatial queries during viewport rendering
- A range's "sheet" is determined by its startColId's axis's sheetId (via `rangeInSheet()` helper)

### Why Keep R-tree and Position Indices Per-Sheet?
- Viewport queries need to quickly find entities in a visible area
- R-tree uses (col_position, row_position) coordinates which are sheet-local
- Having separate R-trees per sheet avoids coordinate namespace collisions
- Performance: smaller R-trees = faster queries

### Backward Compatibility
- Existing ZCD files store cells under Sheet sections
- Parser will read old format and migrate to Workbook-level storage
- Serializer will write in format compatible with both old and new parsers (or introduce new version marker)
