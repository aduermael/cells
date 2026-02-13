// =============================================================================
// CRDT Axis Operations
// =============================================================================
//
// Implementation of axis-level (column/row) CRDT operations for the spreadsheet.
// Axes define the grid structure - columns and rows have UUIDs, positions, and sizes.
//
// Key responsibilities:
// - COL_SET/COL_DELETE: Create/update/delete columns
// - ROW_SET/ROW_DELETE: Create/update/delete rows
// - SHEET_SET/SHEET_DELETE: Create/update/delete sheets
// - WORKBOOK_SET: Update workbook properties
// - NAMED_RANGE_SET/NAMED_RANGE_DELETE: Manage named ranges
//
// Conflict resolution:
// - SET operations: Last-Writer-Wins (highest HLC wins), creates if doesn't exist
// - DELETE operations: Edit (SET) resurrects deleted entities (no data loss)
//
// =============================================================================

#include "core/cells/crdt_internal.h"
#include "core/cells/format_buffer.h"
#include "core/cells/format_code_parser.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"
#include "core/log/include/Logger.h"

namespace cells {
namespace internal {

// =============================================================================
// COL_SET - Create or update column
// =============================================================================
// Payload:
// {"pos":N,"size":N,"sizeOriginal":D,"name":"...","sty":"base64","fmt":"base64","hidden":bool} All
// fields optional except pos is required when creating

ApplyResult applyColSet(Workbook& workbook, const Operation& op) {
    // Use sheetId from operation if available, otherwise fall back to first sheet
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
    }
    if (sheet == nullptr && !workbook.sheets.empty()) {
        sheet = workbook.sheets[0].get();
    }
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    Axis* axis = sheet->getColumn(op.target_id);
    const bool creating = (axis == nullptr);

    if (creating) {
        // Creating new column - pos is required
        const int pos = extractJSONInt(op.payload, "pos", -1);
        if (pos < 0) {
            return ApplyResult::INVALID_PAYLOAD;
        }

        auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, true);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size =
            static_cast<uint32_t>(extractJSONInt(op.payload, "size", DEFAULT_COLUMN_WIDTH));
        axis = newAxis.get();
        sheet->addColumn(std::move(newAxis));
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        return creating ? ApplyResult::SUCCESS : ApplyResult::SUPERSEDED;
    }

    // Update properties if provided
    const int pos = extractJSONInt(op.payload, "pos", -1);
    if (pos >= 0) {
        const uint32_t oldPos = axis->position;
        const auto newPos = static_cast<uint32_t>(pos);
        axis->position = newPos;
        if (!creating) {
            sheet->updateColumnPositionIndex(op.target_id, oldPos, newPos);
        }
    }

    const int size = extractJSONInt(op.payload, "size", -1);
    if (size >= 0) {
        axis->size = static_cast<uint32_t>(size);
        axis->setSizeSet(true);  // Mark as explicitly set
        sheet->getColumnAxisIndex().resize(op.target_id, static_cast<uint32_t>(size));
    }

    if (op.payload.find("\"sizeOriginal\":") != std::string::npos) {
        axis->sizeOriginal = extractJSONDouble(op.payload, "sizeOriginal");
    }

    const std::string name = extractJSONString(op.payload, "name");
    if (!name.empty() || op.payload.find("\"name\":") != std::string::npos) {
        axis->name = name;
    }

    const std::string style_str = extractJSONString(op.payload, "sty");
    if (!style_str.empty()) {
        auto maybeStyle = StyleBuffer::fromBase64(style_str);
        if (maybeStyle.has_value() && !maybeStyle->isEmpty()) {
            workbook.setEntityStyle(axis->id, *maybeStyle);
            axis->setHasStyle(true);
        }
    } else if (op.payload.find("\"sty\":\"\"") != std::string::npos) {
        workbook.clearEntityStyle(axis->id);
        axis->setHasStyle(false);
    }

    const std::string format_str = extractJSONString(op.payload, "fmt");
    if (!format_str.empty()) {
        auto maybeFormat = FormatBuffer::fromBase64(format_str);
        if (maybeFormat.has_value() && !maybeFormat->isEmpty()) {
            workbook.setEntityFormat(axis->id, *maybeFormat);
            axis->setHasFormat(true);
        }
    } else if (op.payload.find("\"fmt\":\"\"") != std::string::npos) {
        workbook.clearEntityFormat(axis->id);
        axis->setHasFormat(false);
    }

    if (op.payload.find("\"hidden\":") != std::string::npos) {
        const bool hidden = extractJSONBool(op.payload, "hidden", false);
        axis->setHidden(hidden);
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// COL_DELETE - Delete column
// =============================================================================

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
        return ApplyResult::SUCCESS;  // Already deleted or never existed
    }

    // Check for newer operations that resurrect the column
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::COL_SET) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Adjust ranges that have this column as a corner
    const uint32_t deletedPos = axis->position;
    auto getNextColId = [targetSheet, deletedPos](const ID& /*colId*/) -> ID {
        const Axis* next = targetSheet->getColumnByPosition(deletedPos + 1);
        return next != nullptr ? next->id : ID{};
    };
    auto getPrevColId = [targetSheet, deletedPos](const ID& /*colId*/) -> ID {
        if (deletedPos == 0) {
            return {};
        }
        const Axis* prev = targetSheet->getColumnByPosition(deletedPos - 1);
        return prev != nullptr ? prev->id : ID{};
    };

    std::vector<ID> rangesToRemove;
    for (const ID& rangeId : targetSheet->getRangeIds()) {
        Range* range = targetSheet->getRange(rangeId);
        if (range == nullptr) {
            continue;
        }
        const CornerDeleteResult result =
            adjustRangeForColumnDeletion(*range, op.target_id, getNextColId, getPrevColId);
        if (result == CornerDeleteResult::INVALIDATED) {
            rangesToRemove.push_back(rangeId);
        } else if (result == CornerDeleteResult::SHRUNK) {
            targetSheet->updateRangeIndex(range);
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
        const Cell* cell = workbook.getCell(cellId);
        if (cell) {
            targetSheet->removeCellFromIndex(cellId);
        }
        workbook.removeCell(cellId);
    }

    targetSheet->removeColumnFromIndex(op.target_id);
    workbook.removeColumn(op.target_id);

    return ApplyResult::SUCCESS;
}

// =============================================================================
// ROW_SET - Create or update row
// =============================================================================
// Payload: {"pos":N,"size":N,"sizeOriginal":D,"sty":"base64","fmt":"base64","hidden":bool}
// All fields optional except pos is required when creating

ApplyResult applyRowSet(Workbook& workbook, const Operation& op) {
    // Use sheetId from operation if available, otherwise fall back to first sheet
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
    }
    if (sheet == nullptr && !workbook.sheets.empty()) {
        sheet = workbook.sheets[0].get();
    }
    if (sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    Axis* axis = sheet->getRow(op.target_id);
    const bool creating = (axis == nullptr);

    if (creating) {
        // Creating new row - pos is required
        const int pos = extractJSONInt(op.payload, "pos", -1);
        if (pos < 0) {
            return ApplyResult::INVALID_PAYLOAD;
        }

        auto newAxis = std::make_unique<Axis>(op.target_id, sheet->id, false);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size =
            static_cast<uint32_t>(extractJSONInt(op.payload, "size", DEFAULT_ROW_HEIGHT));
        axis = newAxis.get();
        sheet->addRow(std::move(newAxis));
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        return creating ? ApplyResult::SUCCESS : ApplyResult::SUPERSEDED;
    }

    // Update properties if provided
    const int pos = extractJSONInt(op.payload, "pos", -1);
    if (pos >= 0) {
        const uint32_t oldPos = axis->position;
        const auto newPos = static_cast<uint32_t>(pos);
        axis->position = newPos;
        if (!creating) {
            sheet->updateRowPositionIndex(op.target_id, oldPos, newPos);
        }
    }

    const int size = extractJSONInt(op.payload, "size", -1);
    if (size >= 0) {
        axis->size = static_cast<uint32_t>(size);
        axis->setSizeSet(true);  // Mark as explicitly set
        sheet->getRowAxisIndex().resize(op.target_id, static_cast<uint32_t>(size));
    }

    if (op.payload.find("\"sizeOriginal\":") != std::string::npos) {
        axis->sizeOriginal = extractJSONDouble(op.payload, "sizeOriginal");
    }

    const std::string style_str = extractJSONString(op.payload, "sty");
    if (!style_str.empty()) {
        auto maybeStyle = StyleBuffer::fromBase64(style_str);
        if (maybeStyle.has_value() && !maybeStyle->isEmpty()) {
            workbook.setEntityStyle(axis->id, *maybeStyle);
            axis->setHasStyle(true);
        }
    } else if (op.payload.find("\"sty\":\"\"") != std::string::npos) {
        workbook.clearEntityStyle(axis->id);
        axis->setHasStyle(false);
    }

    const std::string format_str = extractJSONString(op.payload, "fmt");
    if (!format_str.empty()) {
        auto maybeFormat = FormatBuffer::fromBase64(format_str);
        if (maybeFormat.has_value() && !maybeFormat->isEmpty()) {
            workbook.setEntityFormat(axis->id, *maybeFormat);
            axis->setHasFormat(true);
        }
    } else if (op.payload.find("\"fmt\":\"\"") != std::string::npos) {
        workbook.clearEntityFormat(axis->id);
        axis->setHasFormat(false);
    }

    if (op.payload.find("\"hidden\":") != std::string::npos) {
        const bool hidden = extractJSONBool(op.payload, "hidden", false);
        axis->setHidden(hidden);
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// ROW_DELETE - Delete row
// =============================================================================

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
        return ApplyResult::SUCCESS;  // Already deleted or never existed
    }

    // Check for newer operations that resurrect the row
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::ROW_SET) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Adjust ranges that have this row as a corner
    const uint32_t deletedPos = axis->position;
    auto getNextRowId = [targetSheet, deletedPos](const ID& /*rowId*/) -> ID {
        const Axis* next = targetSheet->getRowByPosition(deletedPos + 1);
        return next != nullptr ? next->id : ID{};
    };
    auto getPrevRowId = [targetSheet, deletedPos](const ID& /*rowId*/) -> ID {
        if (deletedPos == 0) {
            return {};
        }
        const Axis* prev = targetSheet->getRowByPosition(deletedPos - 1);
        return prev != nullptr ? prev->id : ID{};
    };

    std::vector<ID> rangesToRemove;
    for (const ID& rangeId : targetSheet->getRangeIds()) {
        Range* range = targetSheet->getRange(rangeId);
        if (range == nullptr) {
            continue;
        }
        const CornerDeleteResult result =
            adjustRangeForRowDeletion(*range, op.target_id, getNextRowId, getPrevRowId);
        if (result == CornerDeleteResult::INVALIDATED) {
            rangesToRemove.push_back(rangeId);
        } else if (result == CornerDeleteResult::SHRUNK) {
            targetSheet->updateRangeIndex(range);
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
        const Cell* cell = workbook.getCell(cellId);
        if (cell) {
            targetSheet->removeCellFromIndex(cellId);
        }
        workbook.removeCell(cellId);
    }

    targetSheet->removeRowFromIndex(op.target_id);
    workbook.removeRow(op.target_id);

    return ApplyResult::SUCCESS;
}

// =============================================================================
// SHEET_SET - Create or update sheet
// =============================================================================
// Payload: {"name":"...","pos":N}

ApplyResult applySheetSet(Workbook& workbook, const Operation& op) {
    Sheet* sheet = workbook.getSheet(op.target_id);
    const bool creating = (sheet == nullptr);

    if (creating) {
        std::string name = extractJSONString(op.payload, "name");
        if (name.empty()) {
            name = "Sheet";
        }
        auto newSheet = std::make_unique<Sheet>(op.target_id, name);
        workbook.addSheet(std::move(newSheet));
        sheet = workbook.getSheet(op.target_id);
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc && !creating) {
        return ApplyResult::SUPERSEDED;
    }

    // Update name if provided
    const std::string name = extractJSONString(op.payload, "name");
    if (!name.empty()) {
        sheet->name = name;
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// SHEET_DELETE - Delete sheet
// =============================================================================

ApplyResult applySheetDelete(Workbook& workbook, const Operation& op) {
    const Sheet* sheet = workbook.getSheet(op.target_id);
    if (sheet == nullptr) {
        return ApplyResult::SUCCESS;  // Already deleted or never existed
    }

    // Check for newer operations that resurrect the sheet
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        if (latest.type == OpType::SHEET_SET) {
            return ApplyResult::RESURRECTED;
        }
    }

    workbook.removeSheet(op.target_id);
    return ApplyResult::SUCCESS;
}

// =============================================================================
// WORKBOOK_SET - Update workbook properties
// =============================================================================
// Payload: {"name":"..."}

ApplyResult applyWorkbookSet(Workbook& workbook, const Operation& op) {
    if (op.target_id != workbook.id) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::WORKBOOK_SET && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    const std::string name = extractJSONString(op.payload, "name");
    if (!name.empty()) {
        workbook.name = name;
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// NAMED_RANGE_SET - Create or update named range
// =============================================================================

ApplyResult applyNamedRangeSet(Workbook& workbook, const Operation& op) {
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

    NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    if (scopeStr == "W") {
        registry->removeWorkbook(name);
        registry->defineWorkbook(name, target);
    } else if (scopeStr == "S") {
        if (scopeSheetIdStr.empty() || scopeSheetIdStr == "-") {
            return ApplyResult::INVALID_PAYLOAD;
        }
        const ID scopeSheetId(scopeSheetIdStr);
        registry->removeSheet(name, scopeSheetId);
        registry->defineSheet(name, scopeSheetId, target);
    } else {
        return ApplyResult::INVALID_PAYLOAD;
    }

    return ApplyResult::SUCCESS;
}

// =============================================================================
// NAMED_RANGE_DELETE - Delete named range
// =============================================================================

ApplyResult applyNamedRangeDelete(Workbook& workbook, const Operation& op) {
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
