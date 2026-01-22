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
// - Cell value conflicts: Last-Writer-Wins (highest HLC wins)
// - Axis insert conflicts: Interleave by HLC (lower HLC comes first)
// - Delete vs edit conflicts: Edit resurrects the entity (no data loss)
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
// - Cell value conflicts: Last-Writer-Wins (higher HLC wins)
// - Axis insert conflicts: Interleave by HLC (lower HLC comes first)
// - Delete vs edit conflicts: Edit resurrects (no data loss)
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

// Generate a CELL_SET_VALUE operation for a cell.
// Creates the operation with current HLC from the workbook.
// The sheetId version specifies which sheet the cell belongs to.
Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const std::string& payload);
Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const ID& sheetId,
                             const std::string& payload);

// Generate a CELL_CLEAR operation for a cell.
Operation makeCellClearOp(Workbook& workbook, const ID& cellId);

// Generate a CELL_SET_FORMAT operation to set a cell's number format.
// Payload: {"format_id":"FMT_C002"} or {"format_id":"~"} for default
Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const std::string& payload);

// Generate a CELL_SET_STYLE operation to set a cell's style from raw JSON payload.
// Payload: {"style":"<base64>"} or {"style":""} to clear
// Prefer using the StyleBuffer overload below for type safety.
Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const std::string& payload);

// Generate a CELL_SET_STYLE operation using content-addressed StyleBuffer.
// Payload format: {"style":"<base64-encoded-stylebuffer>"}
// This is the new format for content-addressed styles.
// target_id: the cell's UUID
Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const StyleBuffer& style);

// Generate a CELL_SET_STYLE operation to clear the style.
// Payload format: {"style":""}
Operation makeCellClearStyleOp(Workbook& workbook, const ID& cellId);

// Column operations
// The sheetId version specifies which sheet the column belongs to.
Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const ID& sheetId,
                          const std::string& payload);
Operation makeColDeleteOp(Workbook& workbook, const ID& axisId);
Operation makeColResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColRenameOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Row operations
// The sheetId version specifies which sheet the row belongs to.
Operation makeRowInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeRowInsertOp(Workbook& workbook, const ID& axisId, const ID& sheetId,
                          const std::string& payload);
Operation makeRowDeleteOp(Workbook& workbook, const ID& axisId);
Operation makeRowResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeRowMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload);
// Note: No makeRowRenameOp - rows cannot be renamed

// Axis operations (apply to both columns and rows)
// payload: "1" for hidden, "0" for visible
Operation makeAxisSetHiddenOp(Workbook& workbook, const ID& axisId, bool hidden);
// payload: {"style":"<base64-encoded-stylebuffer>"} for content-addressed styles
Operation makeAxisSetStyleOp(Workbook& workbook, const ID& axisId, const StyleBuffer& style);
// Clear axis style - payload: {"style":""}
Operation makeAxisClearStyleOp(Workbook& workbook, const ID& axisId);
// payload: format ID string, or empty string to clear format
Operation makeAxisSetFormatOp(Workbook& workbook, const ID& axisId, const ID& formatId);

// Generate a SHEET_CREATE operation.
Operation makeSheetCreateOp(Workbook& workbook, const ID& sheetId, const std::string& payload);

// Generate a SHEET_DELETE operation.
Operation makeSheetDeleteOp(Workbook& workbook, const ID& sheetId);

// Generate a SHEET_RENAME operation.
Operation makeSheetRenameOp(Workbook& workbook, const ID& sheetId, const std::string& payload);

// Generate a WORKBOOK_RENAME operation.
Operation makeWorkbookRenameOp(Workbook& workbook, const std::string& payload);

// Generate a FORMAT_DEFINE operation for defining a custom number format.
// Payload: {"format_id":"...","format_code":"..."}
Operation makeFormatDefineOp(Workbook& workbook, const ID& formatId, const std::string& payload);

// Generate a NAMED_RANGE_DEFINE operation for defining a named range.
// Payload: JSON with named range definition
// {"name":"MyRange","scope":"W","scopeSheetId":"-",
//  "targetType":"CELL","id1":"...","id2":"-","targetSheetId":"..."}
Operation makeNamedRangeDefineOp(Workbook& workbook, const std::string& payload);

// Generate a NAMED_RANGE_DELETE operation for deleting a named range.
// Payload: {"name":"MyRange","scope":"W","scopeSheetId":"-"}
Operation makeNamedRangeDeleteOp(Workbook& workbook, const std::string& payload);

// =============================================================================
// Range Operations (Unified Range System)
// =============================================================================

// Generate a RANGE_ADD operation for adding a new range.
// Payload: {"sheet_id":"...","start_col_id":"...","start_row_id":"...",
//           "end_col_id":"...","end_row_id":"...","flags":N}
// target_id: the range's own UUID
Operation makeRangeAddOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Generate a RANGE_REMOVE operation for removing a range.
// Payload: {"sheet_id":"..."} - sheet where range exists
// target_id: the range's UUID
Operation makeRangeRemoveOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Generate a RANGE_UPDATE_CORNERS operation for updating range bounds.
// Payload: {"sheet_id":"...","start_col_id":"...","start_row_id":"...",
//           "end_col_id":"...","end_row_id":"..."}
// target_id: the range's UUID
Operation makeRangeUpdateCornersOp(Workbook& workbook, const ID& rangeId,
                                   const std::string& payload);

// Generate a RANGE_UPDATE_FLAGS operation for updating range flags.
// Payload: {"sheet_id":"...","flags":N}
// target_id: the range's UUID
Operation makeRangeUpdateFlagsOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Generate a RANGE_SET_STYLE operation for setting range style metadata.
// Payload: {"style":"<base64>"} or {"style":""} to clear
// target_id: the range's UUID
// For new code, prefer using the StyleBuffer overload below.
Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const std::string& payload);

// Generate a RANGE_SET_STYLE operation using content-addressed StyleBuffer.
// Payload format: {"style":"<base64-encoded-stylebuffer>"}
// This is the new format for content-addressed styles.
// target_id: the range's UUID
Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const StyleBuffer& style);

// Generate a RANGE_SET_STYLE operation to clear the style.
// Payload format: {"style":""}
// target_id: the range's UUID
Operation makeRangeClearStyleOp(Workbook& workbook, const ID& rangeId);

// Bootstrap the OpLog with the current workbook state.
// Called when transitioning from OFFLINE to COLLABORATING mode.
// Generates operations for all existing axes and cells in HLC order.
// Returns the number of operations created.
size_t bootstrapOpLog(Workbook& workbook);

}  // namespace cells

#endif  // CELLS_CRDT_H_
