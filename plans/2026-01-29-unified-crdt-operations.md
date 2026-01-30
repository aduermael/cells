# Unified CRDT Operations

Simplify CRDT operation types from 26 to ~12 by using SET + DELETE pattern.

## Motivation

1. **CRDT resurrection semantics**: A SET after DELETE should revive the entity. Having INSERT as separate breaks this.
2. **Fewer operation types**: Easier to maintain, less code.
3. **Optional properties**: Only store what's explicitly set (e.g., no size unless user resizes).
4. **Unified property updates**: No need for separate CELL_SET_VALUE, CELL_SET_STYLE, CELL_SET_FORMAT.

## New Operation Types

| Type | Description |
|------|-------------|
| `COL_SET` | Create/update column (pos, size, name, style, fmt, hidden) |
| `COL_DELETE` | Delete column |
| `ROW_SET` | Create/update row (pos, size, style, fmt, hidden) |
| `ROW_DELETE` | Delete row |
| `CELL_SET` | Create/update cell (col, row, type, value, style, fmt) |
| `CELL_DELETE` | Clear/delete cell |
| `SHEET_SET` | Create/update sheet (name, pos) |
| `SHEET_DELETE` | Delete sheet |
| `RANGE_SET` | Create/update range (corners, flags, style, fmt) |
| `RANGE_DELETE` | Delete range |
| `WORKBOOK_SET` | Update workbook properties (name) |
| `NAMED_RANGE_SET` | Create/update named range |
| `NAMED_RANGE_DELETE` | Delete named range |

**Removed**: COL_INSERT, ROW_INSERT, COL_MOVE, ROW_MOVE, COL_RESIZE, ROW_RESIZE, COL_RENAME, AXIS_SET_HIDDEN, AXIS_SET_STYLE, AXIS_SET_FORMAT, CELL_SET_VALUE, CELL_SET_STYLE, CELL_SET_FORMAT, CELL_CLEAR, SHEET_CREATE, SHEET_RENAME, WORKBOOK_RENAME, NAMED_RANGE_DEFINE, RANGE_ADD, RANGE_REMOVE, RANGE_UPDATE_CORNERS, RANGE_UPDATE_FLAGS, RANGE_SET_STYLE, RANGE_SET_FORMAT

## Example Transformations

Before:
```
O ... COL_INSERT VWMXYf1B {"pos":1,"size":100}
O ... CELL_SET_VALUE 1B00DbYI {"type":"n","value":"10","col_id":"VWMXYf1B","row_id":"vDw1owqh"}
O ... AXIS_SET_STYLE VWMXYf1B {"style":"BAAB"}
O ... AXIS_SET_FORMAT VWMXYf1B {"format":"DwICAQEk"}
```

After:
```
O ... COL_SET VWMXYf1B {"pos":1}
O ... CELL_SET 1B00DbYI {"col":"VWMXYf1B","row":"vDw1owqh","t":"n","v":"10"}
O ... COL_SET VWMXYf1B {"sty":"BAAB","fmt":"DwICAQEk"}
```

## Payload Conventions

- Only include properties being set (sparse updates)
- Shortened keys for common properties: `col`, `row`, `t` (type), `v` (value), `sty`, `fmt`, `pos`
- Empty string or null to clear a property

## Phase 1: Define New Operation Types
- [ ] 1a: Update `OpType` enum in `operation.h` with new types
- [ ] 1b: Update `opTypeToString` and `stringToOpType` in `operation.cc`
- [ ] 1c: Add legacy type mapping for backwards compatibility (read old files)

## Phase 2: Update Operation Makers
- [ ] 2a: Replace `makeCellSetValueOp`, `makeCellSetStyleOp`, `makeCellSetFormatOp` with unified `makeCellSetOp`
- [ ] 2b: Replace `makeColInsertOp`, `makeColResizeOp`, `makeColMoveOp`, `makeColRenameOp`, axis style/format ops with unified `makeColSetOp`
- [ ] 2c: Same for row operations → `makeRowSetOp`
- [ ] 2d: Replace sheet create/rename with `makeSheetSetOp`
- [ ] 2e: Replace range ops with unified `makeRangeSetOp`
- [ ] 2f: Update workbook rename → `makeWorkbookSetOp`
- [ ] 2g: Update named range define → `makeNamedRangeSetOp`

## Phase 3: Update Operation Application
- [ ] 3a: Implement `applyCellSet` - create cell if needed, update provided properties
- [ ] 3b: Implement `applyColSet` - create column if needed, update provided properties, handle resurrection
- [ ] 3c: Implement `applyRowSet` - same pattern
- [ ] 3d: Implement `applySheetSet` - create/update sheet
- [ ] 3e: Implement `applyRangeSet` - create/update range
- [ ] 3f: Update delete handlers to mark as deleted (allow resurrection)
- [ ] 3g: Remove old apply functions

## Phase 4: Update Callers
- [ ] 4a: Update `bindings.cc` to use new operation makers
- [ ] 4b: Update `sync_manager.cc` if needed
- [ ] 4c: Update any other callers

## Phase 5: File Format
- [ ] 5a: Update snapshot serialization (`X`, `C`, `R` lines) to omit default values
- [ ] 5b: Update oplog parsing to handle both old and new formats
- [ ] 5c: Write migration test with old format file

## Phase 6: Cleanup
- [ ] 6a: Remove dead code (old operation types, old makers, old appliers)
- [ ] 6b: Update documentation
- [ ] 6c: Run full test suite
