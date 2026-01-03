#include "core/cells/crdt.h"

#include <cstdio>

#include <algorithm>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"

namespace cells {

namespace {

// Simple JSON string unescaping for parsing payloads
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

// Create a position resolver for a Sheet
// Returns (col, row) position for a cell ID, or (-1, -1) if not found
// NOTE: Defined early since it's used in applyCellSetValue
PositionResolver makePositionResolver(Sheet* sheet) {
    return [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
        if (sheet == nullptr) {
            return {-1, -1};
        }

        const Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            // Maybe it's a column or row ID, not a cell ID
            const Axis* col = sheet->getColumn(cellId);
            if (col != nullptr) {
                return {static_cast<int32_t>(col->position), -1};
            }
            const Axis* row = sheet->getRow(cellId);
            if (row != nullptr) {
                return {-1, static_cast<int32_t>(row->position)};
            }
            return {-1, -1};
        }

        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            return {-1, -1};
        }

        return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
    };
}

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
// Returns the unescaped string value for the given key
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

    // Unescape the extracted string to handle escaped quotes, newlines, etc.
    return jsonUnescape(json.substr(pos, end - pos));
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
        // For formulas: value_str contains UUID formula, display contains A1 formula (ignored)
        // Note: display field is ignored - we generate display strings from AST

        // Clear old formula dependencies before setting new formula
        if (targetSheet != nullptr) {
            DependencyGraph* depGraph = targetSheet->getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->removeFormula(cell->id);
            }
        }

        // Parse the UUID formula text to create the AST
        FormulaParser parser(value_str);
        std::unique_ptr<ASTNode> ast = parser.parse();

        // Create the formula object with AST
        auto* formula = new Formula();
        formula->ast = ast.release();
        formula->dirty = true;

        // Add to dependency graph for recalculation tracking if we have valid AST
        if (formula->ast != nullptr && targetSheet != nullptr) {
            DependencyGraph* depGraph = targetSheet->getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->addFormula(cell->id, formula->ast, makePositionResolver(targetSheet));

                // Track volatile functions
                if (formula->hasVolatile()) {
                    depGraph->markVolatile(cell->id);
                }
            }
        }

        cell->setFormula(formula);

        // Store result value in raw for display (not the formula text)
        cell->value.raw = "";
    } else {
        // Clear formula if it was a formula cell
        if (cell->formula != nullptr) {
            // Remove from dependency graph first
            if (targetSheet != nullptr) {
                DependencyGraph* depGraph = targetSheet->getDependencyGraph();
                if (depGraph != nullptr) {
                    depGraph->removeFormula(cell->id);
                    depGraph->unmarkVolatile(cell->id);
                }
            }
            cell->clearFormula();
        }
        cell->value.raw = value_str;
    }

    return ApplyResult::SUCCESS;
}

// Apply COL_INSERT operation
// Payload: {"pos":0,"size":100}
ApplyResult applyColInsert(Workbook& workbook, const Operation& op) {
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }
    Sheet* sheet = workbook.sheets[0].get();

    if (sheet->getColumn(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    auto newAxis = std::make_unique<Axis>(op.target_id, true);
    newAxis->position = static_cast<uint32_t>(pos);
    newAxis->size = static_cast<uint32_t>(size);
    sheet->addColumn(std::move(newAxis));

    return ApplyResult::SUCCESS;
}

// Apply ROW_INSERT operation
// Payload: {"pos":0,"size":25}
ApplyResult applyRowInsert(Workbook& workbook, const Operation& op) {
    const int pos = extractJSONInt(op.payload, "pos", -1);
    const int size = extractJSONInt(op.payload, "size", -1);

    if (pos < 0 || size < 0) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (workbook.sheets.empty()) {
        return ApplyResult::INVALID_TARGET;
    }
    Sheet* sheet = workbook.sheets[0].get();

    if (sheet->getRow(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    auto newAxis = std::make_unique<Axis>(op.target_id, false);
    newAxis->position = static_cast<uint32_t>(pos);
    newAxis->size = static_cast<uint32_t>(size);
    sheet->addRow(std::move(newAxis));

    return ApplyResult::SUCCESS;
}

// Apply DIM_INSERT_AXIS operation (legacy, backwards compatibility)
// Payload: {"pos":0,"size":100,"isCol":true}
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

// Apply CELL_SET_FORMAT operation
// Payload: {"format_id":"FMT_C002"} or {"format_id":"~"} for null/default
ApplyResult applyCellSetFormat(Workbook& workbook, const Operation& op) {
    // Find the target cell across all sheets
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

    // Check for newer format operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::CELL_SET_FORMAT && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Parse payload: {"format_id":"FMT_C002"}
    const std::string formatIdStr = extractJSONString(op.payload, "format_id");
    if (formatIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    // Set the format ID (null ID "~" means clear format / use default)
    cell->formatId = ID(formatIdStr);

    return ApplyResult::SUCCESS;
}

// Apply CELL_CLEAR operation
ApplyResult applyCellClear(Workbook& workbook, const Operation& op) {
    Sheet* targetSheet = nullptr;
    const Cell* cell = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
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

    // Remove from dependency graph if it was a formula cell
    if (cell->isFormula()) {
        DependencyGraph* depGraph = targetSheet->getDependencyGraph();
        if (depGraph != nullptr) {
            depGraph->removeFormula(op.target_id);
            depGraph->unmarkVolatile(op.target_id);
        }
    }

    // Remove the cell from the sheet entirely
    targetSheet->cells.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

// Helper to extract size from payload (handles both string and numeric formats)
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

// Apply COL_DELETE operation
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

    // Delete all cells in this column
    std::vector<ID> cellsToRemove;
    for (const auto& [cellId, cell] : targetSheet->cells) {
        if (cell->colId == op.target_id) {
            cellsToRemove.push_back(cellId);
        }
    }
    for (const auto& cellId : cellsToRemove) {
        targetSheet->cells.erase(cellId);
    }

    // Remove the column
    targetSheet->columns.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

// Apply ROW_DELETE operation
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

    // Delete all cells in this row
    std::vector<ID> cellsToRemove;
    for (const auto& [cellId, cell] : targetSheet->cells) {
        if (cell->rowId == op.target_id) {
            cellsToRemove.push_back(cellId);
        }
    }
    for (const auto& cellId : cellsToRemove) {
        targetSheet->cells.erase(cellId);
    }

    // Remove the row
    targetSheet->rows.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

// Apply COL_RESIZE operation
// Payload: {"size":150}
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

// Apply ROW_RESIZE operation
// Payload: {"size":25}
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

// Apply COL_MOVE operation
// Payload: {"targetPos":5}
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

// Apply ROW_MOVE operation
// Payload: {"targetPos":5}
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

// Apply COL_RENAME operation
// Payload: {"name":"NewName"}
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

// Apply DIM_RESIZE_AXIS operation (legacy, backwards compatibility)
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

// Apply WORKBOOK_RENAME operation
// Payload: {"name":"NewName"}
ApplyResult applyWorkbookRename(Workbook& workbook, const Operation& op) {
    // For workbook rename, target_id should be the workbook ID
    // Check if target matches workbook
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

    // Parse payload: {"name":"NewName"}
    const std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    workbook.name = name;

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

// Apply SHEET_CREATE operation
// Payload: {"name":"SheetName"}
ApplyResult applySheetCreate(Workbook& workbook, const Operation& op) {
    // Check if sheet already exists (idempotent operation)
    if (workbook.getSheet(op.target_id) != nullptr) {
        return ApplyResult::ALREADY_APPLIED;
    }

    // Parse payload: {"name":"SheetName"}
    std::string name = extractJSONString(op.payload, "name");
    if (name.empty()) {
        name = "Sheet";  // Default name if not provided
    }

    // Create and add the new sheet
    auto sheet = std::make_unique<Sheet>(op.target_id, name);
    workbook.addSheet(std::move(sheet));

    return ApplyResult::SUCCESS;
}

// Apply SHEET_DELETE operation
ApplyResult applySheetDelete(Workbook& workbook, const Operation& op) {
    // Check if sheet exists
    const Sheet* sheet = workbook.getSheet(op.target_id);
    if (sheet == nullptr) {
        // Sheet doesn't exist - already deleted or never existed
        // Return SUCCESS for idempotency
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations that resurrect the sheet
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);
    if (!latest.isNull() && latest.hlc > op.hlc) {
        // A newer operation exists - if it's a rename or create, it resurrects
        if (latest.type == OpType::SHEET_RENAME || latest.type == OpType::SHEET_CREATE) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Remove the sheet
    workbook.removeSheet(op.target_id);

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

// Apply DIM_RENAME_AXIS operation (legacy, backwards compatibility)
// Only handles columns - rows cannot be renamed.
// Payload: {"name":"NewName"}
ApplyResult applyDimRenameAxis(Workbook& workbook, const Operation& op) {
    Axis* axis = nullptr;

    // Only look for columns - rows cannot be renamed
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
            // Not fully implemented yet - just accept it
            result = ApplyResult::SUCCESS;
            break;

        case OpType::CELL_SET_FORMAT:
            result = applyCellSetFormat(workbook, op);
            break;

        // Column operations
        case OpType::COL_INSERT:
            result = applyColInsert(workbook, op);
            break;

        case OpType::COL_DELETE:
            result = applyColDelete(workbook, op);
            break;

        case OpType::COL_MOVE:
            result = applyColMove(workbook, op);
            break;

        case OpType::COL_RESIZE:
            result = applyColResize(workbook, op);
            break;

        case OpType::COL_RENAME:
            result = applyColRename(workbook, op);
            break;

        // Row operations
        case OpType::ROW_INSERT:
            result = applyRowInsert(workbook, op);
            break;

        case OpType::ROW_DELETE:
            result = applyRowDelete(workbook, op);
            break;

        case OpType::ROW_MOVE:
            result = applyRowMove(workbook, op);
            break;

        case OpType::ROW_RESIZE:
            result = applyRowResize(workbook, op);
            break;

        // Legacy DIM_* operations (backwards compatibility)
        case OpType::DIM_INSERT_AXIS:
            result = applyDimInsertAxis(workbook, op);
            break;

        case OpType::DIM_DELETE_AXIS:
            // Legacy: Not fully implemented - just accept it
            result = ApplyResult::SUCCESS;
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
            result = applySheetCreate(workbook, op);
            break;

        case OpType::SHEET_DELETE:
            result = applySheetDelete(workbook, op);
            break;

        case OpType::SHEET_RENAME:
            result = applySheetRename(workbook, op);
            break;

        case OpType::WORKBOOK_RENAME:
            result = applyWorkbookRename(workbook, op);
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

Operation makeCellSetFormatOp(Workbook& workbook, const ID& cellId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::CELL_SET_FORMAT, cellId, payload};
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

// New COL_*/ROW_* operation makers (no isCol in payload)
Operation makeColInsertOp(Workbook& workbook, const ID& axisId, const std::string& payload) {
    const HLC hlc = workbook.getCurrentHLC();
    return {hlc, OpType::COL_INSERT, axisId, payload};
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

        // Generate COL_INSERT operations for columns (in position order)
        for (const auto& [pos, axis] : columns) {
            const std::string payload =
                "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(axis->size) + "}";
            const Operation op = makeColInsertOp(workbook, axis->id, payload);
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

        // Generate ROW_INSERT operations for rows (in position order)
        for (const auto& [pos, axis] : rows) {
            const std::string payload =
                "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(axis->size) + "}";
            const Operation op = makeRowInsertOp(workbook, axis->id, payload);
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
                if (formula != nullptr && formula->ast != nullptr) {
                    // Generate UUID formula text from AST
                    const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                    // Note: display field is omitted - peers generate display from AST locally
                    payload =
                        "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
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
