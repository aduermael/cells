// =============================================================================
// CRDT Internal API
// =============================================================================
//
// Internal header for CRDT operation application functions.
// These functions are implementation details of crdt.cc and should not be
// used directly by external code. Use applyOperation() from crdt.h instead.
//
// The split is organized by entity type:
// - crdt_cell.cc: Cell operations (set value, set format, clear)
// - crdt_axis.cc: Axis operations (column/row insert, delete, move, resize)
// - crdt.cc: Main dispatcher and operation makers
//
// =============================================================================

#ifndef CELLS_CRDT_INTERNAL_H_
#define CELLS_CRDT_INTERNAL_H_

#include <string>

#include "core/cells/crdt.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"

namespace cells {
namespace internal {

// =============================================================================
// JSON utilities (used by all crdt_*.cc files)
// =============================================================================

// Simple JSON string unescaping for parsing payloads
std::string jsonUnescape(const std::string& str);

// Simple JSON string escaping for payloads
std::string jsonEscape(const std::string& str);

// Extract a string value from JSON for the given key
std::string extractJSONString(const std::string& json, const std::string& key);

// Extract an integer from JSON for the given key
int extractJSONInt(const std::string& json, const std::string& key, int defaultValue = -1);

// Helper to extract size from payload (handles both string and numeric formats)
std::string extractSizePayload(const std::string& payload);

// =============================================================================
// Cell operations (crdt_cell.cc)
// =============================================================================

// Apply CELL_SET_VALUE operation
ApplyResult applyCellSetValue(Workbook& workbook, const Operation& op);

// Apply CELL_SET_FORMAT operation
ApplyResult applyCellSetFormat(Workbook& workbook, const Operation& op);

// Apply CELL_SET_STYLE operation
ApplyResult applyCellSetStyle(Workbook& workbook, const Operation& op);

// Apply CELL_CLEAR operation
ApplyResult applyCellClear(Workbook& workbook, const Operation& op);

// =============================================================================
// Axis operations (crdt_axis.cc)
// =============================================================================

// Column operations
ApplyResult applyColInsert(Workbook& workbook, const Operation& op);
ApplyResult applyColDelete(Workbook& workbook, const Operation& op);
ApplyResult applyColResize(Workbook& workbook, const Operation& op);
ApplyResult applyColMove(Workbook& workbook, const Operation& op);
ApplyResult applyColRename(Workbook& workbook, const Operation& op);

// Row operations
ApplyResult applyRowInsert(Workbook& workbook, const Operation& op);
ApplyResult applyRowDelete(Workbook& workbook, const Operation& op);
ApplyResult applyRowResize(Workbook& workbook, const Operation& op);
ApplyResult applyRowMove(Workbook& workbook, const Operation& op);

// Axis operations (apply to both columns and rows)
ApplyResult applyAxisSetHidden(Workbook& workbook, const Operation& op);
ApplyResult applyAxisSetStyle(Workbook& workbook, const Operation& op);
ApplyResult applyAxisSetFormat(Workbook& workbook, const Operation& op);

// Legacy DIM_* operations (backwards compatibility)
ApplyResult applyDimInsertAxis(Workbook& workbook, const Operation& op);
ApplyResult applyDimResizeAxis(Workbook& workbook, const Operation& op);
ApplyResult applyDimMoveAxis(Workbook& workbook, const Operation& op);
ApplyResult applyDimRenameAxis(Workbook& workbook, const Operation& op);

// Sheet operations
ApplyResult applySheetCreate(Workbook& workbook, const Operation& op);
ApplyResult applySheetDelete(Workbook& workbook, const Operation& op);
ApplyResult applySheetRename(Workbook& workbook, const Operation& op);

// Workbook operations
ApplyResult applyWorkbookRename(Workbook& workbook, const Operation& op);

// Format operations
ApplyResult applyFormatDefine(Workbook& workbook, const Operation& op);

// Style operations
ApplyResult applyStyleDefine(Workbook& workbook, const Operation& op);

// Named range operations
ApplyResult applyNamedRangeDefine(Workbook& workbook, const Operation& op);
ApplyResult applyNamedRangeDelete(Workbook& workbook, const Operation& op);

// Range operations (unified range system)
ApplyResult applyRangeAdd(Workbook& workbook, const Operation& op);
ApplyResult applyRangeRemove(Workbook& workbook, const Operation& op);
ApplyResult applyRangeUpdateCorners(Workbook& workbook, const Operation& op);
ApplyResult applyRangeUpdateFlags(Workbook& workbook, const Operation& op);
ApplyResult applyRangeSetStyle(Workbook& workbook, const Operation& op);

}  // namespace internal
}  // namespace cells

#endif  // CELLS_CRDT_INTERNAL_H_
