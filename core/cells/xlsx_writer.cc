#include "core/cells/xlsx_writer.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "core/cells/ref_converter.h"
#include "core/cells/types.h"

#include "bindings/go/excelize_types.h"

namespace cells {

// ============================================================================
// XLSXWriteError
// ============================================================================

std::string XLSXWriteError::toString() const {
    std::ostringstream oss;
    if (!sheet.empty()) {
        oss << "Sheet \"" << sheet << "\": ";
    }
    oss << message;
    return oss.str();
}

// ============================================================================
// XLSXWriter
// ============================================================================

XLSXWriter::XLSXWriter() = default;

XLSXWriter::XLSXWriter(XLSXWriteOptions options) : options_(std::move(options)) {}

void XLSXWriter::reset() {
    warnings_.clear();
}

void XLSXWriter::addWarning(const std::string& msg) {
    warnings_.push_back(msg);
}

std::vector<ID> XLSXWriter::getOrderedColumns(const Sheet& sheet) const {
    std::vector<std::pair<uint32_t, ID>> columns;
    columns.reserve(sheet.columns.size());

    for (const auto& pair : sheet.columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }

    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(columns.size());
    for (const auto& col : columns) {
        result.push_back(col.second);
    }
    return result;
}

std::vector<ID> XLSXWriter::getOrderedRows(const Sheet& sheet) const {
    std::vector<std::pair<uint32_t, ID>> rows;
    rows.reserve(sheet.rows.size());

    for (const auto& pair : sheet.rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }

    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(row.second);
    }
    return result;
}

std::string XLSXWriter::columnIndexToLetter(size_t index) {
    std::string result;
    size_t n = index;
    do {
        result.insert(result.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26;
        if (n > 0) {
            n--;  // Adjust for 1-based "digits"
        }
    } while (n > 0);
    return result;
}

std::string XLSXWriter::convertFormula(const std::string& formula, const Sheet& /*sheet*/,
                                       const std::vector<ID>& columnIds,
                                       const std::vector<ID>& rowIds) const {
    // Use RefConverter for robust UUID to A1 conversion
    RefConverter converter;
    converter.setContext(columnIds, rowIds);
    return converter.formulaToA1(formula);
}

// Helper to duplicate a C string (returns nullptr if input is nullptr/empty)
static char* dupString(const std::string& str) {
    if (str.empty()) {
        return nullptr;
    }
    char* result = static_cast<char*>(malloc(str.size() + 1));
    if (result != nullptr) {
        memcpy(result, str.c_str(), str.size() + 1);
    }
    return result;
}

XLSXWriteResult XLSXWriter::writeFile(const Workbook& workbook, const std::string& path) {
    reset();
    XLSXWriteResult result;

    if (workbook.sheets.empty()) {
        result.error = XLSXWriteError("Workbook has no sheets");
        return result;
    }

    // Allocate XLSXData structure
    auto* xlData = static_cast<XLSXData*>(malloc(sizeof(XLSXData)));
    if (xlData == nullptr) {
        result.error = XLSXWriteError("Failed to allocate memory");
        return result;
    }

    xlData->sheet_count = static_cast<int>(workbook.sheets.size());
    xlData->sheets = static_cast<XLSXSheet*>(malloc(sizeof(XLSXSheet) * workbook.sheets.size()));
    if (xlData->sheets == nullptr) {
        free(xlData);
        result.error = XLSXWriteError("Failed to allocate memory for sheets");
        return result;
    }

    // Initialize all sheets to zero
    memset(xlData->sheets, 0, sizeof(XLSXSheet) * workbook.sheets.size());

    // Process each sheet
    for (size_t sheetIdx = 0; sheetIdx < workbook.sheets.size(); ++sheetIdx) {
        const auto& sheet = workbook.sheets[sheetIdx];
        XLSXSheet* xlSheet = &xlData->sheets[sheetIdx];

        xlSheet->name = dupString(sheet->name);

        // Get ordered columns and rows
        std::vector<ID> columnIds = getOrderedColumns(*sheet);
        std::vector<ID> rowIds = getOrderedRows(*sheet);

        xlSheet->col_count = static_cast<int>(columnIds.size());
        xlSheet->row_count = static_cast<int>(rowIds.size());

        if (columnIds.empty() || rowIds.empty()) {
            xlSheet->cell_count = 0;
            xlSheet->cells = nullptr;
            xlSheet->col_dims = nullptr;
            xlSheet->col_dim_count = 0;
            xlSheet->row_dims = nullptr;
            xlSheet->row_dim_count = 0;
            continue;
        }

        // Build column ID to index map
        std::unordered_map<ID, size_t> colIdToIndex;
        for (size_t c = 0; c < columnIds.size(); ++c) {
            colIdToIndex[columnIds[c]] = c;
        }

        // Build row ID to index map
        std::unordered_map<ID, size_t> rowIdToIndex;
        for (size_t r = 0; r < rowIds.size(); ++r) {
            rowIdToIndex[rowIds[r]] = r;
        }

        // Write column dimensions if enabled
        if (options_.writeDimensions) {
            std::vector<XLSXColDim> colDims;
            for (size_t c = 0; c < columnIds.size(); ++c) {
                auto it = sheet->columns.find(columnIds[c]);
                if (it != sheet->columns.end() && it->second->size > 0) {
                    // Convert pixels to Excel width units (approximately 7 pixels per character)
                    const double width = static_cast<double>(it->second->size) / 7.0;
                    if (width > 0 && width != 64.0 / 7.0) {  // Skip default width
                        XLSXColDim dim;
                        dim.col = static_cast<int>(c);
                        dim.width = width;
                        dim.hidden = 0;
                        colDims.push_back(dim);
                    }
                }
            }
            if (!colDims.empty()) {
                xlSheet->col_dim_count = static_cast<int>(colDims.size());
                xlSheet->col_dims =
                    static_cast<XLSXColDim*>(malloc(sizeof(XLSXColDim) * colDims.size()));
                memcpy(xlSheet->col_dims, colDims.data(), sizeof(XLSXColDim) * colDims.size());
            }
        }

        // Write row dimensions if enabled
        if (options_.writeDimensions) {
            std::vector<XLSXRowDim> rowDims;
            for (size_t r = 0; r < rowIds.size(); ++r) {
                auto it = sheet->rows.find(rowIds[r]);
                if (it != sheet->rows.end() && it->second->size > 0) {
                    // Convert pixels to points (1 point = 1.333 pixels at 96 DPI)
                    const double height = static_cast<double>(it->second->size) / 1.333;
                    if (height > 0 && height != 15.0) {  // Skip default height
                        XLSXRowDim dim;
                        dim.row = static_cast<int>(r);
                        dim.height = height;
                        dim.hidden = 0;
                        rowDims.push_back(dim);
                    }
                }
            }
            if (!rowDims.empty()) {
                xlSheet->row_dim_count = static_cast<int>(rowDims.size());
                xlSheet->row_dims =
                    static_cast<XLSXRowDim*>(malloc(sizeof(XLSXRowDim) * rowDims.size()));
                memcpy(xlSheet->row_dims, rowDims.data(), sizeof(XLSXRowDim) * rowDims.size());
            }
        }

        // Write cells
        std::vector<XLSXCell> cells;
        cells.reserve(sheet->cells.size());

        for (const auto& cellPair : sheet->cells) {
            const Cell& cell = *cellPair.second;

            // Find column and row indices
            auto colIt = colIdToIndex.find(cell.colId);
            auto rowIt = rowIdToIndex.find(cell.rowId);

            if (colIt == colIdToIndex.end() || rowIt == rowIdToIndex.end()) {
                addWarning("Cell " + cell.id.toString() +
                           " references unknown column or row, skipping");
                continue;
            }

            XLSXCell xlCell;
            memset(&xlCell, 0, sizeof(xlCell));
            xlCell.row = static_cast<int>(rowIt->second);
            xlCell.col = static_cast<int>(colIt->second);

            // Determine cell type and value
            std::string valueStr;
            switch (cell.value.type) {
                case CellValueType::NUMBER:
                case CellValueType::DATE:
                case CellValueType::DATE_TIME: {
                    const double num = cell.value.asNumber();
                    if (std::isnan(num) || std::isinf(num)) {
                        valueStr = cell.value.raw;
                        xlCell.cell_type = XLSX_CELL_TYPE_STRING;
                    } else {
                        valueStr = std::to_string(num);
                        // Remove trailing zeros after decimal point
                        const size_t dot = valueStr.find('.');
                        if (dot != std::string::npos) {
                            const size_t last = valueStr.find_last_not_of('0');
                            if (last > dot) {
                                valueStr = valueStr.substr(0, last + 1);
                            } else {
                                valueStr = valueStr.substr(0, dot);
                            }
                        }
                        xlCell.cell_type = XLSX_CELL_TYPE_NUMBER;
                    }
                    break;
                }

                case CellValueType::STRING:
                    valueStr = cell.value.raw;
                    xlCell.cell_type = XLSX_CELL_TYPE_STRING;
                    break;

                case CellValueType::BOOLEAN:
                    valueStr = cell.value.asBoolean() ? "TRUE" : "FALSE";
                    xlCell.cell_type = XLSX_CELL_TYPE_BOOL;
                    break;

                case CellValueType::ERROR:
                    valueStr = errorToString(cell.value.error);
                    xlCell.cell_type = XLSX_CELL_TYPE_ERROR;
                    break;

                case CellValueType::FORMULA:
                    // Formula cells - write computed value
                    if (!cell.value.raw.empty()) {
                        // Try to parse as number first
                        char* end = nullptr;  // NOLINT(misc-const-correctness)
                        const double num = std::strtod(cell.value.raw.c_str(), &end);
                        if (end != cell.value.raw.c_str() && *end == '\0' && !std::isnan(num) &&
                            !std::isinf(num)) {
                            valueStr = std::to_string(num);
                            xlCell.cell_type = XLSX_CELL_TYPE_NUMBER;
                        } else {
                            valueStr = cell.value.raw;
                            xlCell.cell_type = XLSX_CELL_TYPE_STRING;
                        }
                    }
                    break;
            }

            xlCell.value = dupString(valueStr);

            // Write formula if present and enabled
            if (options_.writeFormulas && cell.formula != nullptr &&
                cell.formula->text != nullptr) {
                std::string formulaText = cell.formula->text;
                // Remove leading '=' if present
                if (!formulaText.empty() && formulaText[0] == '=') {
                    formulaText = formulaText.substr(1);
                }

                // Convert UUID references to A1 notation
                const std::string convertedFormula =
                    convertFormula(formulaText, *sheet, columnIds, rowIds);

                if (!convertedFormula.empty()) {
                    xlCell.formula = dupString(convertedFormula);
                }
            }

            cells.push_back(xlCell);
            result.cellsWritten++;
        }

        // Copy cells to sheet
        xlSheet->cell_count = static_cast<int>(cells.size());
        if (!cells.empty()) {
            xlSheet->cells = static_cast<XLSXCell*>(malloc(sizeof(XLSXCell) * cells.size()));
            memcpy(xlSheet->cells, cells.data(), sizeof(XLSXCell) * cells.size());
        }
    }

    // Write XLSX file using excelize
    char* error = nullptr;
    const int writeResult = ExcelizeWriteXLSX(path.c_str(), xlData, &error);

    // Free the XLSXData structure
    XLSXDataFree(xlData);

    if (writeResult != 0) {
        result.error = XLSXWriteError(std::string("Failed to write XLSX file: ") +
                                      (error ? error : "unknown error"));
        if (error != nullptr) {
            XLSXErrorFree(error);
        }
        return result;
    }

    result.warnings = std::move(warnings_);
    return result;
}

// ============================================================================
// Convenience functions
// ============================================================================

XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path) {
    XLSXWriter writer;
    return writer.writeFile(workbook, path);
}

XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path,
                          const XLSXWriteOptions& options) {
    XLSXWriter writer(options);
    return writer.writeFile(workbook, path);
}

}  // namespace cells
