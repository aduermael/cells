// =============================================================================
// CRDT Range Operations
// =============================================================================
//
// Implements CRDT operation handlers for the unified Range system.
// These operations manage Range creation, removal, and modification.
//
// Operation types:
// - RANGE_SET: Create/update range with corners, flags, style, format
// - RANGE_DELETE: Delete range by ID
//
// Conflict resolution:
// - RANGE_SET: LWW - highest HLC wins, creates if doesn't exist
// - RANGE_DELETE vs RANGE_SET: SET resurrects (no data loss)
//
// Note on overlapping ranges:
// The CRDT layer does NOT prevent overlapping ranges. It's valid for
// two merge ranges to overlap. This allows:
// - Concurrent edits to both succeed (no lost work)
// - UI/application layer to detect and handle conflicts as appropriate
//
// =============================================================================

#include <memory>

#include "core/cells/crdt_internal.h"
#include "core/cells/format_buffer.h"
#include "core/cells/model.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

namespace cells {
namespace internal {

// =============================================================================
// RANGE_SET - Create or update range
// =============================================================================
// Payload: {"startCol":"...","startRow":"...","endCol":"...","endRow":"...",
//           "flags":N,"sty":"base64","fmt":"base64"}
// All fields optional except corners are required when creating

ApplyResult applyRangeSet(Workbook& workbook, const Operation& op) {
    // Extract corner IDs
    const std::string startColIdStr = extractJSONString(op.payload, "startCol");
    const std::string startRowIdStr = extractJSONString(op.payload, "startRow");
    const std::string endColIdStr = extractJSONString(op.payload, "endCol");
    const std::string endRowIdStr = extractJSONString(op.payload, "endRow");

    // Get existing range if any
    Range* range = workbook.getRange(op.target_id);
    const bool creating = (range == nullptr);

    Sheet* sheet = nullptr;
    if (creating) {
        // Need corners to create
        if (startColIdStr.empty() || startRowIdStr.empty() || endColIdStr.empty() ||
            endRowIdStr.empty()) {
            return ApplyResult::INVALID_PAYLOAD;
        }

        // Derive sheet from start column ID
        const ID startColId(startColIdStr);
        sheet = workbook.findAxisSheet(startColId);
        if (sheet == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }

        // Extract flags (default to NONE)
        const int flagsInt = extractJSONInt(op.payload, "flags", 0);
        const auto flags = static_cast<RangeFlags>(flagsInt);

        // Create the range
        auto newRange = std::make_unique<Range>(op.target_id, startColId, ID(startRowIdStr),
                                                ID(endColIdStr), ID(endRowIdStr), flags);
        range = sheet->addRange(std::move(newRange));
        if (range == nullptr) {
            return ApplyResult::ALREADY_APPLIED;
        }
    } else {
        // Get sheet from existing range
        sheet = workbook.findAxisSheet(range->startColId);
        if (sheet == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc && !creating) {
        return ApplyResult::SUPERSEDED;
    }

    // Update corners if provided
    if (!startColIdStr.empty() && !startRowIdStr.empty() && !endColIdStr.empty() &&
        !endRowIdStr.empty()) {
        range->startColId = ID(startColIdStr);
        range->startRowId = ID(startRowIdStr);
        range->endColId = ID(endColIdStr);
        range->endRowId = ID(endRowIdStr);
        sheet->updateRangeIndex(range);
    }

    // Update flags if provided
    if (op.payload.find("\"flags\":") != std::string::npos) {
        const int flagsInt = extractJSONInt(op.payload, "flags", 0);
        range->flags = static_cast<RangeFlags>(flagsInt);
    }

    // Update style if provided
    const std::string style_str = extractJSONString(op.payload, "sty");
    if (!style_str.empty()) {
        auto maybeStyle = StyleBuffer::fromBase64(style_str);
        if (maybeStyle.has_value()) {
            sheet->setRangeStyle(range->id, std::move(maybeStyle.value()));
        }
    } else if (op.payload.find("\"sty\":\"\"") != std::string::npos) {
        sheet->clearRangeStyle(range->id);
    }

    // Update format if provided
    const std::string format_str = extractJSONString(op.payload, "fmt");
    if (!format_str.empty()) {
        auto maybeFormat = FormatBuffer::fromBase64(format_str);
        if (maybeFormat.has_value()) {
            range->setFormat(std::move(maybeFormat.value()));
        }
    } else if (op.payload.find("\"fmt\":\"\"") != std::string::npos) {
        range->clearFormat();
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// RANGE_DELETE - Delete range
// =============================================================================
// Payload: {"sheet":"sheetId"} or empty (derives from range)

ApplyResult applyRangeDelete(Workbook& workbook, const Operation& op) {
    // Try to find range in workbook storage first
    const Range* range = workbook.getRange(op.target_id);

    Sheet* sheet = nullptr;
    if (range != nullptr) {
        sheet = workbook.findAxisSheet(range->startColId);
    } else {
        // Try getting sheet from payload
        const std::string sheetIdStr = extractJSONString(op.payload, "sheet");
        if (!sheetIdStr.empty()) {
            sheet = workbook.getSheet(ID(sheetIdStr));
        }
    }

    if (sheet == nullptr) {
        // Range doesn't exist or can't find sheet - already deleted or never existed
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations that resurrect the range
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::RANGE_SET) {
            return ApplyResult::RESURRECTED;
        }
    }

    sheet->removeRange(op.target_id);
    return ApplyResult::SUCCESS;
}

}  // namespace internal
}  // namespace cells
