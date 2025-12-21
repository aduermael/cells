#include "core/cells/crdt.h"

#include <algorithm>

namespace cells {

namespace {

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

        ID colId(col_id_str);
        ID rowId(row_id_str);

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
    cell->value.raw = value_str;
    cell->value.error = CellError::NONE;

    // Clear formula if it was a formula cell
    if (cell->formula != nullptr) {
        cell->clearFormula();
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

    bool isColumn = (isColStr == "true" || isColStr == "1");

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
    Cell* cell = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            break;
        }
    }

    if (cell == nullptr) {
        return ApplyResult::INVALID_TARGET;
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

    // Clear the cell
    cell->value = CellValue();
    cell->clearFormula();

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
        case OpType::DIM_MOVE_AXIS:
            // Not fully implemented yet - just accept it
            result = ApplyResult::SUCCESS;
            break;

        case OpType::DIM_INSERT_AXIS:
            result = applyDimInsertAxis(workbook, op);
            break;

        case OpType::DIM_RESIZE_AXIS:
            result = applyDimResizeAxis(workbook, op);
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

}  // namespace cells
