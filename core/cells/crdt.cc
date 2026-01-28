// =============================================================================
// CRDT Operations Main Entry
// =============================================================================
//
// Central dispatcher for CRDT operations and operation creation helpers.
// This file coordinates the split implementation across crdt_cell.cc and
// crdt_axis.cc.
//
// Key responsibilities:
// - applyOperation() dispatcher that routes to specific operation handlers
// - Operation maker functions (makeCellSetValueOp, makeColInsertOp, etc.)
// - bootstrapOpLog() for generating OpLog from existing workbook state
// - JSON utility functions used by all crdt_*.cc files
//
// Split structure:
// - crdt.cc: Main entry, dispatch, operation makers, JSON utilities
// - crdt_cell.cc: Cell operations (set value, set format, clear)
// - crdt_axis.cc: Axis operations (column/row CRUD, sheet/workbook ops)
//
// =============================================================================

#include "core/cells/crdt.h"

#include <cstdio>

#include <algorithm>

#include "core/cells/crdt_internal.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/log/include/Logger.h"

namespace cells {

// =============================================================================
// JSON Utilities (in internal namespace, shared by crdt_*.cc)
// =============================================================================

namespace internal {

std::string jsonUnescape(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    size_t i = 0;
    while (i < str.size()) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            const char next = str[i + 1];
            switch (next) {
                case '"':
                    result += '"';
                    i += 2;
                    break;
                case '\\':
                    result += '\\';
                    i += 2;
                    break;
                case 'b':
                    result += '\b';
                    i += 2;
                    break;
                case 'f':
                    result += '\f';
                    i += 2;
                    break;
                case 'n':
                    result += '\n';
                    i += 2;
                    break;
                case 'r':
                    result += '\r';
                    i += 2;
                    break;
                case 't':
                    result += '\t';
                    i += 2;
                    break;
                case 'u':
                    // Unicode escape - simplified handling
                    if (i + 5 < str.size()) {
                        char hex[5] = {str[i + 2], str[i + 3], str[i + 4], str[i + 5], 0};
                        const int code = static_cast<int>(strtol(hex, nullptr, 16));
                        if (code < 128) {
                            result += static_cast<char>(code);
                        }
                        i += 6;
                    } else {
                        result += str[i];
                        i++;
                    }
                    break;
                default:
                    result += str[i];
                    i++;
            }
        } else {
            result += str[i];
            i++;
        }
    }
    return result;
}

std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string extractJSONString(const std::string& json, const std::string& key) {
    const std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }

    pos++;  // Skip opening quote
    size_t end = pos;
    while (end < json.size() && json[end] != '"') {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end++;  // Skip escaped char
        }
        end++;
    }

    return jsonUnescape(json.substr(pos, end - pos));
}

int extractJSONInt(const std::string& json, const std::string& key, int defaultValue) {
    const std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return defaultValue;
    }

    // Parse integer
    int value = 0;
    bool negative = false;
    if (json[pos] == '-') {
        negative = true;
        pos++;
    }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        pos++;
    }
    return negative ? -value : value;
}

std::string extractSizePayload(const std::string& payload) {
    std::string size_str = extractJSONString(payload, "size");
    if (size_str.empty()) {
        // Try numeric format
        size_t pos = payload.find("\"size\":");
        if (pos != std::string::npos) {
            pos += 7;  // Skip "size":
            while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t')) {
                pos++;
            }
            size_t end = pos;
            while (end < payload.size() && payload[end] >= '0' && payload[end] <= '9') {
                end++;
            }
            size_str = payload.substr(pos, end - pos);
        }
    }
    return size_str;
}

}  // namespace internal

// =============================================================================
// Main Operation Dispatcher
// =============================================================================

ApplyResult applyOperation(Workbook& workbook, const Operation& op) {
    OpLog* oplog = workbook.getOpLog();

    // Check for duplicate
    if (oplog->hasOperation(op.hlc)) {
        return ApplyResult::ALREADY_APPLIED;
    }

    ApplyResult result = ApplyResult::SUCCESS;

    switch (op.type) {
        case OpType::CELL_SET_VALUE:
            result = internal::applyCellSetValue(workbook, op);
            break;

        case OpType::CELL_CLEAR:
            result = internal::applyCellClear(workbook, op);
            break;

        case OpType::CELL_SET_STYLE:
            result = internal::applyCellSetStyle(workbook, op);
            break;

        case OpType::CELL_SET_FORMAT:
            result = internal::applyCellSetFormat(workbook, op);
            break;

        // Column operations
        case OpType::COL_INSERT:
            result = internal::applyColInsert(workbook, op);
            break;

        case OpType::COL_DELETE:
            result = internal::applyColDelete(workbook, op);
            break;

        case OpType::COL_MOVE:
            result = internal::applyColMove(workbook, op);
            break;

        case OpType::COL_RESIZE:
            result = internal::applyColResize(workbook, op);
            break;

        case OpType::COL_RENAME:
            result = internal::applyColRename(workbook, op);
            break;

        // Row operations
        case OpType::ROW_INSERT:
            result = internal::applyRowInsert(workbook, op);
            break;

        case OpType::ROW_DELETE:
            result = internal::applyRowDelete(workbook, op);
            break;

        case OpType::ROW_MOVE:
            result = internal::applyRowMove(workbook, op);
            break;

        case OpType::ROW_RESIZE:
            result = internal::applyRowResize(workbook, op);
            break;

        // Axis operations
        case OpType::AXIS_SET_HIDDEN:
            result = internal::applyAxisSetHidden(workbook, op);
            break;

        case OpType::AXIS_SET_STYLE:
            result = internal::applyAxisSetStyle(workbook, op);
            break;

        case OpType::AXIS_SET_FORMAT:
            result = internal::applyAxisSetFormat(workbook, op);
            break;

        case OpType::SHEET_CREATE:
            result = internal::applySheetCreate(workbook, op);
            break;

        case OpType::SHEET_DELETE:
            result = internal::applySheetDelete(workbook, op);
            break;

        case OpType::SHEET_RENAME:
            result = internal::applySheetRename(workbook, op);
            break;

        case OpType::WORKBOOK_RENAME:
            result = internal::applyWorkbookRename(workbook, op);
            break;

        case OpType::FORMAT_DEFINE:
            result = internal::applyFormatDefine(workbook, op);
            break;

        case OpType::NAMED_RANGE_DEFINE:
            result = internal::applyNamedRangeDefine(workbook, op);
            break;

        case OpType::NAMED_RANGE_DELETE:
            result = internal::applyNamedRangeDelete(workbook, op);
            break;

        // Range operations (unified range system)
        case OpType::RANGE_ADD:
            result = internal::applyRangeAdd(workbook, op);
            break;

        case OpType::RANGE_REMOVE:
            result = internal::applyRangeRemove(workbook, op);
            break;

        case OpType::RANGE_UPDATE_CORNERS:
            result = internal::applyRangeUpdateCorners(workbook, op);
            break;

        case OpType::RANGE_UPDATE_FLAGS:
            result = internal::applyRangeUpdateFlags(workbook, op);
            break;

        case OpType::RANGE_SET_STYLE:
            result = internal::applyRangeSetStyle(workbook, op);
            break;

        case OpType::RANGE_SET_FORMAT:
            result = internal::applyRangeSetFormat(workbook, op);
            break;
    }

    // Only add to OpLog on successful application
    if (result == ApplyResult::SUCCESS || result == ApplyResult::SUPERSEDED ||
        result == ApplyResult::RESURRECTED) {
        oplog->addOperation(op);

        // Periodic pruning for non-collaboration mode
        // When not collaborating, oplog is only needed for undo/export, so prune periodically
        constexpr size_t PRUNE_THRESHOLD = 100;
        if (!workbook.isCollaborating() && oplog->size() >= PRUNE_THRESHOLD) {
            // Prune all ops - no peers to sync with
            oplog->clear();
        }
    }

    return result;
}

size_t applyOperations(Workbook& workbook, const std::vector<Operation>& ops) {
    // Sort operations by HLC to ensure consistent application order
    std::vector<Operation> sorted = ops;
    std::sort(sorted.begin(), sorted.end());

    size_t applied = 0;
    for (const auto& op : sorted) {
        const ApplyResult result = applyOperation(workbook, op);
        if (result == ApplyResult::SUCCESS || result == ApplyResult::SUPERSEDED ||
            result == ApplyResult::RESURRECTED) {
            applied++;
        }
    }

    return applied;
}

bool isSuperseded(const Workbook& workbook, const Operation& op) {
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    return !latest.isNull() && latest.hlc >= op.hlc;
}

// =============================================================================
// Operation Makers
// =============================================================================

Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_VALUE, cellId, payload};
}

Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const ID& sheetId,
                             const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_VALUE, cellId, sheetId, payload};
}

Operation makeCellClearOp(Workbook& workbook, const ID& cellId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_CLEAR, cellId, "{}"};
}

Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_FORMAT, cellId, payload};
}

Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const FormatBuffer& format) {
    const HLC hlc = workbook.getCurrentHLC();
    const std::string payload = "{\"format\":\"" + format.toBase64() + "\"}";
    return {hlc, OpType::CELL_SET_FORMAT, cellId, payload};
}

Operation makeCellClearFormatOp(Workbook& workbook, const ID& cellId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_FORMAT, cellId, "{\"format\":\"\"}"};
}

Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_STYLE, cellId, payload};
}

Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const StyleBuffer& style) {
    const HLC hlc = workbook.getCurrentHLC();
    const std::string payload = "{\"style\":\"" + style.toBase64() + "\"}";
    return {hlc, OpType::CELL_SET_STYLE, cellId, payload};
}

Operation makeCellClearStyleOp(Workbook& workbook, const ID& cellId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_STYLE, cellId, "{\"style\":\"\"}"};
}

// COL_*/ROW_* operation makers
Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_INSERT, axisId, payload};
}

Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const ID& sheetId,
                          const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_INSERT, axisId, sheetId, payload};
}

Operation makeColDeleteOp(Workbook& workbook, const ID& axisId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_DELETE, axisId, "{}"};
}

Operation makeColResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_RESIZE, axisId, payload};
}

Operation makeColMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_MOVE, axisId, payload};
}

Operation makeColRenameOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_RENAME, axisId, payload};
}

Operation makeRowInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_INSERT, axisId, payload};
}

Operation makeRowInsertOp(Workbook& workbook, const ID& axisId, const ID& sheetId,
                          const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_INSERT, axisId, sheetId, payload};
}

Operation makeRowDeleteOp(Workbook& workbook, const ID& axisId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_DELETE, axisId, "{}"};
}

Operation makeRowResizeOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_RESIZE, axisId, payload};
}

Operation makeRowMoveOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_MOVE, axisId, payload};
}

Operation makeAxisSetHiddenOp(Workbook& workbook, const ID& axisId, bool hidden) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::AXIS_SET_HIDDEN, axisId, hidden ? "1" : "0"};
}

Operation makeAxisSetStyleOp(Workbook& workbook, const ID& axisId, const StyleBuffer& style) {
    const HLC hlc = workbook.getCurrentHLC();
    const std::string payload = "{\"style\":\"" + style.toBase64() + "\"}";
    return {hlc, OpType::AXIS_SET_STYLE, axisId, payload};
}

Operation makeAxisClearStyleOp(Workbook& workbook, const ID& axisId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::AXIS_SET_STYLE, axisId, "{\"style\":\"\"}"};
}

Operation makeAxisSetFormatOp(Workbook& workbook, const ID& axisId, const ID& formatId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::AXIS_SET_FORMAT, axisId, formatId.isNull() ? "" : formatId.toString()};
}

Operation makeAxisSetFormatOp(Workbook& workbook, const ID& axisId, const FormatBuffer& format) {
    const HLC hlc = workbook.getCurrentHLC();
    const std::string payload = "{\"format\":\"" + format.toBase64() + "\"}";
    return {hlc, OpType::AXIS_SET_FORMAT, axisId, payload};
}

Operation makeAxisClearFormatOp(Workbook& workbook, const ID& axisId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::AXIS_SET_FORMAT, axisId, "{\"format\":\"\"}"};
}

Operation makeSheetCreateOp(Workbook& workbook, const ID& sheetId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::SHEET_CREATE, sheetId, payload};
}

Operation makeSheetDeleteOp(Workbook& workbook, const ID& sheetId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::SHEET_DELETE, sheetId, "{}"};
}

Operation makeSheetRenameOp(Workbook& workbook, const ID& sheetId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::SHEET_RENAME, sheetId, payload};
}

Operation makeWorkbookRenameOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::WORKBOOK_RENAME, workbook.id, payload};
}

Operation makeFormatDefineOp(Workbook& workbook, const ID& formatId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::FORMAT_DEFINE, formatId, payload};
}

Operation makeNamedRangeDefineOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    // Use workbook ID as target since named ranges are workbook-level entities
    return {hlc, OpType::NAMED_RANGE_DEFINE, workbook.id, payload};
}

Operation makeNamedRangeDeleteOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    // Use workbook ID as target since named ranges are workbook-level entities
    return {hlc, OpType::NAMED_RANGE_DELETE, workbook.id, payload};
}

// =============================================================================
// Range Operations (Unified Range System)
// =============================================================================

Operation makeRangeAddOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_ADD, rangeId, payload};
}

Operation makeRangeRemoveOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_REMOVE, rangeId, payload};
}

Operation makeRangeUpdateCornersOp(Workbook& workbook, const ID& rangeId,
                                   const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_UPDATE_CORNERS, rangeId, payload};
}

Operation makeRangeUpdateFlagsOp(Workbook& workbook, const ID& rangeId,
                                 const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_UPDATE_FLAGS, rangeId, payload};
}

Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_SET_STYLE, rangeId, payload};
}

Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const StyleBuffer& style) {
    const HLC hlc = workbook.getCurrentHLC();
    // New format: {"style":"<base64>"}
    const std::string payload = "{\"style\":\"" + style.toBase64() + "\"}";
    return {hlc, OpType::RANGE_SET_STYLE, rangeId, payload};
}

Operation makeRangeClearStyleOp(Workbook& workbook, const ID& rangeId) {
    const HLC hlc = workbook.getCurrentHLC();
    // Clear format: {"style":""}
    return {hlc, OpType::RANGE_SET_STYLE, rangeId, "{\"style\":\"\"}"};
}

Operation makeRangeSetFormatOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_SET_FORMAT, rangeId, payload};
}

Operation makeRangeSetFormatOp(Workbook& workbook, const ID& rangeId, const FormatBuffer& format) {
    const HLC hlc = workbook.getCurrentHLC();
    const std::string payload = "{\"format\":\"" + format.toBase64() + "\"}";
    return {hlc, OpType::RANGE_SET_FORMAT, rangeId, payload};
}

Operation makeRangeClearFormatOp(Workbook& workbook, const ID& rangeId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_SET_FORMAT, rangeId, "{\"format\":\"\"}"};
}

// =============================================================================
// Bootstrap OpLog
// =============================================================================

size_t bootstrapOpLog(Workbook& workbook) {
    size_t count = 0;
    OpLog* oplog = workbook.getOpLog();

    // Clear any existing operations (start fresh)
    oplog->clear();

    // Iterate through all sheets
    for (const auto& sheet : workbook.sheets) {
        // Collect and sort columns by position
        std::vector<std::pair<uint32_t, const Axis*>> columns;
        for (const ID& colId : sheet->getColumnIds()) {
            const Axis* axis = workbook.getColumn(colId);
            if (axis) {
                columns.emplace_back(axis->position, axis);
            }
        }
        std::sort(columns.begin(), columns.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Generate COL_INSERT operations for columns (in position order)
        for (const auto& [pos, axis] : columns) {
            const std::string payload =
                "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(axis->size) + "}";
            const Operation op = makeColInsertOp(workbook, axis->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Collect and sort rows by position
        std::vector<std::pair<uint32_t, const Axis*>> rows;
        for (const ID& rowId : sheet->getRowIds()) {
            const Axis* axis = workbook.getRow(rowId);
            if (axis) {
                rows.emplace_back(axis->position, axis);
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Generate ROW_INSERT operations for rows (in position order)
        for (const auto& [pos, axis] : rows) {
            const std::string payload =
                "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(axis->size) + "}";
            const Operation op = makeRowInsertOp(workbook, axis->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Generate CELL_SET_VALUE operations for all cells
        for (const ID& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }

            // Skip empty cells UNLESS they have a style (styled empty cells need to be synced)
            const bool isEmpty = cell->value.type == CellValueType::STRING &&
                                 cell->value.raw.empty() && cell->formula == nullptr;
            if (isEmpty && !cell->hasStyle()) {
                continue;
            }

            // Build payload based on cell type
            const std::string colIdStr = cell->colId.toString();
            const std::string rowIdStr = cell->rowId.toString();
            std::string idSuffix;
            idSuffix.reserve(40);
            idSuffix += ",\"col_id\":\"";
            idSuffix += colIdStr;
            idSuffix += "\",\"row_id\":\"";
            idSuffix += rowIdStr;
            idSuffix += "\"}";

            std::string payload;
            if (cell->isFormula()) {
                const Formula* formula = cell->getFormula();
                if (formula != nullptr && formula->ast != nullptr) {
                    // Generate UUID formula text from AST
                    const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                    payload = "{\"type\":\"f\",\"value\":\"" + internal::jsonEscape(uuidFormula) +
                              "\"" + idSuffix;
                } else {
                    continue;  // Skip cells with invalid formulas
                }
            } else {
                const char typeChar = valueTypeToChar(cell->value.type);
                payload = "{\"type\":\"" + std::string(1, typeChar) + "\",\"value\":\"" +
                          internal::jsonEscape(cell->value.raw) + "\"" + idSuffix;
            }

            const Operation op = makeCellSetValueOp(workbook, cell->id, payload);
            oplog->addOperation(op);
            count++;
        }
    }

    // Note: FORMAT_DEFINE operations are no longer needed - formats are content-addressed
    // and embedded directly in CELL_SET_FORMAT, AXIS_SET_FORMAT, RANGE_SET_FORMAT operations

    // Generate RANGE_ADD operations for all ranges (merge ranges, style ranges, etc.)
    // This must come AFTER column/row INSERT operations so the axis IDs exist
    for (const auto& sheet : workbook.sheets) {
        for (const ID& rangeId : sheet->getRangeIds()) {
            const Range* range = workbook.getRange(rangeId);
            if (!range) {
                continue;
            }

            // Build RANGE_ADD payload (no sheet_id needed - derived from startColId)
            std::string payload = "{";
            payload += "\"start_col_id\":\"" + range->startColId.toString() + "\"";
            payload += ",\"start_row_id\":\"" + range->startRowId.toString() + "\"";
            payload += ",\"end_col_id\":\"" + range->endColId.toString() + "\"";
            payload += ",\"end_row_id\":\"" + range->endRowId.toString() + "\"";
            payload += ",\"flags\":" + std::to_string(static_cast<int>(range->flags));
            payload += "}";

            const Operation rangeOp = makeRangeAddOp(workbook, range->id, payload);
            oplog->addOperation(rangeOp);
            count++;

            // If this range has a style, generate RANGE_SET_STYLE operation
            if (range->style.has_value()) {
                const Operation styleOp =
                    makeRangeSetStyleOp(workbook, range->id, range->style.value());
                oplog->addOperation(styleOp);
                count++;
            }
        }
    }

    // Generate CELL_SET_STYLE operations for cells that have styles
    for (const auto& sheet : workbook.sheets) {
        for (const ID& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook.getCell(cellId);
            if (!cell || !cell->hasStyle()) {
                continue;
            }

            const StyleBuffer* entityStyle = workbook.getEntityStyle(cellId);
            if (entityStyle != nullptr) {
                const Operation op = makeCellSetStyleOp(workbook, cellId, *entityStyle);
                oplog->addOperation(op);
                count++;
            }
        }
    }

    // Generate AXIS_SET_STYLE operations for columns and rows that have styles
    for (const auto& sheet : workbook.sheets) {
        // Column styles
        for (const ID& colId : sheet->getColumnIds()) {
            const Axis* axis = workbook.getColumn(colId);
            if (!axis || !axis->hasStyle()) {
                continue;
            }

            const StyleBuffer* entityStyle = workbook.getEntityStyle(colId);
            if (entityStyle != nullptr) {
                const Operation op = makeAxisSetStyleOp(workbook, colId, *entityStyle);
                oplog->addOperation(op);
                count++;
            }
        }

        // Row styles
        for (const ID& rowId : sheet->getRowIds()) {
            const Axis* axis = workbook.getRow(rowId);
            if (!axis || !axis->hasStyle()) {
                continue;
            }

            const StyleBuffer* entityStyle = workbook.getEntityStyle(rowId);
            if (entityStyle != nullptr) {
                const Operation op = makeAxisSetStyleOp(workbook, rowId, *entityStyle);
                oplog->addOperation(op);
                count++;
            }
        }
    }

    // Generate CELL_SET_FORMAT operations for cells that have formats
    for (const auto& sheet : workbook.sheets) {
        for (const ID& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook.getCell(cellId);
            if (!cell || !cell->hasFormat()) {
                continue;
            }

            const FormatBuffer* entityFormat = workbook.getEntityFormat(cellId);
            if (entityFormat != nullptr) {
                const Operation op = makeCellSetFormatOp(workbook, cellId, *entityFormat);
                oplog->addOperation(op);
                count++;
            }
        }
    }

    // Generate AXIS_SET_FORMAT operations for columns and rows that have formats
    for (const auto& sheet : workbook.sheets) {
        // Column formats
        for (const ID& colId : sheet->getColumnIds()) {
            const Axis* axis = workbook.getColumn(colId);
            if (!axis || !axis->hasFormat()) {
                continue;
            }

            const FormatBuffer* entityFormat = workbook.getEntityFormat(colId);
            if (entityFormat != nullptr) {
                const Operation op = makeAxisSetFormatOp(workbook, colId, *entityFormat);
                oplog->addOperation(op);
                count++;
            }
        }

        // Row formats
        for (const ID& rowId : sheet->getRowIds()) {
            const Axis* axis = workbook.getRow(rowId);
            if (!axis || !axis->hasFormat()) {
                continue;
            }

            const FormatBuffer* entityFormat = workbook.getEntityFormat(rowId);
            if (entityFormat != nullptr) {
                const Operation op = makeAxisSetFormatOp(workbook, rowId, *entityFormat);
                oplog->addOperation(op);
                count++;
            }
        }
    }

    // Generate RANGE_SET_FORMAT operations for ranges that have formats
    for (const auto& sheet : workbook.sheets) {
        for (const ID& rangeId : sheet->getRangeIds()) {
            const Range* range = workbook.getRange(rangeId);
            if (!range || !range->format.has_value()) {
                continue;
            }

            const Operation op = makeRangeSetFormatOp(workbook, rangeId, range->format.value());
            oplog->addOperation(op);
            count++;
        }
    }

    // Generate NAMED_RANGE_DEFINE operations for all named ranges
    const NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry != nullptr) {
        for (const NamedRange* nr : registry->getAll()) {
            std::string payload = "{";
            payload += "\"name\":\"" + internal::jsonEscape(nr->name) + "\"";

            // Scope
            if (nr->scope == NamedRangeScope::WORKBOOK) {
                payload += ",\"scope\":\"W\",\"scopeSheetId\":\"-\"";
            } else {
                payload +=
                    ",\"scope\":\"S\",\"scopeSheetId\":\"" + nr->scopeSheetId.toString() + "\"";
            }

            // Target type
            const NamedRangeTarget& target = nr->target;
            switch (target.type) {
                case NamedRangeTarget::Type::CELL:
                    payload += ",\"targetType\":\"CELL\"";
                    break;
                case NamedRangeTarget::Type::RANGE:
                    payload += ",\"targetType\":\"RANGE\"";
                    break;
                case NamedRangeTarget::Type::COLUMN:
                    payload += ",\"targetType\":\"COLUMN\"";
                    break;
                case NamedRangeTarget::Type::ROW:
                    payload += ",\"targetType\":\"ROW\"";
                    break;
                case NamedRangeTarget::Type::COLUMN_RANGE:
                    payload += ",\"targetType\":\"COLUMN_RANGE\"";
                    break;
                case NamedRangeTarget::Type::ROW_RANGE:
                    payload += ",\"targetType\":\"ROW_RANGE\"";
                    break;
            }

            // Target IDs
            payload += ",\"id1\":\"" + target.id1.toString() + "\"";
            payload += ",\"id2\":\"" + (target.id2.isNull() ? "-" : target.id2.toString()) + "\"";
            payload += ",\"targetSheetId\":\"" +
                       (target.sheetId.isNull() ? "-" : target.sheetId.toString()) + "\"";
            payload += "}";

            const Operation op = makeNamedRangeDefineOp(workbook, payload);
            oplog->addOperation(op);
            count++;
        }
    }

    return count;
}

}  // namespace cells
