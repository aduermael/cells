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
Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const std::string& payload);

// Generate a CELL_CLEAR operation for a cell.
Operation makeCellClearOp(Workbook& workbook, const ID& cellId);

// Generate a CELL_SET_FORMAT operation to set a cell's number format.
// Payload: {"format_id":"FMT_C002"} or {"format_id":"~"} for default
Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const std::string& payload);

// Generate a CELL_SET_STYLE operation to set a cell's style.
// Payload: {"style_id":"..."} or {"style_id":"~"} for default/no style
Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const std::string& payload);

// Generate a DIM_INSERT_AXIS operation for inserting a column or row.
Operation makeDimInsertAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Generate a DIM_DELETE_AXIS operation for deleting a column or row.
Operation makeDimDeleteAxisOp(Workbook& workbook, const ID& axisId);

// Generate a DIM_RESIZE_AXIS operation for resizing a column or row.
Operation makeDimResizeAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Generate a DIM_MOVE_AXIS operation for moving a column or row to a new position.
Operation makeDimMoveAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Generate a DIM_RENAME_AXIS operation for renaming a column or row.
// DEPRECATED: Use makeColRenameOp instead (rows cannot be renamed).
Operation makeDimRenameAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Column operations (preferred over DIM_* operations)
Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColDeleteOp(Workbook& workbook, const ID& axisId);
Operation makeColResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeColRenameOp(Workbook& workbook, const ID& axisId, const std::string& payload);

// Row operations
Operation makeRowInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeRowDeleteOp(Workbook& workbook, const ID& axisId);
Operation makeRowResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload);
Operation makeRowMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload);
// Note: No makeRowRenameOp - rows cannot be renamed

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

// Generate a STYLE_DEFINE operation for defining a cell style.
// Payload: JSON with style properties (bold, italic, bgColor, etc.)
Operation makeStyleDefineOp(Workbook& workbook, const ID& styleId, const std::string& payload);

// Generate a NAMED_RANGE_DEFINE operation for defining a named range.
// Payload: JSON with named range definition
// {"name":"MyRange","scope":"W","scopeSheetId":"-",
//  "targetType":"CELL","id1":"...","id2":"-","targetSheetId":"..."}
Operation makeNamedRangeDefineOp(Workbook& workbook, const std::string& payload);

// Generate a NAMED_RANGE_DELETE operation for deleting a named range.
// Payload: {"name":"MyRange","scope":"W","scopeSheetId":"-"}
Operation makeNamedRangeDeleteOp(Workbook& workbook, const std::string& payload);

// Bootstrap the OpLog with the current workbook state.
// Called when transitioning from OFFLINE to COLLABORATING mode.
// Generates operations for all existing axes and cells in HLC order.
// Returns the number of operations created.
size_t bootstrapOpLog(Workbook& workbook);

}  // namespace cells

#endif  // CELLS_CRDT_H_
