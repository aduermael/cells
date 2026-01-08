// =============================================================================
// WASM Bindings - File Operations
// =============================================================================
//
// Implementation of file loading and export CellsEngine methods:
// - loadFromCells: Parse .zcd format
// - loadFromCSV: Parse CSV files
// - loadFromXLSXDataPtr: Parse XLSX from binary data
// - exportToCells: Export to .zcd format
// - exportToCSV: Export to CSV
// - exportToXLSX: Export to XLSX binary
// - hasFormulas: Check if workbook contains formulas
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <sys/stat.h>

#include <cstdio>
#include <sstream>

#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/parser.h"
#include "core/cells/serializer.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

namespace cells::wasm {

std::string CellsEngine::loadFromCells(const std::string& content) {
    auto result = cells::parse(content);
    if (!result.ok()) {
        return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
    }
    _workbook = std::move(result.workbook);
    _activeSheetIndex = 0;
    rebuildViewportIndex();

    // Parse and evaluate all formulas in all sheets after loading
    for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
        auto* sheet = _workbook->getSheetByIndex(i);
        if (sheet) {
            std::vector<ID> formulaCells;
            DependencyGraph* depGraph = sheet->getDependencyGraph();

            auto positionResolver = [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
                if (sheet == nullptr) return {-1, -1};
                const Cell* c = sheet->getCell(cellId);
                if (c == nullptr) {
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
                    if (depGraph != nullptr && cell->formula->ast != nullptr) {
                        depGraph->addFormula(cell->id, cell->formula->ast, positionResolver);
                        if (cell->formula->hasVolatile()) {
                            depGraph->markVolatile(cell->id);
                        }
                    }
                    formulaCells.push_back(cellId);
                }
            }
            if (!formulaCells.empty()) {
                cells::recalculate(sheet, formulaCells);
            }
        }
    }

    notifyListeners(ChangeType::DATA_LOADED);
    return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
}

std::string CellsEngine::loadFromCSV(const std::string& content, char delimiter, bool hasHeader) {
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

std::string CellsEngine::loadFromXLSXDataPtr(uintptr_t ptr, size_t size) {
    const char* data = reinterpret_cast<const char*>(ptr);

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
    for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
        auto* sheet = _workbook->getSheetByIndex(i);
        if (sheet) {
            std::vector<ID> formulaCells;
            DependencyGraph* depGraph = sheet->getDependencyGraph();

            auto positionResolver = [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
                if (sheet == nullptr) return {-1, -1};
                const Cell* c = sheet->getCell(cellId);
                if (c == nullptr) {
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
                    if (depGraph != nullptr && cell->formula->ast != nullptr) {
                        depGraph->addFormula(cell->id, cell->formula->ast, positionResolver);
                        if (cell->formula->hasVolatile()) {
                            depGraph->markVolatile(cell->id);
                        }
                    }
                    formulaCells.push_back(cellId);
                }
            }
            if (!formulaCells.empty()) {
                cells::recalculate(sheet, formulaCells);
            }
        }
    }

    notifyListeners(ChangeType::DATA_LOADED);
    return "{\"success\":true,\"sheetCount\":" + std::to_string(_workbook->sheetCount()) + "}";
}

std::string CellsEngine::exportToCells() {
    if (!_workbook) {
        return "";
    }
    return serialize(*_workbook);
}

std::string CellsEngine::exportToCSV() {
    if (!_workbook) {
        return "";
    }
    auto result = writeCSV(*_workbook);
    if (!result.ok()) {
        return "";
    }
    return result.output;
}

std::string CellsEngine::exportToXLSX() {
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

bool CellsEngine::hasFormulas() {
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

}  // namespace cells::wasm
