// =============================================================================
// CRDT Internal API
// =============================================================================
//
// Internal header for CRDT operation application functions.
// These functions are implementation details of crdt.cc and should not be
// used directly by external code. Use applyOperation() from crdt.h instead.
//
// The split is organized by entity type:
// - crdt_cell.cc: Cell operations (CELL_SET, CELL_DELETE)
// - crdt_axis.cc: Axis operations (COL_SET, COL_DELETE, ROW_SET, ROW_DELETE, SHEET_*, etc)
// - crdt_range.cc: Range operations (RANGE_SET, RANGE_DELETE)
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

// Extract a boolean from JSON for the given key
bool extractJSONBool(const std::string& json, const std::string& key, bool defaultValue = false);

// Extract a double from JSON for the given key
double extractJSONDouble(const std::string& json, const std::string& key,
                         double defaultValue = 0.0);

// Resolve sheet for COL/ROW/CELL ops. If op.sheetId is set but the sheet is
// missing (common when peers never received a SHEET_SET because the sheet was
// created outside the CRDT), materialize it so dependent ops can apply.
Sheet* ensureSheetForOp(Workbook& workbook, const Operation& op);

// =============================================================================
// Cell operations (crdt_cell.cc)
// =============================================================================

// Apply CELL_SET operation - creates/updates cell with provided properties
ApplyResult applyCellSet(Workbook& workbook, const Operation& op);

// Apply CELL_DELETE operation - deletes cell
ApplyResult applyCellDelete(Workbook& workbook, const Operation& op);

// =============================================================================
// Column operations (crdt_axis.cc)
// =============================================================================

// Apply COL_SET operation - creates/updates column with provided properties
ApplyResult applyColSet(Workbook& workbook, const Operation& op);

// Apply COL_DELETE operation - deletes column and its cells
ApplyResult applyColDelete(Workbook& workbook, const Operation& op);

// =============================================================================
// Row operations (crdt_axis.cc)
// =============================================================================

// Apply ROW_SET operation - creates/updates row with provided properties
ApplyResult applyRowSet(Workbook& workbook, const Operation& op);

// Apply ROW_DELETE operation - deletes row and its cells
ApplyResult applyRowDelete(Workbook& workbook, const Operation& op);

// =============================================================================
// Sheet operations (crdt_axis.cc)
// =============================================================================

// Apply SHEET_SET operation - creates/updates sheet with provided properties
ApplyResult applySheetSet(Workbook& workbook, const Operation& op);

// Apply SHEET_DELETE operation - deletes sheet
ApplyResult applySheetDelete(Workbook& workbook, const Operation& op);

// =============================================================================
// Workbook operations (crdt_axis.cc)
// =============================================================================

// Apply WORKBOOK_SET operation - updates workbook properties
ApplyResult applyWorkbookSet(Workbook& workbook, const Operation& op);

// =============================================================================
// Named range operations (crdt_axis.cc)
// =============================================================================

// Apply NAMED_RANGE_SET operation - creates/updates named range
ApplyResult applyNamedRangeSet(Workbook& workbook, const Operation& op);

// Apply NAMED_RANGE_DELETE operation - deletes named range
ApplyResult applyNamedRangeDelete(Workbook& workbook, const Operation& op);

// =============================================================================
// Range operations (crdt_range.cc)
// =============================================================================

// Apply RANGE_SET operation - creates/updates range with provided properties
ApplyResult applyRangeSet(Workbook& workbook, const Operation& op);

// Apply RANGE_DELETE operation - deletes range
ApplyResult applyRangeDelete(Workbook& workbook, const Operation& op);

}  // namespace internal
}  // namespace cells

#endif  // CELLS_CRDT_INTERNAL_H_
