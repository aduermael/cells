// =============================================================================
// WASM Bindings - Viewport Operations
// =============================================================================
//
// Implementation of viewport-related CellsEngine methods:
// - queryViewport: Query cells in visible area
// - getColumnPixelOffset/getRowPixelOffset: Pixel coordinate lookups
// - getTotalWidth/getTotalHeight: Total dimensions
//
// The ViewportIndex provides O(log n) spatial queries using an Order Statistic
// Tree, bridging between pixel coordinates and the sparse UUID-based model.
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "core/cells/formula_eval.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/number_formatter.h"

namespace cells::wasm {

std::string CellsEngine::queryViewport(uint32_t col1, uint32_t row1, uint32_t col2, uint32_t row2) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"No sheet available\"}";
    }

    // Convert position range to pixel range for ViewportIndex query
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

    // Get pixel end of last column
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

        // Include formatId if cell has a format
        if (!entry.cell->formatId.isNull()) {
            json << "\"formatId\":\"" << entry.cell->formatId.toString() << "\",";
        }

        if (entry.cell->isFormula()) {
            json << "\"type\":\"f\",";
            Formula* formula = entry.cell->getFormula();
            std::string a1Formula;
            if (formula != nullptr && formula->ast != nullptr) {
                const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                a1Formula = _refConverter.formulaToA1(uuidFormula);
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
                if (!entry.cell->formatId.isNull()) {
                    FormattedValue formatted = formatNumber(
                        _formatRegistry, _workbook->getCustomFormats(), num, entry.cell->formatId);
                    if (!formatted.isError) {
                        displayValue = formatted.text;
                    } else {
                        std::ostringstream numStr;
                        numStr << std::setprecision(15) << num;
                        displayValue = numStr.str();
                    }
                } else if (std::floor(num) == num && std::abs(num) < 1e15) {
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
                displayValue = "";
            }
            json << "\"display\":\"" << jsonEscape(displayValue) << "\"";
        } else {
            char typeChar = valueTypeToChar(entry.cell->value.type);
            json << "\"type\":\"" << typeChar << "\",";

            bool useFormattedValue = false;
            std::string displayValue;

            if (!entry.cell->formatId.isNull() &&
                (entry.cell->value.type == CellValueType::NUMBER)) {
                FormattedValue formatted =
                    formatNumber(_formatRegistry, _workbook->getCustomFormats(),
                                 entry.cell->value.asNumber(), entry.cell->formatId);
                if (!formatted.isError) {
                    displayValue = formatted.text;
                    useFormattedValue = true;
                }
            }

            if (useFormattedValue) {
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\",";
                json << "\"display\":\"" << jsonEscape(displayValue) << "\"";
            } else {
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            }
        }

        json << "}";
    }

    json << "],\"columns\":[";

    // Include column info for the viewport
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
            auto pixelOffset = _viewportIndex.columnToPixel(id);
            json << "\"pixelOffset\":" << (pixelOffset ? *pixelOffset : 0) << ",";
            json << "\"name\":\"" << jsonEscape(col->name) << "\"";
            json << "}";
        }
    }

    json << "],\"rows\":[";

    // Include row info for the viewport
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
            auto pixelOffset = _viewportIndex.rowToPixel(id);
            json << "\"pixelOffset\":" << (pixelOffset ? *pixelOffset : 0) << ",";
            json << "\"name\":\"" << jsonEscape(row->name) << "\"";
            json << "}";
        }
    }

    json << "]}";

    return json.str();
}

int32_t CellsEngine::getColumnPixelOffset(uint32_t position) {
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

int32_t CellsEngine::getRowPixelOffset(uint32_t position) {
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

uint32_t CellsEngine::getTotalWidth() {
    return _viewportIndex.totalWidth();
}

uint32_t CellsEngine::getTotalHeight() {
    return _viewportIndex.totalHeight();
}

}  // namespace cells::wasm
