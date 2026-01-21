// =============================================================================
// CRDT Range Operations
// =============================================================================
//
// Implements CRDT operation handlers for the unified Range system.
// These operations manage Range creation, removal, and modification.
//
// Operation types:
// - RANGE_ADD: Create a new range with corners and flags
// - RANGE_REMOVE: Delete a range by ID
// - RANGE_UPDATE_CORNERS: Update range corner IDs (resize/move)
// - RANGE_UPDATE_FLAGS: Update range flags bitmask
// - RANGE_SET_STYLE: Set style metadata for ranges with RANGE_STYLE flag
//
// Conflict resolution:
// - AddRange: LWW - highest HLC wins for same range ID
// - UpdateCorners/Flags/Style: LWW - highest HLC wins
// - Remove vs Update: Update resurrects (no data loss)
// - Overlapping merge ranges: Both coexist - UI layer handles display
//   (Two overlapping RANGE_MERGE regions both remain; the application
//   can query which ranges contain a cell and decide how to render)
//
// Note on overlapping ranges:
// The CRDT layer does NOT prevent overlapping ranges. It's valid for
// two merge ranges to overlap (e.g., A1:C3 and B2:D4). This allows:
// - Concurrent edits to both succeed (no lost work)
// - UI/application layer to detect and handle conflicts as appropriate
// - Users to manually resolve overlaps via unmerge operations
//
// For style ranges, overlapping is intentional - CSS-like inheritance
// applies styles in creation order (or priority field if added later).
//
// =============================================================================

#include <memory>

#include "core/cells/crdt_internal.h"
#include "core/cells/model.h"
#include "core/cells/range.h"

namespace cells {
namespace internal {

// =============================================================================
// RANGE_ADD Operation
// =============================================================================
//
// Payload: {"start_col_id":"...","start_row_id":"...",
//           "end_col_id":"...","end_row_id":"...","flags":N}
//
// Creates a new range in the sheet that contains the start column.
// The sheet is derived from the start column ID (columns belong to exactly one sheet).
// The range ID is the operation's target_id. If a range with this ID already exists,
// the operation is ignored (LWW handled by comparing HLCs in the main dispatcher).
//

ApplyResult applyRangeAdd(Workbook& workbook, const Operation& op) {
    // Extract corner IDs
    const std::string startColIdStr = extractJSONString(op.payload, "start_col_id");
    const std::string startRowIdStr = extractJSONString(op.payload, "start_row_id");
    const std::string endColIdStr = extractJSONString(op.payload, "end_col_id");
    const std::string endRowIdStr = extractJSONString(op.payload, "end_row_id");

    if (startColIdStr.empty() || startRowIdStr.empty() || endColIdStr.empty() ||
        endRowIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    // Derive sheet from start column ID (columns belong to exactly one sheet)
    const ID startColId(startColIdStr);
    Sheet* sheet = workbook.findAxisSheet(startColId);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check if range already exists
    if (sheet->getRange(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    // Extract flags (default to NONE if not specified)
    const int flagsInt = extractJSONInt(op.payload, "flags", 0);
    const auto flags = static_cast<RangeFlags>(flagsInt);

    // Create the range
    auto range = std::make_unique<Range>(op.target_id, startColId, ID(startRowIdStr),
                                         ID(endColIdStr), ID(endRowIdStr), flags);

    // Add to sheet
    const Range* added = sheet->addRange(std::move(range));
    if (added == nullptr) {
        return ApplyResult::ALREADY_APPLIED;  // ID collision
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// RANGE_REMOVE Operation
// =============================================================================
//
// Payload: {"sheet_id":"..."}
//
// Removes a range from the specified sheet by its ID (op.target_id).
//

ApplyResult applyRangeRemove(Workbook& workbook, const Operation& op) {
    // Extract sheet ID from payload
    const std::string sheetIdStr = extractJSONString(op.payload, "sheet_id");
    if (sheetIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const ID sheetId(sheetIdStr);
    Sheet* sheet = workbook.getSheet(sheetId);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Remove the range
    const bool removed = sheet->removeRange(op.target_id);
    if (!removed) {
        // Range didn't exist - could have been removed by an earlier operation
        return ApplyResult::SUCCESS;  // Idempotent
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// RANGE_UPDATE_CORNERS Operation
// =============================================================================
//
// Payload: {"sheet_id":"...","start_col_id":"...","start_row_id":"...",
//           "end_col_id":"...","end_row_id":"..."}
//
// Updates the corner IDs of an existing range (resize/move operation).
// If the range doesn't exist, this operation creates it (resurrects).
//

ApplyResult applyRangeUpdateCorners(Workbook& workbook, const Operation& op) {
    // Extract sheet ID from payload
    const std::string sheetIdStr = extractJSONString(op.payload, "sheet_id");
    if (sheetIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const ID sheetId(sheetIdStr);
    Sheet* sheet = workbook.getSheet(sheetId);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Extract corner IDs
    const std::string startColIdStr = extractJSONString(op.payload, "start_col_id");
    const std::string startRowIdStr = extractJSONString(op.payload, "start_row_id");
    const std::string endColIdStr = extractJSONString(op.payload, "end_col_id");
    const std::string endRowIdStr = extractJSONString(op.payload, "end_row_id");

    if (startColIdStr.empty() || startRowIdStr.empty() || endColIdStr.empty() ||
        endRowIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    Range* range = sheet->getRange(op.target_id);
    if (range == nullptr) {
        // Range doesn't exist - create it with updated corners (resurrect)
        auto newRange = std::make_unique<Range>(op.target_id, ID(startColIdStr), ID(startRowIdStr),
                                                ID(endColIdStr), ID(endRowIdStr), RangeFlags::NONE);
        sheet->addRange(std::move(newRange));
        return ApplyResult::RESURRECTED;
    }

    // Update corners
    range->startColId = ID(startColIdStr);
    range->startRowId = ID(startRowIdStr);
    range->endColId = ID(endColIdStr);
    range->endRowId = ID(endRowIdStr);

    // Update spatial index
    sheet->updateRangeIndex(range);

    return ApplyResult::SUCCESS;
}

// =============================================================================
// RANGE_UPDATE_FLAGS Operation
// =============================================================================
//
// Payload: {"sheet_id":"...","flags":N}
//
// Updates the flags bitmask of an existing range.
// If the range doesn't exist, this operation is ignored.
//

ApplyResult applyRangeUpdateFlags(Workbook& workbook, const Operation& op) {
    // Extract sheet ID from payload
    const std::string sheetIdStr = extractJSONString(op.payload, "sheet_id");
    if (sheetIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const ID sheetId(sheetIdStr);
    Sheet* sheet = workbook.getSheet(sheetId);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    Range* range = sheet->getRange(op.target_id);
    if (range == nullptr) {
        // Range doesn't exist - cannot update flags without corners
        return ApplyResult::INVALID_TARGET;
    }

    // Extract flags
    const int flagsInt = extractJSONInt(op.payload, "flags", 0);
    range->flags = static_cast<RangeFlags>(flagsInt);

    return ApplyResult::SUCCESS;
}

// =============================================================================
// RANGE_SET_STYLE Operation
// =============================================================================
//
// Two payload formats supported:
//
// Old format (DEPRECATED): {"style_id":"..."}
//   The style_id references a style defined via STYLE_DEFINE operation.
//   If style_id is "~" or empty, clears the style.
//
// New format (content-addressed): {"style":"<base64>"}
//   The style field contains a base64-encoded StyleBuffer.
//   If style is empty "", clears the style.
//   The style data is stored directly in the Range struct.
//
// The range is identified by op.target_id (the range's UUID).
// The sheet is derived from the range's startColId (columns belong to exactly one sheet).
//
// Note: During migration, both formats are supported. The apply function
// checks for "style" field first (new format), then falls back to "style_id" (old).
//

ApplyResult applyRangeSetStyle(Workbook& workbook, const Operation& op) {
    // Get the range by ID from workbook-level storage
    Range* range = workbook.getRange(op.target_id);
    if (range == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Derive sheet from range's startColId (columns belong to exactly one sheet)
    Sheet* sheet = workbook.findAxisSheet(range->startColId);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for new format first: {"style":"<base64>"}
    const std::string styleBase64 = extractJSONString(op.payload, "style");
    if (op.payload.find("\"style\"") != std::string::npos) {
        // New format - content-addressed style
        if (styleBase64.empty()) {
            // Clear style
            sheet->clearRangeStyle(range->id);
        } else {
            // Decode base64 and set style
            auto styleOpt = StyleBuffer::fromBase64(styleBase64);
            if (!styleOpt.has_value()) {
                return ApplyResult::INVALID_PAYLOAD;
            }
            sheet->setRangeStyle(range->id, std::move(styleOpt.value()));
        }
        return ApplyResult::SUCCESS;
    }

    // Fall back to old format: {"style_id":"..."}
    const std::string styleIdStr = extractJSONString(op.payload, "style_id");

    // Set the style association (or remove if styleId is empty/null)
    // setRangeStyleId handles both the mapping and the RANGE_STYLE flag
    if (styleIdStr.empty() || styleIdStr == "~") {
        sheet->setRangeStyleId(range->id, {});  // Remove style (old system)
    } else {
        sheet->setRangeStyleId(range->id, ID(styleIdStr));
    }

    return ApplyResult::SUCCESS;
}

}  // namespace internal
}  // namespace cells
