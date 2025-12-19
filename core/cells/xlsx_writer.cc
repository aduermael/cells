#include "core/cells/xlsx_writer.h"

#include <cmath>

#include <OpenXLSX/OpenXLSX.hpp>  // NOLINT(build/include_order)
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "core/cells/types.h"

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
    // Build lookup maps for column/row ID to index
    std::unordered_map<std::string, size_t> colIdToIndex;
    std::unordered_map<std::string, size_t> rowIdToIndex;

    for (size_t i = 0; i < columnIds.size(); ++i) {
        colIdToIndex[columnIds[i].toString()] = i;
    }
    for (size_t i = 0; i < rowIds.size(); ++i) {
        rowIdToIndex[rowIds[i].toString()] = i;
    }

    // Parse the formula and convert UUID refs to A1 notation
    // Formula format: =$colId$rowId or similar patterns
    // For now, do a simple conversion for cell references

    std::string result;
    result.reserve(formula.size());

    size_t i = 0;
    while (i < formula.size()) {
        // Look for $<8-char-id>$<8-char-id> pattern (cell reference)
        if (formula[i] == '$' && i + 17 < formula.size() && formula[i + 9] == '$') {
            // Extract column ID (chars 1-8) and row ID (chars 10-17)
            std::string colIdStr = formula.substr(i + 1, 8);
            std::string rowIdStr = formula.substr(i + 10, 8);

            auto colIt = colIdToIndex.find(colIdStr);
            auto rowIt = rowIdToIndex.find(rowIdStr);

            if (colIt != colIdToIndex.end() && rowIt != rowIdToIndex.end()) {
                // Convert to A1 notation
                result += columnIndexToLetter(colIt->second);
                result += std::to_string(rowIt->second + 1);  // Excel rows are 1-based
                i += 18;                                      // Skip the entire reference
                continue;
            }
        }

        // Not a reference, copy character as-is
        result += formula[i];
        ++i;
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

    try {
        // Create new XLSX document
        OpenXLSX::XLDocument doc;
        doc.create(path, OpenXLSX::XLForceOverwrite);

        auto xlWorkbook = doc.workbook();

        // OpenXLSX creates a default "Sheet1" - we'll rename or delete it
        for (size_t sheetIdx = 0; sheetIdx < workbook.sheets.size(); ++sheetIdx) {
            const auto& sheet = workbook.sheets[sheetIdx];
            std::string xlSheetName;

            if (sheetIdx == 0) {
                // Rename the default sheet
                xlWorkbook.worksheet("Sheet1").setName(sheet->name);
                xlSheetName = sheet->name;
            } else {
                // Add new sheet
                xlWorkbook.addWorksheet(sheet->name);
                xlSheetName = sheet->name;
            }

            auto xlSheet = xlWorkbook.worksheet(xlSheetName);

            // Get ordered columns and rows
            std::vector<ID> columnIds = getOrderedColumns(*sheet);
            std::vector<ID> rowIds = getOrderedRows(*sheet);

            if (columnIds.empty() || rowIds.empty()) {
                // Empty sheet - skip cell writing
                continue;
            }

            // Write column widths if enabled
            if (options_.writeDimensions) {
                for (size_t c = 0; c < columnIds.size(); ++c) {
                    auto it = sheet->columns.find(columnIds[c]);
                    if (it != sheet->columns.end()) {
                        // Convert pixels to Excel width units (approximately 7 pixels per
                        // character)
                        float width = static_cast<float>(it->second->size) / 7.0f;
                        if (width > 0) {
                            xlSheet.column(static_cast<uint16_t>(c + 1)).setWidth(width);
                        }
                    }
                }
            }

            // Write row heights if enabled
            if (options_.writeDimensions) {
                for (size_t r = 0; r < rowIds.size(); ++r) {
                    auto it = sheet->rows.find(rowIds[r]);
                    if (it != sheet->rows.end()) {
                        // Convert pixels to points (1 point = 1.333 pixels at 96 DPI)
                        double height = static_cast<double>(it->second->size) / 1.333;
                        if (height > 0) {
                            xlSheet.row(static_cast<uint32_t>(r + 1)).setHeight(height);
                        }
                    }
                }
            }

            // Write cells
            for (const auto& cellPair : sheet->cells) {
                const Cell& cell = *cellPair.second;

                // Find column and row indices
                auto colIt = std::find(columnIds.begin(), columnIds.end(), cell.colId);
                auto rowIt = std::find(rowIds.begin(), rowIds.end(), cell.rowId);

                if (colIt == columnIds.end() || rowIt == rowIds.end()) {
                    addWarning("Cell " + cell.id.toString() +
                               " references unknown column or row, skipping");
                    continue;
                }

                size_t colIndex = static_cast<size_t>(colIt - columnIds.begin());
                size_t rowIndex = static_cast<size_t>(rowIt - rowIds.begin());

                // Excel uses 1-based indexing
                auto xlCell = xlSheet.cell(static_cast<uint32_t>(rowIndex + 1),
                                           static_cast<uint16_t>(colIndex + 1));

                // Write cell value based on type
                switch (cell.value.type) {
                    case CellValueType::NUMBER:
                    case CellValueType::DATE:
                    case CellValueType::DATE_TIME: {
                        double num = cell.value.asNumber();
                        if (std::isnan(num) || std::isinf(num)) {
                            xlCell.value() = cell.value.raw;  // Write as string
                        } else {
                            xlCell.value() = num;
                        }
                        break;
                    }

                    case CellValueType::STRING:
                        xlCell.value() = cell.value.raw;
                        break;

                    case CellValueType::BOOLEAN:
                        xlCell.value() = cell.value.asBoolean();
                        break;

                    case CellValueType::ERROR:
                        // Write error as string (Excel will interpret it)
                        xlCell.value() = errorToString(cell.value.error);
                        break;

                    case CellValueType::FORMULA:
                        // Formula cells - write computed value, formula handled below
                        if (!cell.value.raw.empty()) {
                            // Try to parse as number first
                            try {
                                double num = std::stod(cell.value.raw);
                                if (!std::isnan(num) && !std::isinf(num)) {
                                    xlCell.value() = num;
                                } else {
                                    xlCell.value() = cell.value.raw;
                                }
                            } catch (...) {
                                xlCell.value() = cell.value.raw;
                            }
                        }
                        break;
                }

                // Write formula if present and enabled
                if (options_.writeFormulas && cell.formula != nullptr &&
                    cell.formula->text != nullptr) {
                    std::string formulaText = cell.formula->text;
                    // Remove leading '=' if present (OpenXLSX adds it)
                    if (!formulaText.empty() && formulaText[0] == '=') {
                        formulaText = formulaText.substr(1);
                    }

                    // Convert UUID references to A1 notation
                    std::string convertedFormula =
                        convertFormula(formulaText, *sheet, columnIds, rowIds);

                    if (!convertedFormula.empty()) {
                        try {
                            xlCell.formula() = convertedFormula;
                        } catch (const std::exception& e) {
                            addWarning("Could not write formula for cell at row " +
                                       std::to_string(rowIndex + 1) + ", column " +
                                       std::to_string(colIndex + 1) + ": " + e.what());
                        }
                    }
                }

                result.cellsWritten++;
            }
        }

        doc.save();
        doc.close();

        result.warnings = std::move(warnings_);
        return result;

    } catch (const std::exception& e) {
        result.error = XLSXWriteError(std::string("Failed to write XLSX file: ") + e.what());
        return result;
    }
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
