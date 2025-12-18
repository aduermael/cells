#include "core/cells/xlsx_reader.h"

#include <OpenXLSX/OpenXLSX.hpp>  // NOLINT(build/include_order)
#include <algorithm>
#include <sstream>
#include <utility>

#include "core/cells/id.h"
#include "core/cells/types.h"

namespace cells {

// ============================================================================
// XLSXReadError
// ============================================================================

std::string XLSXReadError::toString() const {
    std::ostringstream oss;
    if (!sheet.empty()) {
        oss << "Sheet \"" << sheet << "\"";
        if (row > 0) {
            oss << " at row " << row;
            if (col > 0) {
                oss << ", column " << col;
            }
        }
        oss << ": ";
    }
    oss << message;
    return oss.str();
}

// ============================================================================
// XLSXReader
// ============================================================================

XLSXReader::XLSXReader() = default;

XLSXReader::XLSXReader(XLSXReadOptions options) : options_(std::move(options)) {}

void XLSXReader::reset() {
    warnings_.clear();
}

void XLSXReader::addWarning(const std::string& msg) {
    warnings_.push_back(msg);
}

XLSXReadResult XLSXReader::readFile(const std::string& path) {
    reset();
    XLSXReadResult result;

    try {
        // Open the XLSX document
        OpenXLSX::XLDocument doc;
        doc.open(path);

        // Create workbook with generated ID
        auto workbook = std::make_unique<Workbook>(generate_id(), "Imported");

        // Get the workbook object
        auto xlWorkbook = doc.workbook();
        auto sheetNames = xlWorkbook.worksheetNames();

        // Filter sheets if specific sheet requested
        if (!options_.sheetName.empty()) {
            const auto it =
                std::find(sheetNames.begin(), sheetNames.end(), options_.sheetName);
            if (it == sheetNames.end()) {
                result.error =
                    XLSXReadError("Sheet \"" + options_.sheetName + "\" not found");
                return result;
            }
            sheetNames = {options_.sheetName};
        }

        // Process each worksheet
        for (const auto& sheetName : sheetNames) {
            auto xlSheet = xlWorkbook.worksheet(sheetName);

            // Create our Sheet
            auto sheet = std::make_unique<Sheet>(generate_id(), sheetName);

            // Get dimensions - use rowCount/columnCount to avoid exceptions on empty sheets
            uint32_t rowCount = xlSheet.rowCount();
            uint16_t colCount = xlSheet.columnCount();

            if (rowCount == 0 || colCount == 0) {
                // Empty sheet
                workbook->addSheet(std::move(sheet));
                continue;
            }

            // Create columns and rows
            std::vector<ID> columnIds;
            std::vector<ID> rowIds;

            for (uint16_t c = 1; c <= colCount; ++c) {
                auto col = std::make_unique<Axis>(generate_id(), true);
                col->position = c - 1;

                // Read column width if requested
                if (options_.readDimensions) {
                    try {
                        auto xlCol = xlSheet.column(c);
                        float width = xlCol.width();
                        // Convert Excel width units to pixels (approximate)
                        // Excel width = number of '0' characters that fit
                        // Roughly 7 pixels per character
                        col->size = static_cast<uint32_t>(width * 7.0f);
                    } catch (...) {
                        // Column info may not exist, use default
                        col->size = 64;  // Default width
                    }
                }

                columnIds.push_back(col->id);
                sheet->addColumn(std::move(col));
            }

            for (uint32_t r = 1; r <= rowCount; ++r) {
                auto row = std::make_unique<Axis>(generate_id(), false);
                row->position = r - 1;

                // Read row height if requested
                if (options_.readDimensions) {
                    try {
                        auto xlRow = xlSheet.row(r);
                        double height = xlRow.height();
                        // Convert points to pixels (1 point = 1.333 pixels at 96 DPI)
                        row->size = static_cast<uint32_t>(height * 1.333);
                    } catch (...) {
                        // Row info may not exist, use default
                        row->size = 20;  // Default height
                    }
                }

                rowIds.push_back(row->id);
                sheet->addRow(std::move(row));
            }

            // Read cells
            for (uint32_t r = 1; r <= rowCount; ++r) {
                for (uint16_t c = 1; c <= colCount; ++c) {
                    try {
                        auto xlCell = xlSheet.cell(r, c);

                        // Skip empty cells
                        if (xlCell.value().type() == OpenXLSX::XLValueType::Empty) {
                            continue;
                        }

                        // Create cell
                        auto cell =
                            std::make_unique<Cell>(generate_id(), columnIds[c - 1], rowIds[r - 1]);

                        // Read cell value
                        auto valueType = xlCell.value().type();

                        switch (valueType) {
                            case OpenXLSX::XLValueType::Boolean:
                                cell->value = CellValue(xlCell.value().get<bool>());
                                break;

                            case OpenXLSX::XLValueType::Integer:
                                cell->value =
                                    CellValue(static_cast<double>(xlCell.value().get<int64_t>()));
                                break;

                            case OpenXLSX::XLValueType::Float:
                                cell->value = CellValue(xlCell.value().get<double>());
                                break;

                            case OpenXLSX::XLValueType::String:
                                cell->value = CellValue(xlCell.value().get<std::string>());
                                break;

                            case OpenXLSX::XLValueType::Error:
                                // Map Excel errors to our error types
                                {
                                    std::string errStr = xlCell.value().get<std::string>();
                                    CellError err = CellError::NONE;
                                    if (errStr == "#DIV/0!") {
                                        err = CellError::DIV;
                                    } else if (errStr == "#VALUE!") {
                                        err = CellError::VALUE;
                                    } else if (errStr == "#REF!") {
                                        err = CellError::REF;
                                    } else if (errStr == "#NAME?") {
                                        err = CellError::NAME;
                                    } else if (errStr == "#NUM!") {
                                        err = CellError::NUM;
                                    } else if (errStr == "#N/A" || errStr == "#NULL!") {
                                        err = CellError::NULL_REF;
                                    } else {
                                        err = CellError::VALUE;  // Default
                                    }
                                    cell->value = CellValue(err);
                                }
                                break;

                            default:
                                // Empty or unknown - skip
                                continue;
                        }

                        // Read formula if present and requested
                        if (options_.readFormulas && xlCell.hasFormula()) {
                            try {
                                std::string formulaText = xlCell.formula().get();
                                if (!formulaText.empty()) {
                                    // Store formula with leading '=' for now
                                    // Reference conversion will be done in Phase 8
                                    cell->setFormula(new Formula(("=" + formulaText).c_str()));
                                }
                            } catch (...) {
                                // Shared or array formulas may not be readable
                                addWarning("Could not read formula at " + sheetName + "!" +
                                           xlCell.cellReference().address());
                            }
                        }

                        sheet->addCell(std::move(cell));
                    } catch (const std::exception& e) {
                        addWarning("Error reading cell at row " + std::to_string(r) + ", column " +
                                   std::to_string(c) + " in sheet \"" + sheetName +
                                   "\": " + e.what());
                    }
                }
            }

            workbook->addSheet(std::move(sheet));
        }

        doc.close();

        result.workbook = std::move(workbook);
        result.warnings = std::move(warnings_);
        return result;

    } catch (const std::exception& e) {
        result.error = XLSXReadError(std::string("Failed to read XLSX file: ") + e.what());
        return result;
    }
}

// ============================================================================
// Convenience functions
// ============================================================================

XLSXReadResult readXLSX(const std::string& path) {
    XLSXReader reader;
    return reader.readFile(path);
}

XLSXReadResult readXLSX(const std::string& path, const XLSXReadOptions& options) {
    XLSXReader reader(options);
    return reader.readFile(path);
}

}  // namespace cells
