// =============================================================================
// CRDT Operations API
// =============================================================================
//
// High-level API for applying and creating CRDT operations on a Workbook.
// This is the ONLY correct way to mutate the model when collaboration is active.
//
// Key responsibilities:
// - Apply operations from local edits or remote peers
// - Generate operations with proper HLC timestamps
// - Handle conflict resolution (LWW for cells, interleave for inserts)
// - Bootstrap OpLog when transitioning to collaboration mode
//
// Conflict resolution rules:
// - SET operations: Last-Writer-Wins (highest HLC wins), creates if needed
// - DELETE operations: SET resurrects deleted entities (no data loss)
//
// Usage pattern:
// 1. Create operation via make*Op() helper
// 2. Apply via applyOperation() to mutate model AND add to OpLog
// 3. SyncManager broadcasts to peers automatically
//
// Dependencies: model.h, operation.h
// Used by: bindings.cc (UI-triggered edits), sync_manager.cc (remote ops)
//
// =============================================================================

#ifndef CELLS_CRDT_H_
#define CELLS_CRDT_H_

#include <cstdint>

#include "core/cells/format_buffer.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/style_buffer.h"

namespace cells {

// Result of applying an operation
enum class ApplyResult : std::uint8_t {
    SUCCESS,          // Operation applied successfully
    ALREADY_APPLIED,  // Operation already in OpLog (duplicate)
    SUPERSEDED,       // A newer operation already exists for this entity
    INVALID_TARGET,   // Target entity not found (for cell/axis operations)
    INVALID_PAYLOAD,  // Payload could not be parsed
    RESURRECTED,      // Entity was deleted but resurrected by this edit
};

// Apply a CRDT operation to a workbook.
// Handles conflict resolution using HLC timestamps:
// - SET operations: Last-Writer-Wins (higher HLC wins)
// - DELETE vs SET: SET resurrects (no data loss)
//
// The operation is added to the OpLog if successful.
// Returns the result of applying the operation.
ApplyResult applyOperation(Workbook& workbook, const Operation& op);

// Apply multiple operations, typically from a sync response.
// Applies in HLC order to maintain consistency.
// Returns the number of operations successfully applied.
size_t applyOperations(Workbook& workbook, const std::vector<Operation>& ops);

// Check if an operation would be superseded by existing operations.
// Used to avoid unnecessary work when receiving old operations.
bool isSuperseded(const Workbook& workbook, const Operation& op);

// =============================================================================
// Cell Operations
// =============================================================================

// Generate a CELL_SET operation to create/update a cell.
// Payload: {"col":"colId","row":"rowId","t":"n","v":"42","sty":"base64","fmt":"base64"}
// All fields optional except col/row required when creating a new cell.
Operation makeCellSetOp(Workbook& workbook, const ID& cellId, const std::string& payload);
Operation makeCellSetOp(Workbook& workbook, const ID& cellId, const ID& sheetId,
                        const std::string& payload);

// Generate a CELL_DELETE operation to delete a cell.
Operation makeCellDeleteOp(Workbook& workbook, const ID& cellId);

// Convenience: Set/clear cell style (generates CELL_SET with sty field)
Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const StyleBuffer& style);
Operation makeCellClearStyleOp(Workbook& workbook, const ID& cellId);

// Convenience: Set/clear cell format (generates CELL_SET with fmt field)
Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const FormatBuffer& format);
Operation makeCellClearFormatOp(Workbook& workbook, const ID& cellId);

// =============================================================================
// Column Operations
// =============================================================================

// Generate a COL_SET operation to create/update a column.
// Payload: {"pos":N,"size":N,"name":"...","sty":"base64","fmt":"base64","hidden":bool}
// pos is required when creating a new column.
Operation makeColSetOp(Workbook& workbook, const ID& colId, const std::string& payload);
Operation makeColSetOp(Workbook& workbook, const ID& colId, const ID& sheetId,
                       const std::string& payload);

// Generate a COL_DELETE operation to delete a column.
Operation makeColDeleteOp(Workbook& workbook, const ID& colId);

// Convenience: Set/clear column style (generates COL_SET with sty field)
Operation makeAxisSetStyleOp(Workbook& workbook, const ID& axisId, const StyleBuffer& style);
Operation makeAxisClearStyleOp(Workbook& workbook, const ID& axisId);

// Convenience: Set/clear column format (generates COL_SET with fmt field)
Operation makeAxisSetFormatOp(Workbook& workbook, const ID& axisId, const FormatBuffer& format);
Operation makeAxisClearFormatOp(Workbook& workbook, const ID& axisId);

// Convenience: Set hidden state for column/row
Operation makeAxisSetHiddenOp(Workbook& workbook, const ID& axisId, bool hidden);

// =============================================================================
// Row Operations
// =============================================================================

// Generate a ROW_SET operation to create/update a row.
// Payload: {"pos":N,"size":N,"sty":"base64","fmt":"base64","hidden":bool}
// pos is required when creating a new row.
Operation makeRowSetOp(Workbook& workbook, const ID& rowId, const std::string& payload);
Operation makeRowSetOp(Workbook& workbook, const ID& rowId, const ID& sheetId,
                       const std::string& payload);

// Generate a ROW_DELETE operation to delete a row.
Operation makeRowDeleteOp(Workbook& workbook, const ID& rowId);

// =============================================================================
// Sheet Operations
// =============================================================================

// Generate a SHEET_SET operation to create/update a sheet.
// Payload: {"name":"...","pos":N}
Operation makeSheetSetOp(Workbook& workbook, const ID& sheetId, const std::string& payload);

// Generate a SHEET_DELETE operation to delete a sheet.
Operation makeSheetDeleteOp(Workbook& workbook, const ID& sheetId);

// =============================================================================
// Workbook Operations
// =============================================================================

// Generate a WORKBOOK_SET operation to update workbook properties.
// Payload: {"name":"..."}
Operation makeWorkbookSetOp(Workbook& workbook, const std::string& payload);

// =============================================================================
// Named Range Operations
// =============================================================================

// Generate a NAMED_RANGE_SET operation to create/update a named range.
// Payload: {"name":"MyRange","scope":"W","scopeSheetId":"-",
//           "targetType":"CELL","id1":"...","id2":"-","targetSheetId":"..."}
Operation makeNamedRangeSetOp(Workbook& workbook, const std::string& payload);

// Generate a NAMED_RANGE_DELETE operation to delete a named range.
// Payload: {"name":"MyRange","scope":"W","scopeSheetId":"-"}
Operation makeNamedRangeDeleteOp(Workbook& workbook, const std::string& payload);

// =============================================================================
// Range Operations
// =============================================================================

// Generate a RANGE_SET operation to create/update a range.
// Payload: {"startCol":"...","startRow":"...","endCol":"...","endRow":"...",
//           "flags":N,"sty":"base64","fmt":"base64"}
// Corner IDs required when creating a new range.
Operation makeRangeSetOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Generate a RANGE_DELETE operation to delete a range.
// Payload: {"sheet":"sheetId"} or empty
Operation makeRangeDeleteOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Convenience: Set/clear range style (generates RANGE_SET with sty field)
Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const StyleBuffer& style);
Operation makeRangeClearStyleOp(Workbook& workbook, const ID& rangeId);

// Convenience: Set range format (generates RANGE_SET with fmt field)
Operation makeRangeSetFormatOp(Workbook& workbook, const ID& rangeId, const FormatBuffer& format);

// =============================================================================
// Bootstrap
// =============================================================================

// Bootstrap the OpLog with the current workbook state.
// Called when transitioning from OFFLINE to COLLABORATING mode.
// Generates operations for all existing axes and cells in HLC order.
// Returns the number of operations created.
size_t bootstrapOpLog(Workbook& workbook);

}  // namespace cells

#endif  // CELLS_CRDT_H_
