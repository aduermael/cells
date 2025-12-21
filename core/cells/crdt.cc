#include "core/cells/crdt.h"

#include <cstdio>

#include <algorithm>

namespace cells {

namespace {

// Simple JSON string escaping for payloads
std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);  // Pre-allocate with some extra space
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

// Simple JSON value extraction (reused from operation.cc pattern)
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

    return json.substr(pos, end - pos);
}

// Extract integer from JSON (for col/row positions)
int extractJSONInt(const std::string& json, const std::string& key, int defaultValue = -1) {
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

// Apply CELL_SET_VALUE operation
ApplyResult applyCellSetValue(Workbook& workbook, const Operation& op) {
    // Find the target cell across all sheets
    Cell* cell = nullptr;
    Sheet* targetSheet = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    // Check if there's a newer operation for this cell
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    if (!latest.isNull() && latest.hlc >= op.hlc) {
        // This operation is older than or equal to existing, skip it
        // (but still add to OpLog for history)
        return ApplyResult::SUPERSEDED;
    }

    // Parse payload: {"type":"n","value":"42","col_id":"abc123","row_id":"def456"}
    const std::string type_str = extractJSONString(op.payload, "type");
    const std::string value_str = extractJSONString(op.payload, "value");
    const std::string col_id_str = extractJSONString(op.payload, "col_id");
    const std::string row_id_str = extractJSONString(op.payload, "row_id");

    if (type_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (cell == nullptr) {
        // Cell doesn't exist - create it if we have col_id and row_id
        if (col_id_str.empty() || row_id_str.empty()) {
            return ApplyResult::INVALID_TARGET;
        }

        // Use the first sheet for now
        if (workbook.sheets.empty()) {
            return ApplyResult::INVALID_TARGET;
        }
        targetSheet = workbook.sheets[0].get();

        const ID colId(col_id_str);
        const ID rowId(row_id_str);

        // Verify the column and row exist (they should have been created by DIM_INSERT_AXIS)
        if (targetSheet->getColumn(colId) == nullptr || targetSheet->getRow(rowId) == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }

        // Create the cell with the SAME ID as in the operation
        auto newCell = std::make_unique<Cell>(op.target_id, colId, rowId);
        cell = newCell.get();
        targetSheet->addCell(std::move(newCell));
    }

    // Apply the value based on type
    const CellValueType type = charToValueType(type_str[0]);
    cell->value.type = type;
    cell->value.error = CellError::NONE;

    if (type == CellValueType::FORMULA) {
        // For formulas: value_str contains UUID formula, display contains A1 formula
        const std::string display_str = extractJSONString(op.payload, "display");

        // Create the formula object using UUID formula
        auto* formula = new Formula(value_str.c_str());
        cell->setFormula(formula);

        // Store display formula in raw for UI display
        cell->value.raw = display_str.empty() ? value_str : display_str;
    } else {
        // Clear formula if it was a formula cell
        if (cell->formula != nullptr) {
            cell->clearFormula();
        }
        cell->value.raw = value_str;
    }

    return ApplyResult::SUCCESS;
}

// Apply DIM_INSERT_AXIS operation
ApplyResult applyDimInsertAxis(Workbook& workbook, const Operation& op) {
    // Parse payload: {"pos":0,"size":100,"isCol":true}
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);
    const std::string isColStr = extractJSONString(op.payload, "isCol");

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const bool isColumn = (isColStr == "true" || isColStr == "1");

    // Use the first sheet for now
    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }
    Sheet* sheet = workbook.sheets[0].get();

    // Check if axis with this ID already exists
    if (isColumn) {
        if (sheet->getColumn(op.target_id) != nullptr) {
            return ApplyResult::ALREADY_APPLIED;
        }
        auto newAxis = std::make_unique<Axis>(op.target_id, true);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size = static_cast<uint32_t>(size);
        sheet->addColumn(std::move(newAxis));
    } else {
        if (sheet->getRow(op.target_id) != nullptr) {
            return ApplyResult::ALREADY_APPLIED;
        }
        auto newAxis = std::make_unique<Axis>(op.target_id, false);
        newAxis->position = static_cast<uint32_t>(pos);
        newAxis->size = static_cast<uint32_t>(size);
        sheet->addRow(std::move(newAxis));
    }

    return ApplyResult::SUCCESS;
}

// Apply CELL_CLEAR operation
ApplyResult applyCellClear(Workbook& workbook, const Operation& op) {
    Sheet* targetSheet = nullptr;

    for (auto& s : workbook.sheets) {
        if (s->getCell(op.target_id) != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (targetSheet == nullptr) {
        // Cell doesn't exist - nothing to clear
        // Still return SUCCESS so the operation is recorded in OpLog
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    if (!latest.isNull() && latest.hlc > op.hlc) {
        // A newer operation exists - if it's an edit, it resurrects
        if (latest.type == OpType::CELL_SET_VALUE) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Remove the cell from the sheet entirely
    targetSheet->cells.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

// Apply DIM_RESIZE_AXIS operation
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

    // Parse payload: {"size":150}
    std::string size_str = extractJSONString(op.payload, "size");  // NOLINT(misc-const-correctness)
    if (size_str.empty()) {
        // Try numeric format
        size_t pos = op.payload.find("\"size\":");
        if (pos != std::string::npos) {
            pos += 7;  // Skip "size":
            while (pos < op.payload.size() && (op.payload[pos] == ' ' || op.payload[pos] == '\t')) {
                pos++;
            }
            size_t end = pos;
            while (end < op.payload.size() && op.payload[end] >= '0' && op.payload[end] <= '9') {
                end++;
            }
            size_str = op.payload.substr(pos, end - pos);
        }
    }

    if (size_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const auto new_size = static_cast<uint32_t>(std::stoul(size_str));
    axis->size = new_size;

    return ApplyResult::SUCCESS;
}

// Apply SHEET_RENAME operation
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

    // Parse payload: {"name":"NewName"}
    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    sheet->name = name;

    return ApplyResult::SUCCESS;
}

// Apply DIM_MOVE_AXIS operation
// Payload: {"targetPos":5}
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

    // Parse payload: {"targetPos":5}
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

// Apply DIM_RENAME_AXIS operation
// Payload: {"name":"NewName"}
ApplyResult applyDimRenameAxis(Workbook& workbook, const Operation& op) {
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

    // Check for newer rename operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::DIM_RENAME_AXIS && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Parse payload: {"name":"NewName"}
    const std::string name = extractJSONString(op.payload, "name");
    // Note: empty name is valid (it clears the custom name)

    axis->name = name;

    return ApplyResult::SUCCESS;
}

}  // namespace

ApplyResult applyOperation(Workbook& workbook, const Operation& op) {
    OpLog* oplog = workbook.getOpLog();

    // Check for duplicate
    if (oplog->hasOperation(op.hlc)) {
        return ApplyResult::ALREADY_APPLIED;
    }

    ApplyResult result = ApplyResult::SUCCESS;

    switch (op.type) {
        case OpType::CELL_SET_VALUE:
            result = applyCellSetValue(workbook, op);
            break;

        case OpType::CELL_CLEAR:
            result = applyCellClear(workbook, op);
            break;

        case OpType::CELL_SET_STYLE:
        case OpType::DIM_DELETE_AXIS:
            // Not fully implemented yet - just accept it
            result = ApplyResult::SUCCESS;
            break;

        case OpType::DIM_INSERT_AXIS:
            result = applyDimInsertAxis(workbook, op);
            break;

        case OpType::DIM_RESIZE_AXIS:
            result = applyDimResizeAxis(workbook, op);
            break;

        case OpType::DIM_MOVE_AXIS:
            result = applyDimMoveAxis(workbook, op);
            break;

        case OpType::DIM_RENAME_AXIS:
            result = applyDimRenameAxis(workbook, op);
            break;

        case OpType::SHEET_CREATE:
        case OpType::SHEET_DELETE:
            // Not fully implemented yet - just accept it
            result = ApplyResult::SUCCESS;
            break;

        case OpType::SHEET_RENAME:
            result = applySheetRename(workbook, op);
            break;
    }

    // Add to OpLog regardless of result (for history/sync)
    // Only skip if it's truly a duplicate (already checked above)
    oplog->addOperation(op);

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

Operation makeCellSetValueOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_VALUE, cellId, payload};
}

Operation makeCellClearOp(Workbook& workbook, const ID& cellId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_CLEAR, cellId, "{}"};
}

Operation makeDimInsertAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::DIM_INSERT_AXIS, axisId, payload};
}

Operation makeDimDeleteAxisOp(Workbook& workbook, const ID& axisId) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::DIM_DELETE_AXIS, axisId, "{}"};
}

Operation makeDimResizeAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::DIM_RESIZE_AXIS, axisId, payload};
}

Operation makeDimMoveAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::DIM_MOVE_AXIS, axisId, payload};
}

Operation makeDimRenameAxisOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::DIM_RENAME_AXIS, axisId, payload};
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

size_t bootstrapOpLog(Workbook& workbook) {
    size_t count = 0;
    OpLog* oplog = workbook.getOpLog();

    // Clear any existing operations (start fresh)
    // We're bootstrapping from current state, so existing ops would be stale
    oplog->clear();

    // Iterate through all sheets
    for (const auto& sheet : workbook.sheets) {
        // Collect and sort columns by position
        std::vector<std::pair<uint32_t, Axis*>> columns;
        for (const auto& [id, axis] : sheet->columns) {
            columns.emplace_back(axis->position, axis.get());
        }
        std::sort(columns.begin(), columns.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Generate DIM_INSERT_AXIS operations for columns (in position order)
        for (const auto& [pos, axis] : columns) {
            const std::string payload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(axis->size) +
                                        ",\"isCol\":\"true\"}";
            const Operation op = makeDimInsertAxisOp(workbook, axis->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Collect and sort rows by position
        std::vector<std::pair<uint32_t, Axis*>> rows;
        for (const auto& [id, axis] : sheet->rows) {
            rows.emplace_back(axis->position, axis.get());
        }
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Generate DIM_INSERT_AXIS operations for rows (in position order)
        for (const auto& [pos, axis] : rows) {
            const std::string payload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(axis->size) +
                                        ",\"isCol\":\"false\"}";
            const Operation op = makeDimInsertAxisOp(workbook, axis->id, payload);
            oplog->addOperation(op);
            count++;
        }

        // Generate CELL_SET_VALUE operations for all cells
        for (const auto& [cellId, cell] : sheet->cells) {
            // Skip empty cells
            if (cell->value.type == CellValueType::STRING && cell->value.raw.empty() &&
                cell->formula == nullptr) {
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
                if (formula != nullptr && formula->text != nullptr) {
                    payload = "{\"type\":\"f\",\"value\":\"" +
                              jsonEscape(std::string(formula->text)) + "\",\"display\":\"" +
                              jsonEscape(cell->value.raw) + "\"" + idSuffix;
                } else {
                    continue;  // Skip cells with invalid formulas
                }
            } else {
                const char typeChar = valueTypeToChar(cell->value.type);
                payload = "{\"type\":\"" + std::string(1, typeChar) + "\",\"value\":\"" +
                          jsonEscape(cell->value.raw) + "\"" + idSuffix;
            }

            const Operation op = makeCellSetValueOp(workbook, cell->id, payload);
            oplog->addOperation(op);
            count++;
        }
    }

    return count;
}

}  // namespace cells
