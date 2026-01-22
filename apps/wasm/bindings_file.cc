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
#include "core/cells/formula_resolver.h"
#include "core/cells/named_ranges.h"
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
    const NamedRangeRegistry* namedRanges = _workbook->getNamedRanges();
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

            for (const auto& cellId : sheet->getCellIds()) {
                Cell* cell = _workbook->getCell(cellId);
                if (!cell) continue;
                if (cell->isFormula() && cell->formula != nullptr) {
                    if (depGraph != nullptr && cell->formula->ast != nullptr) {
                        depGraph->addFormula(cell->id, cell->formula->ast, positionResolver,
                                             namedRanges, sheet->id);
                        if (cell->formula->hasVolatile()) {
                            depGraph->markVolatile(cell->id);
                        }
                    }
                    formulaCells.push_back(cellId);
                }
            }
            if (!formulaCells.empty()) {
                cells::recalculate(_workbook.get(), formulaCells);
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

    // Resolve and evaluate all formulas in all sheets after loading
    // XLSX formulas are parsed with A1 notation - we must resolve them to UUIDs first
    NamedRangeRegistry* namedRanges = _workbook->getNamedRanges();
    for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
        auto* sheet = _workbook->getSheetByIndex(i);
        if (sheet) {
            std::vector<ID> formulaCells;
            DependencyGraph* depGraph = sheet->getDependencyGraph();

            // Create resolver for this sheet to convert A1 refs to UUIDs
            FormulaResolver resolver(*_workbook, *sheet, namedRanges);

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

            for (const auto& cellId : sheet->getCellIds()) {
                Cell* cell = _workbook->getCell(cellId);
                if (!cell) continue;
                if (cell->isFormula() && cell->formula != nullptr &&
                    cell->formula->ast != nullptr) {
                    // Create any missing entities referenced by the formula
                    // This ensures all referenced cells exist before resolution
                    RequiredEntities required = resolver.getRequiredEntities(cell->formula->ast);

                    // Create axes by position first (order matters for cell creation)
                    for (const auto& col : required.columns) {
                        sheet->getOrCreateColumnByPosition(col.position);
                    }
                    for (const auto& row : required.rows) {
                        sheet->getOrCreateRowByPosition(row.position);
                    }

                    // Create cells - need to map pending IDs to positions, then to actual IDs
                    for (const auto& pendingCell : required.cells) {
                        // Build map from pending ID to position
                        auto findColPos = [&required, sheet](const ID& colId) -> uint32_t {
                            for (const auto& c : required.columns) {
                                if (c.id == colId) return c.position;
                            }
                            // Column existed before - look up directly
                            const Axis* axis = sheet->getColumn(colId);
                            return axis ? axis->position : 0;
                        };
                        auto findRowPos = [&required, sheet](const ID& rowId) -> uint32_t {
                            for (const auto& r : required.rows) {
                                if (r.id == rowId) return r.position;
                            }
                            // Row existed before - look up directly
                            const Axis* axis = sheet->getRow(rowId);
                            return axis ? axis->position : 0;
                        };

                        uint32_t colPos = findColPos(pendingCell.colId);
                        uint32_t rowPos = findRowPos(pendingCell.rowId);
                        const Axis* col = sheet->getColumnByPosition(colPos);
                        const Axis* row = sheet->getRowByPosition(rowPos);
                        if (col && row) {
                            sheet->getOrCreateCellAt(col->id, row->id);
                        }
                    }

                    // Resolve formula references from A1 notation to UUIDs
                    resolver.resolve(cell->formula->ast);

                    if (depGraph != nullptr) {
                        depGraph->addFormula(cell->id, cell->formula->ast, positionResolver,
                                             namedRanges, sheet->id);
                        if (cell->formula->hasVolatile()) {
                            depGraph->markVolatile(cell->id);
                        }
                    }
                    formulaCells.push_back(cellId);
                }
            }
            if (!formulaCells.empty()) {
                cells::recalculate(_workbook.get(), formulaCells);
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

std::string CellsEngine::exportToXLSXPtr() {
    // Binary-safe XLSX export that avoids UTF-8 encoding corruption.
    // Returns a JSON object with {ptr, size} pointing to the WASM heap.
    // JavaScript should use Module.HEAPU8.slice(ptr, ptr + size) to copy the data,
    // then call freeExportBuffer() to release the memory.

    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    // Ensure /tmp directory exists in Emscripten's virtual filesystem
    mkdir("/tmp", 0777);

    const char* tempPath = "/tmp/export.xlsx";
    auto result = writeXLSX(*_workbook, tempPath);
    if (!result.ok()) {
        return "{\"error\":\"" + jsonEscape(result.error->message) + "\"}";
    }

    // Read back the file into the member buffer
    FILE* f = fopen(tempPath, "rb");
    if (!f) {
        return "{\"error\":\"Failed to read exported file\"}";
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    _exportBuffer.resize(size);
    size_t bytesRead = fread(_exportBuffer.data(), 1, size, f);
    fclose(f);
    remove(tempPath);

    if (bytesRead != static_cast<size_t>(size)) {
        _exportBuffer.clear();
        return "{\"error\":\"Failed to read complete file\"}";
    }

    // Return pointer and size as JSON - JS will read directly from WASM heap
    return "{\"ptr\":" + std::to_string(reinterpret_cast<uintptr_t>(_exportBuffer.data())) +
           ",\"size\":" + std::to_string(_exportBuffer.size()) + "}";
}

void CellsEngine::freeExportBuffer() {
    _exportBuffer.clear();
    _exportBuffer.shrink_to_fit();
}

bool CellsEngine::hasFormulas() {
    if (!_workbook) {
        return false;
    }
    for (size_t i = 0; i < _workbook->sheetCount(); ++i) {
        auto* sheet = _workbook->getSheetByIndex(i);
        if (sheet) {
            for (const auto& cellId : sheet->getCellIds()) {
                Cell* cell = _workbook->getCell(cellId);
                if (cell && cell->isFormula()) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace cells::wasm
