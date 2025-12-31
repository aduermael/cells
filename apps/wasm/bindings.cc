// WASM bindings for cells library using Embind
// Exposes core types and operations to JavaScript

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <sys/stat.h>

#include <cstdint>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/hlc.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"
#include "core/cells/parser.h"
#include "core/cells/viewport_index.h"
#include "core/cells/ref_converter.h"
#include "core/cells/serializer.h"
#include "core/cells/sync_manager.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"
#include "core/cells/luau_sandbox.h"
#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/Lexer.h"
#include "core/log/include/Logger.h"
#include "core/net/include/SyncClient.h"

using namespace emscripten;

namespace cells::wasm {

// ============================================================================
// Change notification types for listener pattern
// ============================================================================

enum class ChangeType {
    CELL_CHANGED,       // Cell value/formula modified
    STRUCTURE_CHANGED,  // Rows/columns added, removed, resized, moved
    SHEET_CHANGED,      // Active sheet changed, sheet added/deleted/renamed/moved
    DATA_LOADED,        // New file loaded or workbook created
    LOAD_PROGRESS,      // File loading progress update (includes cell count)
    SYNC_STATE_CHANGED, // Sync connection state changed
    PEER_JOINED,        // A peer joined the sync session
    PEER_LEFT,          // A peer left the sync session
    PRESENCE_CHANGED    // Remote peer presence (cursor/selection) changed
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

class CellsEngine : public cells::net::SyncClientDelegate {
public:
    CellsEngine() : _workbook(nullptr), _activeSheetIndex(0), _listener(val::null()) {}
    ~CellsEngine() override { disableSync(); }

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
        rebuildViewportIndex();

        // Parse and evaluate all formulas in all sheets after loading
        // Step 1: Parse formula text into ASTs (formulas are stored as UUID-format text)
        // Step 2: Add formulas to dependency graph for reactive updates
        // Step 3: Recalculate to compute the display values
        for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
            auto* sheet = _workbook->getSheetByIndex(i);
            if (sheet) {
                std::vector<ID> formulaCells;
                DependencyGraph* depGraph = sheet->getDependencyGraph();

                // Create position resolver for this sheet
                auto positionResolver = [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
                    if (sheet == nullptr) return {-1, -1};
                    const Cell* c = sheet->getCell(cellId);
                    if (c == nullptr) {
                        // Maybe it's a column or row ID
                        const Axis* col = sheet->getColumn(cellId);
                        if (col != nullptr) return {static_cast<int32_t>(col->position), -1};
                        const Axis* row = sheet->getRow(cellId);
                        if (row != nullptr) return {-1, static_cast<int32_t>(row->position)};
                        return {-1, -1};
                    }
                    const Axis* col = sheet->getColumn(c->colId);
                    const Axis* row = sheet->getRow(c->rowId);
                    if (col == nullptr || row == nullptr) return {-1, -1};
                    return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
                };

                for (const auto& [cellId, cell] : sheet->cells) {
                    if (cell->isFormula() && cell->formula != nullptr) {
                        // Parse the formula text (UUID format) into AST
                        // This is required for evaluation
                        if (cell->formula->parse()) {
                            // Add to dependency graph for reactive updates
                            if (depGraph != nullptr && cell->formula->ast != nullptr) {
                                depGraph->addFormula(cell->id, cell->formula->ast, positionResolver);
                                // Track volatile functions
                                if (cell->formula->hasVolatile()) {
                                    depGraph->markVolatile(cell->id);
                                }
                            }
                        }
                        formulaCells.push_back(cellId);
                    }
                }
                // Recalculate formulas in dependency order
                if (!formulaCells.empty()) {
                    cells::recalculate(sheet, formulaCells);
                }
            }
        }

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
        rebuildViewportIndex();
        notifyListeners(ChangeType::DATA_LOADED);
        return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
    }

    // Parse XLSX from binary data passed via WASM heap pointer
    // This avoids UTF-8 encoding issues that occur with string parameters
    std::string loadFromXLSXDataPtr(uintptr_t ptr, size_t size) {
        const char* data = reinterpret_cast<const char*>(ptr);

        // Set up options with progress callback
        XLSXReadOptions options;
        options.progressCallback = [this](size_t cellsLoaded, size_t totalEstimate) {
            notifyLoadProgress(cellsLoaded, totalEstimate);
        };

        auto result = readXLSXFromMemory(data, size, options);

        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildViewportIndex();

        // Parse and evaluate all formulas in all sheets after loading
        // Step 1: Parse formula text into ASTs (formulas are stored as UUID-format text)
        // Step 2: Add formulas to dependency graph for reactive updates
        // Step 3: Recalculate to compute the display values
        for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
            auto* sheet = _workbook->getSheetByIndex(i);
            if (sheet) {
                std::vector<ID> formulaCells;
                DependencyGraph* depGraph = sheet->getDependencyGraph();

                // Create position resolver for this sheet
                auto positionResolver = [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
                    if (sheet == nullptr) return {-1, -1};
                    const Cell* c = sheet->getCell(cellId);
                    if (c == nullptr) {
                        // Maybe it's a column or row ID
                        const Axis* col = sheet->getColumn(cellId);
                        if (col != nullptr) return {static_cast<int32_t>(col->position), -1};
                        const Axis* row = sheet->getRow(cellId);
                        if (row != nullptr) return {-1, static_cast<int32_t>(row->position)};
                        return {-1, -1};
                    }
                    const Axis* col = sheet->getColumn(c->colId);
                    const Axis* row = sheet->getRow(c->rowId);
                    if (col == nullptr || row == nullptr) return {-1, -1};
                    return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
                };

                for (const auto& [cellId, cell] : sheet->cells) {
                    if (cell->isFormula() && cell->formula != nullptr) {
                        // Parse the formula text (UUID format) into AST
                        // This is required for evaluation
                        if (cell->formula->parse()) {
                            // Add to dependency graph for reactive updates
                            if (depGraph != nullptr && cell->formula->ast != nullptr) {
                                depGraph->addFormula(cell->id, cell->formula->ast, positionResolver);
                                // Track volatile functions
                                if (cell->formula->hasVolatile()) {
                                    depGraph->markVolatile(cell->id);
                                }
                            }
                        }
                        formulaCells.push_back(cellId);
                    }
                }
                // Recalculate formulas in dependency order
                if (!formulaCells.empty()) {
                    cells::recalculate(sheet, formulaCells);
                }
            }
        }

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

        // Use sheet's actual row/column counts, with minimums like Excel
        // The sheet already tracks all rows/columns, so rowCount() is accurate
        constexpr uint32_t MIN_COLS = 26;
        constexpr uint32_t MIN_ROWS = 100;

        uint32_t colCount = std::max(MIN_COLS, static_cast<uint32_t>(sheet->columnCount()));
        uint32_t rowCount = std::max(MIN_ROWS, static_cast<uint32_t>(sheet->rowCount()));

        std::ostringstream json;
        json << "{";
        json << "\"name\":\"" << jsonEscape(sheet->name) << "\",";
        json << "\"rowCount\":" << rowCount << ",";
        json << "\"colCount\":" << colCount << ",";
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
            rebuildViewportIndex();
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

        // Generate sheet ID
        ID sheetId = generate_id();
        size_t newIndex = _workbook->sheetCount();

        // Create and apply SHEET_CREATE operation via CRDT system
        std::string payload = "{\"name\":\"" + jsonEscape(sheetName) + "\"}";
        Operation op = makeSheetCreateOp(*_workbook, sheetId, payload);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

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

        // Get the sheet ID before removing
        Sheet* sheet = _workbook->getSheetByIndex(static_cast<size_t>(index));
        ID sheetId = sheet->id;

        // Create and apply SHEET_DELETE operation via CRDT system
        Operation op = makeSheetDeleteOp(*_workbook, sheetId);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

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

        rebuildViewportIndex();
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

        // Get sheet ID
        Sheet* sheet = _workbook->getSheetByIndex(static_cast<size_t>(index));
        ID sheetId = sheet->id;

        // Create and apply SHEET_RENAME operation via CRDT system
        std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation op = makeSheetRenameOp(*_workbook, sheetId, payload);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

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
    // Parameters are logical column/row positions (not pixel coordinates).
    // Returns pending values if available (pending > committed priority).
    // The "pending" field indicates if the value is from a pending operation.
    std::string queryViewport(uint32_t col1, uint32_t row1, uint32_t col2, uint32_t row2) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"No sheet available\"}";
        }

        // Convert position range to pixel range for ViewportIndex query
        // We need to find the pixel bounds for the given column/row position range
        uint32_t pixelX1 = 0;
        uint32_t pixelY1 = 0;
        uint32_t pixelX2 = 0;
        uint32_t pixelY2 = 0;

        // Get pixel start of first column
        if (auto colId = _viewportIndex.getColumnAt(col1)) {
            if (auto pixel = _viewportIndex.columnToPixel(*colId)) {
                pixelX1 = *pixel;
            }
        }

        // Get pixel end of last column (start of col2, or total width if col2 is beyond)
        if (col2 > 0) {
            if (auto colId = _viewportIndex.getColumnAt(col2 - 1)) {
                if (auto pixel = _viewportIndex.columnToPixel(*colId)) {
                    if (auto width = _viewportIndex.getColumnWidth(*colId)) {
                        pixelX2 = *pixel + *width;
                    }
                }
            }
        }
        if (pixelX2 == 0) {
            pixelX2 = _viewportIndex.totalWidth();
        }

        // Get pixel start of first row
        if (auto rowId = _viewportIndex.getRowAt(row1)) {
            if (auto pixel = _viewportIndex.rowToPixel(*rowId)) {
                pixelY1 = *pixel;
            }
        }

        // Get pixel end of last row
        if (row2 > 0) {
            if (auto rowId = _viewportIndex.getRowAt(row2 - 1)) {
                if (auto pixel = _viewportIndex.rowToPixel(*rowId)) {
                    if (auto height = _viewportIndex.getRowHeight(*rowId)) {
                        pixelY2 = *pixel + *height;
                    }
                }
            }
        }
        if (pixelY2 == 0) {
            pixelY2 = _viewportIndex.totalHeight();
        }

        auto entries = _viewportIndex.queryViewport(pixelX1, pixelY1, pixelX2, pixelY2);

        std::ostringstream json;
        json << "{\"cells\":[";

        bool firstCell = true;
        for (const auto& entry : entries) {
            if (!firstCell) {
                json << ",";
            }
            firstCell = false;

            // Get logical column/row positions from the sheet's axes
            uint32_t colPos = 0;
            uint32_t rowPos = 0;
            auto colIt = sheet->columns.find(entry.cell->colId);
            if (colIt != sheet->columns.end()) {
                colPos = colIt->second->position;
            }
            auto rowIt = sheet->rows.find(entry.cell->rowId);
            if (rowIt != sheet->rows.end()) {
                rowPos = rowIt->second->position;
            }

            json << "{";
            json << "\"id\":\"" << entry.cell->id.toString() << "\",";
            json << "\"col\":" << colPos << ",";
            json << "\"row\":" << rowPos << ",";

            if (entry.cell->isFormula()) {
                json << "\"type\":\"f\",";
                Formula* formula = entry.cell->getFormula();
                std::string a1Formula;
                if (formula != nullptr && formula->text != nullptr) {
                    a1Formula = _refConverter.formulaToA1(formula->text);
                    json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
                }

                // Evaluate the formula and show the calculated value
                EvalResult result = evaluateCell(sheet, entry.cell);
                std::string displayValue;
                if (result.isError()) {
                    displayValue = errorToString(result.getError());
                    json << "\"isError\":true,";
                } else if (result.isNumber()) {
                    const double num = result.getNumber();
                    if (std::floor(num) == num && std::abs(num) < 1e15) {
                        displayValue = std::to_string(static_cast<long long>(num));
                    } else {
                        std::ostringstream numStr;
                        numStr << std::setprecision(15) << num;
                        displayValue = numStr.str();
                        // Remove trailing zeros after decimal
                        size_t dot = displayValue.find('.');
                        if (dot != std::string::npos) {
                            size_t last = displayValue.find_last_not_of('0');
                            if (last != std::string::npos && last > dot) {
                                displayValue = displayValue.substr(0, last + 1);
                            } else if (last == dot) {
                                displayValue = displayValue.substr(0, dot);
                            }
                        }
                    }
                } else if (result.isString()) {
                    displayValue = result.getString();
                } else if (result.isBoolean()) {
                    displayValue = result.getBoolean() ? "TRUE" : "FALSE";
                } else {
                    // Empty or other
                    displayValue = "";
                }
                json << "\"display\":\"" << jsonEscape(displayValue) << "\"";
            } else {
                char typeChar = valueTypeToChar(entry.cell->value.type);
                json << "\"type\":\"" << typeChar << "\",";
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            }

            json << "}";
        }

        json << "],\"columns\":[";

        // Include column info for the viewport (with pixel offsets)
        bool firstCol = true;
        for (const auto& [id, col] : sheet->columns) {
            if (col->position >= col1 && col->position < col2) {
                if (!firstCol) {
                    json << ",";
                }
                firstCol = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << col->position << ",";
                json << "\"width\":" << col->size << ",";
                // Include pixel offset (O(log n) lookup via ViewportIndex)
                auto pixelOffset = _viewportIndex.columnToPixel(id);
                json << "\"pixelOffset\":" << (pixelOffset ? *pixelOffset : 0) << ",";
                json << "\"name\":\"" << jsonEscape(col->name) << "\"";
                json << "}";
            }
        }

        json << "],\"rows\":[";

        // Include row info for the viewport (with pixel offsets)
        bool firstRow = true;
        for (const auto& [id, row] : sheet->rows) {
            if (row->position >= row1 && row->position < row2) {
                if (!firstRow) {
                    json << ",";
                }
                firstRow = false;
                json << "{";
                json << "\"id\":\"" << id.toString() << "\",";
                json << "\"pos\":" << row->position << ",";
                json << "\"height\":" << row->size << ",";
                // Include pixel offset (O(log n) lookup via ViewportIndex)
                auto pixelOffset = _viewportIndex.rowToPixel(id);
                json << "\"pixelOffset\":" << (pixelOffset ? *pixelOffset : 0) << ",";
                json << "\"name\":\"" << jsonEscape(row->name) << "\"";
                json << "}";
            }
        }

        json << "]}";

        return json.str();
    }

    // Get the pixel X offset for a column at the given position
    // Returns -1 if position is out of range
    int32_t getColumnPixelOffset(uint32_t position) {
        auto colId = _viewportIndex.getColumnAt(position);
        if (!colId) {
            return -1;
        }
        auto pixel = _viewportIndex.columnToPixel(*colId);
        if (!pixel) {
            return -1;
        }
        return static_cast<int32_t>(*pixel);
    }

    // Get the pixel Y offset for a row at the given position
    // Returns -1 if position is out of range
    int32_t getRowPixelOffset(uint32_t position) {
        auto rowId = _viewportIndex.getRowAt(position);
        if (!rowId) {
            return -1;
        }
        auto pixel = _viewportIndex.rowToPixel(*rowId);
        if (!pixel) {
            return -1;
        }
        return static_cast<int32_t>(*pixel);
    }

    // Get the total pixel width of all columns
    uint32_t getTotalWidth() { return _viewportIndex.totalWidth(); }

    // Get the total pixel height of all rows
    uint32_t getTotalHeight() { return _viewportIndex.totalHeight(); }

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

        // Note: No viewport index update needed - cell value changes don't affect spatial position

        // Trigger recalculation: mark dependents dirty and recalculate
        // This ensures formulas that depend on this cell get updated
        markDirty(sheet, cellId);
        std::vector<ID> changed = {cellId};
        cells::recalculate(sheet, changed);

        // Also recalculate volatile cells (RAND, NOW, etc.) on any cell change
        // This matches Excel behavior where volatile functions update on every edit
        cells::recalculateVolatile(sheet);

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
        bool colCreated = false;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            colId = generate_id();
            colCreated = true;
            std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                     ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
            Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
            applyOperation(*_workbook, colOp);
        }

        // Find or create row at position
        ID rowId;
        bool rowCreated = false;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            rowId = generate_id();
            rowCreated = true;
            std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                     ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
            Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
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

        // Incremental viewport index updates for created axes and cell
        if (colCreated) {
            _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
        }
        if (rowCreated) {
            _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
        }
        Cell* newCell = sheet->getCell(cellId);
        if (newCell) {
            _viewportIndex.onCellAdded(newCell);
        }

        // Trigger recalculation for the new cell and its dependents
        markDirty(sheet, cellId);
        std::vector<ID> changed = {cellId};
        cells::recalculate(sheet, changed);

        // Also recalculate volatile cells (RAND, NOW, etc.) on any cell change
        // This matches Excel behavior where volatile functions update on every edit
        cells::recalculateVolatile(sheet);

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
        bool colCreated = false;
        for (const auto& [id, axis] : sheet->columns) {
            if (axis->position == col) {
                colId = id;
                break;
            }
        }
        if (colId.isNull()) {
            colId = generate_id();
            colCreated = true;
            std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                     ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
            Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
            applyOperation(*_workbook, colOp);
        }

        // Find or create row at position
        ID rowId;
        bool rowCreated = false;
        for (const auto& [id, axis] : sheet->rows) {
            if (axis->position == row) {
                rowId = id;
                break;
            }
        }
        if (rowId.isNull()) {
            rowId = generate_id();
            rowCreated = true;
            std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                     ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
            Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
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

                // Incremental updates for any axes we created while searching
                if (colCreated) {
                    _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
                }
                if (rowCreated) {
                    _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
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

        // Incremental viewport index updates for created axes and cell
        if (colCreated) {
            _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
        }
        if (rowCreated) {
            _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
        }
        Cell* newCell = sheet->getCell(cellId);
        if (newCell) {
            _viewportIndex.onCellAdded(newCell);
        }
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

        // Get cell pointer before deletion for incremental update
        Cell* cellPtr = it->second.get();

        // Create and apply CELL_CLEAR operation via CRDT system
        Operation op = makeCellClearOp(*_workbook, cellId);
        applyOperation(*_workbook, op);

        // Queue broadcast to sync with peers (if any) and prune old operations
        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }

        // Incremental viewport index update - remove cell
        _viewportIndex.onCellRemoved(cellPtr);
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
                // Get cell pointer before deletion for incremental update
                Cell* cellPtr = cell.get();

                // Create and apply CELL_CLEAR operation via CRDT system
                Operation op = makeCellClearOp(*_workbook, id);
                applyOperation(*_workbook, op);

                // Queue broadcast to sync with peers (if any) and prune old operations
                if (_syncManager) {
                    _syncManager->queueOperationsBroadcast();
                    _syncManager->pruneOpLog();
                }

                // Incremental viewport index update - remove cell
                _viewportIndex.onCellRemoved(cellPtr);
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
        Operation op = makeColResizeOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Incremental viewport index update
        _viewportIndex.onAxisResized(colId, true, width);
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

        bool colCreated = false;
        if (!column) {
            colId = generate_id();
            colCreated = true;
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(width) + "}";
            Operation insertOp = makeColInsertOp(*_workbook, colId, insertPayload);
            applyOperation(*_workbook, insertOp);
            column = sheet->getColumn(colId);
        } else {
            std::string resizePayload = "{\"size\":" + std::to_string(width) + "}";
            Operation resizeOp = makeColResizeOp(*_workbook, colId, resizePayload);
            applyOperation(*_workbook, resizeOp);
        }

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Incremental viewport index update
        if (colCreated) {
            _viewportIndex.onAxisInserted(colId, true, pos, width);
        } else {
            _viewportIndex.onAxisResized(colId, true, width);
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
        Operation op = makeRowResizeOp(*_workbook, rowId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Incremental viewport index update
        _viewportIndex.onAxisResized(rowId, false, height);
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

        bool rowCreated = false;
        if (!row) {
            rowId = generate_id();
            rowCreated = true;
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                        ",\"size\":" + std::to_string(height) + "}";
            Operation insertOp = makeRowInsertOp(*_workbook, rowId, insertPayload);
            applyOperation(*_workbook, insertOp);
            row = sheet->getRow(rowId);
        } else {
            std::string resizePayload = "{\"size\":" + std::to_string(height) + "}";
            Operation resizeOp = makeRowResizeOp(*_workbook, rowId, resizePayload);
            applyOperation(*_workbook, resizeOp);
        }

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Incremental viewport index update
        if (rowCreated) {
            _viewportIndex.onAxisInserted(rowId, false, pos, height);
        } else {
            _viewportIndex.onAxisResized(rowId, false, height);
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
        Operation op = makeColRenameOp(*_workbook, colId, payload);
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
            // Create COL_INSERT operation for new column
            std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                       ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
            Operation insertOp = makeColInsertOp(*_workbook, colId, insertPayload);
            applyOperation(*_workbook, insertOp);

            // Now rename it
            std::string renamePayload = "{\"name\":\"" + jsonEscape(name) + "\"}";
            Operation renameOp = makeColRenameOp(*_workbook, colId, renamePayload);
            applyOperation(*_workbook, renameOp);

            column = sheet->getColumn(colId);
        } else {
            std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
            Operation op = makeColRenameOp(*_workbook, colId, payload);
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

        // Full rebuild needed - this operation shifts multiple axes at once
        // Individual onAxisMoved calls would be O(n log n) anyway
        rebuildViewportIndex();
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

        // Full rebuild needed - this operation shifts multiple axes at once
        // Individual onAxisMoved calls would be O(n log n) anyway
        rebuildViewportIndex();
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
        Operation op = makeColMoveOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Full viewport rebuild - the incremental onAxisMoved expects tree positions,
        // not Sheet positions, and the Sheet allows sparse positions while the tree
        // stores columns contiguously. A full rebuild is simpler and correct.
        rebuildViewportIndex();
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
        Operation op = makeRowMoveOp(*_workbook, rowId, payload);
        applyOperation(*_workbook, op);

        // Prune old operations
        if (_syncManager) {
            _syncManager->pruneOpLog();
        }

        // Full viewport rebuild (same reason as moveColumn)
        rebuildViewportIndex();
        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true}";
    }

    // ========================================================================
    // Column/row insert/delete operations
    // ========================================================================

    // Insert a column at the specified position (0-indexed)
    // If insertBefore is true, insert before the position; otherwise insert after
    std::string insertColumnAt(uint32_t position, bool insertBefore) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Calculate the actual insertion position
        uint32_t insertPos = insertBefore ? position : position + 1;

        // Insert the column
        Axis* newCol = sheet->insertColumnAt(insertPos);
        if (!newCol) {
            return "{\"error\":\"Failed to insert column\"}";
        }

        // Incremental viewport index update
        _viewportIndex.onAxisInserted(newCol->id, true, newCol->position, newCol->size);
        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true,\"id\":\"" + newCol->id.toString() + "\",\"position\":" +
               std::to_string(newCol->position) + "}";
    }

    // Insert a row at the specified position (0-indexed)
    // If insertBefore is true, insert before the position; otherwise insert after
    std::string insertRowAt(uint32_t position, bool insertBefore) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return "{\"error\":\"Sheet not found\"}";
        }

        // Calculate the actual insertion position
        uint32_t insertPos = insertBefore ? position : position + 1;

        // Insert the row
        Axis* newRow = sheet->insertRowAt(insertPos);
        if (!newRow) {
            return "{\"error\":\"Failed to insert row\"}";
        }

        // Incremental viewport index update
        _viewportIndex.onAxisInserted(newRow->id, false, newRow->position, newRow->size);
        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true,\"id\":\"" + newRow->id.toString() + "\",\"position\":" +
               std::to_string(newRow->position) + "}";
    }

    // Delete a column by its ID
    std::string deleteColumnById(const std::string& colIdStr) {
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

        // Incremental viewport index update - must be called before deletion
        // This removes the axis and any cells in that column from the index
        _viewportIndex.onAxisDeleted(colId, true);

        if (!sheet->deleteColumn(colId)) {
            return "{\"error\":\"Column not found\"}";
        }

        notifyListeners(ChangeType::STRUCTURE_CHANGED);

        return "{\"success\":true}";
    }

    // Delete a row by its ID
    std::string deleteRowById(const std::string& rowIdStr) {
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

        // Incremental viewport index update - must be called before deletion
        // This removes the axis and any cells in that row from the index
        _viewportIndex.onAxisDeleted(rowId, false);

        if (!sheet->deleteRow(rowId)) {
            return "{\"error\":\"Row not found\"}";
        }

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

    // Check if workbook contains any formula cells
    // Returns true if any cell in any sheet has a formula
    bool hasFormulas() {
        if (!_workbook) {
            return false;
        }
        for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
            auto* sheet = _workbook->getSheetByIndex(i);
            if (sheet) {
                for (const auto& [cellId, cell] : sheet->cells) {
                    if (cell->isFormula()) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ========================================================================
    // Workbook name
    // ========================================================================

    std::string getWorkbookName() { return _workbook ? _workbook->name : ""; }

    void setWorkbookName(const std::string& name) {
        if (!_workbook) {
            return;
        }

        // Update the name
        _workbook->name = name;

        // Create CRDT operation for sync
        std::ostringstream payload;
        payload << "{\"name\":\"" << jsonEscape(name) << "\"}";
        Operation op = makeWorkbookRenameOp(*_workbook, payload.str());
        applyOperation(*_workbook, op);

        notifyListeners(ChangeType::STRUCTURE_CHANGED);
    }

    // ========================================================================
    // Create empty workbook
    // ========================================================================

    void createEmptyWorkbook() {
        _workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        _workbook->addSheet(std::move(sheet));
        _activeSheetIndex = 0;
        rebuildViewportIndex();
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
                rebuildViewportIndex();
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
                rebuildViewportIndex();
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
            rebuildViewportIndex();
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
            rebuildViewportIndex();
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

    // ========================================================================
    // C++ SyncClient methods (P2P synchronization via WebRTC)
    // ========================================================================

    // Enable sync - connects to signaling server and joins a room.
    // url: WebSocket URL for signaling server (e.g., "wss://server.example.com/ws")
    // roomId: Document/room ID to join
    // peerId: Local peer ID (generated if empty)
    std::string enableSync(const std::string& url, const std::string& roomId,
                           const std::string& peerId = "") {
        if (!_workbook) {
            return "{\"error\":\"No workbook\"}";
        }

        // Clean up existing sync client if any
        if (_syncClient) {
            _syncClient->stopSync();
            _syncClient.reset();
        }

        // Switch to collaboration mode and bootstrap OpLog if not already collaborating.
        // This generates operations for all existing content (columns, rows, cells)
        // so that joining peers can receive the full document state.
        size_t bootstrappedOps = 0;
        if (!_workbook->isCollaborating()) {
            _workbook->startCollaboration();
            bootstrappedOps = bootstrapOpLog(*_workbook);
        }

        // Configure sync client
        cells::net::SyncClientConfig config;
        config.signaling_url = url;

        // Create sync client
        _syncClient = std::make_unique<cells::net::SyncClient>(_workbook.get(), config);
        _syncClient->setDelegate(this);

        // Start sync
        _syncClient->startSync(roomId, peerId);

        std::ostringstream json;
        json << "{\"success\":true,\"peerId\":\"" << _syncClient->getPeerId()
             << "\",\"bootstrapped\":" << bootstrappedOps << "}";
        return json.str();
    }

    // Disable sync - disconnects from peers and signaling server.
    void disableSync() {
        if (_syncClient) {
            _syncClient->stopSync();
            _syncClient.reset();
        }
    }

    // Get current sync state as a JSON object.
    // Returns: { state, peerId, roomId, peerCount, oplogSize, peers: [...] }
    std::string getSyncState() {
        if (!_syncClient) {
            return "{\"state\":\"offline\",\"peerId\":\"\",\"roomId\":\"\",\"peerCount\":0,\"oplogSize\":0,\"peers\":[]}";
        }

        // Get oplog size
        size_t oplogSize = 0;
        if (_workbook) {
            OpLog* oplog = _workbook->getOpLog();
            if (oplog) {
                oplogSize = oplog->size();
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"state\":\"" << cells::net::syncClientStateToString(_syncClient->getState()) << "\",";
        json << "\"peerId\":\"" << _syncClient->getPeerId() << "\",";
        json << "\"roomId\":\"" << _syncClient->getRoomId() << "\",";
        json << "\"peerCount\":" << _syncClient->getPeerCount() << ",";
        json << "\"oplogSize\":" << oplogSize << ",";
        json << "\"peers\":[";

        auto peers = _syncClient->getPeers();
        for (size_t i = 0; i < peers.size(); i++) {
            if (i > 0) json << ",";
            json << "{";
            json << "\"id\":\"" << peers[i].id << "\",";
            json << "\"connected\":" << (peers[i].is_connected ? "true" : "false") << ",";
            json << "\"synced\":" << (peers[i].is_synced ? "true" : "false") << ",";
            json << "\"latency\":" << peers[i].latency_ms;
            json << "}";
        }

        json << "]";
        json << "}";
        return json.str();
    }

    // Check if sync is currently enabled/connected.
    bool isSyncEnabled() { return _syncClient != nullptr && _syncClient->isConnected(); }

    // Process outgoing sync messages - call periodically (e.g., in requestAnimationFrame).
    void processSyncOutgoing() {
        if (_syncClient) {
            _syncClient->processOutgoing();
        }
    }

    // Process pending presence updates - call periodically.
    void processSyncPresence() {
        if (_syncClient) {
            _syncClient->processPresenceUpdates();
        }
    }

    // Broadcast local operations to peers - call after local edits.
    void broadcastSyncOperations() {
        if (_syncClient) {
            _syncClient->broadcastOperations();
        }
    }

    // ========================================================================
    // C++ SyncClient presence methods
    // ========================================================================

    // Set local user's display name (shown to other peers).
    void setSyncLocalName(const std::string& name) {
        if (_syncClient) {
            _syncClient->setLocalName(name);
        }
    }

    // Set current sheet (for multi-sheet presence tracking).
    void setSyncCurrentSheet(const std::string& sheetId) {
        if (_syncClient) {
            _syncClient->setCurrentSheet(sheetId);
        }
    }

    // Set cursor position (cell the user is editing).
    void setSyncCursor(int col, int row) {
        if (_syncClient) {
            _syncClient->setCursor(col, row);
        }
    }

    // Clear cursor position.
    void clearSyncCursor() {
        if (_syncClient) {
            _syncClient->clearCursor();
        }
    }

    // Set selection range.
    void setSyncSelection(int startCol, int startRow, int endCol, int endRow) {
        if (_syncClient) {
            cells::net::CursorPosition start;
            start.col = startCol;
            start.row = startRow;
            cells::net::CursorPosition end;
            end.col = endCol;
            end.row = endRow;
            _syncClient->setSelection(start, end);
        }
    }

    // Clear selection.
    void clearSyncSelection() {
        if (_syncClient) {
            _syncClient->clearSelection();
        }
    }

    // Set mouse position (for cursor tracking).
    void setSyncMousePosition(double x, double y) {
        if (_syncClient) {
            _syncClient->setMousePosition(x, y);
        }
    }

    // Clear mouse position.
    void clearSyncMousePosition() {
        if (_syncClient) {
            _syncClient->clearMousePosition();
        }
    }

    // Set editing state (ephemeral, shows what user is typing).
    void setSyncEditing(int32_t col, int32_t row, const std::string& text) {
        if (_syncClient) {
            _syncClient->setEditing(col, row, text);
        }
    }

    // Clear editing state.
    void clearSyncEditing() {
        if (_syncClient) {
            _syncClient->clearEditing();
        }
    }

    // Get remote peers' presence data as JSON.
    std::string getRemotePresences() {
        if (!_syncClient) {
            return "{\"peers\":{}}";
        }

        std::ostringstream json;
        json << "{\"peers\":{";

        auto remotePeers = _syncClient->getRemotePeers();
        bool first = true;
        for (const auto& [peerId, presence] : remotePeers) {
            if (!first) json << ",";
            first = false;

            json << "\"" << peerId << "\":{";
            json << "\"name\":\"" << jsonEscape(presence.name) << "\",";
            json << "\"color\":\"" << jsonEscape(presence.color) << "\",";
            json << "\"sheetId\":\"" << jsonEscape(presence.sheet_id) << "\",";

            if (presence.has_cursor) {
                json << "\"cursor\":{\"col\":" << presence.cursor.col << ",\"row\":"
                     << presence.cursor.row << "},";
            } else {
                json << "\"cursor\":null,";
            }

            if (presence.has_selection) {
                json << "\"selection\":{\"startCol\":" << presence.selection.start.col
                     << ",\"startRow\":" << presence.selection.start.row << ",\"endCol\":"
                     << presence.selection.end.col << ",\"endRow\":" << presence.selection.end.row
                     << "},";
            } else {
                json << "\"selection\":null,";
            }

            if (presence.has_mouse) {
                json << "\"mouse\":{\"x\":" << presence.mouse.x << ",\"y\":" << presence.mouse.y
                     << "},";
            } else {
                json << "\"mouse\":null,";
            }

            if (presence.is_editing) {
                json << "\"editing\":{\"col\":" << presence.editing_cell.col
                     << ",\"row\":" << presence.editing_cell.row << ",\"text\":\""
                     << jsonEscape(presence.editing_text) << "\"}";
            } else {
                json << "\"editing\":null";
            }

            json << "}";
        }

        json << "}}";
        return json.str();
    }

    // ========================================================================
    // SyncClientDelegate implementation
    // ========================================================================

    void syncClientStateDidChange(cells::net::SyncClient& /*client*/,
                                  cells::net::SyncClientState /*state*/) override {
        notifyListeners(ChangeType::SYNC_STATE_CHANGED);
    }

    void syncClientPeerDidChange(cells::net::SyncClient& /*client*/,
                                 const cells::net::PeerInfo& peer) override {
        // Peer connected/synced - notify with PEER_JOINED
        if (peer.is_connected) {
            notifyListenersWithData(ChangeType::PEER_JOINED, peer.id);
        }
    }

    void syncClientPeerDidDisconnect(cells::net::SyncClient& /*client*/,
                                     const std::string& peer_id) override {
        notifyListenersWithData(ChangeType::PEER_LEFT, peer_id);
    }

    void syncClientDataDidChange(cells::net::SyncClient& /*client*/) override {
        // Remote operations modified data - rebuild quadtree and notify
        rebuildViewportIndex();
        notifyListeners(ChangeType::CELL_CHANGED);
    }

    void syncClientDidError(cells::net::SyncClient& /*client*/,
                            const std::string& error) override {
        LOG_ERROR("SyncClient error: %s", error.c_str());
    }

    void syncClientLatencyDidUpdate(cells::net::SyncClient& /*client*/,
                                    const std::string& /*peer_id*/,
                                    int /*latency_ms*/) override {
        // Latency updates - not currently notified to JS
    }

    void syncClientPresenceDidUpdate(cells::net::SyncClient& /*client*/,
                                     const std::string& peer_id,
                                     const cells::net::PresenceData& /*presence*/) override {
        notifyListenersWithData(ChangeType::PRESENCE_CHANGED, peer_id);
    }

    void syncClientPresenceDidRemove(cells::net::SyncClient& /*client*/,
                                     const std::string& peer_id) override {
        notifyListenersWithData(ChangeType::PRESENCE_CHANGED, peer_id);
    }

    // ========================================================================
    // Formula API methods (Phase 7)
    // ========================================================================

    // Validate a formula without side effects.
    // Returns JSON with parse errors and AST structure.
    // Used for live feedback while user types in formula bar.
    // Does NOT require a cell or workbook - just validates syntax.
    std::string validateFormula(const std::string& formulaText) {
        FormulaParser parser(formulaText);
        auto ast = parser.parse();

        std::ostringstream json;
        json << "{";
        json << "\"formula\":\"" << jsonEscape(formulaText) << "\",";
        json << "\"valid\":" << (ast && parser.errors().empty() ? "true" : "false") << ",";

        // Include parse errors
        json << "\"errors\":[";
        const auto& errors = parser.errors();
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << jsonEscape(errors[i]) << "\"";
        }
        json << "],";

        // Include basic AST info (type of root node) for partial feedback
        json << "\"rootType\":";
        if (ast) {
            // Convert AST node type to string
            const char* typeStr = "unknown";
            switch (ast->type) {
                case ASTNodeType::NUMBER_LITERAL: typeStr = "NumberLiteral"; break;
                case ASTNodeType::STRING_LITERAL: typeStr = "StringLiteral"; break;
                case ASTNodeType::BOOLEAN_LITERAL: typeStr = "BooleanLiteral"; break;
                case ASTNodeType::CELL_REF: typeStr = "CellRef"; break;
                case ASTNodeType::RANGE_REF: typeStr = "RangeRef"; break;
                case ASTNodeType::COLUMN_REF: typeStr = "ColumnRef"; break;
                case ASTNodeType::ROW_REF: typeStr = "RowRef"; break;
                case ASTNodeType::COLUMN_RANGE_REF: typeStr = "ColumnRangeRef"; break;
                case ASTNodeType::ROW_RANGE_REF: typeStr = "RowRangeRef"; break;
                case ASTNodeType::NAMED_REF: typeStr = "NamedRef"; break;
                case ASTNodeType::BINARY_OP: typeStr = "BinaryOp"; break;
                case ASTNodeType::UNARY_OP: typeStr = "UnaryOp"; break;
                case ASTNodeType::FUNCTION_CALL: typeStr = "FunctionCall"; break;
                case ASTNodeType::ERROR_NODE: typeStr = "Error"; break;
            }
            json << "\"" << typeStr << "\"";
        } else {
            json << "null";
        }

        json << "}";
        return json.str();
    }

    // Get the A1 display string for a cell's formula.
    // Returns empty string if cell has no formula.
    // Used to show formula in formula bar when cell is selected.
    std::string getFormulaDisplay(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "";

        if (cellIdStr.size() != ID_LENGTH) return "";
        ID cellId(cellIdStr);

        Cell* cell = sheet->getCell(cellId);
        if (!cell || !cell->isFormula()) return "";

        Formula* formula = cell->getFormula();
        if (!formula || !formula->text) return "";

        // Convert UUID format to A1 notation
        return _refConverter.formulaToA1(formula->text);
    }

    // Get dependencies for a cell's formula (what cells this formula reads from).
    // Returns JSON array of reference info for UI highlighting.
    // Each reference includes: type, cellId, col/row positions, source position in formula.
    std::string getCellDependencies(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
        ID cellId(cellIdStr);

        DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (!depGraph) return "{\"error\":\"No dependency graph\"}";

        std::vector<DependencyRef> deps = depGraph->getDependencies(cellId);

        std::ostringstream json;
        json << "{\"dependencies\":[";

        for (size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) json << ",";
            const auto& dep = deps[i];

            json << "{";

            // Type
            switch (dep.type) {
                case DependencyRef::Type::CELL:
                    json << "\"type\":\"cell\",";
                    json << "\"cellId\":\"" << dep.cellId.toString() << "\"";
                    break;
                case DependencyRef::Type::RANGE:
                    json << "\"type\":\"range\",";
                    json << "\"startCellId\":\"" << dep.startCellId.toString() << "\",";
                    json << "\"endCellId\":\"" << dep.endCellId.toString() << "\"";
                    break;
                case DependencyRef::Type::COLUMN:
                    json << "\"type\":\"column\",";
                    json << "\"columnId\":\"" << dep.columnId.toString() << "\"";
                    break;
                case DependencyRef::Type::ROW:
                    json << "\"type\":\"row\",";
                    json << "\"rowId\":\"" << dep.rowId.toString() << "\"";
                    break;
                case DependencyRef::Type::COLUMN_RANGE:
                    json << "\"type\":\"columnRange\",";
                    json << "\"startColumnId\":\"" << dep.startColumnId.toString() << "\",";
                    json << "\"endColumnId\":\"" << dep.endColumnId.toString() << "\"";
                    break;
                case DependencyRef::Type::ROW_RANGE:
                    json << "\"type\":\"rowRange\",";
                    json << "\"startRowId\":\"" << dep.startRowId.toString() << "\",";
                    json << "\"endRowId\":\"" << dep.endRowId.toString() << "\"";
                    break;
            }

            // Source position (for colored highlighting in formula bar)
            json << ",\"sourceStart\":" << dep.sourceStart;
            json << ",\"sourceEnd\":" << dep.sourceEnd;

            json << "}";
        }

        json << "]}";
        return json.str();
    }

    // Get cells that depend on the given cell (cells whose formulas read this cell).
    // Used for "Show Dependents" feature.
    // Returns JSON array of cell IDs.
    std::string getCellDependents(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
        ID cellId(cellIdStr);

        DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (!depGraph) return "{\"error\":\"No dependency graph\"}";

        std::vector<ID> dependents = depGraph->getDependents(cellId);

        std::ostringstream json;
        json << "{\"dependents\":[";

        for (size_t i = 0; i < dependents.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << dependents[i].toString() << "\"";
        }

        json << "]}";
        return json.str();
    }

    // Get references from a formula with source positions (for colored highlighting).
    // Uses the FormulaResolver to extract references from parsed AST.
    // This parses the formula fresh and resolves references in the current sheet context.
    // Returns references with their positions in the formula text.
    std::string getFormulaReferences(const std::string& formulaText) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        // Parse the formula
        FormulaParser parser(formulaText);
        auto ast = parser.parse();

        if (!ast) {
            return "{\"error\":\"Parse failed\",\"references\":[]}";
        }

        // Snapshot existing entities before resolution
        // (to track what gets created by formula resolution)
        std::set<ID> existingColumns;
        std::set<ID> existingRows;
        std::set<ID> existingCells;
        for (const auto& [id, _] : sheet->columns) existingColumns.insert(id);
        for (const auto& [id, _] : sheet->rows) existingRows.insert(id);
        for (const auto& [id, _] : sheet->cells) existingCells.insert(id);

        // Resolve references in current sheet context
        // This may create new cells/columns/rows for references to empty locations
        FormulaResolver resolver(*_workbook, *sheet, _workbook->getNamedRanges());
        ResolveResult result = resolver.resolve(ast.get());

        if (!result.success) {
            // Still try to extract what we can
        }

        // Generate operations for newly created columns, rows, and cells
        // This ensures they sync to other peers when in collaboration mode
        if (_workbook->isCollaborating()) {
            // Generate COL_INSERT operations for new columns
            for (const auto& [id, col] : sheet->columns) {
                if (existingColumns.find(id) == existingColumns.end()) {
                    std::string payload = "{\"pos\":" + std::to_string(col->position) +
                                          ",\"size\":" + std::to_string(col->size) + "}";
                    Operation op = makeColInsertOp(*_workbook, id, payload);
                    _workbook->getOpLog()->addOperation(op);
                }
            }

            // Generate ROW_INSERT operations for new rows
            for (const auto& [id, row] : sheet->rows) {
                if (existingRows.find(id) == existingRows.end()) {
                    std::string payload = "{\"pos\":" + std::to_string(row->position) +
                                          ",\"size\":" + std::to_string(row->size) + "}";
                    Operation op = makeRowInsertOp(*_workbook, id, payload);
                    _workbook->getOpLog()->addOperation(op);
                }
            }

            // Generate CELL_SET_VALUE operations for new cells
            // (empty cells created by formula references)
            for (const auto& [id, cell] : sheet->cells) {
                if (existingCells.find(id) == existingCells.end()) {
                    std::string payload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" +
                                          cell->colId.toString() + "\",\"row_id\":\"" +
                                          cell->rowId.toString() + "\"}";
                    Operation op = makeCellSetValueOp(*_workbook, id, payload);
                    _workbook->getOpLog()->addOperation(op);
                }
            }

            // Queue operations broadcast to peers
            if (_syncManager) {
                _syncManager->queueOperationsBroadcast();
            }
        }

        // Rebuild RefConverter to include any newly created cells
        // This ensures formulaToUuid can find them when the formula is committed
        rebuildViewportIndex();

        // Extract references with positions
        std::vector<ReferenceInfo> refs = resolver.extractReferences(ast.get());

        std::ostringstream json;
        json << "{\"references\":[";

        for (size_t i = 0; i < refs.size(); ++i) {
            if (i > 0) json << ",";
            const auto& ref = refs[i];

            json << "{";

            // Type, IDs, and resolved positions
            switch (ref.type) {
                case ReferenceInfo::Type::CELL: {
                    json << "\"type\":\"cell\",";
                    json << "\"cellId\":\"" << ref.cellId.toString() << "\"";
                    // Include resolved position so TypeScript doesn't need viewport lookup
                    const Cell* cell = sheet->getCell(ref.cellId);
                    if (cell) {
                        const Axis* col = sheet->getColumn(cell->colId);
                        const Axis* row = sheet->getRow(cell->rowId);
                        if (col && row) {
                            json << ",\"col\":" << col->position;
                            json << ",\"row\":" << row->position;
                        }
                    }
                    break;
                }
                case ReferenceInfo::Type::RANGE: {
                    json << "\"type\":\"range\",";
                    json << "\"topLeftCellId\":\"" << ref.topLeftCellId.toString() << "\",";
                    json << "\"bottomRightCellId\":\"" << ref.bottomRightCellId.toString() << "\"";
                    // Include resolved positions for range corners
                    const Cell* topLeft = sheet->getCell(ref.topLeftCellId);
                    const Cell* bottomRight = sheet->getCell(ref.bottomRightCellId);
                    if (topLeft && bottomRight) {
                        const Axis* startCol = sheet->getColumn(topLeft->colId);
                        const Axis* startRow = sheet->getRow(topLeft->rowId);
                        const Axis* endCol = sheet->getColumn(bottomRight->colId);
                        const Axis* endRow = sheet->getRow(bottomRight->rowId);
                        if (startCol && startRow && endCol && endRow) {
                            json << ",\"startCol\":" << startCol->position;
                            json << ",\"startRow\":" << startRow->position;
                            json << ",\"endCol\":" << endCol->position;
                            json << ",\"endRow\":" << endRow->position;
                        }
                    }
                    break;
                }
                case ReferenceInfo::Type::COLUMN: {
                    json << "\"type\":\"column\",";
                    json << "\"axisId\":\"" << ref.axisId.toString() << "\"";
                    const Axis* axis = sheet->getColumn(ref.axisId);
                    if (axis) {
                        json << ",\"col\":" << axis->position;
                    }
                    break;
                }
                case ReferenceInfo::Type::ROW: {
                    json << "\"type\":\"row\",";
                    json << "\"axisId\":\"" << ref.axisId.toString() << "\"";
                    const Axis* axis = sheet->getRow(ref.axisId);
                    if (axis) {
                        json << ",\"row\":" << axis->position;
                    }
                    break;
                }
                case ReferenceInfo::Type::COLUMN_RANGE: {
                    json << "\"type\":\"columnRange\",";
                    json << "\"startAxisId\":\"" << ref.startAxisId.toString() << "\",";
                    json << "\"endAxisId\":\"" << ref.endAxisId.toString() << "\"";
                    const Axis* startAxis = sheet->getColumn(ref.startAxisId);
                    const Axis* endAxis = sheet->getColumn(ref.endAxisId);
                    if (startAxis && endAxis) {
                        json << ",\"startCol\":" << startAxis->position;
                        json << ",\"endCol\":" << endAxis->position;
                    }
                    break;
                }
                case ReferenceInfo::Type::ROW_RANGE: {
                    json << "\"type\":\"rowRange\",";
                    json << "\"startAxisId\":\"" << ref.startAxisId.toString() << "\",";
                    json << "\"endAxisId\":\"" << ref.endAxisId.toString() << "\"";
                    const Axis* startAxis = sheet->getRow(ref.startAxisId);
                    const Axis* endAxis = sheet->getRow(ref.endAxisId);
                    if (startAxis && endAxis) {
                        json << ",\"startRow\":" << startAxis->position;
                        json << ",\"endRow\":" << endAxis->position;
                    }
                    break;
                }
                case ReferenceInfo::Type::NAMED:
                    json << "\"type\":\"named\",";
                    json << "\"name\":\"" << jsonEscape(ref.namedRangeName) << "\"";
                    break;
            }

            // Sheet ID (if cross-sheet reference)
            if (!ref.sheetId.isNull()) {
                json << ",\"sheetId\":\"" << ref.sheetId.toString() << "\"";
            }

            // Source position in formula text
            json << ",\"sourceStart\":" << ref.sourcePosition.start;
            json << ",\"sourceEnd\":" << ref.sourcePosition.end;

            json << "}";
        }

        json << "]}";
        return json.str();
    }

    // Parse a formula partially (for live editing) and extract valid references.
    // Used when user is typing incomplete formula like "=SUM(A1+"
    // Returns references that were successfully parsed, even if formula is incomplete.
    std::string getReferencesFromPartial(const std::string& formulaText) {
        // Same as getFormulaReferences - our parser has error recovery
        // so it extracts valid references even from incomplete formulas
        return getFormulaReferences(formulaText);
    }

    // Detect circular reference starting from a cell.
    // Returns cycle path if found, empty array if no cycle.
    std::string detectCircularRef(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
        ID cellId(cellIdStr);

        DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (!depGraph) return "{\"error\":\"No dependency graph\"}";

        std::vector<ID> cycle = depGraph->detectCycle(cellId);

        std::ostringstream json;
        json << "{\"hasCycle\":" << (cycle.empty() ? "false" : "true") << ",";
        json << "\"cycle\":[";

        for (size_t i = 0; i < cycle.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << cycle[i].toString() << "\"";
        }

        json << "]}";
        return json.str();
    }

    // Get list of volatile cells in the current sheet.
    // Volatile cells (containing NOW, RAND, etc.) need recalculation on every change.
    std::string getVolatileCells() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (!depGraph) return "{\"error\":\"No dependency graph\"}";

        std::vector<ID> volatile_cells = depGraph->getVolatileCells();

        std::ostringstream json;
        json << "{\"volatileCells\":[";

        for (size_t i = 0; i < volatile_cells.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << volatile_cells[i].toString() << "\"";
        }

        json << "]}";
        return json.str();
    }

    // ========================================================================
    // Formula Evaluation (Phase 8)
    // ========================================================================

    // Evaluate a cell and return the display value (calculated result).
    // For formula cells, returns the computed result (number, string, boolean, or error).
    // For non-formula cells, returns the cell's raw value.
    // Returns JSON: {"value": "...", "type": "n|s|b|e|empty", "error"?: "..."}
    std::string getCellDisplayValue(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        if (cellIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid cell ID\"}";
        }
        ID cellId(cellIdStr);

        Cell* cell = sheet->getCell(cellId);
        if (!cell) {
            // Non-existent cell returns empty
            return "{\"value\":\"\",\"type\":\"empty\"}";
        }

        // Evaluate the cell (or get its value if not a formula)
        EvalResult result = evaluateCell(sheet, cell);

        std::ostringstream json;
        json << "{";

        if (result.isError()) {
            json << "\"value\":\"" << jsonEscape(errorToString(result.getError())) << "\",";
            json << "\"type\":\"e\",";
            json << "\"error\":\"" << jsonEscape(errorToString(result.getError())) << "\"";
        } else if (result.isNumber()) {
            // Format number nicely (avoid unnecessary decimal places)
            const double num = result.getNumber();
            if (std::floor(num) == num && std::abs(num) < 1e15) {
                json << "\"value\":\"" << static_cast<long long>(num) << "\",";
            } else {
                std::ostringstream numStr;
                numStr << std::setprecision(15) << num;
                json << "\"value\":\"" << numStr.str() << "\",";
            }
            json << "\"type\":\"n\"";
        } else if (result.isString()) {
            json << "\"value\":\"" << jsonEscape(result.getString()) << "\",";
            json << "\"type\":\"s\"";
        } else if (result.isBoolean()) {
            json << "\"value\":\"" << (result.getBoolean() ? "TRUE" : "FALSE") << "\",";
            json << "\"type\":\"b\"";
        } else if (result.isEmpty()) {
            json << "\"value\":\"\",";
            json << "\"type\":\"empty\"";
        } else {
            // Range or other - shouldn't happen for a single cell result
            json << "\"value\":\"\",";
            json << "\"type\":\"empty\"";
        }

        json << "}";
        return json.str();
    }

    // Trigger recalculation of all dirty cells.
    // This evaluates formulas that are marked dirty and updates their values.
    // Uses the dependency graph to determine the correct evaluation order.
    // Returns JSON: {"recalculated": count, "errors": count}
    std::string recalculate() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        // Get all dirty cells in proper recalculation order
        std::vector<ID> dirtyCells = getDirtyCells(sheet);

        int recalculated = 0;
        int errors = 0;

        // Recalculate each dirty cell
        for (const ID& cellId : dirtyCells) {
            Cell* cell = sheet->getCell(cellId);
            if (cell && cell->isFormula()) {
                EvalResult result = evaluateCell(sheet, cell);
                ++recalculated;
                if (result.isError()) {
                    ++errors;
                }
            }
        }

        // Also recalculate volatile cells if any
        recalculateVolatile(sheet);

        std::ostringstream json;
        json << "{\"recalculated\":" << recalculated << ",\"errors\":" << errors << "}";
        return json.str();
    }

    // Check if any cells need recalculation.
    // Returns true if there are dirty formula cells.
    bool hasDirtyCellsCheck() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return false;
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return false;

        return hasDirtyCells(sheet);
    }

    // Mark a cell as dirty and mark all its dependents as dirty.
    // Use this when a cell's value changes to trigger dependent recalculation.
    // Returns JSON: {"success": true, "markedDirty": count}
    std::string markCellDirty(const std::string& cellIdStr) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        if (cellIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid cell ID\"}";
        }
        ID cellId(cellIdStr);

        // Mark this cell and its dependents as dirty
        markDirty(sheet, cellId);

        // Count dirty cells
        int dirtyCount = 0;
        for (const auto& [id, cell] : sheet->cells) {
            const Formula* formula = cell->getFormula();
            if (formula && formula->dirty) {
                ++dirtyCount;
            }
        }

        std::ostringstream json;
        json << "{\"success\":true,\"markedDirty\":" << dirtyCount << "}";
        return json.str();
    }

    // Get list of dirty cell IDs (cells needing recalculation)
    // Returns JSON: {"dirtyCells": ["id1", "id2", ...]}
    std::string getDirtyCellIds() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return "{\"error\":\"No sheet available\"}";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) return "{\"error\":\"Sheet not found\"}";

        std::vector<ID> dirtyCells = getDirtyCells(sheet);

        std::ostringstream json;
        json << "{\"dirtyCells\":[";

        for (size_t i = 0; i < dirtyCells.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << dirtyCells[i].toString() << "\"";
        }

        json << "]}";
        return json.str();
    }

    // ========================================================================
    // Debug/Development methods
    // ========================================================================

    // Parse a formula and return its AST as JSON.
    // This is a debug function for visualizing the parse tree.
    // Does not modify any state or require a workbook to be loaded.
    std::string debugParseFormula(const std::string& formulaText) {
        FormulaParser parser(formulaText);
        auto ast = parser.parse();

        std::ostringstream json;
        json << "{";

        // Include the original formula
        json << "\"formula\":\"" << jsonEscape(formulaText) << "\",";

        // Include parse errors if any
        json << "\"errors\":[";
        const auto& errors = parser.errors();
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) {
                json << ",";
            }
            json << "\"" << jsonEscape(errors[i]) << "\"";
        }
        json << "],";

        // Include the AST (or null if parsing failed completely)
        json << "\"ast\":";
        if (ast) {
            json << ast->toJson();
        } else {
            json << "null";
        }

        json << "}";
        return json.str();
    }

    // ========================================================================
    // Scripting API (Luau)
    // ========================================================================

    // Execute a Luau script in the sandboxed environment
    // Returns JSON: {"success":true,"output":"..."} or {"success":false,"error":"..."}
    std::string executeScript(const std::string& script) {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return R"({"success":false,"error":"No workbook loaded"})";
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (sheet == nullptr) {
            return R"({"success":false,"error":"Invalid sheet"})";
        }

        // Set the context for API functions
        _luauSandbox.setContext(_workbook.get(), sheet);

        // Execute the script
        ScriptResult result = _luauSandbox.execute(script);

        // Build JSON response
        std::ostringstream json;
        json << "{\"success\":" << (result.success ? "true" : "false");

        if (result.success) {
            json << ",\"output\":\"" << jsonEscape(result.output) << "\"";
        } else {
            json << ",\"error\":\"" << jsonEscape(result.error) << "\"";
        }

        json << ",\"instructions\":" << result.instructions;
        json << "}";

        // If script modified data, rebuild viewport index and notify listeners
        // Note: We always rebuild after script execution since scripts may modify cells
        if (result.success) {
            rebuildViewportIndex();
            notifyListeners(ChangeType::CELL_CHANGED);
        }

        return json.str();
    }

    // Tokenize a Luau script using the Luau lexer
    // Returns JSON array of tokens: [{"type":"keyword","text":"local","start":0,"end":5},...]
    // Token types: keyword, string, number, comment, name, operator, error
    std::string tokenizeLuau(const std::string& source) {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::Lexer lexer(source.data(), source.size(), names);
        lexer.setSkipComments(false);  // We want to capture comments for highlighting

        std::ostringstream json;
        json << "[";
        bool first = true;

        while (true) {
            const Luau::Lexeme& lexeme = lexer.next();
            if (lexeme.type == Luau::Lexeme::Eof) {
                break;
            }

            if (!first) {
                json << ",";
            }
            first = false;

            // Determine token type category
            const char* tokenType = "operator";
            if (lexeme.type >= Luau::Lexeme::Reserved_BEGIN &&
                lexeme.type < Luau::Lexeme::Reserved_END) {
                tokenType = "keyword";
            } else if (lexeme.type == Luau::Lexeme::QuotedString ||
                       lexeme.type == Luau::Lexeme::RawString ||
                       lexeme.type == Luau::Lexeme::InterpStringBegin ||
                       lexeme.type == Luau::Lexeme::InterpStringMid ||
                       lexeme.type == Luau::Lexeme::InterpStringEnd ||
                       lexeme.type == Luau::Lexeme::InterpStringSimple) {
                tokenType = "string";
            } else if (lexeme.type == Luau::Lexeme::Number) {
                tokenType = "number";
            } else if (lexeme.type == Luau::Lexeme::Comment ||
                       lexeme.type == Luau::Lexeme::BlockComment) {
                tokenType = "comment";
            } else if (lexeme.type == Luau::Lexeme::Name) {
                tokenType = "name";
            } else if (lexeme.type == Luau::Lexeme::BrokenString ||
                       lexeme.type == Luau::Lexeme::BrokenComment ||
                       lexeme.type == Luau::Lexeme::BrokenUnicode ||
                       lexeme.type == Luau::Lexeme::BrokenInterpDoubleBrace ||
                       lexeme.type == Luau::Lexeme::Error) {
                tokenType = "error";
            }

            // Calculate byte offsets from location
            // Location stores line (0-indexed) and column (0-indexed)
            // We need to convert to byte offsets in the source string
            unsigned int startOffset = 0;
            unsigned int endOffset = 0;
            unsigned int currentOffset = 0;
            unsigned int currentLine = 0;

            for (size_t i = 0; i < source.size(); ++i) {
                if (currentLine == lexeme.location.begin.line &&
                    currentOffset == lexeme.location.begin.column) {
                    startOffset = static_cast<unsigned int>(i);
                }
                if (currentLine == lexeme.location.end.line &&
                    currentOffset == lexeme.location.end.column) {
                    endOffset = static_cast<unsigned int>(i);
                    break;
                }
                if (source[i] == '\n') {
                    ++currentLine;
                    currentOffset = 0;
                } else {
                    ++currentOffset;
                }
            }

            // If we hit end of source before finding end position, set to source size
            if (endOffset == 0 && startOffset > 0) {
                endOffset = static_cast<unsigned int>(source.size());
            }

            // Extract the text from source (more accurate than using lexeme.toString())
            std::string text = source.substr(startOffset, endOffset - startOffset);

            // Build JSON object for this token
            json << "{\"type\":\"" << tokenType << "\","
                 << "\"text\":\"" << jsonEscape(text) << "\","
                 << "\"start\":" << startOffset << ","
                 << "\"end\":" << endOffset << "}";
        }

        json << "]";
        return json.str();
    }

private:
    void rebuildViewportIndex() {
        if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
            return;
        }

        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (!sheet) {
            return;
        }

        _viewportIndex.clear();
        _viewportIndex.build(*sheet);
        _refConverter.setContext(*sheet);
    }

    // Notify the registered listener of a data change
    // Called after rebuildViewportIndex() completes
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
            case ChangeType::LOAD_PROGRESS:
                typeStr = "load_progress";
                break;
            case ChangeType::SYNC_STATE_CHANGED:
                typeStr = "sync_state";
                break;
            case ChangeType::PEER_JOINED:
                typeStr = "peer_joined";
                break;
            case ChangeType::PEER_LEFT:
                typeStr = "peer_left";
                break;
            case ChangeType::PRESENCE_CHANGED:
                typeStr = "presence";
                break;
        }

        // Call the JavaScript callback with the change type
        _listener(std::string(typeStr));
    }

    // Notify listener with additional data (e.g., peer ID)
    void notifyListenersWithData(ChangeType type, const std::string& data) {
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
            case ChangeType::LOAD_PROGRESS:
                typeStr = "load_progress";
                break;
            case ChangeType::SYNC_STATE_CHANGED:
                typeStr = "sync_state";
                break;
            case ChangeType::PEER_JOINED:
                typeStr = "peer_joined";
                break;
            case ChangeType::PEER_LEFT:
                typeStr = "peer_left";
                break;
            case ChangeType::PRESENCE_CHANGED:
                typeStr = "presence";
                break;
        }

        // Call the JavaScript callback with the change type and data
        _listener(std::string(typeStr), data);
    }

    // Notify listener of loading progress
    void notifyLoadProgress(size_t cellsLoaded, size_t totalEstimate) {
        if (_listener.isNull() || _listener.isUndefined()) {
            return;
        }
        std::ostringstream data;
        data << cellsLoaded << ":" << totalEstimate;
        _listener(std::string("load_progress"), data.str());
    }

    std::unique_ptr<Workbook> _workbook;
    size_t _activeSheetIndex;
    ViewportIndex _viewportIndex;
    RefConverter _refConverter;
    val _listener;  // JavaScript callback for change notifications
    std::unique_ptr<SyncManager> _syncManager;  // CRDT sync manager (for JS-based sync)
    std::unique_ptr<cells::net::SyncClient> _syncClient;  // C++ sync client (for WebRTC P2P)
    LuauSandbox _luauSandbox;  // Sandboxed Luau scripting engine
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
        .function("getColumnPixelOffset", &cells::wasm::CellsEngine::getColumnPixelOffset)
        .function("getRowPixelOffset", &cells::wasm::CellsEngine::getRowPixelOffset)
        .function("getTotalWidth", &cells::wasm::CellsEngine::getTotalWidth)
        .function("getTotalHeight", &cells::wasm::CellsEngine::getTotalHeight)
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
        // Column/row insert/delete
        .function("insertColumnAt", &cells::wasm::CellsEngine::insertColumnAt)
        .function("insertRowAt", &cells::wasm::CellsEngine::insertRowAt)
        .function("deleteColumnById", &cells::wasm::CellsEngine::deleteColumnById)
        .function("deleteRowById", &cells::wasm::CellsEngine::deleteRowById)
        // Export
        .function("exportToCells", &cells::wasm::CellsEngine::exportToCells)
        .function("exportToCSV", &cells::wasm::CellsEngine::exportToCSV)
        .function("exportToXLSX", &cells::wasm::CellsEngine::exportToXLSX)
        .function("hasFormulas", &cells::wasm::CellsEngine::hasFormulas)
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
        .function("setCollabMode", &cells::wasm::CellsEngine::setCollabMode)
        // C++ SyncClient (P2P WebRTC sync)
        .function("enableSync", &cells::wasm::CellsEngine::enableSync)
        .function("disableSync", &cells::wasm::CellsEngine::disableSync)
        .function("getSyncState", &cells::wasm::CellsEngine::getSyncState)
        .function("isSyncEnabled", &cells::wasm::CellsEngine::isSyncEnabled)
        .function("processSyncOutgoing", &cells::wasm::CellsEngine::processSyncOutgoing)
        .function("processSyncPresence", &cells::wasm::CellsEngine::processSyncPresence)
        .function("broadcastSyncOperations", &cells::wasm::CellsEngine::broadcastSyncOperations)
        // C++ SyncClient presence
        .function("setSyncLocalName", &cells::wasm::CellsEngine::setSyncLocalName)
        .function("setSyncCurrentSheet", &cells::wasm::CellsEngine::setSyncCurrentSheet)
        .function("setSyncCursor", &cells::wasm::CellsEngine::setSyncCursor)
        .function("clearSyncCursor", &cells::wasm::CellsEngine::clearSyncCursor)
        .function("setSyncSelection", &cells::wasm::CellsEngine::setSyncSelection)
        .function("clearSyncSelection", &cells::wasm::CellsEngine::clearSyncSelection)
        .function("setSyncMousePosition", &cells::wasm::CellsEngine::setSyncMousePosition)
        .function("clearSyncMousePosition", &cells::wasm::CellsEngine::clearSyncMousePosition)
        .function("setSyncEditing", &cells::wasm::CellsEngine::setSyncEditing)
        .function("clearSyncEditing", &cells::wasm::CellsEngine::clearSyncEditing)
        .function("getRemotePresences", &cells::wasm::CellsEngine::getRemotePresences)
        // Formula API (Phase 7)
        .function("validateFormula", &cells::wasm::CellsEngine::validateFormula)
        .function("getFormulaDisplay", &cells::wasm::CellsEngine::getFormulaDisplay)
        .function("getCellDependencies", &cells::wasm::CellsEngine::getCellDependencies)
        .function("getCellDependents", &cells::wasm::CellsEngine::getCellDependents)
        .function("getFormulaReferences", &cells::wasm::CellsEngine::getFormulaReferences)
        .function("getReferencesFromPartial", &cells::wasm::CellsEngine::getReferencesFromPartial)
        .function("detectCircularRef", &cells::wasm::CellsEngine::detectCircularRef)
        .function("getVolatileCells", &cells::wasm::CellsEngine::getVolatileCells)
        // Formula Evaluation (Phase 8)
        .function("getCellDisplayValue", &cells::wasm::CellsEngine::getCellDisplayValue)
        .function("recalculate", &cells::wasm::CellsEngine::recalculate)
        .function("hasDirtyCells", &cells::wasm::CellsEngine::hasDirtyCellsCheck)
        .function("markCellDirty", &cells::wasm::CellsEngine::markCellDirty)
        .function("getDirtyCellIds", &cells::wasm::CellsEngine::getDirtyCellIds)
        // Debug/Development
        .function("debugParseFormula", &cells::wasm::CellsEngine::debugParseFormula)
        // Scripting (Luau)
        .function("executeScript", &cells::wasm::CellsEngine::executeScript)
        .function("tokenizeLuau", &cells::wasm::CellsEngine::tokenizeLuau);

    // Logger bindings - control logging from JavaScript
    enum_<cells::log::Level>("LogLevel")
        .value("DEBUG", cells::log::Level::kDebug)
        .value("INFO", cells::log::Level::kInfo)
        .value("WARN", cells::log::Level::kWarn)
        .value("ERROR", cells::log::Level::kError);

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
