#include "core/cells/xlsx_reader.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include "core/cells/id.h"
#include "core/cells/types.h"

#include "bindings/go/excelize_types.h"

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

// Helper to map cell type enum to CellValue
static CellValue parseCellValue(const XLSXCell& xlCell) {
    const char* value = xlCell.value;
    if (value == nullptr || value[0] == '\0') {
        return CellValue("");
    }

    switch (xlCell.cell_type) {
        case XLSX_CELL_TYPE_NUMBER: {
            // Try to parse as double
            char* end = nullptr;
            double num = std::strtod(value, &end);
            if (end != value && *end == '\0') {
                return CellValue(num);
            }
            return CellValue(std::string(value));
        }

        case XLSX_CELL_TYPE_BOOL: {
            std::string val(value);
            bool boolVal = (val == "TRUE" || val == "true" || val == "1");
            return CellValue(boolVal);
        }

        case XLSX_CELL_TYPE_ERROR: {
            std::string errStr(value);
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
            return CellValue(err);
        }

        case XLSX_CELL_TYPE_DATE:
            // Store dates as numbers (Excel serial date format)
            {
                char* end = nullptr;
                double num = std::strtod(value, &end);
                if (end != value && *end == '\0') {
                    return CellValue(num);
                }
            }
            return CellValue(std::string(value));

        case XLSX_CELL_TYPE_STRING:
        case XLSX_CELL_TYPE_EMPTY:
        default:
            return CellValue(std::string(value));
    }
}

XLSXReadResult XLSXReader::readFile(const std::string& path) {
    reset();
    XLSXReadResult result;

    // Parse XLSX using excelize (Go)
    char* error = nullptr;
    XLSXData* xlData = ExcelizeParseXLSX(path.c_str(), &error);

    if (error != nullptr) {
        result.error = XLSXReadError(std::string("Failed to read XLSX file: ") + error);
        XLSXErrorFree(error);
        return result;
    }

    if (xlData == nullptr) {
        result.error = XLSXReadError("Failed to read XLSX file: unknown error");
        return result;
    }

    // Create workbook with generated ID
    auto workbook = std::make_unique<Workbook>(generate_id(), "Imported");

    // Process each sheet
    for (int sheetIdx = 0; sheetIdx < xlData->sheet_count; ++sheetIdx) {
        const XLSXSheet& xlSheet = xlData->sheets[sheetIdx];
        std::string sheetName(xlSheet.name);

        // Filter sheets if specific sheet requested
        if (!options_.sheetName.empty() && sheetName != options_.sheetName) {
            continue;
        }

        // Create our Sheet
        auto sheet = std::make_unique<Sheet>(generate_id(), sheetName);

        // Skip empty sheets
        if (xlSheet.row_count == 0 || xlSheet.col_count == 0) {
            workbook->addSheet(std::move(sheet));
            continue;
        }

        // Create columns and rows
        std::vector<ID> columnIds;
        std::vector<ID> rowIds;

        for (int c = 0; c < xlSheet.col_count; ++c) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(c);
            col->size = 64;  // Default width

            // Apply column dimensions if available
            if (options_.readDimensions && xlSheet.col_dims != nullptr) {
                for (int d = 0; d < xlSheet.col_dim_count; ++d) {
                    if (xlSheet.col_dims[d].col == c) {
                        // Convert Excel width units to pixels (approximately 7 pixels per
                        // character)
                        col->size = static_cast<uint32_t>(xlSheet.col_dims[d].width * 7.0);
                        break;
                    }
                }
            }

            columnIds.push_back(col->id);
            sheet->addColumn(std::move(col));
        }

        for (int r = 0; r < xlSheet.row_count; ++r) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(r);
            row->size = 20;  // Default height in pixels

            // Apply row dimensions if available
            if (options_.readDimensions && xlSheet.row_dims != nullptr) {
                for (int d = 0; d < xlSheet.row_dim_count; ++d) {
                    if (xlSheet.row_dims[d].row == r) {
                        // Convert points to pixels (1 point = 1.333 pixels at 96 DPI)
                        row->size = static_cast<uint32_t>(xlSheet.row_dims[d].height * 1.333);
                        break;
                    }
                }
            }

            rowIds.push_back(row->id);
            sheet->addRow(std::move(row));
        }

        // Read cells
        for (int cellIdx = 0; cellIdx < xlSheet.cell_count; ++cellIdx) {
            const XLSXCell& xlCell = xlSheet.cells[cellIdx];

            // Validate indices
            if (xlCell.row < 0 || xlCell.row >= xlSheet.row_count || xlCell.col < 0 ||
                xlCell.col >= xlSheet.col_count) {
                addWarning("Cell at invalid position (" + std::to_string(xlCell.row) + ", " +
                           std::to_string(xlCell.col) + "), skipping");
                continue;
            }

            // Create cell
            auto cell =
                std::make_unique<Cell>(generate_id(), columnIds[xlCell.col], rowIds[xlCell.row]);

            // Parse cell value
            cell->value = parseCellValue(xlCell);

            // Read formula if present and requested
            if (options_.readFormulas && xlCell.formula != nullptr && xlCell.formula[0] != '\0') {
                std::string formulaText(xlCell.formula);
                if (options_.readFormulaText) {
                    // Store formula with leading '='
                    cell->setFormula(new Formula(("=" + formulaText).c_str()));
                } else {
                    // Just mark as formula without text
                    cell->setFormula(new Formula("="));
                }
            }

            sheet->addCell(std::move(cell));
        }

        workbook->addSheet(std::move(sheet));
    }

    // Check if requested sheet was found
    if (!options_.sheetName.empty() && workbook->sheets.empty()) {
        XLSXDataFree(xlData);
        result.error = XLSXReadError("Sheet \"" + options_.sheetName + "\" not found");
        return result;
    }

    // Clean up excelize data
    XLSXDataFree(xlData);

    result.workbook = std::move(workbook);
    result.warnings = std::move(warnings_);
    return result;
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
