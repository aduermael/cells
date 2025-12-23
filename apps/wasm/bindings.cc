// WASM bindings for cells library using Embind
// Exposes core types and operations to JavaScript

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <sys/stat.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/hlc.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"
#include "core/cells/parser.h"
#include "core/cells/quadtree.h"
#include "core/cells/ref_converter.h"
#include "core/cells/serializer.h"
#include "core/cells/sync_manager.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"
#include "core/log/include/Logger.h"

using namespace emscripten;

namespace cells::wasm {

// ============================================================================
// Change notification types for listener pattern
// ============================================================================

enum class ChangeType {
    CELL_CHANGED,       // Cell value/formula modified
    STRUCTURE_CHANGED,  // Rows/columns added, removed, resized, moved
    SHEET_CHANGED,      // Active sheet changed, sheet added/deleted/renamed/moved
    DATA_LOADED         // New file loaded or workbook created
};

// ============================================================================
// Helper functions for JSON serialization (matching server.cc API)
// ============================================================================

std::string jsonEscape(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        switch (c) {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\b':
                ss << "\\b";
                break;
            case '\f':
                ss << "\\f";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    ss << buf;
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

// Helper to extract a JSON string field from operation payload
// Payload format: {"type":"n","value":"42",...}
std::string extractPayloadField(const std::string& payload, const std::string& key) {
    const std::string searchKey = "\"" + key + "\":\"";
    size_t pos = payload.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
    pos += searchKey.length();

    size_t end = pos;
    while (end < payload.size() && payload[end] != '"') {
        if (payload[end] == '\\' && end + 1 < payload.size()) {
            end++;  // Skip escaped char
        }
        end++;
    }

    return payload.substr(pos, end - pos);
}

// ============================================================================
// CellsEngine - main wrapper class exposing the spreadsheet engine to JS
// ============================================================================

class CellsEngine {
public:
    CellsEngine() : _workbook(nullptr), _activeSheetIndex(0), _listener(val::null()) {}

    // ========================================================================
    // Listener registration for change notifications
    // ========================================================================

    // Set a JavaScript callback function that will be called on data changes
    // The callback receives a change type string: "cell", "structure", "sheet", "loaded"
    void setListener(val callback) { _listener = callback; }

    // Remove the listener
    void removeListener() { _listener = val::null(); }

    // ========================================================================
    // File loading methods
    // ========================================================================

    // Parse .zcd format from string
    std::string loadFromCells(const std::string& content) {
        auto result = cells::parse(content);
        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildQuadtree();
        notifyListeners(ChangeType::DATA_LOADED);
        return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
    }

    // Parse CSV from string
    std::string loadFromCSV(const std::string& content, char delimiter, bool hasHeader) {
        CSVReadOptions opts;
        opts.delimiter = delimiter;
        opts.hasHeader = hasHeader;
        auto result = readCSV(content, opts);
        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildQuadtree();
        notifyListeners(ChangeType::DATA_LOADED);
        return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
    }

    // Parse XLSX from binary data passed via WASM heap pointer
    // This avoids UTF-8 encoding issues that occur with string parameters
    std::string loadFromXLSXDataPtr(uintptr_t ptr, size_t size) {
        const char* data = reinterpret_cast<const char*>(ptr);
        auto result = readXLSXFromMemory(data, size);

        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildQuadtree();
        notifyListeners(ChangeType::DATA_LOADED);
        return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
    }

    // ========================================================================
    // Sheet info methods
    // ========================================================================

    std::string getSheetInfo() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);

        // Calculate actual dimensions (min 26 cols, 100 rows like Excel)
        constexpr uint32_t MIN_COLS = 26;
        constexpr uint32_t MIN_ROWS = 100;
        uint32_t maxCol = MIN_COLS;
        uint32_t maxRow = MIN_ROWS;

        for (const auto& [id, col] : sheet->columns) {
            if (col->position >= maxCol) {
                maxCol = col->position + 1;
            }
        }
        for (const auto& [id, row] : sheet->rows) {
            if (row->position >= maxRow) {
                maxRow = row->position + 1;
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"name\":\"" << jsonEscape(sheet->name) << "\",";
        json << "\"rowCount\":" << maxRow << ",";
        json << "\"colCount\":" << maxCol << ",";
        json << "\"defaultColWidth\":" << DEFAULT_COLUMN_WIDTH << ",";
        json << "\"defaultRowHeight\":" << DEFAULT_ROW_HEIGHT;
        json << "}";

        return json.str();
    }

    int getSheetCount() { return _workbook ? static_cast<int>(_workbook->sheetCount()) : 0; }

    std::string getSheetName(int index) {
        if (!_workbook || index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
            return "";
        }
        return _workbook->getSheetByIndex(index)->name;
    }

    void setActiveSheet(int index) {
        if (_workbook && index >= 0 && static_cast<size_t>(index) < _workbook->sheetCount()) {
            _activeSheetIndex = static_cast<size_t>(index);
            rebuildQuadtree();
            notifyListeners(ChangeType::SHEET_CHANGED);
        }
    }

    int getActiveSheetIndex() { return static_cast<int>(_activeSheetIndex); }

    std::string addSheet(const std::string& name) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        // Generate unique name if empty or duplicate
        std::string sheetName = name.empty() ? "Sheet" : name;
        int suffix = 1;

        auto isNameTaken = [this](const std::string& n) {
            for (size_t i = 0; i < _workbook->sheetCount(); i++) {
                if (_workbook->getSheetByIndex(i)->name == n) {
                    return true;
                }
            }
            return false;
        };

        if (name.empty() || isNameTaken(sheetName)) {
            std::string baseName = name.empty() ? "Sheet" : name;
            do {
                sheetName = baseName + std::to_string(suffix++);
            } while (isNameTaken(sheetName));
        }

        auto sheet = std::make_unique<Sheet>(generate_id(), sheetName);
        size_t newIndex = _workbook->sheetCount();
        _workbook->addSheet(std::move(sheet));
        notifyListeners(ChangeType::SHEET_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"index\":" << newIndex << ",\"name\":\"" << jsonEscape(sheetName)
             << "\"}";
        return json.str();
    }

    std::string deleteSheet(int index) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        if (index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
            return "{\"error\":\"Invalid sheet index\"}";
        }

        // Don't allow deleting the last sheet - caller should handle this
        if (_workbook->sheetCount() <= 1) {
            return "{\"error\":\"Cannot delete last sheet\"}";
        }

        // Remove the sheet
        _workbook->sheets.erase(_workbook->sheets.begin() + index);

        // Adjust active sheet index if needed
        if (_activeSheetIndex >= _workbook->sheetCount()) {
            _activeSheetIndex = _workbook->sheetCount() - 1;
        } else if (static_cast<size_t>(index) < _activeSheetIndex) {
            _activeSheetIndex--;
        } else if (static_cast<size_t>(index) == _activeSheetIndex) {
            // Stay on same index (now pointing to next sheet) or go to previous if at end
            if (_activeSheetIndex >= _workbook->sheetCount()) {
                _activeSheetIndex = _workbook->sheetCount() - 1;
            }
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::SHEET_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"activeIndex\":" << _activeSheetIndex << "}";
        return json.str();
    }

    std::string renameSheet(int index, const std::string& name) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        if (index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
            return "{\"error\":\"Invalid sheet index\"}";
        }

        if (name.empty()) {
            return "{\"error\":\"Sheet name cannot be empty\"}";
        }

        // Check for duplicate names
        for (size_t i = 0; i < _workbook->sheetCount(); i++) {
            if (static_cast<int>(i) != index && _workbook->getSheetByIndex(i)->name == name) {
                return "{\"error\":\"Sheet name already exists\"}";
            }
        }

        _workbook->getSheetByIndex(index)->name = name;
        notifyListeners(ChangeType::SHEET_CHANGED);
        return "{\"success\":true}";
    }

    std::string moveSheet(int fromIndex, int toIndex) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        size_t count = _workbook->sheetCount();
        if (fromIndex < 0 || static_cast<size_t>(fromIndex) >= count) {
            return "{\"error\":\"Invalid source index\"}";
        }
        if (toIndex < 0 || static_cast<size_t>(toIndex) > count) {
            return "{\"error\":\"Invalid target index\"}";
        }

        if (fromIndex == toIndex || fromIndex + 1 == toIndex) {
            return "{\"success\":true}";  // No-op
        }

        // Move the sheet
        auto sheet = std::move(_workbook->sheets[fromIndex]);
        _workbook->sheets.erase(_workbook->sheets.begin() + fromIndex);

        int insertAt = toIndex > fromIndex ? toIndex - 1 : toIndex;
        _workbook->sheets.insert(_workbook->sheets.begin() + insertAt, std::move(sheet));

        // Update active sheet index if it moved
        if (static_cast<size_t>(fromIndex) == _activeSheetIndex) {
            _activeSheetIndex = insertAt;
        } else if (fromIndex < toIndex) {
            if (_activeSheetIndex > static_cast<size_t>(fromIndex) &&
                _activeSheetIndex <= static_cast<size_t>(insertAt)) {
                _activeSheetIndex--;
            }
        } else {
            if (_activeSheetIndex >= static_cast<size_t>(toIndex) &&
                _activeSheetIndex < static_cast<size_t>(fromIndex)) {
                _activeSheetIndex++;
            }
        }

        notifyListeners(ChangeType::SHEET_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"activeIndex\":" << _activeSheetIndex << "}";
        return json.str();
    }

    // ========================================================================
    // Viewport query (cells in visible area)
    // ========================================================================

    // Query cells in the visible viewport area.
    // Returns pending values if available (pending > committed priority).
    // The "pending" field indicates if the value is from a pending operation.
    std::string queryViewport(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto entries = _quadtree.query(x1, y1, x2, y2);
        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);

        std::ostringstream json;
        json << "{\"cells\":[";

        bool firstCell = true;
        for (const auto& entry : entries) {
            if (!firstCell) {
                json << ",";
            }
            firstCell = false;

            json << "{";
            json << "\"id\":\"" << entry.cell->id.toString() << "\",";
            json << "\"col\":" << entry.x << ",";
            json << "\"row\":" << entry.y << ",";

            if (entry.cell->isFormula()) {
                json << "\"type\":\"f\",";
                Formula* formula = entry.cell->getFormula();
                if (formula != nullptr && formula->text != nullptr) {
                    std::string a1Formula = _refConverter.formulaToA1(formula->text);
                    json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
                }
                json << "\"display\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            } else {
                char typeChar = valueTypeToChar(entry.cell->value.type);
                json << "\"type\":\"" << typeChar << "\",";
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            }

            json << "}";
        }

        json << "],\"columns\":[";

        // Include column info for the viewport
        bool firstCol = true;
        for (const auto& [id, col] : sheet->columns) {
            if (col->position >= x1 && col->position < x2) {
                if (!firstCol) {
                    json << ",";
                }
                firstCol = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << col->position << ",";
                json << "\"width\":" << col->size << ",";
                json << "\"name\":\"" << jsonEscape(col->name) << "\"";
                json << "}";
            }
        }

        json << "],\"rows\":[";

        // Include row info for the viewport
        bool firstRow = true;
        for (const auto& [id, row] : sheet->rows) {
            if (row->position >= y1 && row->position < y2) {
                if (!firstRow) {
                    json << ",";
                }
                firstRow = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << row->position << ",";
                json << "\"height\":" << row->size << ",";
                json << "\"name\":\"" << jsonEscape(row->name) << "\"";
                json << "}";
            }
        }

        json << "]}";

        return json.str();
    }

    // ========================================================================
    // Cell operations
    // ========================================================================

    // Update a cell value.
    // Creates a CELL_SET_VALUE operation and applies it via applyOperation().
    // This ensures CRDT conflict resolution and proper sync with peers.
    // Operations are pruned when no peers are connected.
    std::string updateCell(const std::string& cellIdStr, const std::string& value) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (cellIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid cell ID\"}";
        }
        ID cellId(cellIdStr);

        Cell* cell = sheet->getCell(cellId);
        if (!cell) {
            return "{\"error\":\"Cell not found\"}";
        }

        // Determine value type
        char typeChar = 's';  // default to string
        if (!value.empty() && value[0] == '=') {
            typeChar = 'f';  // formula
        } else if (value.empty()) {
            typeChar = 's';  // empty string (not 'e' - use 's' with empty value)
        } else if (value == "TRUE" || value == "true") {
            typeChar = 'b';
        } else if (value == "FALSE" || value == "false") {
            typeChar = 'b';
        } else {
            // Try parsing as number
            char* endptr = nullptr;
            strtod(value.c_str(), &endptr);
            if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
                typeChar = 'n';
            }
        }

        // Get column and row IDs for operation payload
        std::string colIdStr = cell->colId.toString();
        std::string rowIdStr = cell->rowId.toString();

        // Build ID suffix for payload
        std::string idSuffix = ",\"col_id\":\"" + colIdStr + "\",\"row_id\":\"" + rowIdStr + "\"}";

        // Build the payload for the CRDT operation
        std::string payload;
        if (typeChar == 'f') {
            std::string uuidFormula = _refConverter.formulaToUuid(value);
            payload = "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\",\"display\":\"" + jsonEscape(value) + "\"" + idSuffix;
        } else if (typeChar == 'b') {
            payload = "{\"type\":\"b\",\"value\":\"" + std::string(value == "TRUE" || value == "true" ? "true" : "false") + "\"" + idSuffix;
        } else if (typeChar == 'n') {
            payload = "{\"type\":\"n\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
        } else {
            payload = "{\"type\":\"s\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
        }

        // Create and apply the operation via applyOperation()
        // This adds to OpLog and applies CRDT logic (conflict resolution, etc.)
        Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::CELL_CHANGED);
        return "{\"success\":true}";
    }

    // Create a new cell at the given position.
    // Always creates operations and applies them (works in both online and offline modes).
    // Operations are pruned immediately when not collaborating with peers.
    std::string createCell(uint32_t col, uint32_t row, const std::string& value) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Find or create column at position
        ID colId;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            colId = generate_id();
            std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                     ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) +
                                     ",\"isCol\":\"true\"}";
            Operation colOp = makeDimInsertAxisOp(*_workbook, colId, colPayload);
            applyOperation(*_workbook, colOp);
        }

        // Find or create row at position
        ID rowId;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            rowId = generate_id();
            std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                     ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) +
                                     ",\"isCol\":\"false\"}";
            Operation rowOp = makeDimInsertAxisOp(*_workbook, rowId, rowPayload);
            applyOperation(*_workbook, rowOp);
        }

        // Create new cell via CELL_SET_VALUE operation
        // The operation will create the cell if it doesn't exist
        ID cellId = generate_id();

        // Build ID suffix for payload (required for cell creation)
        std::string idSuffix = ",\"col_id\":\"" + colId.toString() + "\",\"row_id\":\"" + rowId.toString() + "\"}";

        // Determine type and build payload
        std::string payload;
        if (!value.empty() && value[0] == '=') {
            std::string uuidFormula = _refConverter.formulaToUuid(value);
            payload = "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\",\"display\":\"" + jsonEscape(value) + "\"" + idSuffix;
        } else if (value == "TRUE" || value == "true") {
            payload = "{\"type\":\"b\",\"value\":\"true\"" + idSuffix;
        } else if (value == "FALSE" || value == "false") {
            payload = "{\"type\":\"b\",\"value\":\"false\"" + idSuffix;
        } else if (!value.empty()) {
            char* endptr = nullptr;
            strtod(value.c_str(), &endptr);
            if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
                payload = "{\"type\":\"n\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
            } else {
                payload = "{\"type\":\"s\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
            }
        } else {
            // Empty value - create cell with empty string
            payload = "{\"type\":\"s\",\"value\":\"\"" + idSuffix;
        }

        // Create and apply the CRDT operation (this creates the cell)
        Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations (immediate if no peers)
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::CELL_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << cellId.toString() << "\"}";
        return json.str();
    }

    // Get or create a cell at the given position.
    // - Returns existing cell ID and value if cell already exists
    // - Creates column/row/cell as needed via operations
    // - For new cells, returns empty value
    // This is the primary API for editing - single call avoids multiple round trips.
    std::string getOrCreateCellAt(uint32_t col, uint32_t row) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Find or create column at position
        ID colId;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            colId = generate_id();
            std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                     ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) +
                                     ",\"isCol\":\"true\"}";
            Operation colOp = makeDimInsertAxisOp(*_workbook, colId, colPayload);
            applyOperation(*_workbook, colOp);
        }

        // Find or create row at position
        ID rowId;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            rowId = generate_id();
            std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                     ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) +
                                     ",\"isCol\":\"false\"}";
            Operation rowOp = makeDimInsertAxisOp(*_workbook, rowId, rowPayload);
            applyOperation(*_workbook, rowOp);
        }

        // Check if cell already exists at this position
        for (const auto& [id, cell] : sheet->cells) {
            if (cell->colId == colId && cell->rowId == rowId) {
                // Cell already exists, return its ID and current value
                // Prune operations before returning
                if (_syncManager) {
                    _syncManager->pruneOpLog();
                }

                std::ostringstream json;
                json << "{\"success\":true,\"id\":\"" << id.toString() << "\",\"existed\":true,";

                // Include value/formula
                if (cell->isFormula()) {
                    Formula* formula = cell->getFormula();
                    if (formula != nullptr && formula->text != nullptr) {
                        std::string a1Formula = _refConverter.formulaToA1(formula->text);
                        json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
                    }
                    json << "\"value\":\"" << jsonEscape(cell->value.raw) << "\"";
                } else {
                    json << "\"value\":\"" << jsonEscape(cell->value.raw) << "\"";
                }
                json << "}";
                return json.str();
            }
        }

        // Create new cell via CELL_SET_VALUE operation with empty value
        // This ensures the cell creation is synced to peers
        ID cellId = generate_id();

        // Build payload for cell creation (empty string value)
        std::string payload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" +
                              colId.toString() + "\",\"row_id\":\"" + rowId.toString() + "\"}";

        // Create and apply the operation (this creates the cell)
        Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations (immediate if no peers)
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::CELL_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << cellId.toString() << "\",\"existed\":false,\"value\":\"\"}";
        return json.str();
    }

    std::string deleteCell(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (cellIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid cell ID\"}";
        }
        ID cellId(cellIdStr);

        auto it = sheet->cells.find(cellId);
        if (it == sheet->cells.end()) {
            return "{\"error\":\"Cell not found\"}";
        }

        // Create and apply CELL_CLEAR operation via CRDT system
        Operation op = makeCellClearOp(*_workbook, cellId);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::CELL_CHANGED);

        return "{\"success\":true}";
    }

    // Delete a cell at the given position if it exists.
    // - Does nothing if no cell exists at that position (returns deleted: false)
    // - Returns deleted: true if a cell was actually deleted
    // This simplifies deletion logic - no need to first check if cell exists.
    std::string deleteCellAt(uint32_t col, uint32_t row) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Find column at position
        ID colId;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            // No column at this position means no cell
            return "{\"success\":true,\"deleted\":false}";
        }

        // Find row at position
        ID rowId;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            // No row at this position means no cell
            return "{\"success\":true,\"deleted\":false}";
        }

        // Find cell at this position
        for (const auto& [id, cell] : sheet->cells) {
            if (cell->colId == colId && cell->rowId == rowId) {
                // Create and apply CELL_CLEAR operation via CRDT system
                Operation op = makeCellClearOp(*_workbook, id);
                applyOperation(*_workbook, op);

                // Queue broadcast to sync with peers (if any) and prune old operations
                if (_syncManager) {
                    _syncManager->queueOperationsBroadcast();
                    _syncManager->pruneOpLog();
                }

                rebuildQuadtree();
                notifyListeners(ChangeType::CELL_CHANGED);
                return "{\"success\":true,\"deleted\":true}";
            }
        }

        // No cell at this position
        return "{\"success\":true,\"deleted\":false}";
    }

    // ========================================================================
    // Column/row resize operations
    // Always creates operations and applies them. Operations are pruned when no peers.
    // ========================================================================

    // Resize a column by ID.
    std::string resizeColumn(const std::string& colIdStr, uint32_t width) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (colIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid column ID\"}";
        }
        ID colId(colIdStr);

        auto it = sheet->columns.find(colId);
        if (it == sheet->columns.end()) {
            return "{\"error\":\"Column not found\"}";
        }

        // Clamp width
        if (width < 20) width = 20;
        if (width > 1000) width = 1000;

        std::string payload = "{\"size\":" + std::to_string(width) + "}";
        Operation op = makeDimResizeAxisOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);
        return "{\"success\":true}";
    }

    // Resize a column by position.
    std::string resizeColumnByPos(uint32_t pos, uint32_t width) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Clamp width
        if (width < 20) width = 20;
        if (width > 1000) width = 1000;

        // Find column at position, or create it
        Axis* column = nullptr;
        ID colId;
        for (auto& [id, col] : sheet->columns) {
            if (col->position == pos) {
                column = col.get();
                colId = id;
                break;
            }
        }

        if (!column) {
            colId = generate_id();
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(width) +
                                        ",\"isCol\":\"true\"}";
            Operation insertOp = makeDimInsertAxisOp(*_workbook, colId, insertPayload);
            applyOperation(*_workbook, insertOp);
            column = sheet->getColumn(colId);
        } else {
            std::string resizePayload = "{\"size\":" + std::to_string(width) + "}";
            Operation resizeOp = makeDimResizeAxisOp(*_workbook, colId, resizePayload);
            applyOperation(*_workbook, resizeOp);
        }

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << colId.toString() << "\"}";
        return json.str();
    }

    // Resize a row by ID.
    std::string resizeRow(const std::string& rowIdStr, uint32_t height) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (rowIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid row ID\"}";
        }
        ID rowId(rowIdStr);

        auto it = sheet->rows.find(rowId);
        if (it == sheet->rows.end()) {
            return "{\"error\":\"Row not found\"}";
        }

        // Clamp height
        if (height < 10) height = 10;
        if (height > 500) height = 500;

        std::string payload = "{\"size\":" + std::to_string(height) + "}";
        Operation op = makeDimResizeAxisOp(*_workbook, rowId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);
        return "{\"success\":true}";
    }

    // Resize a row by position.
    std::string resizeRowByPos(uint32_t pos, uint32_t height) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Clamp height
        if (height < 10) height = 10;
        if (height > 500) height = 500;

        // Find row at position, or create it
        Axis* row = nullptr;
        ID rowId;
        for (auto& [id, r] : sheet->rows) {
            if (r->position == pos) {
                row = r.get();
                rowId = id;
                break;
            }
        }

        if (!row) {
            rowId = generate_id();
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(height) +
                                        ",\"isCol\":\"false\"}";
            Operation insertOp = makeDimInsertAxisOp(*_workbook, rowId, insertPayload);
            applyOperation(*_workbook, insertOp);
            row = sheet->getRow(rowId);
        } else {
            std::string resizePayload = "{\"size\":" + std::to_string(height) + "}";
            Operation resizeOp = makeDimResizeAxisOp(*_workbook, rowId, resizePayload);
            applyOperation(*_workbook, resizeOp);
        }

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << rowId.toString() << "\"}";
        return json.str();
    }

    // ========================================================================
    // Column/row rename operations
    // ========================================================================

    std::string renameColumn(const std::string& colIdStr, const std::string& name) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (colIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid column ID\"}";
        }
        ID colId(colIdStr);

        auto it = sheet->columns.find(colId);
        if (it == sheet->columns.end()) {
            return "{\"error\":\"Column not found\"}";
        }

        std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation op = makeDimRenameAxisOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);
        return "{\"success\":true}";
    }

    std::string renameColumnByPos(uint32_t pos, const std::string& name) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Find column at position, or create it
        Axis* column = nullptr;
        ID colId;
        for (auto& [id, col] : sheet->columns) {
            if (col->position == pos) {
                column = col.get();
                colId = id;
                break;
            }
        }

        if (!column) {
            colId = generate_id();
            // Create DIM_INSERT_AXIS operation for new column
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                       ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) +
                                       ",\"isCol\":\"true\"}";
            Operation insertOp = makeDimInsertAxisOp(*_workbook, colId, insertPayload);
            applyOperation(*_workbook, insertOp);

            // Now rename it
            std::string renamePayload = "{\"name\":\"" + jsonEscape(name) + "\"}";
            Operation renameOp = makeDimRenameAxisOp(*_workbook, colId, renamePayload);
            applyOperation(*_workbook, renameOp);

            column = sheet->getColumn(colId);
        } else {
            std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
            Operation op = makeDimRenameAxisOp(*_workbook, colId, payload);
            applyOperation(*_workbook, op);
        }

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << (column ? column->id.toString() : colId.toString()) << "\"}";
        return json.str();
    }

    // ========================================================================
    // Column/row move operations
    // ========================================================================

    // Shift column positions to make room for an "empty" column move
    // When moving empty position S to target T:
    // - If S > T (moving left): columns at [T, S) shift right (+1)
    // - If S < T (moving right): columns at (S, T] shift left (-1)
    std::string shiftColumnsForEmptyMove(uint32_t sourcePos, uint32_t targetPos) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (sourcePos == targetPos || sourcePos + 1 == targetPos) {
            return "{\"success\":true}";  // No-op
        }

        if (sourcePos > targetPos) {
            // Moving left: columns at [targetPos, sourcePos) shift right
            for (auto& [id, col] : sheet->columns) {
                if (col->position >= targetPos && col->position < sourcePos) {
                    col->position++;
                }
            }
        } else {
            // Moving right: columns at (sourcePos, targetPos] shift left
            // Note: targetPos is "insert before", so actual new pos is targetPos-1
            for (auto& [id, col] : sheet->columns) {
                if (col->position > sourcePos && col->position < targetPos) {
                    col->position--;
                }
            }
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::STRUCTURE_CHANGED);
        return "{\"success\":true}";
    }

    std::string shiftRowsForEmptyMove(uint32_t sourcePos, uint32_t targetPos) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (sourcePos == targetPos || sourcePos + 1 == targetPos) {
            return "{\"success\":true}";  // No-op
        }

        if (sourcePos > targetPos) {
            // Moving left: rows at [targetPos, sourcePos) shift down
            for (auto& [id, row] : sheet->rows) {
                if (row->position >= targetPos && row->position < sourcePos) {
                    row->position++;
                }
            }
        } else {
            // Moving right: rows at (sourcePos, targetPos] shift up
            for (auto& [id, row] : sheet->rows) {
                if (row->position > sourcePos && row->position < targetPos) {
                    row->position--;
                }
            }
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::STRUCTURE_CHANGED);
        return "{\"success\":true}";
    }

    std::string moveColumn(const std::string& colIdStr, uint32_t targetPos) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (colIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid column ID\"}";
        }
        ID colId(colIdStr);

        auto it = sheet->columns.find(colId);
        if (it == sheet->columns.end()) {
            return "{\"error\":\"Column not found\"}";
        }

        uint32_t currentPos = it->second->position;

        if (targetPos == currentPos || targetPos == currentPos + 1) {
            return "{\"success\":true}";
        }

        std::string payload = "{\"targetPos\":" + std::to_string(targetPos) + "}";
        Operation op = makeDimMoveAxisOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true}";
    }

    std::string moveRow(const std::string& rowIdStr, uint32_t targetPos) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        if (rowIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid row ID\"}";
        }
        ID rowId(rowIdStr);

        auto it = sheet->rows.find(rowId);
        if (it == sheet->rows.end()) {
            return "{\"error\":\"Row not found\"}";
        }

        uint32_t currentPos = it->second->position;

        if (targetPos == currentPos || targetPos == currentPos + 1) {
            return "{\"success\":true}";
        }

        std::string payload = "{\"targetPos\":" + std::to_string(targetPos) + "}";
        Operation op = makeDimMoveAxisOp(*_workbook, rowId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        rebuildQuadtree();
        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true}";
    }

    // ========================================================================
    // Export methods
    // ========================================================================

    std::string exportToCells() {
        if (!_workbook) {
            return "";
        }
        return serialize(*_workbook);
    }

    std::string exportToCSV() {
        if (!_workbook) {
            return "";
        }
        auto result = writeCSV(*_workbook);
        if (!result.ok()) {
            return "";
        }
        return result.output;
    }

    // Export to XLSX returns binary data as string
    // JS side should treat this as binary
    std::string exportToXLSX() {
        if (!_workbook) {
            return "";
        }

        // Ensure /tmp directory exists in Emscripten's virtual filesystem
        mkdir("/tmp", 0777);

        const char* tempPath = "/tmp/export.xlsx";
        auto result = writeXLSX(*_workbook, tempPath);
        if (!result.ok()) {
            return "";
        }

        // Read back the file
        FILE* f = fopen(tempPath, "rb");
        if (!f) {
            return "";
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::string data(size, '\0');
        fread(&data[0], 1, size, f);
        fclose(f);

        remove(tempPath);

        return data;
    }

    // ========================================================================
    // Workbook name
    // ========================================================================

    std::string getWorkbookName() { return _workbook ? _workbook->name : ""; }

    void setWorkbookName(const std::string& name) {
        if (_workbook) {
            _workbook->name = name;
        }
    }

    // ========================================================================
    // Create empty workbook
    // ========================================================================

    void createEmptyWorkbook() {
        _workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        _workbook->addSheet(std::move(sheet));
        _activeSheetIndex = 0;
        rebuildQuadtree();
        notifyListeners(ChangeType::DATA_LOADED);
        LOG_INFO("Created empty workbook with id=%s", _workbook->id.toString().c_str());
    }

    // ========================================================================
    // CRDT collaboration methods
    // ========================================================================

    // Set the local node ID for HLC generation.
    // Should be called once when initializing collaboration.
    std::string setNodeId(const std::string& nodeIdStr) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        if (nodeIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid node ID length\"}";
        }

        ID nodeId(nodeIdStr);
        _workbook->setNodeId(nodeId);

        return "{\"success\":true}";
    }

    // Get the local node ID.
    std::string getNodeId() {
        if (!_workbook) {
            return "";
        }

        const ID& nodeId = _workbook->getNodeId();
        if (nodeId.isNull()) {
            return "";
        }

        return nodeId.toString();
    }

    // Get the current (highest) HLC timestamp as a string.
    std::string getCurrentHLC() {
        if (!_workbook) {
            return "";
        }

        HLC hlc = _workbook->getCurrentHLC();
        return hlc.toString();
    }

    // Get all operations since a given HLC timestamp (exclusive).
    // Returns JSON array of operations in HLC order.
    // If sinceHLC is empty, returns all operations.
    std::string getOperationsSince(const std::string& sinceHLCStr) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        OpLog* oplog = _workbook->getOpLog();
        if (!oplog) {
            return "{\"error\":\"No oplog\"}";
        }

        HLC sinceHLC;
        if (!sinceHLCStr.empty()) {
            sinceHLC = HLC::fromString(sinceHLCStr);
        }

        std::vector<Operation> ops = oplog->getOperationsSince(sinceHLC);

        std::ostringstream json;
        json << "{\"operations\":[";

        bool first = true;
        for (const auto& op : ops) {
            if (!first) {
                json << ",";
            }
            first = false;
            json << op.toJSON();
        }

        json << "]}";
        return json.str();
    }

    // Apply a remote operation from JSON format.
    // Returns result indicating success/failure and reason.
    std::string applyRemoteOperation(const std::string& opJson) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\",\"result\":\"error\"}";
        }

        Operation op = Operation::fromJSON(opJson);
        if (op.isNull()) {
            return "{\"error\":\"Invalid operation JSON\",\"result\":\"error\"}";
        }

        ApplyResult result = applyOperation(*_workbook, op);

        std::string resultStr;
        switch (result) {
            case ApplyResult::SUCCESS:
                resultStr = "success";
                // Rebuild quadtree and notify listeners for successful operations
                rebuildQuadtree();
                notifyListeners(ChangeType::CELL_CHANGED);
                break;
            case ApplyResult::ALREADY_APPLIED:
                resultStr = "already_applied";
                break;
            case ApplyResult::SUPERSEDED:
                resultStr = "superseded";
                break;
            case ApplyResult::INVALID_TARGET:
                resultStr = "invalid_target";
                break;
            case ApplyResult::INVALID_PAYLOAD:
                resultStr = "invalid_payload";
                break;
            case ApplyResult::RESURRECTED:
                resultStr = "resurrected";
                rebuildQuadtree();
                notifyListeners(ChangeType::CELL_CHANGED);
                break;
        }

        std::ostringstream json;
        json << "{\"result\":\"" << resultStr << "\"}";
        return json.str();
    }

    // Apply multiple remote operations from JSON array.
    // Returns count of successfully applied operations.
    std::string applyRemoteOperations(const std::string& opsJson) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        // Parse JSON array of operations
        // Expected format: {"operations":[{op1},{op2},...]}
        // For simplicity, we'll parse it manually

        std::vector<Operation> ops;

        // Find the operations array
        size_t arrStart = opsJson.find("\"operations\":[");
        if (arrStart == std::string::npos) {
            return "{\"error\":\"Invalid format, expected operations array\"}";
        }
        arrStart += 14;  // Skip past "operations":[

        size_t arrEnd = opsJson.rfind(']');
        if (arrEnd == std::string::npos || arrEnd <= arrStart) {
            return "{\"error\":\"Invalid format, malformed operations array\"}";
        }

        std::string arrContent = opsJson.substr(arrStart, arrEnd - arrStart);

        // Parse individual operations
        // Each operation is a JSON object {...}
        size_t pos = 0;
        while (pos < arrContent.size()) {
            // Skip whitespace and commas
            while (pos < arrContent.size() &&
                   (arrContent[pos] == ' ' || arrContent[pos] == ',' || arrContent[pos] == '\n' ||
                    arrContent[pos] == '\r' || arrContent[pos] == '\t')) {
                pos++;
            }
            if (pos >= arrContent.size()) {
                break;
            }

            // Find the start of an object
            if (arrContent[pos] != '{') {
                break;
            }

            // Find matching closing brace (handle nested objects)
            size_t objStart = pos;
            int braceCount = 1;
            pos++;
            while (pos < arrContent.size() && braceCount > 0) {
                if (arrContent[pos] == '{') {
                    braceCount++;
                } else if (arrContent[pos] == '}') {
                    braceCount--;
                } else if (arrContent[pos] == '"') {
                    // Skip string content
                    pos++;
                    while (pos < arrContent.size() && arrContent[pos] != '"') {
                        if (arrContent[pos] == '\\' && pos + 1 < arrContent.size()) {
                            pos++;
                        }
                        pos++;
                    }
                }
                pos++;
            }

            if (braceCount == 0) {
                std::string opJson = arrContent.substr(objStart, pos - objStart);
                Operation op = Operation::fromJSON(opJson);
                if (!op.isNull()) {
                    ops.push_back(op);
                }
            }
        }

        size_t applied = applyOperations(*_workbook, ops);

        if (applied > 0) {
            rebuildQuadtree();
            notifyListeners(ChangeType::CELL_CHANGED);
        }

        std::ostringstream json;
        json << "{\"applied\":" << applied << ",\"total\":" << ops.size() << "}";
        return json.str();
    }

    // Get the number of operations in the OpLog.
    int getOpLogSize() {
        if (!_workbook) {
            return 0;
        }

        OpLog* oplog = _workbook->getOpLog();
        if (!oplog) {
            return 0;
        }

        return static_cast<int>(oplog->size());
    }

    // Check if an operation with the given HLC already exists in the OpLog.
    bool hasOperation(const std::string& hlcStr) {
        if (!_workbook) {
            return false;
        }

        OpLog* oplog = _workbook->getOpLog();
        if (!oplog) {
            return false;
        }

        HLC hlc = HLC::fromString(hlcStr);
        return oplog->hasOperation(hlc);
    }

    // ========================================================================
    // SyncManager methods (Phase 1e)
    // ========================================================================

    // Initialize the sync manager. Must be called before using sync features.
    // Should be called after setNodeId.
    std::string initSyncManager() {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        _syncManager = std::make_unique<SyncManager>(_workbook.get());
        return "{\"success\":true}";
    }

    // Add a peer to track. Queues a hello message for the peer.
    std::string addPeer(const std::string& peerIdStr) {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        if (peerIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid peer ID length\"}";
        }

        ID peerId(peerIdStr);
        _syncManager->addPeer(peerId);
        return "{\"success\":true}";
    }

    // Remove a peer from tracking.
    std::string removePeer(const std::string& peerIdStr) {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        if (peerIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid peer ID length\"}";
        }

        ID peerId(peerIdStr);
        _syncManager->removePeer(peerId);
        return "{\"success\":true}";
    }

    // Get list of connected peer IDs as JSON array.
    std::string getPeerIds() {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        std::vector<ID> peers = _syncManager->getPeerIds();

        std::ostringstream json;
        json << "{\"peers\":[";
        for (size_t i = 0; i < peers.size(); i++) {
            if (i > 0) {
                json << ",";
            }
            json << "\"" << peers[i].toString() << "\"";
        }
        json << "]}";
        return json.str();
    }

    // Get the number of connected peers.
    int getPeerCount() {
        if (!_syncManager) {
            return 0;
        }
        return static_cast<int>(_syncManager->peerCount());
    }

    // Handle an incoming message from a peer.
    // Returns JSON with outgoing messages to send.
    // Format: {"messages":[{"peerId":"...","json":"..."}, ...]}
    // If peerId is empty/null, it's a broadcast message.
    std::string handlePeerMessage(const std::string& peerIdStr, const std::string& messageJson) {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        if (peerIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid peer ID length\"}";
        }

        ID peerId(peerIdStr);
        HandleMessageResult result = _syncManager->handleMessage(peerId, messageJson);

        // Only rebuild/notify when data actually changed
        if (result.dataModified) {
            rebuildQuadtree();
            notifyListeners(ChangeType::CELL_CHANGED);
        }

        std::ostringstream json;
        json << "{\"messages\":[";
        for (size_t i = 0; i < result.messages.size(); i++) {
            if (i > 0) {
                json << ",";
            }
            json << "{";
            if (result.messages[i].isBroadcast()) {
                json << "\"peerId\":null,";
            } else {
                json << "\"peerId\":\"" << result.messages[i].peerId.toString() << "\",";
            }
            // Escape the JSON message
            json << "\"json\":" << "\"" << jsonEscape(result.messages[i].json) << "\"";
            json << "}";
        }
        json << "]}";
        return json.str();
    }

    // Get and clear all pending outgoing messages.
    // Format: {"messages":[{"peerId":"...","json":"..."}, ...]}
    std::string getOutgoingMessages() {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        std::vector<OutgoingMessage> messages = _syncManager->getOutgoingMessages();

        std::ostringstream json;
        json << "{\"messages\":[";
        for (size_t i = 0; i < messages.size(); i++) {
            if (i > 0) {
                json << ",";
            }
            json << "{";
            if (messages[i].isBroadcast()) {
                json << "\"peerId\":null,";
            } else {
                json << "\"peerId\":\"" << messages[i].peerId.toString() << "\",";
            }
            json << "\"json\":" << "\"" << jsonEscape(messages[i].json) << "\"";
            json << "}";
        }
        json << "]}";
        return json.str();
    }

    // Queue a broadcast of new operations to all peers.
    // Call this after local edits to push changes.
    std::string queueOperationsBroadcast() {
        if (!_syncManager) {
            return "{\"error\":\"SyncManager not initialized\"}";
        }

        _syncManager->queueOperationsBroadcast();
        return "{\"success\":true}";
    }

    // ========================================================================
    // Collaboration mode methods
    // ========================================================================

    // Get current collaboration mode as string: "offline" or "collaborating"
    std::string getCollabMode() {
        if (!_workbook) {
            return "offline";
        }
        return _workbook->isCollaborating() ? "collaborating" : "offline";
    }

    // Check if currently in collaboration mode
    bool isCollaborating() {
        if (!_workbook) {
            return false;
        }
        return _workbook->isCollaborating();
    }

    // Start collaboration mode - call when user clicks "Share" or joins a room.
    // This switches to COLLABORATING mode and bootstraps OpLog with current state.
    std::string startCollaboration() {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        // Check if already collaborating
        if (_workbook->isCollaborating()) {
            return "{\"success\":true,\"mode\":\"collaborating\",\"bootstrapped\":0}";
        }

        // Switch to collaboration mode
        _workbook->startCollaboration();

        // Bootstrap OpLog with current workbook state
        // This generates operations for all existing axes and cells
        size_t opCount = bootstrapOpLog(*_workbook);

        std::ostringstream json;
        json << "{\"success\":true,\"mode\":\"collaborating\",\"bootstrapped\":" << opCount << "}";
        return json.str();
    }

    // Set collaboration mode explicitly (for testing or edge cases)
    std::string setCollabMode(const std::string& mode) {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        if (mode == "offline") {
            _workbook->setCollabMode(cells::CollabMode::OFFLINE);
        } else if (mode == "collaborating") {
            _workbook->setCollabMode(cells::CollabMode::COLLABORATING);
        } else {
            return "{\"error\":\"Invalid mode. Use 'offline' or 'collaborating'\"}";
        }

        return "{\"success\":true,\"mode\":\"" + mode + "\"}";
    }

private:
    void rebuildQuadtree() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return;
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return;
        }

        _quadtree.clear();
        _quadtree.build(*sheet);
        _refConverter.setContext(*sheet);
    }

    // Notify the registered listener of a data change
    // Called after rebuildQuadtree() completes
    void notifyListeners(ChangeType type) {
        if (_listener.isNull() || _listener.isUndefined()) {
            return;
        }

        // Convert ChangeType to string for JavaScript
        const char* typeStr = nullptr;
        switch (type) {
            case ChangeType::CELL_CHANGED:
                typeStr = "cell";
                break;
            case ChangeType::STRUCTURE_CHANGED:
                typeStr = "structure";
                break;
            case ChangeType::SHEET_CHANGED:
                typeStr = "sheet";
                break;
            case ChangeType::DATA_LOADED:
                typeStr = "loaded";
                break;
        }

        // Call the JavaScript callback with the change type
        _listener(std::string(typeStr));
    }

    std::unique_ptr<Workbook> _workbook;
    size_t _activeSheetIndex;
    Quadtree _quadtree;
    RefConverter _refConverter;
    val _listener;  // JavaScript callback for change notifications
    std::unique_ptr<SyncManager> _syncManager;  // CRDT sync manager
};

}  // namespace cells::wasm

// ============================================================================
// Embind bindings
// ============================================================================

EMSCRIPTEN_BINDINGS(cells) {
    class_<cells::wasm::CellsEngine>("CellsEngine")
        .constructor<>()
        // Listener registration
        .function("setListener", &cells::wasm::CellsEngine::setListener)
        .function("removeListener", &cells::wasm::CellsEngine::removeListener)
        // File loading
        .function("loadFromCells", &cells::wasm::CellsEngine::loadFromCells)
        .function("loadFromCSV", &cells::wasm::CellsEngine::loadFromCSV)
        .function("loadFromXLSXDataPtr", &cells::wasm::CellsEngine::loadFromXLSXDataPtr)
        // Sheet info
        .function("getSheetInfo", &cells::wasm::CellsEngine::getSheetInfo)
        .function("getSheetCount", &cells::wasm::CellsEngine::getSheetCount)
        .function("getSheetName", &cells::wasm::CellsEngine::getSheetName)
        .function("getActiveSheetIndex", &cells::wasm::CellsEngine::getActiveSheetIndex)
        .function("setActiveSheet", &cells::wasm::CellsEngine::setActiveSheet)
        .function("addSheet", &cells::wasm::CellsEngine::addSheet)
        .function("deleteSheet", &cells::wasm::CellsEngine::deleteSheet)
        .function("renameSheet", &cells::wasm::CellsEngine::renameSheet)
        .function("moveSheet", &cells::wasm::CellsEngine::moveSheet)
        // Viewport
        .function("queryViewport", &cells::wasm::CellsEngine::queryViewport)
        // Cell operations
        .function("updateCell", &cells::wasm::CellsEngine::updateCell)
        .function("createCell", &cells::wasm::CellsEngine::createCell)
        .function("getOrCreateCellAt", &cells::wasm::CellsEngine::getOrCreateCellAt)
        .function("deleteCell", &cells::wasm::CellsEngine::deleteCell)
        .function("deleteCellAt", &cells::wasm::CellsEngine::deleteCellAt)
        // Column/row resize
        .function("resizeColumn", &cells::wasm::CellsEngine::resizeColumn)
        .function("resizeColumnByPos", &cells::wasm::CellsEngine::resizeColumnByPos)
        .function("resizeRow", &cells::wasm::CellsEngine::resizeRow)
        .function("resizeRowByPos", &cells::wasm::CellsEngine::resizeRowByPos)
        // Column/row rename
        .function("renameColumn", &cells::wasm::CellsEngine::renameColumn)
        .function("renameColumnByPos", &cells::wasm::CellsEngine::renameColumnByPos)
        // Column/row move
        .function("moveColumn", &cells::wasm::CellsEngine::moveColumn)
        .function("moveRow", &cells::wasm::CellsEngine::moveRow)
        .function("shiftColumnsForEmptyMove", &cells::wasm::CellsEngine::shiftColumnsForEmptyMove)
        .function("shiftRowsForEmptyMove", &cells::wasm::CellsEngine::shiftRowsForEmptyMove)
        // Export
        .function("exportToCells", &cells::wasm::CellsEngine::exportToCells)
        .function("exportToCSV", &cells::wasm::CellsEngine::exportToCSV)
        .function("exportToXLSX", &cells::wasm::CellsEngine::exportToXLSX)
        // Workbook management
        .function("getWorkbookName", &cells::wasm::CellsEngine::getWorkbookName)
        .function("setWorkbookName", &cells::wasm::CellsEngine::setWorkbookName)
        .function("createEmptyWorkbook", &cells::wasm::CellsEngine::createEmptyWorkbook)
        // CRDT collaboration
        .function("setNodeId", &cells::wasm::CellsEngine::setNodeId)
        .function("getNodeId", &cells::wasm::CellsEngine::getNodeId)
        .function("getCurrentHLC", &cells::wasm::CellsEngine::getCurrentHLC)
        .function("getOperationsSince", &cells::wasm::CellsEngine::getOperationsSince)
        .function("applyRemoteOperation", &cells::wasm::CellsEngine::applyRemoteOperation)
        .function("applyRemoteOperations", &cells::wasm::CellsEngine::applyRemoteOperations)
        .function("getOpLogSize", &cells::wasm::CellsEngine::getOpLogSize)
        .function("hasOperation", &cells::wasm::CellsEngine::hasOperation)
        // SyncManager methods
        .function("initSyncManager", &cells::wasm::CellsEngine::initSyncManager)
        .function("addPeer", &cells::wasm::CellsEngine::addPeer)
        .function("removePeer", &cells::wasm::CellsEngine::removePeer)
        .function("getPeerIds", &cells::wasm::CellsEngine::getPeerIds)
        .function("getPeerCount", &cells::wasm::CellsEngine::getPeerCount)
        .function("handlePeerMessage", &cells::wasm::CellsEngine::handlePeerMessage)
        .function("getOutgoingMessages", &cells::wasm::CellsEngine::getOutgoingMessages)
        .function("queueOperationsBroadcast", &cells::wasm::CellsEngine::queueOperationsBroadcast)
        // Collaboration mode
        .function("getCollabMode", &cells::wasm::CellsEngine::getCollabMode)
        .function("isCollaborating", &cells::wasm::CellsEngine::isCollaborating)
        .function("startCollaboration", &cells::wasm::CellsEngine::startCollaboration)
        .function("setCollabMode", &cells::wasm::CellsEngine::setCollabMode);

    // Logger bindings - control logging from JavaScript
    enum_<cells::log::Level>("LogLevel")
        .value("DEBUG", cells::log::Level::DEBUG)
        .value("INFO", cells::log::Level::INFO)
        .value("WARN", cells::log::Level::WARN)
        .value("ERROR", cells::log::Level::ERROR);

    // Free functions for logging
    function("logDebug", +[](const std::string& msg) {
        cells::log::Logger::instance().debug("%s", msg.c_str());
    });
    function("logInfo", +[](const std::string& msg) {
        cells::log::Logger::instance().info("%s", msg.c_str());
    });
    function("logWarn", +[](const std::string& msg) {
        cells::log::Logger::instance().warn("%s", msg.c_str());
    });
    function("logError", +[](const std::string& msg) {
        cells::log::Logger::instance().error("%s", msg.c_str());
    });

    // Logger configuration
    function("setLogEnabled", +[](bool enabled) {
        cells::log::Logger::instance().setEnabled(enabled);
    });
    function("isLogEnabled", +[]() {
        return cells::log::Logger::instance().isEnabled();
    });
    function("setLogLevel", +[](cells::log::Level level) {
        cells::log::Logger::instance().setMinLevel(level);
    });
    function("getLogLevel", +[]() {
        return cells::log::Logger::instance().getMinLevel();
    });
}
