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

#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/number_formatter.h"
#include "core/cells/range.h"

namespace cells::wasm {

// =============================================================================
// Helper: Effective Style Resolution
// =============================================================================
// Resolves the effective style for a cell following the hierarchy:
// 1. Cell's own style (highest priority)
// 2. Range styles (for ranges with RANGE_STYLE flag)
// 3. Column's default style
// 4. Row's default style
// 5. No style (null)

struct EffectiveStyleResult {
    ID styleId;                   // The resolved style ID (for single source)
    const CellStyle* style;       // The resolved style pointer (may be null)
    CellStyle mergedStyle;        // Combined style from multiple ranges (used when fromRange is true)
    bool hasMergedStyle;          // true if mergedStyle is valid
    bool fromCell;                // true if style came from cell itself
    bool fromRange;               // true if style came from a range
    bool fromColumn;              // true if style came from column default
    bool fromRow;                 // true if style came from row default
};

// Helper to merge two CellStyle objects. Properties from 'overlay' fill in
// properties not set (at default) in 'base'. This supports CSS-like cascading
// where multiple ranges can contribute different properties.
CellStyle mergeStyles(const CellStyle& base, const CellStyle& overlay) {
    CellStyle result = base;

    // Merge boolean properties (overlay wins if base is false/default)
    if (!result.bold && overlay.bold) result.bold = true;
    if (!result.italic && overlay.italic) result.italic = true;
    if (!result.underline && overlay.underline) result.underline = true;
    if (!result.wrapText && overlay.wrapText) result.wrapText = true;

    // Merge string properties (overlay wins if base is empty)
    if (result.bgColor.empty() && !overlay.bgColor.empty()) result.bgColor = overlay.bgColor;
    if (result.textColor.empty() && !overlay.textColor.empty()) result.textColor = overlay.textColor;
    if (result.fontFamily.empty() && !overlay.fontFamily.empty()) result.fontFamily = overlay.fontFamily;

    // Merge numeric properties (overlay wins if base is 0/default)
    if (result.fontSize == 0 && overlay.fontSize != 0) result.fontSize = overlay.fontSize;

    // Merge alignment (overlay wins if base is default)
    if (result.hAlign == TextAlign::GENERAL && overlay.hAlign != TextAlign::GENERAL) result.hAlign = overlay.hAlign;
    if (result.vAlign == VerticalAlign::BOTTOM && overlay.vAlign != VerticalAlign::BOTTOM) result.vAlign = overlay.vAlign;

    // Merge borders (each edge individually)
    if (!result.border.top.hasValue() && overlay.border.top.hasValue()) result.border.top = overlay.border.top;
    if (!result.border.right.hasValue() && overlay.border.right.hasValue()) result.border.right = overlay.border.right;
    if (!result.border.bottom.hasValue() && overlay.border.bottom.hasValue()) result.border.bottom = overlay.border.bottom;
    if (!result.border.left.hasValue() && overlay.border.left.hasValue()) result.border.left = overlay.border.left;

    return result;
}

EffectiveStyleResult getEffectiveStyle(const Cell& cell, const Sheet& sheet, const Workbook& workbook,
                                       uint32_t colPos, uint32_t rowPos) {
    EffectiveStyleResult result = {{}, nullptr, {}, false, false, false, false, false};
    CellStyle combinedStyle;
    bool hasAnyStyle = false;

    // Priority 1: Cell's own style (highest priority - start with this as base)
    // Note: We don't return early - we continue to merge lower-priority styles
    // to fill in any properties not explicitly set at the cell level.
    // Read style from workbook map (not from cell field)
    const ID cellStyleId = workbook.getStyleId(cell.id);
    if (!cellStyleId.isNull()) {
        const CellStyle* cellStyle = workbook.getStyle(cellStyleId);
        if (cellStyle != nullptr) {
            combinedStyle = *cellStyle;
            result.styleId = cellStyleId;
            result.fromCell = true;
            hasAnyStyle = true;
        }
    }

    // Priority 2: Range styles (for ranges with RANGE_STYLE flag)
    // Merge styles from all overlapping ranges to fill gaps from cell style
    // e.g., if cell has border and Range has bold, the cell gets both properties
    std::vector<Range*> styleRanges = sheet.getRangesAt(colPos, rowPos, RangeFlags::STYLE);
    for (Range* range : styleRanges) {
        // Get content-addressed StyleBuffer from Range
        const StyleBuffer* styleBuffer = range->getStyle();
        if (styleBuffer == nullptr) {
            continue;
        }

        // Convert StyleBuffer to CellStyle for merging
        CellStyle rangeStyle = styleBuffer->toCellStyle();

        if (!hasAnyStyle) {
            // First style found - use it as base
            combinedStyle = rangeStyle;
            hasAnyStyle = true;
        } else {
            // Merge range's style into combined style (fills gaps)
            combinedStyle = mergeStyles(combinedStyle, rangeStyle);
        }
        result.fromRange = true;
    }

    // Priority 3: Column's default style (fills remaining gaps)
    const Axis* col = sheet.getColumn(cell.colId);
    if (col != nullptr && col->hasStyle()) {
        const ID colStyleId = workbook.getStyleId(col->id);
        if (!colStyleId.isNull()) {
            const CellStyle* colStyle = workbook.getStyle(colStyleId);
            if (colStyle != nullptr) {
                if (!hasAnyStyle) {
                    combinedStyle = *colStyle;
                    result.styleId = colStyleId;
                    hasAnyStyle = true;
                } else {
                    combinedStyle = mergeStyles(combinedStyle, *colStyle);
                }
                result.fromColumn = true;
            }
        }
    }

    // Priority 4: Row's default style (fills remaining gaps)
    const Axis* row = sheet.getRow(cell.rowId);
    if (row != nullptr && row->hasStyle()) {
        const ID rowStyleId = workbook.getStyleId(row->id);
        if (!rowStyleId.isNull()) {
            const CellStyle* rowStyle = workbook.getStyle(rowStyleId);
            if (rowStyle != nullptr) {
                if (!hasAnyStyle) {
                    combinedStyle = *rowStyle;
                    result.styleId = rowStyleId;
                    hasAnyStyle = true;
                } else {
                    combinedStyle = mergeStyles(combinedStyle, *rowStyle);
                }
                result.fromRow = true;
            }
        }
    }

    // Return result with merged style if we found any styles
    if (hasAnyStyle) {
        result.mergedStyle = combinedStyle;
        result.hasMergedStyle = true;
    }

    return result;
}

// Helper: Convert BorderStyle enum to JSON string value
const char* borderStyleToString(cells::BorderStyle style) {
    switch (style) {
        case cells::BorderStyle::NONE: return "none";
        case cells::BorderStyle::THIN: return "thin";
        case cells::BorderStyle::MEDIUM: return "medium";
        case cells::BorderStyle::THICK: return "thick";
        case cells::BorderStyle::DASHED: return "dashed";
        case cells::BorderStyle::DOTTED: return "dotted";
        case cells::BorderStyle::DOUBLE: return "double";
        case cells::BorderStyle::HAIR: return "hair";
        case cells::BorderStyle::MEDIUM_DASHED: return "mediumDashed";
        case cells::BorderStyle::DASH_DOT: return "dashDot";
        case cells::BorderStyle::MEDIUM_DASH_DOT: return "mediumDashDot";
        case cells::BorderStyle::DASH_DOT_DOT: return "dashDotDot";
        case cells::BorderStyle::MEDIUM_DASH_DOT_DOT: return "mediumDashDotDot";
        case cells::BorderStyle::SLANT_DASH_DOT: return "slantDashDot";
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
        const Axis* col = sheet->getColumn(entry.cell->colId);
        if (col) {
            colPos = col->position;
        }
        const Axis* row = sheet->getRow(entry.cell->rowId);
        if (row) {
            rowPos = row->position;
        }

        json << "{";
        json << "\"id\":\"" << entry.cell->id.toString() << "\",";
        json << "\"col\":" << colPos << ",";
        json << "\"row\":" << rowPos << ",";

        // Include formatId if cell has a format (read from workbook map)
        const ID cellFormatId = _workbook->getFormatId(entry.cell->id);
        if (!cellFormatId.isNull()) {
            json << "\"formatId\":\"" << cellFormatId.toString() << "\",";
        }

        // Include effective style (resolves cell > range > column > row hierarchy)
        EffectiveStyleResult effectiveStyle = getEffectiveStyle(*entry.cell, *sheet, *_workbook, colPos, rowPos);
        // Get style pointer: use merged style if available, otherwise use the single style pointer
        const CellStyle* style = effectiveStyle.hasMergedStyle
            ? &effectiveStyle.mergedStyle
            : effectiveStyle.style;
        if (!effectiveStyle.styleId.isNull() && style != nullptr) {
            json << "\"styleId\":\"" << effectiveStyle.styleId.toString() << "\",";
            // Include inline style properties for efficient rendering
            json << "\"style\":{";
            json << "\"bold\":" << (style->bold ? "true" : "false");
            json << ",\"italic\":" << (style->italic ? "true" : "false");
            json << ",\"underline\":" << (style->underline ? "true" : "false");
            json << ",\"wrapText\":" << (style->wrapText ? "true" : "false");
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
            // Alignment - serialize enum values (only if not default GENERAL)
            switch (style->hAlign) {
                case TextAlign::GENERAL:
                    // Don't serialize - GENERAL is the default (content-type-aware)
                    break;
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
            // Indicate if style is inherited (from range, column, or row)
            if (effectiveStyle.fromRange) {
                json << ",\"inheritedFrom\":\"range\"";
            } else if (effectiveStyle.fromColumn) {
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
                    // Use FormulaDisplayConverter for context-aware display
                    FormulaDisplayConverter displayConverter(*sheet, _workbook.get());
                    const std::string a1Formula = displayConverter.toDisplayString(masterFormula->ast);
                    json << "\"masterFormula\":\"" << jsonEscape(a1Formula) << "\",";
                }
            }
        }

        // Check if this cell is a spill master (has a spill range)
        const SpillInfo* spillInfo = sheet->getSpillInfo(entry.cell->id);
        if (spillInfo != nullptr && !spillInfo->spilledPositions.empty()) {
            json << "\"isSpillMaster\":true,";
        }

        // Check if this cell is part of a merged cell region using Range system
        std::vector<Range*> mergeRanges = sheet->getRangesAt(colPos, rowPos, RangeFlags::MERGE);
        if (!mergeRanges.empty()) {
            // Use the first merge range found (typically only one merge per cell)
            Range* mergeRange = mergeRanges[0];

            // Get corner positions for the merge range
            const Axis* startCol = sheet->getColumn(mergeRange->startColId);
            const Axis* startRow = sheet->getRow(mergeRange->startRowId);
            const Axis* endCol = sheet->getColumn(mergeRange->endColId);
            const Axis* endRow = sheet->getRow(mergeRange->endRowId);

            if (startCol && startRow && endCol && endRow) {
                uint32_t anchorColPos = startCol->position;
                uint32_t anchorRowPos = startRow->position;
                uint32_t colSpan = endCol->position - startCol->position + 1;
                uint32_t rowSpan = endRow->position - startRow->position + 1;

                // Check if this cell is the anchor (top-left)
                if (colPos == anchorColPos && rowPos == anchorRowPos) {
                    json << "\"isMergeAnchor\":true,";
                    json << "\"mergeColSpan\":" << colSpan << ",";
                    json << "\"mergeRowSpan\":" << rowSpan << ",";
                } else {
                    json << "\"isMergedCell\":true,";
                    // Include anchor position and span for non-anchor merged cells
                    // so the editor can position correctly
                    json << "\"mergeAnchorCol\":" << anchorColPos << ",";
                    json << "\"mergeAnchorRow\":" << anchorRowPos << ",";
                    json << "\"mergeColSpan\":" << colSpan << ",";
                    json << "\"mergeRowSpan\":" << rowSpan << ",";
                }
            }
        }

        if (entry.cell->isFormula()) {
            json << "\"type\":\"f\",";
            Formula* formula = entry.cell->getFormula();
            std::string a1Formula;
            if (formula != nullptr && formula->ast != nullptr) {
                // Use FormulaDisplayConverter for context-aware display
                // This handles cross-sheet references correctly
                FormulaDisplayConverter displayConverter(*sheet, _workbook.get());
                a1Formula = displayConverter.toDisplayString(formula->ast);
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
                // Compute edit value for formula results (using cellFormatId from workbook map)
                editValue = formatEditValue(_formatRegistry, num, cellFormatId);
                if (!cellFormatId.isNull()) {
                    FormattedValue formatted = formatNumber(
                        _formatRegistry, _workbook->getCustomFormats(), num, cellFormatId);
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

            if (!cellFormatId.isNull() &&
                (entry.cell->value.type == CellValueType::NUMBER)) {
                const double numValue = entry.cell->value.asNumber();
                FormattedValue formatted =
                    formatNumber(_formatRegistry, _workbook->getCustomFormats(),
                                 numValue, cellFormatId);
                if (!formatted.isError) {
                    displayValue = formatted.text;
                    useFormattedValue = true;
                }
                // Compute edit value for formatted numbers (using cellFormatId from workbook map)
                editValue = formatEditValue(_formatRegistry, numValue, cellFormatId);
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
    for (const auto& cellId : sheet->getCellIds()) {
        Cell* cell = _workbook->getCell(cellId);
        if (!cell) continue;

        const SpillInfo* spillInfo = sheet->getSpillInfo(cell->id);
        if (spillInfo == nullptr || spillInfo->spilledPositions.empty()) {
            continue;
        }

        // Get the master cell's formula for display (grayed out in spilled cells)
        std::string masterFormula;
        Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            // Use FormulaDisplayConverter for context-aware display
            FormulaDisplayConverter displayConverter(*sheet, _workbook.get());
            masterFormula = displayConverter.toDisplayString(formula->ast);
        }

        // Check each spilled position
        for (size_t i = 0; i < spillInfo->spilledPositions.size(); ++i) {
            const auto& [colId, rowId] = spillInfo->spilledPositions[i];

            // Skip if there's an actual cell at this position (already handled above)
            if (actualCellPositions.count({colId, rowId}) > 0) {
                continue;
            }

            // Check if position is within the viewport
            const Axis* col = sheet->getColumn(colId);
            const Axis* row = sheet->getRow(rowId);
            if (!col || !row) {
                continue;
            }

            uint32_t colPos = col->position;
            uint32_t rowPos = row->position;

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
    for (const ID& id : sheet->getColumnIds()) {
        const Axis* col = sheet->getColumn(id);
        if (!col) {
            continue;
        }
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
            json << "\"hidden\":" << (col->hidden() ? "true" : "false");
            json << "}";
        }
    }

    json << "],\"rows\":[";

    // Include row info for the viewport
    bool firstRow = true;
    for (const ID& id : sheet->getRowIds()) {
        const Axis* row = sheet->getRow(id);
        if (!row) {
            continue;
        }
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
            json << "\"hidden\":" << (row->hidden() ? "true" : "false");
            json << "}";
        }
    }

    json << "],\"styleRanges\":[";

    // Include style ranges that overlap with the viewport
    // These allow the frontend to render backgrounds for empty cells
    bool firstRange = true;
    for (const ID& rangeId : sheet->getRangeIds()) {
        const Range* range = sheet->getRange(rangeId);
        if (range == nullptr) {
            continue;
        }

        // Only include RANGE_STYLE ranges
        if (!range->hasFlag(RangeFlags::STYLE)) {
            continue;
        }

        // Get the style from the range's StyleBuffer
        const StyleBuffer* styleBuffer = range->getStyle();
        if (styleBuffer == nullptr) {
            continue;
        }
        CellStyle style = styleBuffer->toCellStyle();

        // Get position bounds from corner IDs
        const Axis* startCol = sheet->getColumn(range->startColId);
        const Axis* startRow = sheet->getRow(range->startRowId);
        const Axis* endCol = sheet->getColumn(range->endColId);
        const Axis* endRow = sheet->getRow(range->endRowId);

        if (!startCol || !startRow || !endCol || !endRow) {
            continue;
        }

        uint32_t rangeCol1 = std::min(startCol->position, endCol->position);
        uint32_t rangeCol2 = std::max(startCol->position, endCol->position);
        uint32_t rangeRow1 = std::min(startRow->position, endRow->position);
        uint32_t rangeRow2 = std::max(startRow->position, endRow->position);

        // Check if range overlaps with viewport
        if (rangeCol2 < col1 || rangeCol1 >= col2 || rangeRow2 < row1 || rangeRow1 >= row2) {
            continue;
        }

        if (!firstRange) {
            json << ",";
        }
        firstRange = false;

        json << "{";
        json << "\"startCol\":" << rangeCol1 << ",";
        json << "\"startRow\":" << rangeRow1 << ",";
        json << "\"endCol\":" << rangeCol2 << ",";
        json << "\"endRow\":" << rangeRow2 << ",";
        json << "\"style\":{";
        bool firstProp = true;
        // Add all non-default style properties
        if (!style.bgColor.empty()) {
            json << "\"bgColor\":\"" << style.bgColor << "\"";
            firstProp = false;
        }
        if (!style.textColor.empty()) {
            if (!firstProp) json << ",";
            json << "\"textColor\":\"" << style.textColor << "\"";
            firstProp = false;
        }
        if (style.bold) {
            if (!firstProp) json << ",";
            json << "\"bold\":true";
            firstProp = false;
        }
        if (style.italic) {
            if (!firstProp) json << ",";
            json << "\"italic\":true";
            firstProp = false;
        }
        if (style.underline) {
            if (!firstProp) json << ",";
            json << "\"underline\":true";
            firstProp = false;
        }
        if (style.wrapText) {
            if (!firstProp) json << ",";
            json << "\"wrapText\":true";
            firstProp = false;
        }
        if (!style.fontFamily.empty()) {
            if (!firstProp) json << ",";
            json << "\"fontFamily\":\"" << style.fontFamily << "\"";
            firstProp = false;
        }
        if (style.fontSize != 0) {
            if (!firstProp) json << ",";
            json << "\"fontSize\":" << static_cast<int>(style.fontSize);
            firstProp = false;
        }
        json << "}}";
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
    Axis* masterColAxis = sheet->getColumn(masterCell->colId);
    Axis* masterRowAxis = sheet->getRow(masterCell->rowId);
    if (masterColAxis != nullptr) {
        masterCol = masterColAxis->position;
    }
    if (masterRowAxis != nullptr) {
        masterRow = masterRowAxis->position;
    }

    // Calculate the bounding box of the spill range
    uint32_t minCol = masterCol;
    uint32_t maxCol = masterCol;
    uint32_t minRow = masterRow;
    uint32_t maxRow = masterRow;

    for (const auto& [spillColId, spillRowId] : spillInfo->spilledPositions) {
        Axis* spillColAxis = sheet->getColumn(spillColId);
        Axis* spillRowAxis = sheet->getRow(spillRowId);
        if (spillColAxis != nullptr && spillRowAxis != nullptr) {
            uint32_t spillCol = spillColAxis->position;
            uint32_t spillRow = spillRowAxis->position;
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
