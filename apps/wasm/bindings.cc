// WASM bindings for cells library using Embind
// Exposes core types and operations to JavaScript

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/parser.h"
#include "core/cells/quadtree.h"
#include "core/cells/ref_converter.h"
#include "core/cells/serializer.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

using namespace emscripten;

namespace cells::wasm {

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

// ============================================================================
// CellsEngine - main wrapper class exposing the spreadsheet engine to JS
// ============================================================================

class CellsEngine {
public:
    CellsEngine() : _workbook(nullptr), _activeSheetIndex(0) {}

    // ========================================================================
    // File loading methods
    // ========================================================================

    // Parse .cells format from string
    std::string loadFromCells(const std::string& content) {
        auto result = cells::parse(content);
        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildQuadtree();
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
        return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
    }

    // Parse XLSX from file data (writes to temp file, reads, deletes)
    // Uses Emscripten's virtual filesystem
    std::string loadFromXLSXData(const std::string& data) {
        // Write data to Emscripten virtual filesystem
        const char* tempPath = "/tmp/upload.xlsx";

        // Write binary data to temp file
        FILE* f = fopen(tempPath, "wb");
        if (!f) {
            return "{\"error\":\"Failed to create temp file\"}";
        }
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);

        // Read XLSX
        auto result = readXLSX(tempPath);

        // Clean up temp file
        remove(tempPath);

        if (!result.ok()) {
            return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
        }
        _workbook = std::move(result.workbook);
        _activeSheetIndex = 0;
        rebuildQuadtree();
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
        }
    }

    // ========================================================================
    // Viewport query (cells in visible area)
    // ========================================================================

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

        // Clear formula if cell had one
        if (cell->isFormula()) {
            cell->clearFormula();
        }

        // Check if this is a formula
        if (!value.empty() && value[0] == '=') {
            std::string uuidFormula = _refConverter.formulaToUuid(value);
            auto* formula = new Formula(uuidFormula.c_str());
            cell->setFormula(formula);
            cell->value = CellValue(value);
            cell->value.type = CellValueType::FORMULA;
        } else if (value.empty()) {
            cell->value = CellValue();
        } else if (value == "TRUE" || value == "true") {
            cell->value = CellValue(true);
        } else if (value == "FALSE" || value == "false") {
            cell->value = CellValue(false);
        } else {
            // Try parsing as number
            char* endptr = nullptr;
            double num = strtod(value.c_str(), &endptr);
            if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
                cell->value = CellValue(num);
            } else {
                cell->value = CellValue(value);
            }
        }

        rebuildQuadtree();
        return "{\"success\":true}";
    }

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
            auto newCol = std::make_unique<Axis>(generate_id(), true);
            newCol->position = col;
            newCol->size = DEFAULT_COLUMN_WIDTH;
            colId = newCol->id;
            sheet->addColumn(std::move(newCol));
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
            auto newRow = std::make_unique<Axis>(generate_id(), false);
            newRow->position = row;
            newRow->size = DEFAULT_ROW_HEIGHT;
            rowId = newRow->id;
            sheet->addRow(std::move(newRow));
        }

        // Create new cell
        auto newCell = std::make_unique<Cell>(generate_id(), colId, rowId);
        if (!value.empty()) {
            newCell->value = CellValue(value);
        }
        ID cellId = newCell->id;
        sheet->addCell(std::move(newCell));

        rebuildQuadtree();

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << cellId.toString() << "\"}";
        return json.str();
    }

    // ========================================================================
    // Column/row resize operations
    // ========================================================================

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

        it->second->size = width;
        return "{\"success\":true}";
    }

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
        for (auto& [id, col] : sheet->columns) {
            if (col->position == pos) {
                column = col.get();
                break;
            }
        }

        if (!column) {
            auto newCol = std::make_unique<Axis>(generate_id(), true);
            newCol->position = pos;
            newCol->size = width;
            column = newCol.get();
            sheet->addColumn(std::move(newCol));
        } else {
            column->size = width;
        }

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << column->id.toString() << "\"}";
        return json.str();
    }

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

        it->second->size = height;
        return "{\"success\":true}";
    }

    // ========================================================================
    // Column/row move operations
    // ========================================================================

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

        uint32_t newPos = targetPos;
        if (targetPos > currentPos) {
            newPos = targetPos - 1;
        }

        for (auto& [id, col] : sheet->columns) {
            if (id == colId) continue;

            if (currentPos < newPos) {
                if (col->position > currentPos && col->position <= newPos) {
                    col->position--;
                }
            } else {
                if (col->position >= newPos && col->position < currentPos) {
                    col->position++;
                }
            }
        }

        it->second->position = newPos;
        rebuildQuadtree();

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

        uint32_t newPos = targetPos;
        if (targetPos > currentPos) {
            newPos = targetPos - 1;
        }

        for (auto& [id, row] : sheet->rows) {
            if (id == rowId) continue;

            if (currentPos < newPos) {
                if (row->position > currentPos && row->position <= newPos) {
                    row->position--;
                }
            } else {
                if (row->position >= newPos && row->position < currentPos) {
                    row->position++;
                }
            }
        }

        it->second->position = newPos;
        rebuildQuadtree();

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

    std::unique_ptr<Workbook> _workbook;
    size_t _activeSheetIndex;
    Quadtree _quadtree;
    RefConverter _refConverter;
};

}  // namespace cells::wasm

// ============================================================================
// Embind bindings
// ============================================================================

EMSCRIPTEN_BINDINGS(cells) {
    class_<cells::wasm::CellsEngine>("CellsEngine")
        .constructor<>()
        // File loading
        .function("loadFromCells", &cells::wasm::CellsEngine::loadFromCells)
        .function("loadFromCSV", &cells::wasm::CellsEngine::loadFromCSV)
        .function("loadFromXLSXData", &cells::wasm::CellsEngine::loadFromXLSXData)
        // Sheet info
        .function("getSheetInfo", &cells::wasm::CellsEngine::getSheetInfo)
        .function("getSheetCount", &cells::wasm::CellsEngine::getSheetCount)
        .function("getSheetName", &cells::wasm::CellsEngine::getSheetName)
        .function("setActiveSheet", &cells::wasm::CellsEngine::setActiveSheet)
        // Viewport
        .function("queryViewport", &cells::wasm::CellsEngine::queryViewport)
        // Cell operations
        .function("updateCell", &cells::wasm::CellsEngine::updateCell)
        .function("createCell", &cells::wasm::CellsEngine::createCell)
        // Column/row resize
        .function("resizeColumn", &cells::wasm::CellsEngine::resizeColumn)
        .function("resizeColumnByPos", &cells::wasm::CellsEngine::resizeColumnByPos)
        .function("resizeRow", &cells::wasm::CellsEngine::resizeRow)
        // Column/row move
        .function("moveColumn", &cells::wasm::CellsEngine::moveColumn)
        .function("moveRow", &cells::wasm::CellsEngine::moveRow)
        // Export
        .function("exportToCells", &cells::wasm::CellsEngine::exportToCells)
        .function("exportToCSV", &cells::wasm::CellsEngine::exportToCSV)
        .function("exportToXLSX", &cells::wasm::CellsEngine::exportToXLSX)
        // Workbook management
        .function("getWorkbookName", &cells::wasm::CellsEngine::getWorkbookName)
        .function("setWorkbookName", &cells::wasm::CellsEngine::setWorkbookName)
        .function("createEmptyWorkbook", &cells::wasm::CellsEngine::createEmptyWorkbook);
}
