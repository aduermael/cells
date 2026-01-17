// =============================================================================
// CRDT Axis Operations
// =============================================================================
//
// Implementation of axis-level (column/row) CRDT operations for the spreadsheet.
// Axes define the grid structure - columns and rows have UUIDs, positions, and sizes.
//
// Key responsibilities:
// - Insert/delete/move/resize columns and rows
// - Handle legacy DIM_* operations for backwards compatibility
// - Apply sheet and workbook operations (create, delete, rename)
// - Apply format definition operations
// - Manage position updates when axes are moved or deleted
//
// Conflict resolution:
// - Inserts: Interleave by HLC timestamp (lower HLC comes first)
// - Moves/resizes: Last-Writer-Wins (highest HLC wins)
// - Delete vs edit: Edit resurrects the entity (no data loss)
//
// =============================================================================

#include "core/cells/crdt_internal.h"
#include "core/cells/format_code_parser.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"

namespace cells {
namespace internal {

ApplyResult applyColInsert(Workbook& workbook, const Operation& op) {
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }

    // Use sheetId from operation if available, otherwise fall back to first sheet
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
    }
    if (sheet == nullptr) {
        sheet = workbook.sheets[0].get();
    }

    if (sheet->getColumn(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, true);
    newAxis->position = static_cast<uint32_t>(pos);
    newAxis->size = static_cast<uint32_t>(size);
    sheet->addColumn(std::move(newAxis));

    return ApplyResult::SUCCESS;
}

ApplyResult applyRowInsert(Workbook& workbook, const Operation& op) {
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }

    // Use sheetId from operation if available, otherwise fall back to first sheet
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
    }
    if (sheet == nullptr) {
        sheet = workbook.sheets[0].get();
    }

    if (sheet->getRow(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, false);
    newAxis->position = static_cast<uint32_t>(pos);
    newAxis->size = static_cast<uint32_t>(size);
    sheet->addRow(std::move(newAxis));

    return ApplyResult::SUCCESS;
}

ApplyResult applyDimInsertAxis(Workbook& workbook, const Operation& op) {
    // Legacy operation - parse payload: {"pos":0,"size":100,"isCol":true}
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);
    const std::string isColStr = extractJSONString(op.payload, "isCol");

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const bool isColumn = (isColStr == "true" || isColStr == "1");

    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }

    // Use sheetId from operation if available, otherwise fall back to first sheet
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
    }
    if (sheet == nullptr) {
        sheet = workbook.sheets[0].get();
    }

    if (isColumn) {
        if (sheet->getColumn(op.target_id) != nullptr) {
            return ApplyResult::ALREADY_APPLIED;
        }
        auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, true);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size = static_cast<uint32_t>(size);
        sheet->addColumn(std::move(newAxis));
    } else {
        if (sheet->getRow(op.target_id) != nullptr) {
            return ApplyResult::ALREADY_APPLIED;
        }
        auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, false);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size = static_cast<uint32_t>(size);
        sheet->addRow(std::move(newAxis));
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyColDelete(Workbook& workbook, const Operation& op) {
    Sheet* targetSheet = nullptr;
    const Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (targetSheet == nullptr || axis == nullptr) {
        // Column doesn't exist - already deleted or never existed
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations that resurrect the column
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::COL_INSERT || latest.type == OpType::COL_RENAME ||
            latest.type == OpType::COL_RESIZE) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Adjust ranges that have this column as a corner
    const uint32_t deletedPos = axis->position;
    auto getNextColId = [targetSheet, deletedPos](const ID& /*colId*/) -> ID {
        // Next column is at position + 1
        const Axis* next = targetSheet->getColumnByPosition(deletedPos + 1);
        return next != nullptr ? next->id : ID{};
    };
    auto getPrevColId = [targetSheet, deletedPos](const ID& /*colId*/) -> ID {
        // Previous column is at position - 1 (guard against underflow)
        if (deletedPos == 0) {
            return {};
        }
        const Axis* prev = targetSheet->getColumnByPosition(deletedPos - 1);
        return prev != nullptr ? prev->id : ID{};
    };

    std::vector<ID> rangesToRemove;
    for (auto& [rangeId, range] : targetSheet->getRanges()) {
        const CornerDeleteResult result =
            adjustRangeForColumnDeletion(*range, op.target_id, getNextColId, getPrevColId);
        if (result == CornerDeleteResult::INVALIDATED) {
            rangesToRemove.push_back(rangeId);
        } else if (result == CornerDeleteResult::SHRUNK) {
            // Update the range index with new bounds
            targetSheet->updateRangeIndex(range.get());
        }
    }
    for (const ID& rangeId : rangesToRemove) {
        targetSheet->removeRange(rangeId);
    }

    // Delete all cells in this column
    std::vector<ID> cellsToRemove;
    for (const ID& cellId : targetSheet->getCellIds()) {
        const Cell* cell = workbook.getCell(cellId);
        if (cell && cell->colId == op.target_id) {
            cellsToRemove.push_back(cellId);
        }
    }
    for (const auto& cellId : cellsToRemove) {
        // Remove from sheet's position index
        const Cell* cell = workbook.getCell(cellId);
        if (cell) {
            targetSheet->removeCellFromIndex(cellId);
        }
        workbook.removeCell(cellId);
    }

    // Remove the column
    targetSheet->columns.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

ApplyResult applyRowDelete(Workbook& workbook, const Operation& op) {
    Sheet* targetSheet = nullptr;
    const Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (targetSheet == nullptr || axis == nullptr) {
        // Row doesn't exist - already deleted or never existed
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations that resurrect the row
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::ROW_INSERT || latest.type == OpType::ROW_RESIZE) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Adjust ranges that have this row as a corner
    const uint32_t deletedPos = axis->position;
    auto getNextRowId = [targetSheet, deletedPos](const ID& /*rowId*/) -> ID {
        // Next row is at position + 1
        const Axis* next = targetSheet->getRowByPosition(deletedPos + 1);
        return next != nullptr ? next->id : ID{};
    };
    auto getPrevRowId = [targetSheet, deletedPos](const ID& /*rowId*/) -> ID {
        // Previous row is at position - 1 (guard against underflow)
        if (deletedPos == 0) {
            return {};
        }
        const Axis* prev = targetSheet->getRowByPosition(deletedPos - 1);
        return prev != nullptr ? prev->id : ID{};
    };

    std::vector<ID> rangesToRemove;
    for (auto& [rangeId, range] : targetSheet->getRanges()) {
        const CornerDeleteResult result =
            adjustRangeForRowDeletion(*range, op.target_id, getNextRowId, getPrevRowId);
        if (result == CornerDeleteResult::INVALIDATED) {
            rangesToRemove.push_back(rangeId);
        } else if (result == CornerDeleteResult::SHRUNK) {
            // Update the range index with new bounds
            targetSheet->updateRangeIndex(range.get());
        }
    }
    for (const ID& rangeId : rangesToRemove) {
        targetSheet->removeRange(rangeId);
    }

    // Delete all cells in this row
    std::vector<ID> cellsToRemove;
    for (const ID& cellId : targetSheet->getCellIds()) {
        const Cell* cell = workbook.getCell(cellId);
        if (cell && cell->rowId == op.target_id) {
            cellsToRemove.push_back(cellId);
        }
    }
    for (const auto& cellId : cellsToRemove) {
        // Remove from sheet's position index
        const Cell* cell = workbook.getCell(cellId);
        if (cell) {
            targetSheet->removeCellFromIndex(cellId);
        }
        workbook.removeCell(cellId);
    }

    // Remove the row
    targetSheet->rows.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

ApplyResult applyColResize(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer resize operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::COL_RESIZE || existing.type == OpType::DIM_RESIZE_AXIS) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string size_str = extractSizePayload(op.payload);
    if (size_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const auto new_size = static_cast<uint32_t>(std::stoul(size_str));
    axis->size = new_size;

    return ApplyResult::SUCCESS;
}

ApplyResult applyRowResize(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer resize operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::ROW_RESIZE || existing.type == OpType::DIM_RESIZE_AXIS) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string size_str = extractSizePayload(op.payload);
    if (size_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const auto new_size = static_cast<uint32_t>(std::stoul(size_str));
    axis->size = new_size;

    return ApplyResult::SUCCESS;
}

ApplyResult applyAxisSetHidden(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    // Search for axis in all sheets (could be column or row)
    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            break;
        }
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer set_hidden operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::AXIS_SET_HIDDEN && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Payload is just "1" (hidden) or "0" (visible)
    axis->hidden = (op.payload == "1");

    return ApplyResult::SUCCESS;
}

ApplyResult applyAxisSetStyle(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    // Search for axis in all sheets (could be column or row)
    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            break;
        }
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer set_style operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::AXIS_SET_STYLE && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Payload is the style ID (or empty string to clear)
    if (op.payload.empty()) {
        axis->defaultStyleId = ID{};  // Clear style
    } else {
        axis->defaultStyleId = ID(op.payload);
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyColMove(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;
    Sheet* targetSheet = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (axis == nullptr || targetSheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer move operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::COL_MOVE || existing.type == OpType::DIM_MOVE_AXIS) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const int targetPos = extractJSONInt(op.payload, "targetPos", -1);
    if (targetPos < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const uint32_t currentPos = axis->position;
    auto newPos = static_cast<uint32_t>(targetPos);

    if (newPos == currentPos || newPos == currentPos + 1) {
        return ApplyResult::SUCCESS;
    }

    if (newPos > currentPos) {
        newPos = newPos - 1;
    }

    // Update other columns' positions
    for (auto& [id, ax] : targetSheet->columns) {
        if (id == op.target_id) {
            continue;
        }
        if (currentPos < newPos) {
            if (ax->position > currentPos && ax->position <= newPos) {
                ax->position--;
            }
        } else {
            if (ax->position >= newPos && ax->position < currentPos) {
                ax->position++;
            }
        }
    }

    axis->position = newPos;
    return ApplyResult::SUCCESS;
}

ApplyResult applyRowMove(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;
    Sheet* targetSheet = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (axis == nullptr || targetSheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer move operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::ROW_MOVE || existing.type == OpType::DIM_MOVE_AXIS) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const int targetPos = extractJSONInt(op.payload, "targetPos", -1);
    if (targetPos < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const uint32_t currentPos = axis->position;
    auto newPos = static_cast<uint32_t>(targetPos);

    if (newPos == currentPos || newPos == currentPos + 1) {
        return ApplyResult::SUCCESS;
    }

    if (newPos > currentPos) {
        newPos = newPos - 1;
    }

    // Update other rows' positions
    for (auto& [id, ax] : targetSheet->rows) {
        if (id == op.target_id) {
            continue;
        }
        if (currentPos < newPos) {
            if (ax->position > currentPos && ax->position <= newPos) {
                ax->position--;
            }
        } else {
            if (ax->position >= newPos && ax->position < currentPos) {
                ax->position++;
            }
        }
    }

    axis->position = newPos;
    return ApplyResult::SUCCESS;
}

ApplyResult applyColRename(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer rename operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::COL_RENAME || existing.type == OpType::DIM_RENAME_AXIS) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string name = extractJSONString(op.payload, "name");
    axis->name = name;

    return ApplyResult::SUCCESS;
}

ApplyResult applyDimResizeAxis(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis == nullptr) {
            axis = s->getRow(op.target_id);
        }
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer resize operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::DIM_RESIZE_AXIS && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string size_str = extractSizePayload(op.payload);
    if (size_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const auto new_size = static_cast<uint32_t>(std::stoul(size_str));
    axis->size = new_size;

    return ApplyResult::SUCCESS;
}

ApplyResult applyDimMoveAxis(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;
    Sheet* targetSheet = nullptr;
    bool isColumn = false;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            isColumn = true;
            break;
        }
        axis = s->getRow(op.target_id);
        if (axis != nullptr) {
            targetSheet = s.get();
            isColumn = false;
            break;
        }
    }

    if (axis == nullptr || targetSheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer move operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::DIM_MOVE_AXIS && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const int targetPos = extractJSONInt(op.payload, "targetPos", -1);
    if (targetPos < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const uint32_t currentPos = axis->position;
    auto newPos = static_cast<uint32_t>(targetPos);

    if (newPos == currentPos || newPos == currentPos + 1) {
        return ApplyResult::SUCCESS;  // No-op
    }

    // Adjust newPos if moving forward (same logic as JS)
    if (newPos > currentPos) {
        newPos = newPos - 1;
    }

    // Update other axes' positions
    auto& axisMap = isColumn ? targetSheet->columns : targetSheet->rows;
    for (auto& [id, ax] : axisMap) {
        if (id == op.target_id) {
            continue;
        }

        if (currentPos < newPos) {
            if (ax->position > currentPos && ax->position <= newPos) {
                ax->position--;
            }
        } else {
            if (ax->position >= newPos && ax->position < currentPos) {
                ax->position++;
            }
        }
    }

    axis->position = newPos;

    return ApplyResult::SUCCESS;
}

ApplyResult applyDimRenameAxis(Workbook& workbook, const Operation& op) {
    // Legacy operation - only handles columns (rows cannot be renamed)
    Axis* axis = nullptr;

    for (auto& s : workbook.sheets) {
        axis = s->getColumn(op.target_id);
        if (axis != nullptr) {
            break;
        }
    }

    if (axis == nullptr) {
        // Could be a row ID from old data - silently accept for backwards compat
        return ApplyResult::SUCCESS;
    }

    // Check for newer rename operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if ((existing.type == OpType::DIM_RENAME_AXIS || existing.type == OpType::COL_RENAME) &&
            existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string name = extractJSONString(op.payload, "name");
    // Note: empty name is valid (it clears the custom name)
    axis->name = name;

    return ApplyResult::SUCCESS;
}

ApplyResult applyWorkbookRename(Workbook& workbook, const Operation& op) {
    if (op.target_id != workbook.id) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer rename operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::WORKBOOK_RENAME && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    workbook.name = name;

    return ApplyResult::SUCCESS;
}

ApplyResult applySheetRename(Workbook& workbook, const Operation& op) {
    Sheet* sheet = workbook.getSheet(op.target_id);
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer rename operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::SHEET_RENAME && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    sheet->name = name;

    return ApplyResult::SUCCESS;
}

ApplyResult applySheetCreate(Workbook& workbook, const Operation& op) {
    // Check if sheet already exists (idempotent operation)
    if (workbook.getSheet(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        name = "Sheet";  // Default name if not provided
    }

    auto sheet = std::make_unique<Sheet>(op.target_id, name);
    workbook.addSheet(std::move(sheet));

    return ApplyResult::SUCCESS;
}

ApplyResult applySheetDelete(Workbook& workbook, const Operation& op) {
    const Sheet* sheet = workbook.getSheet(op.target_id);
    if (sheet == nullptr) {
        // Sheet doesn't exist - already deleted or never existed
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations that resurrect the sheet
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::SHEET_RENAME || latest.type == OpType::SHEET_CREATE) {
            return ApplyResult::RESURRECTED;
        }
    }

    workbook.removeSheet(op.target_id);

    return ApplyResult::SUCCESS;
}

ApplyResult applyFormatDefine(Workbook& workbook, const Operation& op) {
    // Check if this format is already defined
    if (workbook.hasCustomFormat(op.target_id)) {
        return ApplyResult::ALREADY_APPLIED;
    }

    const std::string formatCode = extractJSONString(op.payload, "format_code");
    if (formatCode.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    // Validate the format code
    auto validationError = validateFormatCode(formatCode);
    if (validationError) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    workbook.registerCustomFormat(op.target_id, formatCode);

    return ApplyResult::SUCCESS;
}

// Helper to extract boolean from JSON payload
static bool extractJSONBool(const std::string& json, const std::string& key,
                            bool defaultValue = false) {
    // Look for "key":true or "key":false
    const std::string keyPattern = "\"" + key + "\":";
    auto pos = json.find(keyPattern);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    pos += keyPattern.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size()) {
        return defaultValue;
    }
    if (json.substr(pos, 4) == "true") {
        return true;
    }
    if (json.substr(pos, 5) == "false") {
        return false;
    }
    return defaultValue;
}

ApplyResult applyStyleDefine(Workbook& workbook, const Operation& op) {
    // Check if this style is already defined
    if (workbook.hasStyle(op.target_id)) {
        return ApplyResult::ALREADY_APPLIED;
    }

    // Parse style properties from JSON payload
    CellStyle style;
    style.bold = extractJSONBool(op.payload, "bold", false);
    style.italic = extractJSONBool(op.payload, "italic", false);
    style.underline = extractJSONBool(op.payload, "underline", false);
    style.wrapText = extractJSONBool(op.payload, "wrapText", false);
    style.bgColor = extractJSONString(op.payload, "bgColor");
    style.textColor = extractJSONString(op.payload, "textColor");
    style.fontFamily = extractJSONString(op.payload, "fontFamily");
    style.fontSize = static_cast<uint8_t>(extractJSONInt(op.payload, "fontSize", 0));

    // Parse alignment enums
    const std::string hAlignStr = extractJSONString(op.payload, "hAlign");
    if (hAlignStr == "center") {
        style.hAlign = TextAlign::CENTER;
    } else if (hAlignStr == "right") {
        style.hAlign = TextAlign::RIGHT;
    } else if (hAlignStr == "justify") {
        style.hAlign = TextAlign::JUSTIFY;
    } else {
        style.hAlign = TextAlign::LEFT;
    }

    const std::string vAlignStr = extractJSONString(op.payload, "vAlign");
    if (vAlignStr == "top") {
        style.vAlign = VerticalAlign::TOP;
    } else if (vAlignStr == "middle") {
        style.vAlign = VerticalAlign::MIDDLE;
    } else {
        style.vAlign = VerticalAlign::BOTTOM;
    }

    workbook.registerStyle(op.target_id, style);

    return ApplyResult::SUCCESS;
}

ApplyResult applyNamedRangeDefine(Workbook& workbook, const Operation& op) {
    // Extract named range properties from JSON payload
    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const std::string scopeStr = extractJSONString(op.payload, "scope");
    if (scopeStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const std::string scopeSheetIdStr = extractJSONString(op.payload, "scopeSheetId");
    const std::string targetTypeStr = extractJSONString(op.payload, "targetType");
    const std::string id1Str = extractJSONString(op.payload, "id1");
    const std::string id2Str = extractJSONString(op.payload, "id2");
    const std::string targetSheetIdStr = extractJSONString(op.payload, "targetSheetId");

    if (targetTypeStr.empty() || id1Str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    // Build target
    NamedRangeTarget target;
    if (targetTypeStr == "CELL") {
        target.type = NamedRangeTarget::Type::CELL;
    } else if (targetTypeStr == "RANGE") {
        target.type = NamedRangeTarget::Type::RANGE;
    } else if (targetTypeStr == "COLUMN") {
        target.type = NamedRangeTarget::Type::COLUMN;
    } else if (targetTypeStr == "ROW") {
        target.type = NamedRangeTarget::Type::ROW;
    } else if (targetTypeStr == "COLUMN_RANGE") {
        target.type = NamedRangeTarget::Type::COLUMN_RANGE;
    } else if (targetTypeStr == "ROW_RANGE") {
        target.type = NamedRangeTarget::Type::ROW_RANGE;
    } else {
        return ApplyResult::INVALID_PAYLOAD;
    }

    target.id1 = ID(id1Str);
    if (!id2Str.empty() && id2Str != "-") {
        target.id2 = ID(id2Str);
    }
    if (!targetSheetIdStr.empty() && targetSheetIdStr != "-") {
        target.sheetId = ID(targetSheetIdStr);
    }

    // Register the named range
    NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    if (scopeStr == "W") {
        // Workbook scope - overwrite if exists (idempotent)
        registry->removeWorkbook(name);  // Remove if exists
        registry->defineWorkbook(name, target);
    } else if (scopeStr == "S") {
        // Sheet scope
        if (scopeSheetIdStr.empty() || scopeSheetIdStr == "-") {
            return ApplyResult::INVALID_PAYLOAD;
        }
        const ID scopeSheetId(scopeSheetIdStr);
        registry->removeSheet(name, scopeSheetId);  // Remove if exists
        registry->defineSheet(name, scopeSheetId, target);
    } else {
        return ApplyResult::INVALID_PAYLOAD;
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyNamedRangeDelete(Workbook& workbook, const Operation& op) {
    // Extract deletion key from JSON payload
    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const std::string scopeStr = extractJSONString(op.payload, "scope");
    if (scopeStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    if (scopeStr == "W") {
        registry->removeWorkbook(name);
    } else if (scopeStr == "S") {
        const std::string scopeSheetIdStr = extractJSONString(op.payload, "scopeSheetId");
        if (scopeSheetIdStr.empty() || scopeSheetIdStr == "-") {
            return ApplyResult::INVALID_PAYLOAD;
        }
        const ID scopeSheetId(scopeSheetIdStr);
        registry->removeSheet(name, scopeSheetId);
    } else {
        return ApplyResult::INVALID_PAYLOAD;
    }

    return ApplyResult::SUCCESS;
}

}  // namespace internal
}  // namespace cells
