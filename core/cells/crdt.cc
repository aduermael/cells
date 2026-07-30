// =============================================================================
// CRDT Operations Main Entry
// =============================================================================
//
// Central dispatcher for CRDT operations and operation creation helpers.
// This file coordinates the split implementation across crdt_cell.cc,
// crdt_axis.cc, and crdt_range.cc.
//
// Key responsibilities:
// - applyOperation() dispatcher that routes to specific operation handlers
// - Operation maker functions (makeCellSetOp, makeColSetOp, etc.)
// - bootstrapOpLog() for generating OpLog from existing workbook state
// - JSON utility functions used by all crdt_*.cc files
//
// Split structure:
// - crdt.cc: Main entry, dispatch, operation makers, JSON utilities
// - crdt_cell.cc: Cell operations (CELL_SET, CELL_DELETE)
// - crdt_axis.cc: Axis operations (COL_SET, COL_DELETE, ROW_SET, ROW_DELETE, etc.)
// - crdt_range.cc: Range operations (RANGE_SET, RANGE_DELETE)
//
// =============================================================================

#include "core/cells/crdt.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <charconv>

#include "core/cells/crdt_internal.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
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

Sheet* ensureSheetForOp(Workbook& workbook, const Operation& op) {
    Sheet* sheet = nullptr;
    if (!op.sheetId.isNull()) {
        sheet = workbook.getSheet(op.sheetId);
        if (sheet == nullptr) {
            // Peer history often lacks SHEET_SET when the sheet was created by
            // createEmptyWorkbook() outside the oplog. Materialize so COL/ROW/CELL apply.
            auto newSheet = std::make_unique<Sheet>(op.sheetId, "Sheet1");
            workbook.addSheet(std::move(newSheet));
            sheet = workbook.getSheet(op.sheetId);
        }
    }
    if (sheet == nullptr && !workbook.sheets.empty()) {
        sheet = workbook.sheets[0].get();
    }
    return sheet;
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

bool extractJSONBool(const std::string& json, const std::string& key, bool defaultValue) {
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

    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return defaultValue;
}

double extractJSONDouble(const std::string& json, const std::string& key, double defaultValue) {
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

    const char* start = json.c_str() + pos;
    char* endPtr = nullptr;  // NOLINT(misc-const-correctness)
    const double value = std::strtod(start, &endPtr);
    if (endPtr == start) {
        return defaultValue;
    }
    return value;
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
        // Cell operations
        case OpType::CELL_SET:
            result = internal::applyCellSet(workbook, op);
            break;
        case OpType::CELL_DELETE:
            result = internal::applyCellDelete(workbook, op);
            break;

        // Column operations
        case OpType::COL_SET:
            result = internal::applyColSet(workbook, op);
            break;
        case OpType::COL_DELETE:
            result = internal::applyColDelete(workbook, op);
            break;

        // Row operations
        case OpType::ROW_SET:
            result = internal::applyRowSet(workbook, op);
            break;
        case OpType::ROW_DELETE:
            result = internal::applyRowDelete(workbook, op);
            break;

        // Sheet operations
        case OpType::SHEET_SET:
            result = internal::applySheetSet(workbook, op);
            break;
        case OpType::SHEET_DELETE:
            result = internal::applySheetDelete(workbook, op);
            break;

        // Workbook operations
        case OpType::WORKBOOK_SET:
            result = internal::applyWorkbookSet(workbook, op);
            break;

        // Named range operations
        case OpType::NAMED_RANGE_SET:
            result = internal::applyNamedRangeSet(workbook, op);
            break;
        case OpType::NAMED_RANGE_DELETE:
            result = internal::applyNamedRangeDelete(workbook, op);
            break;

        // Range operations
        case OpType::RANGE_SET:
            result = internal::applyRangeSet(workbook, op);
            break;
        case OpType::RANGE_DELETE:
            result = internal::applyRangeDelete(workbook, op);
            break;
    }

    // Only add to OpLog on successful application
    if (result == ApplyResult::SUCCESS || result == ApplyResult::SUPERSEDED ||
        result == ApplyResult::RESURRECTED) {
        oplog->addOperation(op);

        // Periodic pruning for non-collaboration mode
        constexpr size_t PRUNE_THRESHOLD = 100;
        if (!workbook.isCollaborating() && oplog->size() >= PRUNE_THRESHOLD) {
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
        } else {
            // INVALID_TARGET usually means col/row missing (peer never got COL_SET/ROW_SET)
            const char* why = "OTHER";
            switch (result) {
                case ApplyResult::ALREADY_APPLIED:
                    why = "ALREADY_APPLIED";
                    break;
                case ApplyResult::INVALID_TARGET:
                    why = "INVALID_TARGET";
                    break;
                case ApplyResult::INVALID_PAYLOAD:
                    why = "INVALID_PAYLOAD";
                    break;
                default:
                    break;
            }
            LOG_DEBUG("[CRDT] apply failed %s op=%s target=%s sheet=%s payload=%.80s", why,
                      opTypeToString(op.type), op.target_id.toString().c_str(),
                      op.sheetId.toString().c_str(), op.payload.c_str());
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

Operation makeCellSetOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET, cellId, payload};
}

Operation makeCellSetOp(Workbook& workbook, const ID& cellId, const ID& sheetId,
                        const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET, cellId, sheetId, payload};
}

Operation makeCellDeleteOp(Workbook& workbook, const ID& cellId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_DELETE, cellId, "{}"};
}

Operation makeColSetOp(Workbook& workbook, const ID& colId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_SET, colId, payload};
}

Operation makeColSetOp(Workbook& workbook, const ID& colId, const ID& sheetId,
                       const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_SET, colId, sheetId, payload};
}

Operation makeColDeleteOp(Workbook& workbook, const ID& colId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_DELETE, colId, "{}"};
}

Operation makeRowSetOp(Workbook& workbook, const ID& rowId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_SET, rowId, payload};
}

Operation makeRowSetOp(Workbook& workbook, const ID& rowId, const ID& sheetId,
                       const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_SET, rowId, sheetId, payload};
}

Operation makeRowDeleteOp(Workbook& workbook, const ID& rowId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::ROW_DELETE, rowId, "{}"};
}

Operation makeSheetSetOp(Workbook& workbook, const ID& sheetId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::SHEET_SET, sheetId, payload};
}

Operation makeSheetDeleteOp(Workbook& workbook, const ID& sheetId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::SHEET_DELETE, sheetId, "{}"};
}

Operation makeWorkbookSetOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::WORKBOOK_SET, workbook.id, payload};
}

Operation makeNamedRangeSetOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::NAMED_RANGE_SET, workbook.id, payload};
}

Operation makeNamedRangeDeleteOp(Workbook& workbook, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::NAMED_RANGE_DELETE, workbook.id, payload};
}

Operation makeRangeSetOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_SET, rangeId, payload};
}

Operation makeRangeDeleteOp(Workbook& workbook, const ID& rangeId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::RANGE_DELETE, rangeId, payload};
}

// =============================================================================
// Convenience Functions for Style/Format Operations
// =============================================================================

Operation makeCellSetStyleOp(Workbook& workbook, const ID& cellId, const StyleBuffer& style) {
    // Build full-state payload for resurrection correctness
    // Include all current cell properties so this operation is self-sufficient
    const Cell* cell = workbook.getCell(cellId);
    if (!cell) {
        // Cell doesn't exist, just set style (will fail at apply time)
        const std::string payload = "{\"sty\":\"" + style.toBase64() + "\"}";
        return makeCellSetOp(workbook, cellId, payload);
    }

    std::string payload = "{\"col\":\"" + cell->colId.toString() + "\"";
    payload += ",\"row\":\"" + cell->rowId.toString() + "\"";

    // Include value/formula
    if (cell->isFormula()) {
        const Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
            payload += ",\"t\":\"f\",\"v\":\"" + internal::jsonEscape(uuidFormula) + "\"";
        }
    } else {
        const bool isEmpty = cell->value.type == CellValueType::STRING && cell->value.raw.empty();
        if (!isEmpty) {
            const char typeChar = valueTypeToChar(cell->value.type);
            payload += ",\"t\":\"" + std::string(1, typeChar) + "\"";
            payload += ",\"v\":\"" + internal::jsonEscape(cell->value.raw) + "\"";
        }
    }

    // Include new style
    payload += ",\"sty\":\"" + style.toBase64() + "\"";

    // Include existing format if present
    if (cell->hasFormat()) {
        const FormatBuffer* fmt = workbook.getEntityFormat(cellId);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }

    payload += "}";
    return makeCellSetOp(workbook, cellId, payload);
}

Operation makeCellClearStyleOp(Workbook& workbook, const ID& cellId) {
    // Build full-state payload for resurrection correctness
    const Cell* cell = workbook.getCell(cellId);
    if (!cell) {
        return makeCellSetOp(workbook, cellId, "{\"sty\":\"\"}");
    }

    std::string payload = "{\"col\":\"" + cell->colId.toString() + "\"";
    payload += ",\"row\":\"" + cell->rowId.toString() + "\"";

    // Include value/formula
    if (cell->isFormula()) {
        const Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
            payload += ",\"t\":\"f\",\"v\":\"" + internal::jsonEscape(uuidFormula) + "\"";
        }
    } else {
        const bool isEmpty = cell->value.type == CellValueType::STRING && cell->value.raw.empty();
        if (!isEmpty) {
            const char typeChar = valueTypeToChar(cell->value.type);
            payload += ",\"t\":\"" + std::string(1, typeChar) + "\"";
            payload += ",\"v\":\"" + internal::jsonEscape(cell->value.raw) + "\"";
        }
    }

    // Clear style (empty string)
    payload += ",\"sty\":\"\"";

    // Include existing format if present
    if (cell->hasFormat()) {
        const FormatBuffer* fmt = workbook.getEntityFormat(cellId);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }

    payload += "}";
    return makeCellSetOp(workbook, cellId, payload);
}

Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const FormatBuffer& format) {
    // Build full-state payload for resurrection correctness
    const Cell* cell = workbook.getCell(cellId);
    if (!cell) {
        const std::string payload = "{\"fmt\":\"" + format.toBase64() + "\"}";
        return makeCellSetOp(workbook, cellId, payload);
    }

    std::string payload = "{\"col\":\"" + cell->colId.toString() + "\"";
    payload += ",\"row\":\"" + cell->rowId.toString() + "\"";

    // Include value/formula
    if (cell->isFormula()) {
        const Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
            payload += ",\"t\":\"f\",\"v\":\"" + internal::jsonEscape(uuidFormula) + "\"";
        }
    } else {
        const bool isEmpty = cell->value.type == CellValueType::STRING && cell->value.raw.empty();
        if (!isEmpty) {
            const char typeChar = valueTypeToChar(cell->value.type);
            payload += ",\"t\":\"" + std::string(1, typeChar) + "\"";
            payload += ",\"v\":\"" + internal::jsonEscape(cell->value.raw) + "\"";
        }
    }

    // Include existing style if present
    if (cell->hasStyle()) {
        const StyleBuffer* sty = workbook.getEntityStyle(cellId);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }

    // Include new format
    payload += ",\"fmt\":\"" + format.toBase64() + "\"";

    payload += "}";
    return makeCellSetOp(workbook, cellId, payload);
}

Operation makeCellClearFormatOp(Workbook& workbook, const ID& cellId) {
    // Build full-state payload for resurrection correctness
    const Cell* cell = workbook.getCell(cellId);
    if (!cell) {
        return makeCellSetOp(workbook, cellId, "{\"fmt\":\"\"}");
    }

    std::string payload = "{\"col\":\"" + cell->colId.toString() + "\"";
    payload += ",\"row\":\"" + cell->rowId.toString() + "\"";

    // Include value/formula
    if (cell->isFormula()) {
        const Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
            payload += ",\"t\":\"f\",\"v\":\"" + internal::jsonEscape(uuidFormula) + "\"";
        }
    } else {
        const bool isEmpty = cell->value.type == CellValueType::STRING && cell->value.raw.empty();
        if (!isEmpty) {
            const char typeChar = valueTypeToChar(cell->value.type);
            payload += ",\"t\":\"" + std::string(1, typeChar) + "\"";
            payload += ",\"v\":\"" + internal::jsonEscape(cell->value.raw) + "\"";
        }
    }

    // Include existing style if present
    if (cell->hasStyle()) {
        const StyleBuffer* sty = workbook.getEntityStyle(cellId);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }

    // Clear format (empty string)
    payload += ",\"fmt\":\"\"";

    payload += "}";
    return makeCellSetOp(workbook, cellId, payload);
}

// Helper to build full-state axis payload
namespace {
std::string buildFullAxisPayload(Workbook& workbook, const Axis* axis, const StyleBuffer* newStyle,
                                 const FormatBuffer* newFormat, const bool* newHidden) {
    std::string payload = "{\"pos\":" + std::to_string(axis->position);

    // Only include size if explicitly set (sizeSet=true)
    if (axis->sizeSet()) {
        payload += ",\"size\":" + std::to_string(axis->size);
    }

    if (!axis->name.empty()) {
        payload += ",\"name\":\"" + internal::jsonEscape(axis->name) + "\"";
    }

    // Style: use new style if provided, otherwise include existing
    if (newStyle != nullptr) {
        if (newStyle->isEmpty()) {
            // Empty style signals clear - use empty string
            payload += ",\"sty\":\"\"";
        } else {
            payload += ",\"sty\":\"" + newStyle->toBase64() + "\"";
        }
    } else if (axis->hasStyle()) {
        const StyleBuffer* sty = workbook.getEntityStyle(axis->id);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }

    // Format: use new format if provided, otherwise include existing
    if (newFormat != nullptr) {
        if (newFormat->isEmpty()) {
            // Empty format signals clear - use empty string
            payload += ",\"fmt\":\"\"";
        } else {
            payload += ",\"fmt\":\"" + newFormat->toBase64() + "\"";
        }
    } else if (axis->hasFormat()) {
        const FormatBuffer* fmt = workbook.getEntityFormat(axis->id);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }

    // Hidden: use new value if provided, otherwise include existing
    if (newHidden != nullptr) {
        payload += *newHidden ? ",\"hidden\":true" : ",\"hidden\":false";
    } else if (axis->hidden()) {
        payload += ",\"hidden\":true";
    }

    payload += "}";
    return payload;
}
}  // namespace

Operation makeAxisSetStyleOp(Workbook& workbook, const ID& axisId, const StyleBuffer& style) {
    // Build full-state payload for resurrection correctness
    const Axis* axis = workbook.getColumn(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, &style, nullptr, nullptr);
        return makeColSetOp(workbook, axisId, payload);
    }

    axis = workbook.getRow(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, &style, nullptr, nullptr);
        return makeRowSetOp(workbook, axisId, payload);
    }

    // Axis doesn't exist, fall back to sparse payload
    const std::string payload = "{\"sty\":\"" + style.toBase64() + "\"}";
    return makeColSetOp(workbook, axisId, payload);
}

Operation makeAxisClearStyleOp(Workbook& workbook, const ID& axisId) {
    // Build full-state payload with empty style
    const StyleBuffer emptyStyle;
    const Axis* axis = workbook.getColumn(axisId);
    if (axis != nullptr) {
        const std::string payload =
            buildFullAxisPayload(workbook, axis, &emptyStyle, nullptr, nullptr);
        return makeColSetOp(workbook, axisId, payload);
    }

    axis = workbook.getRow(axisId);
    if (axis != nullptr) {
        const std::string payload =
            buildFullAxisPayload(workbook, axis, &emptyStyle, nullptr, nullptr);
        return makeRowSetOp(workbook, axisId, payload);
    }

    return makeColSetOp(workbook, axisId, "{\"sty\":\"\"}");
}

Operation makeAxisSetFormatOp(Workbook& workbook, const ID& axisId, const FormatBuffer& format) {
    // Build full-state payload for resurrection correctness
    const Axis* axis = workbook.getColumn(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, nullptr, &format, nullptr);
        return makeColSetOp(workbook, axisId, payload);
    }

    axis = workbook.getRow(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, nullptr, &format, nullptr);
        return makeRowSetOp(workbook, axisId, payload);
    }

    const std::string payload = "{\"fmt\":\"" + format.toBase64() + "\"}";
    return makeColSetOp(workbook, axisId, payload);
}

Operation makeAxisClearFormatOp(Workbook& workbook, const ID& axisId) {
    // Build full-state payload with empty format
    const FormatBuffer emptyFormat;
    const Axis* axis = workbook.getColumn(axisId);
    if (axis != nullptr) {
        const std::string payload =
            buildFullAxisPayload(workbook, axis, nullptr, &emptyFormat, nullptr);
        return makeColSetOp(workbook, axisId, payload);
    }

    axis = workbook.getRow(axisId);
    if (axis != nullptr) {
        const std::string payload =
            buildFullAxisPayload(workbook, axis, nullptr, &emptyFormat, nullptr);
        return makeRowSetOp(workbook, axisId, payload);
    }

    return makeColSetOp(workbook, axisId, "{\"fmt\":\"\"}");
}

Operation makeAxisSetHiddenOp(Workbook& workbook, const ID& axisId, bool hidden) {
    // Build full-state payload for resurrection correctness
    const Axis* axis = workbook.getColumn(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, nullptr, nullptr, &hidden);
        return makeColSetOp(workbook, axisId, payload);
    }

    axis = workbook.getRow(axisId);
    if (axis != nullptr) {
        const std::string payload = buildFullAxisPayload(workbook, axis, nullptr, nullptr, &hidden);
        return makeRowSetOp(workbook, axisId, payload);
    }

    const std::string payload = hidden ? "{\"hidden\":true}" : "{\"hidden\":false}";
    return makeColSetOp(workbook, axisId, payload);
}

Operation makeRangeSetStyleOp(Workbook& workbook, const ID& rangeId, const StyleBuffer& style) {
    // Build full-state payload for resurrection correctness
    const Range* range = workbook.getRange(rangeId);
    if (!range) {
        const std::string payload = "{\"sty\":\"" + style.toBase64() + "\"}";
        return makeRangeSetOp(workbook, rangeId, payload);
    }

    std::string payload = "{\"startCol\":\"" + range->startColId.toString() + "\"";
    payload += ",\"startRow\":\"" + range->startRowId.toString() + "\"";
    payload += ",\"endCol\":\"" + range->endColId.toString() + "\"";
    payload += ",\"endRow\":\"" + range->endRowId.toString() + "\"";
    payload += ",\"flags\":" + std::to_string(static_cast<int>(range->flags));
    payload += ",\"sty\":\"" + style.toBase64() + "\"";

    // Include existing format if present
    if (range->format.has_value()) {
        payload += ",\"fmt\":\"" + range->format->toBase64() + "\"";
    }

    payload += "}";
    return makeRangeSetOp(workbook, rangeId, payload);
}

Operation makeRangeClearStyleOp(Workbook& workbook, const ID& rangeId) {
    // Build full-state payload with empty style
    const Range* range = workbook.getRange(rangeId);
    if (!range) {
        return makeRangeSetOp(workbook, rangeId, "{\"sty\":\"\"}");
    }

    std::string payload = "{\"startCol\":\"" + range->startColId.toString() + "\"";
    payload += ",\"startRow\":\"" + range->startRowId.toString() + "\"";
    payload += ",\"endCol\":\"" + range->endColId.toString() + "\"";
    payload += ",\"endRow\":\"" + range->endRowId.toString() + "\"";
    payload += ",\"flags\":" + std::to_string(static_cast<int>(range->flags));
    payload += ",\"sty\":\"\"";

    // Include existing format if present
    if (range->format.has_value()) {
        payload += ",\"fmt\":\"" + range->format->toBase64() + "\"";
    }

    payload += "}";
    return makeRangeSetOp(workbook, rangeId, payload);
}

Operation makeRangeSetFormatOp(Workbook& workbook, const ID& rangeId, const FormatBuffer& format) {
    // Build full-state payload for resurrection correctness
    const Range* range = workbook.getRange(rangeId);
    if (!range) {
        const std::string payload = "{\"fmt\":\"" + format.toBase64() + "\"}";
        return makeRangeSetOp(workbook, rangeId, payload);
    }

    std::string payload = "{\"startCol\":\"" + range->startColId.toString() + "\"";
    payload += ",\"startRow\":\"" + range->startRowId.toString() + "\"";
    payload += ",\"endCol\":\"" + range->endColId.toString() + "\"";
    payload += ",\"endRow\":\"" + range->endRowId.toString() + "\"";
    payload += ",\"flags\":" + std::to_string(static_cast<int>(range->flags));

    // Include existing style if present
    if (range->style.has_value()) {
        payload += ",\"sty\":\"" + range->style->toBase64() + "\"";
    }

    payload += ",\"fmt\":\"" + format.toBase64() + "\"";

    payload += "}";
    return makeRangeSetOp(workbook, rangeId, payload);
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
        // Always emit SHEET_SET first so peers can materialize the sheet before
        // COL/ROW/CELL ops that reference sheetId (createEmptyWorkbook does not
        // go through CRDT, so without this bootstrap peer_ops=3 with no sheet).
        {
            std::string sheetPayload = "{\"name\":\"" + internal::jsonEscape(sheet->name) + "\"}";
            const Operation sheetOp = makeSheetSetOp(workbook, sheet->id, sheetPayload);
            oplog->addOperation(sheetOp);
            count++;
        }

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

        // Generate COL_SET operations for columns (in position order)
        for (const auto& [pos, axis] : columns) {
            std::string payload = "{\"pos\":" + std::to_string(pos);
            // Only include size if explicitly set (sizeSet=true)
            if (axis->sizeSet()) {
                payload += ",\"size\":" + std::to_string(axis->size);
            }
            if (axis->sizeOriginal > 0) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), axis->sizeOriginal);
                payload += ",\"sizeOriginal\":";
                payload.append(buf, static_cast<size_t>(ptr - buf));
            }
            if (!axis->name.empty()) {
                payload += ",\"name\":\"" + internal::jsonEscape(axis->name) + "\"";
            }
            if (axis->hasStyle()) {
                const StyleBuffer* sty = workbook.getEntityStyle(axis->id);
                if (sty) {
                    payload += ",\"sty\":\"" + sty->toBase64() + "\"";
                }
            }
            if (axis->hasFormat()) {
                const FormatBuffer* fmt = workbook.getEntityFormat(axis->id);
                if (fmt) {
                    payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
                }
            }
            if (axis->hidden()) {
                payload += ",\"hidden\":true";
            }
            payload += "}";
            const Operation op = makeColSetOp(workbook, axis->id, sheet->id, payload);
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

        // Generate ROW_SET operations for rows (in position order)
        for (const auto& [pos, axis] : rows) {
            std::string payload = "{\"pos\":" + std::to_string(pos);
            // Only include size if explicitly set (sizeSet=true)
            if (axis->sizeSet()) {
                payload += ",\"size\":" + std::to_string(axis->size);
            }
            if (axis->sizeOriginal > 0) {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), axis->sizeOriginal);
                payload += ",\"sizeOriginal\":";
                payload.append(buf, static_cast<size_t>(ptr - buf));
            }
            if (axis->hasStyle()) {
                const StyleBuffer* sty = workbook.getEntityStyle(axis->id);
                if (sty) {
                    payload += ",\"sty\":\"" + sty->toBase64() + "\"";
                }
            }
            if (axis->hasFormat()) {
                const FormatBuffer* fmt = workbook.getEntityFormat(axis->id);
                if (fmt) {
                    payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
                }
            }
            if (axis->hidden()) {
                payload += ",\"hidden\":true";
            }
            payload += "}";
            const Operation op = makeRowSetOp(workbook, axis->id, sheet->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Generate CELL_SET operations for all cells
        for (const ID& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }

            // Skip empty cells UNLESS they have a style (styled empty cells need to be synced)
            const bool isEmpty = cell->value.type == CellValueType::STRING &&
                                 cell->value.raw.empty() && cell->formula == nullptr;
            if (isEmpty && !cell->hasStyle() && !cell->hasFormat()) {
                continue;
            }

            // Build payload
            std::string payload = "{\"col\":\"" + cell->colId.toString() + "\"";
            payload += ",\"row\":\"" + cell->rowId.toString() + "\"";

            if (cell->isFormula()) {
                const Formula* formula = cell->getFormula();
                if (formula != nullptr && formula->ast != nullptr) {
                    const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                    payload += ",\"t\":\"f\",\"v\":\"" + internal::jsonEscape(uuidFormula) + "\"";
                } else {
                    continue;  // Skip cells with invalid formulas
                }
            } else if (!isEmpty) {
                const char typeChar = valueTypeToChar(cell->value.type);
                payload += ",\"t\":\"" + std::string(1, typeChar) + "\"";
                payload += ",\"v\":\"" + internal::jsonEscape(cell->value.raw) + "\"";
            }

            if (cell->hasStyle()) {
                const StyleBuffer* sty = workbook.getEntityStyle(cell->id);
                if (sty) {
                    payload += ",\"sty\":\"" + sty->toBase64() + "\"";
                }
            }
            if (cell->hasFormat()) {
                const FormatBuffer* fmt = workbook.getEntityFormat(cell->id);
                if (fmt) {
                    payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
                }
            }

            payload += "}";
            const Operation op = makeCellSetOp(workbook, cell->id, sheet->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Generate RANGE_SET operations for all ranges
        for (const ID& rangeId : sheet->getRangeIds()) {
            const Range* range = workbook.getRange(rangeId);
            if (!range) {
                continue;
            }

            std::string payload = "{\"startCol\":\"" + range->startColId.toString() + "\"";
            payload += ",\"startRow\":\"" + range->startRowId.toString() + "\"";
            payload += ",\"endCol\":\"" + range->endColId.toString() + "\"";
            payload += ",\"endRow\":\"" + range->endRowId.toString() + "\"";
            payload += ",\"flags\":" + std::to_string(static_cast<int>(range->flags));

            if (range->style.has_value()) {
                payload += ",\"sty\":\"" + range->style->toBase64() + "\"";
            }
            if (range->format.has_value()) {
                payload += ",\"fmt\":\"" + range->format->toBase64() + "\"";
            }

            payload += "}";
            const Operation op = makeRangeSetOp(workbook, range->id, payload);
            oplog->addOperation(op);
            count++;
        }
    }

    // Generate NAMED_RANGE_SET operations for all named ranges
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

            const Operation op = makeNamedRangeSetOp(workbook, payload);
            oplog->addOperation(op);
            count++;
        }
    }

    return count;
}

// =============================================================================
// Collab-safe ensure + grid helpers
// =============================================================================

Axis* ensureColumnViaCrdt(Workbook& workbook, Sheet& sheet, uint32_t position, bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    Axis* existing = sheet.getColumnByPosition(position);
    if (existing != nullptr) {
        return existing;
    }
    const ID colId = generate_id();
    const std::string payload = "{\"pos\":" + std::to_string(position) + "}";
    const Operation op = makeColSetOp(workbook, colId, sheet.id, payload);
    applyOperation(workbook, op);
    if (outCreated != nullptr) {
        *outCreated = true;
    }
    return sheet.getColumn(colId);
}

Axis* ensureRowViaCrdt(Workbook& workbook, Sheet& sheet, uint32_t position, bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    Axis* existing = sheet.getRowByPosition(position);
    if (existing != nullptr) {
        return existing;
    }
    const ID rowId = generate_id();
    const std::string payload = "{\"pos\":" + std::to_string(position) + "}";
    const Operation op = makeRowSetOp(workbook, rowId, sheet.id, payload);
    applyOperation(workbook, op);
    if (outCreated != nullptr) {
        *outCreated = true;
    }
    return sheet.getRow(rowId);
}

Cell* ensureCellViaCrdt(Workbook& workbook, Sheet& sheet, const ID& colId, const ID& rowId,
                        bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    Cell* existing = sheet.getCellAt(colId, rowId);
    if (existing != nullptr) {
        return existing;
    }
    const ID cellId = generate_id();
    const std::string payload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" + colId.toString() +
                                "\",\"row\":\"" + rowId.toString() + "\"}";
    const Operation op = makeCellSetOp(workbook, cellId, sheet.id, payload);
    applyOperation(workbook, op);
    if (outCreated != nullptr) {
        *outCreated = true;
    }
    return sheet.getCell(cellId);
}

Cell* ensureCellAtPositionViaCrdt(Workbook& workbook, Sheet& sheet, uint32_t colPos,
                                  uint32_t rowPos, bool* outColCreated, bool* outRowCreated,
                                  bool* outCellCreated) {
    Axis* col = ensureColumnViaCrdt(workbook, sheet, colPos, outColCreated);
    Axis* row = ensureRowViaCrdt(workbook, sheet, rowPos, outRowCreated);
    if (col == nullptr || row == nullptr) {
        if (outCellCreated != nullptr) {
            *outCellCreated = false;
        }
        return nullptr;
    }
    return ensureCellViaCrdt(workbook, sheet, col->id, row->id, outCellCreated);
}

Axis* setColumnWidthByPosition(Workbook& workbook, Sheet& sheet, uint32_t pos, uint32_t width,
                               bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    Axis* column = sheet.getColumnByPosition(pos);
    if (column == nullptr) {
        const ID colId = generate_id();
        const std::string payload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(width) + "}";
        applyOperation(workbook, makeColSetOp(workbook, colId, sheet.id, payload));
        if (outCreated != nullptr) {
            *outCreated = true;
        }
        return sheet.getColumn(colId);
    }

    // Full-state payload for resurrection correctness (matches prior WASM path)
    std::string payload = "{\"pos\":" + std::to_string(column->position);
    payload += ",\"size\":" + std::to_string(width);
    if (!column->name.empty()) {
        payload += ",\"name\":\"" + internal::jsonEscape(column->name) + "\"";
    }
    if (column->hasStyle()) {
        const StyleBuffer* sty = workbook.getEntityStyle(column->id);
        if (sty != nullptr) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }
    if (column->hasFormat()) {
        const FormatBuffer* fmt = workbook.getEntityFormat(column->id);
        if (fmt != nullptr) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }
    if (column->hidden()) {
        payload += ",\"hidden\":true";
    }
    payload += "}";
    applyOperation(workbook, makeColSetOp(workbook, column->id, sheet.id, payload));
    return sheet.getColumn(column->id);
}

Axis* setRowHeightByPosition(Workbook& workbook, Sheet& sheet, uint32_t pos, uint32_t height,
                             bool* outCreated) {
    if (outCreated != nullptr) {
        *outCreated = false;
    }
    Axis* row = sheet.getRowByPosition(pos);
    if (row == nullptr) {
        const ID rowId = generate_id();
        const std::string payload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(height) + "}";
        applyOperation(workbook, makeRowSetOp(workbook, rowId, sheet.id, payload));
        if (outCreated != nullptr) {
            *outCreated = true;
        }
        return sheet.getRow(rowId);
    }

    std::string payload = "{\"pos\":" + std::to_string(row->position);
    payload += ",\"size\":" + std::to_string(height);
    if (row->hasStyle()) {
        const StyleBuffer* sty = workbook.getEntityStyle(row->id);
        if (sty != nullptr) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }
    if (row->hasFormat()) {
        const FormatBuffer* fmt = workbook.getEntityFormat(row->id);
        if (fmt != nullptr) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }
    if (row->hidden()) {
        payload += ",\"hidden\":true";
    }
    payload += "}";
    applyOperation(workbook, makeRowSetOp(workbook, row->id, sheet.id, payload));
    return sheet.getRow(row->id);
}

}  // namespace cells
