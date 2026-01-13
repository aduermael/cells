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
#include <set>
#include <sstream>

#include "core/cells/formula_eval.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/number_formatter.h"

namespace cells::wasm {

// =============================================================================
// Helper: Effective Style Resolution
// =============================================================================
// Resolves the effective style for a cell following the hierarchy:
// 1. Cell's own style (highest priority)
// 2. Column's default style
// 3. Row's default style
// 4. No style (null)

struct EffectiveStyleResult {
    ID styleId;                   // The resolved style ID
    const CellStyle* style;       // The resolved style pointer (may be null)
    bool fromCell;                // true if style came from cell itself
    bool fromColumn;              // true if style came from column default
    bool fromRow;                 // true if style came from row default
};

EffectiveStyleResult getEffectiveStyle(const Cell& cell, const Sheet& sheet, const Workbook& workbook) {
    EffectiveStyleResult result = {{}, nullptr, false, false, false};

    // Priority 1: Cell's own style
    if (!cell.styleId.isNull()) {
        result.styleId = cell.styleId;
        result.style = workbook.getStyle(cell.styleId);
        result.fromCell = true;
        return result;
    }

    // Priority 2: Column's default style
    const Axis* col = sheet.columns.count(cell.colId) > 0
        ? sheet.columns.at(cell.colId).get()
        : nullptr;
    if (col && !col->defaultStyleId.isNull()) {
        result.styleId = col->defaultStyleId;
        result.style = workbook.getStyle(col->defaultStyleId);
        result.fromColumn = true;
        return result;
    }

    // Priority 3: Row's default style
    const Axis* row = sheet.rows.count(cell.rowId) > 0
        ? sheet.rows.at(cell.rowId).get()
        : nullptr;
    if (row && !row->defaultStyleId.isNull()) {
        result.styleId = row->defaultStyleId;
        result.style = workbook.getStyle(row->defaultStyleId);
        result.fromRow = true;
        return result;
    }

    // No style found
    return result;
}

// Helper: Convert BorderStyle enum to JSON string value
const char* borderStyleToString(BorderStyle style) {
    switch (style) {
        case BorderStyle::NONE: return "none";
        case BorderStyle::THIN: return "thin";
        case BorderStyle::MEDIUM: return "medium";
        case BorderStyle::THICK: return "thick";
        case BorderStyle::DASHED: return "dashed";
        case BorderStyle::DOTTED: return "dotted";
        case BorderStyle::DOUBLE: return "double";
        case BorderStyle::HAIR: return "hair";
        case BorderStyle::MEDIUM_DASHED: return "mediumDashed";
        case BorderStyle::DASH_DOT: return "dashDot";
        case BorderStyle::MEDIUM_DASH_DOT: return "mediumDashDot";
        case BorderStyle::DASH_DOT_DOT: return "dashDotDot";
        case BorderStyle::MEDIUM_DASH_DOT_DOT: return "mediumDashDotDot";
        case BorderStyle::SLANT_DASH_DOT: return "slantDashDot";
        default: return "none";
    }
}

// Helper: Serialize a single border edge to JSON
void serializeBorderEdge(std::ostringstream& json, const char* name, const BorderEdge& edge) {
    json << "\"" << name << "\":{\"style\":\"" << borderStyleToString(edge.style) << "\"";
    if (!edge.color.empty()) {
        json << ",\"color\":\"" << edge.color << "\"";
    }
    json << "}";
}

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

        // Include effective style (resolves cell > column > row hierarchy)
        EffectiveStyleResult effectiveStyle = getEffectiveStyle(*entry.cell, *sheet, *_workbook);
        if (!effectiveStyle.styleId.isNull() && effectiveStyle.style != nullptr) {
            json << "\"styleId\":\"" << effectiveStyle.styleId.toString() << "\",";
            // Include inline style properties for efficient rendering
            const CellStyle* style = effectiveStyle.style;
            json << "\"style\":{";
            json << "\"bold\":" << (style->bold ? "true" : "false");
            json << ",\"italic\":" << (style->italic ? "true" : "false");
            json << ",\"underline\":" << (style->underline ? "true" : "false");
            if (!style->bgColor.empty()) {
                json << ",\"bgColor\":\"" << style->bgColor << "\"";
            }
            if (!style->textColor.empty()) {
                json << ",\"textColor\":\"" << style->textColor << "\"";
            }
            if (!style->fontFamily.empty()) {
                json << ",\"fontFamily\":\"" << style->fontFamily << "\"";
            }
            if (style->fontSize > 0) {
                json << ",\"fontSize\":" << static_cast<int>(style->fontSize);
            }
            // Alignment - serialize enum values
            switch (style->hAlign) {
                case TextAlign::LEFT: json << ",\"hAlign\":\"left\""; break;
                case TextAlign::CENTER: json << ",\"hAlign\":\"center\""; break;
                case TextAlign::RIGHT: json << ",\"hAlign\":\"right\""; break;
                case TextAlign::JUSTIFY: json << ",\"hAlign\":\"justify\""; break;
            }
            switch (style->vAlign) {
                case VerticalAlign::TOP: json << ",\"vAlign\":\"top\""; break;
                case VerticalAlign::MIDDLE: json << ",\"vAlign\":\"middle\""; break;
                case VerticalAlign::BOTTOM: json << ",\"vAlign\":\"bottom\""; break;
            }
            // Include border if any edge has a non-NONE style
            if (style->border.hasValue()) {
                json << ",\"border\":{";
                serializeBorderEdge(json, "top", style->border.top);
                json << ",";
                serializeBorderEdge(json, "right", style->border.right);
                json << ",";
                serializeBorderEdge(json, "bottom", style->border.bottom);
                json << ",";
                serializeBorderEdge(json, "left", style->border.left);
                json << "}";
            }
            // Indicate if style is inherited from axis (column or row)
            if (effectiveStyle.fromColumn) {
                json << ",\"inheritedFrom\":\"column\"";
            } else if (effectiveStyle.fromRow) {
                json << ",\"inheritedFrom\":\"row\"";
            }
            json << "},";
        }

        // Check if this cell is part of a spill range
        ID spillMaster = sheet->getSpillMaster(entry.cell->colId, entry.cell->rowId);
        bool isSpilledCell = !spillMaster.isNull();
        if (isSpilledCell) {
            json << "\"isSpilled\":true,";
            json << "\"spillMasterId\":\"" << spillMaster.toString() << "\",";
            // Include master formula for display in formula bar (grayed out)
            Cell* masterCell = sheet->getCell(spillMaster);
            if (masterCell != nullptr) {
                Formula* masterFormula = masterCell->getFormula();
                if (masterFormula != nullptr && masterFormula->ast != nullptr) {
                    const std::string uuidFormula =
                        FormulaSerializer::serialize(masterFormula->ast);
                    const std::string a1Formula = _refConverter.formulaToA1(uuidFormula);
                    json << "\"masterFormula\":\"" << jsonEscape(a1Formula) << "\",";
                }
            }
        }

        // Check if this cell is a spill master (has a spill range)
        const SpillInfo* spillInfo = sheet->getSpillInfo(entry.cell->id);
        if (spillInfo != nullptr && !spillInfo->spilledPositions.empty()) {
            json << "\"isSpillMaster\":true,";
        }

        // Check if this cell is part of a merged cell region
        const MergeRange* mergeRange = sheet->getMergeRange(entry.cell->colId, entry.cell->rowId);
        if (mergeRange != nullptr) {
            if (sheet->isMergeAnchor(entry.cell->colId, entry.cell->rowId)) {
                json << "\"isMergeAnchor\":true,";
                json << "\"mergeColSpan\":" << mergeRange->colSpan << ",";
                json << "\"mergeRowSpan\":" << mergeRange->rowSpan << ",";
            } else {
                json << "\"isMergedCell\":true,";
            }
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
            std::string editValue;
            if (result.isError()) {
                displayValue = errorToString(result.getError());
                editValue = displayValue;
                json << "\"isError\":true,";
            } else if (result.isNumber()) {
                const double num = result.getNumber();
                // Compute edit value for formula results
                editValue = formatEditValue(_formatRegistry, num, entry.cell->formatId);
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
                editValue = displayValue;
            } else if (result.isBoolean()) {
                displayValue = result.getBoolean() ? "TRUE" : "FALSE";
                editValue = displayValue;
            } else {
                displayValue = "";
                editValue = "";
            }
            json << "\"display\":\"" << jsonEscape(displayValue) << "\",";
            json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
        } else {
            char typeChar = valueTypeToChar(entry.cell->value.type);
            json << "\"type\":\"" << typeChar << "\",";

            bool useFormattedValue = false;
            std::string displayValue;
            std::string editValue;

            if (!entry.cell->formatId.isNull() &&
                (entry.cell->value.type == CellValueType::NUMBER)) {
                const double numValue = entry.cell->value.asNumber();
                FormattedValue formatted =
                    formatNumber(_formatRegistry, _workbook->getCustomFormats(),
                                 numValue, entry.cell->formatId);
                if (!formatted.isError) {
                    displayValue = formatted.text;
                    useFormattedValue = true;
                }
                // Compute edit value for formatted numbers
                editValue = formatEditValue(_formatRegistry, numValue, entry.cell->formatId);
            }

            if (useFormattedValue) {
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\",";
                json << "\"display\":\"" << jsonEscape(displayValue) << "\",";
                json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
            } else {
                json << "\"value\":\"" << jsonEscape(entry.cell->value.raw) << "\"";
            }
        }

        json << "}";
    }

    // Also include "virtual" spilled cells - positions with spilled values but no actual cell
    // These need to be rendered but aren't in the cells map
    // We collect positions that are spilled AND don't have an actual cell
    std::set<std::pair<ID, ID>> actualCellPositions;
    for (const auto& entry : entries) {
        actualCellPositions.insert({entry.cell->colId, entry.cell->rowId});
    }

    // Iterate through all spill masters and check if any spilled positions are in viewport
    for (const auto& [cellId, cell] : sheet->cells) {
        const SpillInfo* spillInfo = sheet->getSpillInfo(cell->id);
        if (spillInfo == nullptr || spillInfo->spilledPositions.empty()) {
            continue;
        }

        // Get the master cell's formula for display (grayed out in spilled cells)
        std::string masterFormula;
        Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
            masterFormula = _refConverter.formulaToA1(uuidFormula);
        }

        // Check each spilled position
        for (size_t i = 0; i < spillInfo->spilledPositions.size(); ++i) {
            const auto& [colId, rowId] = spillInfo->spilledPositions[i];

            // Skip if there's an actual cell at this position (already handled above)
            if (actualCellPositions.count({colId, rowId}) > 0) {
                continue;
            }

            // Check if position is within the viewport
            auto colIt = sheet->columns.find(colId);
            auto rowIt = sheet->rows.find(rowId);
            if (colIt == sheet->columns.end() || rowIt == sheet->rows.end()) {
                continue;
            }

            uint32_t colPos = colIt->second->position;
            uint32_t rowPos = rowIt->second->position;

            if (colPos < col1 || colPos >= col2 || rowPos < row1 || rowPos >= row2) {
                continue;
            }

            // This is a virtual spilled cell in the viewport
            if (!firstCell) {
                json << ",";
            }
            firstCell = false;

            json << "{";
            // Virtual spilled cells don't have their own ID - use a generated one
            json << "\"col\":" << colPos << ",";
            json << "\"row\":" << rowPos << ",";
            json << "\"isSpilled\":true,";
            json << "\"spillMasterId\":\"" << cell->id.toString() << "\",";
            if (!masterFormula.empty()) {
                json << "\"masterFormula\":\"" << jsonEscape(masterFormula) << "\",";
            }
            json << "\"type\":\"s\",";  // Spilled value type

            // Get the spilled value
            if (i < spillInfo->spilledValues.size()) {
                const CellValue& val = spillInfo->spilledValues[i];
                std::string displayValue;
                if (val.type == CellValueType::NUMBER) {
                    double num = val.asNumber();
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
                } else if (val.type == CellValueType::STRING) {
                    displayValue = val.raw;
                } else if (val.type == CellValueType::BOOLEAN) {
                    displayValue = val.asBoolean() ? "TRUE" : "FALSE";
                } else if (val.type == CellValueType::ERROR) {
                    displayValue = errorToString(val.error);
                    json << "\"isError\":true,";
                }
                json << "\"display\":\"" << jsonEscape(displayValue) << "\",";
                json << "\"value\":\"" << jsonEscape(val.raw) << "\"";
            } else {
                json << "\"display\":\"\",";
                json << "\"value\":\"\"";
            }

            json << "}";
        }
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
            json << "\"name\":\"" << jsonEscape(col->name) << "\",";
            json << "\"hidden\":" << (col->hidden ? "true" : "false");
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
            json << "\"name\":\"" << jsonEscape(row->name) << "\",";
            json << "\"hidden\":" << (row->hidden ? "true" : "false");
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

std::string CellsEngine::getSpillRangeAt(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    // Get column and row IDs at the given positions
    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (!colAxis || !rowAxis) {
        return "{}";
    }

    const ID& colId = colAxis->id;
    const ID& rowId = rowAxis->id;

    // First check if there's a cell at this position that is a spill master
    Cell* cell = sheet->getCellAt(colId, rowId);
    const SpillInfo* spillInfo = nullptr;
    ID masterCellId;

    if (cell) {
        spillInfo = sheet->getSpillInfo(cell->id);
        if (spillInfo && !spillInfo->spilledPositions.empty()) {
            masterCellId = cell->id;
        }
    }

    // If not a master, check if this position is spilled into
    if (spillInfo == nullptr || spillInfo->spilledPositions.empty()) {
        masterCellId = sheet->getSpillMaster(colId, rowId);
        if (!masterCellId.isNull()) {
            spillInfo = sheet->getSpillInfo(masterCellId);
        }
    }

    // Not part of any spill range
    if (spillInfo == nullptr || spillInfo->spilledPositions.empty()) {
        return "{}";
    }

    // Get master cell position
    Cell* masterCell = sheet->getCell(masterCellId);
    if (!masterCell) {
        return "{}";
    }

    uint32_t masterCol = 0;
    uint32_t masterRow = 0;
    auto masterColIt = sheet->columns.find(masterCell->colId);
    auto masterRowIt = sheet->rows.find(masterCell->rowId);
    if (masterColIt != sheet->columns.end()) {
        masterCol = masterColIt->second->position;
    }
    if (masterRowIt != sheet->rows.end()) {
        masterRow = masterRowIt->second->position;
    }

    // Calculate the bounding box of the spill range
    uint32_t minCol = masterCol;
    uint32_t maxCol = masterCol;
    uint32_t minRow = masterRow;
    uint32_t maxRow = masterRow;

    for (const auto& [spillColId, spillRowId] : spillInfo->spilledPositions) {
        auto spillColIt = sheet->columns.find(spillColId);
        auto spillRowIt = sheet->rows.find(spillRowId);
        if (spillColIt != sheet->columns.end() && spillRowIt != sheet->rows.end()) {
            uint32_t spillCol = spillColIt->second->position;
            uint32_t spillRow = spillRowIt->second->position;
            minCol = std::min(minCol, spillCol);
            maxCol = std::max(maxCol, spillCol);
            minRow = std::min(minRow, spillRow);
            maxRow = std::max(maxRow, spillRow);
        }
    }

    std::ostringstream json;
    json << "{";
    json << "\"masterId\":\"" << masterCellId.toString() << "\",";
    json << "\"masterCol\":" << masterCol << ",";
    json << "\"masterRow\":" << masterRow << ",";
    json << "\"minCol\":" << minCol << ",";
    json << "\"minRow\":" << minRow << ",";
    json << "\"maxCol\":" << maxCol << ",";
    json << "\"maxRow\":" << maxRow << ",";
    json << "\"spillCount\":" << spillInfo->spilledPositions.size();
    json << "}";

    return json.str();
}

}  // namespace cells::wasm
