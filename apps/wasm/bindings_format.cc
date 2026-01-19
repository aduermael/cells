// =============================================================================
// WASM Bindings - Number Format Operations
// =============================================================================
//
// Implementation of number formatting CellsEngine methods:
// - setCellFormat/setCellFormatAt: Apply formats to cells
// - getAvailableFormats: List all available formats
// - createCustomFormat: Create custom Excel-style formats
// - getFormulaFunctions: List available formula functions
// - getCellFormatId: Get a cell's format ID
// - parseUserInputValue: Parse user input with format detection
// - formatCellValue/formatWithCode/formatCellById: Format values
// - getFormatDetails/makeFormatId: Format ID utilities
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include "core/cells/crdt.h"
#include "core/cells/range.h"
#include "core/cells/range_index.h"
#include "core/cells/format_code_formatter.h"
#include "core/cells/format_code_parser.h"
#include "core/cells/formula_functions.h"
#include "core/cells/id.h"
#include "core/cells/input_parser.h"
#include "core/cells/number_formatter.h"
#include "core/cells/operation.h"
#include "core/cells/style_registry.h"

namespace cells::wasm {

std::string CellsEngine::setCellFormat(const std::string& cellIdStr,
                                        const std::string& formatIdStr) {
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

    auto* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    ID formatId;
    if (formatIdStr != "~" && !formatIdStr.empty()) {
        if (formatIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid format ID\"}";
        }
        formatId = ID(formatIdStr);
        if (!_formatRegistry.hasFormat(formatId) && !_workbook->hasCustomFormat(formatId)) {
            const ParsedFormatId parsed = parseFormatId(formatIdStr);
            if (!parsed.valid) {
                return "{\"error\":\"Format not found\"}";
            }
        }
    }

    std::string payload = "{\"format_id\":\"" + formatIdStr + "\"}";
    Operation op = makeCellSetFormatOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::setCellFormatAt(uint32_t col, uint32_t row,
                                          const std::string& formatIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    ID formatId;
    if (formatIdStr != "~" && !formatIdStr.empty()) {
        if (formatIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid format ID\"}";
        }
        formatId = ID(formatIdStr);
        if (!_formatRegistry.hasFormat(formatId) && !_workbook->hasCustomFormat(formatId)) {
            const ParsedFormatId parsed = parseFormatId(formatIdStr);
            if (!parsed.valid) {
                return "{\"error\":\"Format not found\"}";
            }
        }
    }

    // Find or create column at position
    ID colId;
    bool colCreated = false;
    Axis* colAxis = sheet->getColumnByPosition(col);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                 ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    // Find or create row at position
    ID rowId;
    bool rowCreated = false;
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                 ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Find or create cell at this position
    ID cellId;
    bool cellCreated = false;
    Cell* existingCell = sheet->getCellAt(colId, rowId);
    if (existingCell) {
        cellId = existingCell->id;
    }
    if (cellId.isNull()) {
        cellId = generate_id();
        cellCreated = true;
        std::string cellPayload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" +
                                  colId.toString() + "\",\"row_id\":\"" + rowId.toString() + "\"}";
        Operation cellOp = makeCellSetValueOp(*_workbook, cellId, cellPayload);
        applyOperation(*_workbook, cellOp);
    }

    std::string payload = "{\"format_id\":\"" + formatIdStr + "\"}";
    Operation op = makeCellSetFormatOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    if (cellCreated) {
        Cell* newCell = sheet->getCell(cellId);
        if (newCell) {
            _viewportIndex.onCellAdded(newCell);
        }
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::getAvailableFormats() {
    std::ostringstream ss;
    ss << "[";

    bool first = true;

    // Built-in formats from registry
    const auto& allFormats = _formatRegistry.getAllFormats();
    for (const auto& [id, format] : allFormats) {
        if (!first) {
            ss << ",";
        }
        first = false;

        ss << "{";
        ss << "\"id\":\"" << id.toString() << "\"";
        ss << ",\"category\":\"" << formatCategoryToString(format.category) << "\"";
        ss << ",\"formatCode\":\"" << jsonEscape(format.formatCode) << "\"";
        ss << ",\"decimalPlaces\":" << static_cast<int>(format.decimalPlaces);
        ss << ",\"useThousandsSeparator\":" << (format.useThousandsSeparator ? "true" : "false");
        ss << ",\"currencySymbol\":\"" << jsonEscape(format.currencySymbol) << "\"";
        ss << ",\"isAccounting\":" << (format.isAccounting ? "true" : "false");
        ss << ",\"isCustom\":false";
        ss << "}";
    }

    // Custom formats from workbook
    if (_workbook) {
        const auto& customFormats = _workbook->getCustomFormats();
        for (const auto& [formatId, formatCode] : customFormats) {
            if (!first) {
                ss << ",";
            }
            first = false;

            const ParsedFormatCode parsed = parseFormatCode(formatCode);
            NumberFormatCategory category = NumberFormatCategory::NUMBER;
            if (parsed.hasPercent) {
                category = NumberFormatCategory::PERCENTAGE;
            } else if (!parsed.currencySymbol.empty()) {
                category = NumberFormatCategory::CURRENCY;
            }

            ss << "{";
            ss << "\"id\":\"" << formatId.toString() << "\"";
            ss << ",\"category\":\"" << formatCategoryToString(category) << "\"";
            ss << ",\"formatCode\":\"" << jsonEscape(formatCode) << "\"";
            ss << ",\"decimalPlaces\":" << static_cast<int>(parsed.decimalPlaces);
            ss << ",\"useThousandsSeparator\":"
               << (parsed.hasThousandsSeparator ? "true" : "false");
            ss << ",\"currencySymbol\":\"" << jsonEscape(parsed.currencySymbol) << "\"";
            ss << ",\"isAccounting\":false";
            ss << ",\"isCustom\":true";
            ss << "}";
        }
    }

    ss << "]";
    return ss.str();
}

std::string CellsEngine::createCustomFormat(const std::string& formatCode) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    auto validationError = validateFormatCode(formatCode);
    if (validationError) {
        return "{\"error\":\"" + jsonEscape(*validationError) + "\"}";
    }

    const ID formatId = generate_id();

    const bool isNew = _workbook->registerCustomFormat(formatId, formatCode);
    if (!isNew) {
        return "{\"error\":\"Format ID collision\"}";
    }

    if (_workbook->isCollaborating()) {
        std::string payload = "{\"format_code\":\"" + jsonEscape(formatCode) + "\"}";
        Operation op = makeFormatDefineOp(*_workbook, formatId, payload);
        applyOperation(*_workbook, op);

        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }
    }

    return "{\"success\":true,\"formatId\":\"" + formatId.toString() + "\"}";
}

std::string CellsEngine::getFormulaFunctions() {
    std::ostringstream ss;
    ss << "[";

    const auto& functions = cells::FunctionRegistry::instance().getFunctionList();
    bool first = true;
    for (const auto& fn : functions) {
        if (!first) {
            ss << ",";
        }
        first = false;

        ss << "{";
        ss << "\"name\":\"" << jsonEscape(fn.name) << "\"";
        ss << ",\"signature\":\"" << jsonEscape(fn.signature) << "\"";
        ss << ",\"description\":\"" << jsonEscape(fn.description) << "\"";
        ss << ",\"category\":\"" << jsonEscape(fn.category) << "\"";
        ss << "}";
    }

    ss << "]";
    return ss.str();
}

std::string CellsEngine::getCellFormatId(const std::string& cellIdStr) {
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

    auto* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    const ID formatId = _workbook->getCellFormatId(cell->id);
    std::string formatIdStr = formatId.isNull() ? "~" : formatId.toString();
    return "{\"formatId\":\"" + formatIdStr + "\"}";
}

std::string CellsEngine::parseUserInputValue(const std::string& input) {
    ParsedInput result = parseUserInput(input);

    std::ostringstream ss;
    ss << "{";
    ss << "\"success\":" << (result.success ? "true" : "false");

    if (result.success) {
        if (result.valueType == CellValueType::NUMBER) {
            ss << ",\"type\":\"number\"";
            ss << ",\"numericValue\":" << std::setprecision(15) << result.numericValue;
        } else {
            ss << ",\"type\":\"string\"";
            ss << ",\"stringValue\":\"" << jsonEscape(result.stringValue) << "\"";
        }

        std::string formatIdStr = result.formatId.isNull() ? "~" : result.formatId.toString();
        ss << ",\"formatId\":\"" << formatIdStr << "\"";
        ss << ",\"category\":\"" << formatCategoryToString(result.formatCategory) << "\"";
    } else {
        ss << ",\"error\":\"" << jsonEscape(result.errorMessage) << "\"";
    }

    ss << "}";
    return ss.str();
}

std::string CellsEngine::formatCellValue(double value, const std::string& formatIdStr) {
    ID formatId;
    if (formatIdStr != "~" && !formatIdStr.empty()) {
        if (formatIdStr.size() != ID_LENGTH) {
            return "{\"error\":\"Invalid format ID\"}";
        }
        formatId = ID(formatIdStr);
    }

    const auto& customFormats =
        _workbook ? _workbook->getCustomFormats() : _emptyCustomFormats;
    FormattedValue result = formatNumber(_formatRegistry, customFormats, value, formatId);

    std::ostringstream ss;
    if (result.isError) {
        ss << "{\"error\":\"" << jsonEscape(result.errorMessage) << "\"}";
    } else {
        ss << "{\"text\":\"" << jsonEscape(result.text) << "\"}";
    }
    return ss.str();
}

std::string CellsEngine::formatWithCode(double value, const std::string& formatCode) {
    FormatCodeResult result = cells::formatWithCode(value, formatCode);

    std::ostringstream ss;
    if (!result.success) {
        ss << "{\"error\":\"" << jsonEscape(result.errorMessage) << "\"}";
    } else {
        ss << "{\"text\":\"" << jsonEscape(result.text) << "\"}";
    }
    return ss.str();
}

std::string CellsEngine::formatCellById(const std::string& cellIdStr) {
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

    auto* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    double numericValue = 0.0;
    bool isNumeric = false;

    if (cell->value.type == CellValueType::NUMBER) {
        numericValue = cell->value.asNumber();
        isNumeric = true;
    } else if (cell->value.type == CellValueType::FORMULA_NUMBER) {
        numericValue = cell->value.asNumber();
        isNumeric = true;
    }

    if (!isNumeric) {
        std::string text;
        if (cell->value.type == CellValueType::STRING ||
            cell->value.type == CellValueType::FORMULA_STRING) {
            text = cell->value.asString();
        } else if (cell->value.type == CellValueType::BOOLEAN ||
                   cell->value.type == CellValueType::FORMULA_BOOLEAN) {
            text = cell->value.asBoolean() ? "TRUE" : "FALSE";
        } else if (cell->value.type == CellValueType::ERROR ||
                   cell->value.type == CellValueType::FORMULA_ERROR) {
            text = cell->value.asString().empty() ? "#ERROR!" : cell->value.asString();
        } else {
            text = "";
        }
        return "{\"text\":\"" + jsonEscape(text) + "\"}";
    }

    const ID cellFormatId = _workbook->getCellFormatId(cell->id);
    FormattedValue result =
        formatNumber(_formatRegistry, _workbook->getCustomFormats(), numericValue, cellFormatId);

    std::ostringstream ss;
    if (result.isError) {
        ss << "{\"error\":\"" << jsonEscape(result.errorMessage) << "\"}";
    } else {
        ss << "{\"text\":\"" << jsonEscape(result.text) << "\"}";
    }
    return ss.str();
}

std::string CellsEngine::getFormatDetails(const std::string& formatId) {
    return cells::getFormatDetails(formatId);
}

std::string CellsEngine::makeFormatId(const std::string& category, int decimals, bool separator,
                                       const std::string& currency) {
    std::string result = cells::makeFormatId(category, decimals, separator, currency);
    if (result.empty()) {
        return "{\"error\":\"Invalid parameters\"}";
    }
    return "{\"formatId\":\"" + result + "\"}";
}

// ============================================================================
// Cell style operations
// ============================================================================

namespace {

// Helper to convert BorderStyle enum to string
std::string borderStyleToString(BorderStyle style) {
    switch (style) {
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

// Helper to convert string to BorderStyle enum
BorderStyle stringToBorderStyle(const std::string& str) {
    if (str == "thin") return BorderStyle::THIN;
    if (str == "medium") return BorderStyle::MEDIUM;
    if (str == "thick") return BorderStyle::THICK;
    if (str == "dashed") return BorderStyle::DASHED;
    if (str == "dotted") return BorderStyle::DOTTED;
    if (str == "double") return BorderStyle::DOUBLE;
    if (str == "hair") return BorderStyle::HAIR;
    if (str == "mediumDashed") return BorderStyle::MEDIUM_DASHED;
    if (str == "dashDot") return BorderStyle::DASH_DOT;
    if (str == "mediumDashDot") return BorderStyle::MEDIUM_DASH_DOT;
    if (str == "dashDotDot") return BorderStyle::DASH_DOT_DOT;
    if (str == "mediumDashDotDot") return BorderStyle::MEDIUM_DASH_DOT_DOT;
    if (str == "slantDashDot") return BorderStyle::SLANT_DASH_DOT;
    return BorderStyle::NONE;
}

// Helper to serialize a border edge to JSON
void serializeBorderEdge(std::ostringstream& ss, const std::string& name, const BorderEdge& edge, bool& first) {
    if (edge.hasValue()) {
        if (!first) ss << ",";
        ss << "\"" << name << "\":{";
        ss << "\"style\":\"" << borderStyleToString(edge.style) << "\"";
        if (!edge.color.empty()) {
            ss << ",\"color\":\"" << jsonEscape(edge.color) << "\"";
        }
        ss << "}";
        first = false;
    }
}

// Helper to serialize CellStyle to JSON (sparse representation - only non-default values)
std::string styleToJson(const CellStyle& style) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;

    // Only include non-default values (sparse representation)
    if (style.bold) {
        if (!first) ss << ",";
        ss << "\"bold\":true";
        first = false;
    }
    if (style.italic) {
        if (!first) ss << ",";
        ss << "\"italic\":true";
        first = false;
    }
    if (style.underline) {
        if (!first) ss << ",";
        ss << "\"underline\":true";
        first = false;
    }
    if (style.wrapText) {
        if (!first) ss << ",";
        ss << "\"wrapText\":true";
        first = false;
    }
    if (!style.bgColor.empty()) {
        if (!first) ss << ",";
        ss << "\"bgColor\":\"" << jsonEscape(style.bgColor) << "\"";
        first = false;
    }
    if (!style.textColor.empty()) {
        if (!first) ss << ",";
        ss << "\"textColor\":\"" << jsonEscape(style.textColor) << "\"";
        first = false;
    }
    if (!style.fontFamily.empty()) {
        if (!first) ss << ",";
        ss << "\"fontFamily\":\"" << jsonEscape(style.fontFamily) << "\"";
        first = false;
    }
    if (style.fontSize != 0) {
        if (!first) ss << ",";
        ss << "\"fontSize\":" << static_cast<int>(style.fontSize);
        first = false;
    }
    // Only include hAlign if not GENERAL (the default)
    if (style.hAlign != TextAlign::GENERAL) {
        if (!first) ss << ",";
        ss << "\"hAlign\":\"";
        switch (style.hAlign) {
            case TextAlign::LEFT:
                ss << "left";
                break;
            case TextAlign::CENTER:
                ss << "center";
                break;
            case TextAlign::RIGHT:
                ss << "right";
                break;
            case TextAlign::JUSTIFY:
                ss << "justify";
                break;
            default:
                break;
        }
        ss << "\"";
        first = false;
    }
    // Only include vAlign if not BOTTOM (the default)
    if (style.vAlign != VerticalAlign::BOTTOM) {
        if (!first) ss << ",";
        ss << "\"vAlign\":\"";
        switch (style.vAlign) {
            case VerticalAlign::TOP:
                ss << "top";
                break;
            case VerticalAlign::MIDDLE:
                ss << "middle";
                break;
            default:
                break;
        }
        ss << "\"";
        first = false;
    }
    // Include border if any edge has a value
    if (style.border.hasValue()) {
        if (!first) ss << ",";
        ss << "\"border\":{";
        bool borderFirst = true;
        serializeBorderEdge(ss, "top", style.border.top, borderFirst);
        serializeBorderEdge(ss, "right", style.border.right, borderFirst);
        serializeBorderEdge(ss, "bottom", style.border.bottom, borderFirst);
        serializeBorderEdge(ss, "left", style.border.left, borderFirst);
        ss << "}";
        first = false;
    }

    ss << "}";
    return ss.str();
}

// Helper to check if a JSON field is present
bool hasJsonField(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    return json.find(searchKey) != std::string::npos;
}

// Helper to extract a boolean field from JSON
bool extractBoolField(const std::string& json, const std::string& key, bool defaultValue) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    pos += searchKey.length();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size()) {
        return defaultValue;
    }
    return json.substr(pos, 4) == "true";
}

// Helper to extract an integer field from JSON
int extractIntField(const std::string& json, const std::string& key, int defaultValue) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    pos += searchKey.length();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size()) {
        return defaultValue;
    }
    int value = 0;
    bool negative = false;
    if (json[pos] == '-') {
        negative = true;
        pos++;
    }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        pos++;
    }
    return negative ? -value : value;
}

// Helper to extract a nested border edge from JSON
// Looking for pattern like: "top":{"style":"thin","color":"#000000"}
BorderEdge extractBorderEdge(const std::string& json, const std::string& edgeName) {
    BorderEdge edge;

    // Look for "border":{...} section
    std::string borderKey = "\"border\":";
    size_t borderPos = json.find(borderKey);
    if (borderPos == std::string::npos) {
        return edge;
    }

    // Find the border object
    size_t braceStart = json.find('{', borderPos + borderKey.length());
    if (braceStart == std::string::npos) {
        return edge;
    }

    // Find the matching closing brace
    int braceCount = 1;
    size_t braceEnd = braceStart + 1;
    while (braceEnd < json.size() && braceCount > 0) {
        if (json[braceEnd] == '{') braceCount++;
        else if (json[braceEnd] == '}') braceCount--;
        braceEnd++;
    }

    std::string borderJson = json.substr(braceStart, braceEnd - braceStart);

    // Look for the edge within the border object
    std::string edgeKey = "\"" + edgeName + "\":";
    size_t edgePos = borderJson.find(edgeKey);
    if (edgePos == std::string::npos) {
        return edge;
    }

    // Find the edge object
    size_t edgeBraceStart = borderJson.find('{', edgePos + edgeKey.length());
    if (edgeBraceStart == std::string::npos) {
        return edge;
    }

    int edgeBraceCount = 1;
    size_t edgeBraceEnd = edgeBraceStart + 1;
    while (edgeBraceEnd < borderJson.size() && edgeBraceCount > 0) {
        if (borderJson[edgeBraceEnd] == '{') edgeBraceCount++;
        else if (borderJson[edgeBraceEnd] == '}') edgeBraceCount--;
        edgeBraceEnd++;
    }

    std::string edgeJson = borderJson.substr(edgeBraceStart, edgeBraceEnd - edgeBraceStart);

    // Extract style and color from edge JSON
    std::string styleStr = extractPayloadField(edgeJson, "style");
    edge.style = stringToBorderStyle(styleStr);
    edge.color = extractPayloadField(edgeJson, "color");

    return edge;
}

// Helper to parse CellStyle from JSON
CellStyle parseStyleJson(const std::string& json) {
    CellStyle style;
    style.bold = extractBoolField(json, "bold", false);
    style.italic = extractBoolField(json, "italic", false);
    style.underline = extractBoolField(json, "underline", false);
    style.wrapText = extractBoolField(json, "wrapText", false);
    style.bgColor = extractPayloadField(json, "bgColor");
    style.textColor = extractPayloadField(json, "textColor");
    style.fontFamily = extractPayloadField(json, "fontFamily");
    style.fontSize = static_cast<uint8_t>(extractIntField(json, "fontSize", 0));

    std::string hAlignStr = extractPayloadField(json, "hAlign");
    if (hAlignStr == "center") {
        style.hAlign = TextAlign::CENTER;
    } else if (hAlignStr == "right") {
        style.hAlign = TextAlign::RIGHT;
    } else if (hAlignStr == "justify") {
        style.hAlign = TextAlign::JUSTIFY;
    } else {
        style.hAlign = TextAlign::LEFT;
    }

    std::string vAlignStr = extractPayloadField(json, "vAlign");
    if (vAlignStr == "top") {
        style.vAlign = VerticalAlign::TOP;
    } else if (vAlignStr == "middle") {
        style.vAlign = VerticalAlign::MIDDLE;
    } else {
        style.vAlign = VerticalAlign::BOTTOM;
    }

    // Parse border if present
    if (hasJsonField(json, "border")) {
        style.border.top = extractBorderEdge(json, "top");
        style.border.right = extractBorderEdge(json, "right");
        style.border.bottom = extractBorderEdge(json, "bottom");
        style.border.left = extractBorderEdge(json, "left");
    }

    return style;
}

// Merges style JSON into an existing style in-place (only updates fields present in JSON)
void mergeStyleJson(CellStyle& style, const std::string& json) {
    // Only update fields that are actually present in the JSON
    if (hasJsonField(json, "bold")) {
        style.bold = extractBoolField(json, "bold", style.bold);
    }
    if (hasJsonField(json, "italic")) {
        style.italic = extractBoolField(json, "italic", style.italic);
    }
    if (hasJsonField(json, "underline")) {
        style.underline = extractBoolField(json, "underline", style.underline);
    }
    if (hasJsonField(json, "wrapText")) {
        style.wrapText = extractBoolField(json, "wrapText", style.wrapText);
    }
    if (hasJsonField(json, "bgColor")) {
        style.bgColor = extractPayloadField(json, "bgColor");
    }
    if (hasJsonField(json, "textColor")) {
        style.textColor = extractPayloadField(json, "textColor");
    }
    if (hasJsonField(json, "fontFamily")) {
        style.fontFamily = extractPayloadField(json, "fontFamily");
    }
    if (hasJsonField(json, "fontSize")) {
        style.fontSize = static_cast<uint8_t>(extractIntField(json, "fontSize", style.fontSize));
    }
    if (hasJsonField(json, "hAlign")) {
        std::string hAlignStr = extractPayloadField(json, "hAlign");
        if (hAlignStr == "center") {
            style.hAlign = TextAlign::CENTER;
        } else if (hAlignStr == "right") {
            style.hAlign = TextAlign::RIGHT;
        } else if (hAlignStr == "justify") {
            style.hAlign = TextAlign::JUSTIFY;
        } else {
            style.hAlign = TextAlign::LEFT;
        }
    }
    if (hasJsonField(json, "vAlign")) {
        std::string vAlignStr = extractPayloadField(json, "vAlign");
        if (vAlignStr == "top") {
            style.vAlign = VerticalAlign::TOP;
        } else if (vAlignStr == "middle") {
            style.vAlign = VerticalAlign::MIDDLE;
        } else {
            style.vAlign = VerticalAlign::BOTTOM;
        }
    }
    // Merge border properties if present
    if (hasJsonField(json, "border")) {
        BorderEdge topEdge = extractBorderEdge(json, "top");
        BorderEdge rightEdge = extractBorderEdge(json, "right");
        BorderEdge bottomEdge = extractBorderEdge(json, "bottom");
        BorderEdge leftEdge = extractBorderEdge(json, "left");
        // Only update edges that are specified in the JSON
        if (topEdge.hasValue() || topEdge.style != BorderStyle::NONE) {
            style.border.top = topEdge;
        }
        if (rightEdge.hasValue() || rightEdge.style != BorderStyle::NONE) {
            style.border.right = rightEdge;
        }
        if (bottomEdge.hasValue() || bottomEdge.style != BorderStyle::NONE) {
            style.border.bottom = bottomEdge;
        }
        if (leftEdge.hasValue() || leftEdge.style != BorderStyle::NONE) {
            style.border.left = leftEdge;
        }
    }
}

// Helper to merge two CellStyles - newStyle properties override baseStyle properties.
// Used for exact match case: when new range matches existing range, merge their styles.
CellStyle mergeStyles(const CellStyle& baseStyle, const CellStyle& newStyle,
                      const std::string& newStyleJson) {
    CellStyle result = baseStyle;

    // Only merge properties that are actually set in the new style (present in JSON)
    if (hasJsonField(newStyleJson, "bold")) {
        result.bold = newStyle.bold;
    }
    if (hasJsonField(newStyleJson, "italic")) {
        result.italic = newStyle.italic;
    }
    if (hasJsonField(newStyleJson, "underline")) {
        result.underline = newStyle.underline;
    }
    if (hasJsonField(newStyleJson, "wrapText")) {
        result.wrapText = newStyle.wrapText;
    }
    if (hasJsonField(newStyleJson, "bgColor")) {
        result.bgColor = newStyle.bgColor;
    }
    if (hasJsonField(newStyleJson, "textColor")) {
        result.textColor = newStyle.textColor;
    }
    if (hasJsonField(newStyleJson, "fontFamily")) {
        result.fontFamily = newStyle.fontFamily;
    }
    if (hasJsonField(newStyleJson, "fontSize")) {
        result.fontSize = newStyle.fontSize;
    }
    if (hasJsonField(newStyleJson, "hAlign")) {
        result.hAlign = newStyle.hAlign;
    }
    if (hasJsonField(newStyleJson, "vAlign")) {
        result.vAlign = newStyle.vAlign;
    }
    if (hasJsonField(newStyleJson, "border")) {
        result.border = newStyle.border;
    }

    return result;
}

// Helper to strip conflicting properties from a style based on what's set in another style's JSON.
// Used for contained case: when existing range is fully inside new range, strip conflicting props.
// Returns the stripped style and a bool indicating if the style is now empty.
std::pair<CellStyle, bool> stripConflictingProperties(const CellStyle& existingStyle,
                                                       const std::string& newStyleJson) {
    CellStyle result = existingStyle;

    // Strip properties that are set in the new style JSON
    if (hasJsonField(newStyleJson, "bold")) {
        result.bold = false;  // Reset to default
    }
    if (hasJsonField(newStyleJson, "italic")) {
        result.italic = false;
    }
    if (hasJsonField(newStyleJson, "underline")) {
        result.underline = false;
    }
    if (hasJsonField(newStyleJson, "wrapText")) {
        result.wrapText = false;
    }
    if (hasJsonField(newStyleJson, "bgColor")) {
        result.bgColor = "";  // Reset to default
    }
    if (hasJsonField(newStyleJson, "textColor")) {
        result.textColor = "";
    }
    if (hasJsonField(newStyleJson, "fontFamily")) {
        result.fontFamily = "";
    }
    if (hasJsonField(newStyleJson, "fontSize")) {
        result.fontSize = 0;
    }
    if (hasJsonField(newStyleJson, "hAlign")) {
        result.hAlign = TextAlign::GENERAL;
    }
    if (hasJsonField(newStyleJson, "vAlign")) {
        result.vAlign = VerticalAlign::BOTTOM;
    }
    if (hasJsonField(newStyleJson, "border")) {
        result.border = CellBorder();  // Reset to default (no borders)
    }

    return {result, result.isEmpty()};
}

// Helper to check if two style JSONs have conflicting properties (same property set in both).
// Returns true if at least one property is set in both styles (they would overlap).
// Used to determine if existing range styles need to be split when a new range style is applied.
bool stylesHaveConflictingProperties(const std::string& styleJson1, const std::string& styleJson2) {
    // Check each style property - if both styles set the same property, they conflict
    if (hasJsonField(styleJson1, "bgColor") && hasJsonField(styleJson2, "bgColor")) {
        return true;
    }
    if (hasJsonField(styleJson1, "textColor") && hasJsonField(styleJson2, "textColor")) {
        return true;
    }
    if (hasJsonField(styleJson1, "bold") && hasJsonField(styleJson2, "bold")) {
        return true;
    }
    if (hasJsonField(styleJson1, "italic") && hasJsonField(styleJson2, "italic")) {
        return true;
    }
    if (hasJsonField(styleJson1, "underline") && hasJsonField(styleJson2, "underline")) {
        return true;
    }
    if (hasJsonField(styleJson1, "wrapText") && hasJsonField(styleJson2, "wrapText")) {
        return true;
    }
    if (hasJsonField(styleJson1, "fontFamily") && hasJsonField(styleJson2, "fontFamily")) {
        return true;
    }
    if (hasJsonField(styleJson1, "fontSize") && hasJsonField(styleJson2, "fontSize")) {
        return true;
    }
    if (hasJsonField(styleJson1, "hAlign") && hasJsonField(styleJson2, "hAlign")) {
        return true;
    }
    if (hasJsonField(styleJson1, "vAlign") && hasJsonField(styleJson2, "vAlign")) {
        return true;
    }
    if (hasJsonField(styleJson1, "border") && hasJsonField(styleJson2, "border")) {
        return true;
    }
    // No conflicting properties
    return false;
}

// Helper to check which properties are set in a CellStyle (compared to default).
// Returns a JSON-like string of the set properties (used for conflict detection).
std::string getStylePropertiesJson(const CellStyle& style) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;
    if (style.bold) {
        if (!first) ss << ",";
        ss << "\"bold\":true";
        first = false;
    }
    if (style.italic) {
        if (!first) ss << ",";
        ss << "\"italic\":true";
        first = false;
    }
    if (style.underline) {
        if (!first) ss << ",";
        ss << "\"underline\":true";
        first = false;
    }
    if (style.wrapText) {
        if (!first) ss << ",";
        ss << "\"wrapText\":true";
        first = false;
    }
    if (!style.bgColor.empty()) {
        if (!first) ss << ",";
        ss << "\"bgColor\":\"" << style.bgColor << "\"";
        first = false;
    }
    if (!style.textColor.empty()) {
        if (!first) ss << ",";
        ss << "\"textColor\":\"" << style.textColor << "\"";
        first = false;
    }
    if (!style.fontFamily.empty()) {
        if (!first) ss << ",";
        ss << "\"fontFamily\":\"" << style.fontFamily << "\"";
        first = false;
    }
    if (style.fontSize != 0) {
        if (!first) ss << ",";
        ss << "\"fontSize\":" << static_cast<int>(style.fontSize);
        first = false;
    }
    if (style.hAlign != TextAlign::GENERAL) {
        if (!first) ss << ",";
        ss << "\"hAlign\":\"";
        switch (style.hAlign) {
            case TextAlign::LEFT:
                ss << "left";
                break;
            case TextAlign::CENTER:
                ss << "center";
                break;
            case TextAlign::RIGHT:
                ss << "right";
                break;
            case TextAlign::JUSTIFY:
                ss << "justify";
                break;
            default:
                break;
        }
        ss << "\"";
        first = false;
    }
    if (style.vAlign != VerticalAlign::BOTTOM) {
        if (!first) ss << ",";
        ss << "\"vAlign\":\"";
        switch (style.vAlign) {
            case VerticalAlign::TOP:
                ss << "top";
                break;
            case VerticalAlign::MIDDLE:
                ss << "middle";
                break;
            default:
                break;
        }
        ss << "\"";
        first = false;
    }
    if (style.border.hasValue()) {
        if (!first) ss << ",";
        ss << "\"border\":true";
        first = false;
    }
    ss << "}";
    return ss.str();
}

// Helper to strip style properties covered by a range style from a cell style.
// When a range style sets a property, that property is cleared from cells within the range
// to avoid redundancy (the range will provide the style, cell-level overrides are removed).
// Returns the modified cell style with covered properties reset to defaults.
CellStyle stripMatchingStyleProperties(const CellStyle& cellStyle, const CellStyle& /* rangeStyle */,
                                        const std::string& styleJson) {
    CellStyle result = cellStyle;

    // Clear properties that the range style is setting (specified in styleJson)
    // This removes cell-level overrides when a range style provides the same property

    if (hasJsonField(styleJson, "bold")) {
        result.bold = false;  // Reset to default
    }
    if (hasJsonField(styleJson, "italic")) {
        result.italic = false;
    }
    if (hasJsonField(styleJson, "underline")) {
        result.underline = false;
    }
    if (hasJsonField(styleJson, "wrapText")) {
        result.wrapText = false;
    }
    if (hasJsonField(styleJson, "bgColor")) {
        result.bgColor = "";  // Reset to default (empty)
    }
    if (hasJsonField(styleJson, "textColor")) {
        result.textColor = "";
    }
    if (hasJsonField(styleJson, "fontFamily")) {
        result.fontFamily = "";
    }
    if (hasJsonField(styleJson, "fontSize")) {
        result.fontSize = 0;  // Reset to default (0 = system default)
    }
    if (hasJsonField(styleJson, "hAlign")) {
        result.hAlign = TextAlign::GENERAL;
    }
    if (hasJsonField(styleJson, "vAlign")) {
        result.vAlign = VerticalAlign::BOTTOM;
    }
    if (hasJsonField(styleJson, "border")) {
        result.border = CellBorder();  // Reset to default (no borders)
    }

    return result;
}

}  // namespace

std::string CellsEngine::setCellStyle(const std::string& cellIdStr, const std::string& styleJson) {
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

    auto* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    // Get existing style (if any) and merge with incoming JSON (read from workbook map)
    CellStyle style;
    const ID existingStyleId = _workbook->getStyleId(cell->id);
    if (!existingStyleId.isNull()) {
        const CellStyle* existingStyle = _workbook->getStyle(existingStyleId);
        if (existingStyle) {
            style = *existingStyle;
        }
    }
    mergeStyleJson(style, styleJson);

    ID styleId;
    if (!style.isEmpty()) {
        // Use hash-based O(1) lookup for deduplication
        bool isNewStyle = false;
        styleId = _workbook->findOrRegisterStyle(style, &isNewStyle);

        // Create STYLE_DEFINE operation for sync if this is a new style
        if (isNewStyle && _workbook->isCollaborating()) {
            std::string fullStyleJson = styleToJson(style);
            Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
            applyOperation(*_workbook, styleOp);
        }
    }

    // Set the cell's styleId
    std::string payload =
        "{\"style_id\":\"" + (styleId.isNull() ? "~" : styleId.toString()) + "\"}";
    Operation op = makeCellSetStyleOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::setCellStyleAt(uint32_t col, uint32_t row, const std::string& styleJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // First, try to find existing cell to get its current style
    CellStyle style;
    ID existingColId, existingRowId;
    Cell* existingCell = nullptr;

    // Find existing column at position
    Axis* existingColAxis = sheet->getColumnByPosition(col);
    if (existingColAxis != nullptr) {
        existingColId = existingColAxis->id;
    }

    // Find existing row at position
    Axis* existingRowAxis = sheet->getRowByPosition(row);
    if (existingRowAxis != nullptr) {
        existingRowId = existingRowAxis->id;
    }

    // If both column and row exist, try to find the cell
    if (!existingColId.isNull() && !existingRowId.isNull()) {
        existingCell = sheet->getCellAt(existingColId, existingRowId);
    }

    // Get existing style if cell has one (from workbook map)
    if (existingCell && existingCell->hasStyle()) {
        const ID existingStyleId = _workbook->getStyleId(existingCell->id);
        if (!existingStyleId.isNull()) {
            const CellStyle* existingStyle = _workbook->getStyle(existingStyleId);
            if (existingStyle) {
                style = *existingStyle;
            }
        }
    }

    // Merge incoming JSON with existing style
    mergeStyleJson(style, styleJson);

    ID styleId;
    if (!style.isEmpty()) {
        // Use hash-based O(1) lookup for deduplication
        bool isNewStyle = false;
        styleId = _workbook->findOrRegisterStyle(style, &isNewStyle);

        // Create STYLE_DEFINE operation for sync if this is a new style
        if (isNewStyle && _workbook->isCollaborating()) {
            std::string fullStyleJson = styleToJson(style);
            Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
            applyOperation(*_workbook, styleOp);
        }
    }

    // Find or create column at position
    ID colId = existingColId;
    bool colCreated = false;
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                 ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    // Find or create row at position
    ID rowId = existingRowId;
    bool rowCreated = false;
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                 ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Find or create cell at this position
    ID cellId;
    bool cellCreated = false;
    if (existingCell) {
        cellId = existingCell->id;
    }
    if (cellId.isNull()) {
        cellId = generate_id();
        cellCreated = true;
        std::string cellPayload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" + colId.toString() +
                                  "\",\"row_id\":\"" + rowId.toString() + "\"}";
        Operation cellOp = makeCellSetValueOp(*_workbook, cellId, cellPayload);
        applyOperation(*_workbook, cellOp);
    }

    // Set the cell's styleId
    std::string payload =
        "{\"style_id\":\"" + (styleId.isNull() ? "~" : styleId.toString()) + "\"}";
    Operation op = makeCellSetStyleOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    if (cellCreated) {
        Cell* newCell = sheet->getCell(cellId);
        if (newCell) {
            _viewportIndex.onCellAdded(newCell);
        }
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::getCellStyle(const std::string& cellIdStr) {
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

    auto* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    // Read style from workbook map
    const ID styleId = _workbook->getStyleId(cell->id);
    if (styleId.isNull()) {
        // Return empty/default style
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const CellStyle* style = _workbook->getStyle(styleId);
    if (!style) {
        // Style ID is set but not found in registry - return default
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    return styleToJson(*style);
}

std::string CellsEngine::getCellStyleAt(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find column and row at positions
    ID colId, rowId;
    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }

    if (colId.isNull() || rowId.isNull()) {
        // No axis at this position - return default style
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    // Find cell at this position
    Cell* cell = sheet->getCellAt(colId, rowId);

    // Read style from workbook map
    const ID styleId = cell ? _workbook->getStyleId(cell->id) : ID();
    if (!cell || styleId.isNull()) {
        // No cell or no style - return default
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const CellStyle* style = _workbook->getStyle(styleId);
    if (!style) {
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    return styleToJson(*style);
}

std::string CellsEngine::createStyle(const std::string& styleJson) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    CellStyle style = parseStyleJson(styleJson);

    // Use hash-based O(1) lookup for deduplication
    bool isNewStyle = false;
    ID styleId = _workbook->findOrRegisterStyle(style, &isNewStyle);

    if (!isNewStyle) {
        return "{\"success\":true,\"styleId\":\"" + styleId.toString() + "\",\"existing\":true}";
    }

    // Create STYLE_DEFINE operation for sync (new style)
    if (_workbook->isCollaborating()) {
        Operation styleOp = makeStyleDefineOp(*_workbook, styleId, styleJson);
        applyOperation(*_workbook, styleOp);

        if (_syncManager) {
            _syncManager->queueOperationsBroadcast();
            _syncManager->pruneOpLog();
        }
    }

    return "{\"success\":true,\"styleId\":\"" + styleId.toString() + "\"}";
}

std::string CellsEngine::getAvailableStyles() {
    if (!_workbook) {
        return "[]";
    }

    std::ostringstream ss;
    ss << "[";

    const auto& styles = _workbook->getStyles();
    bool first = true;
    for (const auto& [id, style] : styles) {
        if (!first) {
            ss << ",";
        }
        first = false;

        ss << "{\"id\":\"" << id.toString() << "\",\"style\":" << styleToJson(style) << "}";
    }

    ss << "]";
    return ss.str();
}

// =============================================================================
// Range Style Operations
// =============================================================================

std::string CellsEngine::setRangeStyle(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                       uint32_t endRow, const std::string& styleJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Normalize coordinates (ensure start <= end)
    const uint32_t minCol = std::min(startCol, endCol);
    const uint32_t maxCol = std::max(startCol, endCol);
    const uint32_t minRow = std::min(startRow, endRow);
    const uint32_t maxRow = std::max(startRow, endRow);

    // Find or create columns/rows at the corner positions
    ID startColId, endColId, startRowId, endRowId;

    // Find start column
    Axis* startColAxis = sheet->getColumnByPosition(minCol);
    if (startColAxis != nullptr) {
        startColId = startColAxis->id;
    }
    if (startColId.isNull()) {
        startColId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(minCol) + ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation op = makeColInsertOp(*_workbook, startColId, payload);
        applyOperation(*_workbook, op);
    }

    // Find end column
    Axis* endColAxis = sheet->getColumnByPosition(maxCol);
    if (endColAxis != nullptr) {
        endColId = endColAxis->id;
    }
    if (endColId.isNull()) {
        endColId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(maxCol) + ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation op = makeColInsertOp(*_workbook, endColId, payload);
        applyOperation(*_workbook, op);
    }

    // Find start row
    Axis* startRowAxis = sheet->getRowByPosition(minRow);
    if (startRowAxis != nullptr) {
        startRowId = startRowAxis->id;
    }
    if (startRowId.isNull()) {
        startRowId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(minRow) + ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation op = makeRowInsertOp(*_workbook, startRowId, payload);
        applyOperation(*_workbook, op);
    }

    // Find end row
    Axis* endRowAxis = sheet->getRowByPosition(maxRow);
    if (endRowAxis != nullptr) {
        endRowId = endRowAxis->id;
    }
    if (endRowId.isNull()) {
        endRowId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(maxRow) + ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation op = makeRowInsertOp(*_workbook, endRowId, payload);
        applyOperation(*_workbook, op);
    }

    // Parse the new style from JSON
    CellStyle style;
    mergeStyleJson(style, styleJson);

    // =========================================================================
    // Handle overlapping ranges with conflicting properties (Phase K)
    // =========================================================================
    // Three cases:
    // 1. EXACT MATCH: existing range has same bounds → merge styles into existing (K3/K4)
    // 2. CONTAINED: existing range is fully inside new range → strip conflicting props (K5)
    // 3. PARTIAL OVERLAP: split the existing range to avoid conflict (J2/J3)
    //
    // IMPORTANT: Check for exact-match BEFORE checking if style is empty.
    // This allows toggling off a property (e.g., bold: false) to properly
    // merge with an existing range that has that property set.

    // Query for overlapping style ranges using the R-tree index
    std::vector<Range*> overlappingRanges;
    const RangeIndex* rangeIndex = sheet->getRangeIndex();
    if (rangeIndex) {
        overlappingRanges = rangeIndex->queryRange(minCol, minRow, maxCol, maxRow, RangeFlags::STYLE);
    }

    // The new range's rectangle
    PositionRect newRect{minCol, minRow, maxCol, maxRow};

    // Check for exact match first - if found, merge and return early
    for (Range* existingRange : overlappingRanges) {
        if (!existingRange || !existingRange->hasFlag(RangeFlags::STYLE)) {
            continue;
        }

        // Get the existing range's position bounds
        const Axis* existingStartCol = sheet->getColumn(existingRange->startColId);
        const Axis* existingStartRow = sheet->getRow(existingRange->startRowId);
        const Axis* existingEndCol = sheet->getColumn(existingRange->endColId);
        const Axis* existingEndRow = sheet->getRow(existingRange->endRowId);

        if (!existingStartCol || !existingStartRow || !existingEndCol || !existingEndRow) {
            continue;
        }

        PositionRect existingRect{
            std::min(existingStartCol->position, existingEndCol->position),
            std::min(existingStartRow->position, existingEndRow->position),
            std::max(existingStartCol->position, existingEndCol->position),
            std::max(existingStartRow->position, existingEndRow->position)};

        // Case 1: EXACT MATCH - merge styles into existing range
        if (existingRect == newRect) {
            ID existingStyleId = sheet->getRangeStyleId(existingRange->id);
            const CellStyle* existingStyle = existingStyleId.isNull() ? nullptr : _workbook->getStyle(existingStyleId);

            // Merge new style properties into existing style
            CellStyle mergedStyle = existingStyle ? mergeStyles(*existingStyle, style, styleJson) : style;

            // If merged style is empty (all defaults), delete the range instead
            if (mergedStyle.isEmpty()) {
                std::ostringstream removePayload;
                removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
                Operation removeOp = makeRangeRemoveOp(*_workbook, existingRange->id, removePayload.str());
                applyOperation(*_workbook, removeOp);

                if (_syncManager) {
                    _syncManager->queueOperationsBroadcast();
                    _syncManager->pruneOpLog();
                }

                rebuildViewportIndex();
                notifyListeners(ChangeType::CELL_CHANGED);

                return "{\"success\":true,\"rangeId\":\"" + existingRange->id.toString() +
                       "\",\"deleted\":true}";
            }

            // Find or create the merged style
            ID mergedStyleId;
            const auto& existingStyles = _workbook->getStyles();
            for (const auto& [id, s] : existingStyles) {
                if (s == mergedStyle) {
                    mergedStyleId = id;
                    break;
                }
            }
            if (mergedStyleId.isNull()) {
                mergedStyleId = generate_id();
                _workbook->registerStyle(mergedStyleId, mergedStyle);
                if (_workbook->isCollaborating()) {
                    std::string fullStyleJson = styleToJson(mergedStyle);
                    Operation styleOp = makeStyleDefineOp(*_workbook, mergedStyleId, fullStyleJson);
                    applyOperation(*_workbook, styleOp);
                }
            }

            // Update the existing range's style
            std::ostringstream updatePayload;
            updatePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
            updatePayload << "\"style_id\":\"" << mergedStyleId.toString() << "\"}";
            Operation updateOp = makeRangeSetStyleOp(*_workbook, existingRange->id, updatePayload.str());
            applyOperation(*_workbook, updateOp);

            if (_syncManager) {
                _syncManager->queueOperationsBroadcast();
                _syncManager->pruneOpLog();
            }

            rebuildViewportIndex();
            notifyListeners(ChangeType::CELL_CHANGED);

            // Return early - no new range needed, we merged into existing
            return "{\"success\":true,\"rangeId\":\"" + existingRange->id.toString() +
                   "\",\"styleId\":\"" + mergedStyleId.toString() + "\",\"merged\":true}";
        }
    }

    // After checking for exact matches, now validate the style for new range creation.
    // If the style is empty (all defaults like bold:false), we can't create a new range.
    // The only way to "clear" a style is to have an exact-match range that gets merged
    // to empty (handled above with range deletion).
    ID styleId;
    if (!style.isEmpty()) {
        // Use hash-based O(1) lookup for deduplication
        bool isNewStyle = false;
        styleId = _workbook->findOrRegisterStyle(style, &isNewStyle);

        // Create STYLE_DEFINE operation for sync if this is a new style
        if (isNewStyle && _workbook->isCollaborating()) {
            std::string fullStyleJson = styleToJson(style);
            Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
            applyOperation(*_workbook, styleOp);
        }
    }

    if (styleId.isNull()) {
        // Style is empty (all defaults) and no exact-match range was found.
        // This can happen when trying to "clear" a style on a range that doesn't exist.
        return "{\"error\":\"Empty style\"}";
    }

    // Track operations to perform after iteration (can't modify while iterating)
    struct SplitOperation {
        ID oldRangeId;
        ID oldStyleId;
        std::vector<PositionRect> newRects;
    };
    std::vector<SplitOperation> splitOps;

    struct ContainedOperation {
        ID rangeId;
        ID existingStyleId;
        CellStyle strippedStyle;  // Style with conflicting props removed
        bool deleteRange;          // True if stripped style is empty
    };
    std::vector<ContainedOperation> containedOps;

    for (Range* existingRange : overlappingRanges) {
        if (!existingRange || !existingRange->hasFlag(RangeFlags::STYLE)) {
            continue;
        }

        // Get the existing range's style
        ID existingStyleId = sheet->getRangeStyleId(existingRange->id);
        if (existingStyleId.isNull()) {
            continue;
        }

        const CellStyle* existingStyle = _workbook->getStyle(existingStyleId);
        if (!existingStyle) {
            continue;
        }

        // Check if the existing style has conflicting properties with the new style
        std::string existingStyleJson = getStylePropertiesJson(*existingStyle);
        if (!stylesHaveConflictingProperties(styleJson, existingStyleJson)) {
            // No conflict - different properties, can layer (skip)
            continue;
        }

        // Get the existing range's position bounds
        const Axis* existingStartCol = sheet->getColumn(existingRange->startColId);
        const Axis* existingStartRow = sheet->getRow(existingRange->startRowId);
        const Axis* existingEndCol = sheet->getColumn(existingRange->endColId);
        const Axis* existingEndRow = sheet->getRow(existingRange->endRowId);

        if (!existingStartCol || !existingStartRow || !existingEndCol || !existingEndRow) {
            continue;
        }

        PositionRect existingRect{
            std::min(existingStartCol->position, existingEndCol->position),
            std::min(existingStartRow->position, existingEndRow->position),
            std::max(existingStartCol->position, existingEndCol->position),
            std::max(existingStartRow->position, existingEndRow->position)};

        // Check if they actually overlap
        if (!existingRect.overlaps(newRect)) {
            continue;
        }

        // Case 2: CONTAINED - existing range is fully inside new range
        // Strip conflicting properties from the existing range's style
        if (newRect.contains(existingRect)) {
            auto [strippedStyle, isEmpty] = stripConflictingProperties(*existingStyle, styleJson);
            ContainedOperation op;
            op.rangeId = existingRange->id;
            op.existingStyleId = existingStyleId;
            op.strippedStyle = strippedStyle;
            op.deleteRange = isEmpty;
            containedOps.push_back(std::move(op));
            continue;  // Don't also split
        }

        // Case 3: PARTIAL OVERLAP - split the existing range
        std::vector<PositionRect> splitRects = subtractRectangle(existingRect, newRect);

        SplitOperation op;
        op.oldRangeId = existingRange->id;
        op.oldStyleId = existingStyleId;
        op.newRects = std::move(splitRects);
        splitOps.push_back(std::move(op));
    }

    // Execute contained operations: update or delete ranges
    for (const ContainedOperation& containedOp : containedOps) {
        if (containedOp.deleteRange) {
            // Style is now empty, delete the range
            std::ostringstream removePayload;
            removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
            Operation removeOp = makeRangeRemoveOp(*_workbook, containedOp.rangeId, removePayload.str());
            applyOperation(*_workbook, removeOp);
        } else {
            // Update the range with stripped style
            ID newStyleId;
            const auto& existingStyles = _workbook->getStyles();
            for (const auto& [id, s] : existingStyles) {
                if (s == containedOp.strippedStyle) {
                    newStyleId = id;
                    break;
                }
            }
            if (newStyleId.isNull()) {
                newStyleId = generate_id();
                _workbook->registerStyle(newStyleId, containedOp.strippedStyle);
                if (_workbook->isCollaborating()) {
                    std::string fullStyleJson = styleToJson(containedOp.strippedStyle);
                    Operation styleOp = makeStyleDefineOp(*_workbook, newStyleId, fullStyleJson);
                    applyOperation(*_workbook, styleOp);
                }
            }

            std::ostringstream updatePayload;
            updatePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
            updatePayload << "\"style_id\":\"" << newStyleId.toString() << "\"}";
            Operation updateOp = makeRangeSetStyleOp(*_workbook, containedOp.rangeId, updatePayload.str());
            applyOperation(*_workbook, updateOp);
        }
    }

    // Execute split operations: delete old ranges, create new split ranges
    for (const SplitOperation& splitOp : splitOps) {
        // IMPORTANT: Add a temporary reference to the old style BEFORE deleting the old range.
        // When removeRange() is called, it releases the style reference. If the style's
        // refcount drops to 0, it gets garbage collected before we can use it for the
        // new split ranges. By adding a temp ref first, we keep the style alive during the split.
        StyleRegistry* registry = _workbook->getStyleRegistry();
        const bool needsTempRef = (registry != nullptr && !splitOp.oldStyleId.isNull() &&
                                   !splitOp.newRects.empty());
        if (needsTempRef) {
            registry->addRef(splitOp.oldStyleId);
        }

        // Delete the old range (this releases one ref to the style)
        std::ostringstream removePayload;
        removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
        Operation removeOp = makeRangeRemoveOp(*_workbook, splitOp.oldRangeId, removePayload.str());
        applyOperation(*_workbook, removeOp);

        // Create new ranges for each split rectangle
        for (const PositionRect& rect : splitOp.newRects) {
            // Find or create column/row IDs for the rectangle corners
            ID rectStartColId, rectEndColId, rectStartRowId, rectEndRowId;

            // Find start column
            Axis* rectStartColAxis = sheet->getColumnByPosition(rect.minCol);
            if (rectStartColAxis != nullptr) {
                rectStartColId = rectStartColAxis->id;
            }
            if (rectStartColId.isNull()) {
                rectStartColId = generate_id();
                std::string payload = "{\"pos\":" + std::to_string(rect.minCol) +
                                      ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
                Operation op = makeColInsertOp(*_workbook, rectStartColId, payload);
                applyOperation(*_workbook, op);
            }

            // Find end column
            Axis* rectEndColAxis = sheet->getColumnByPosition(rect.maxCol);
            if (rectEndColAxis != nullptr) {
                rectEndColId = rectEndColAxis->id;
            }
            if (rectEndColId.isNull()) {
                rectEndColId = generate_id();
                std::string payload = "{\"pos\":" + std::to_string(rect.maxCol) +
                                      ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
                Operation op = makeColInsertOp(*_workbook, rectEndColId, payload);
                applyOperation(*_workbook, op);
            }

            // Find start row
            Axis* rectStartRowAxis = sheet->getRowByPosition(rect.minRow);
            if (rectStartRowAxis != nullptr) {
                rectStartRowId = rectStartRowAxis->id;
            }
            if (rectStartRowId.isNull()) {
                rectStartRowId = generate_id();
                std::string payload = "{\"pos\":" + std::to_string(rect.minRow) +
                                      ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
                Operation op = makeRowInsertOp(*_workbook, rectStartRowId, payload);
                applyOperation(*_workbook, op);
            }

            // Find end row
            Axis* rectEndRowAxis = sheet->getRowByPosition(rect.maxRow);
            if (rectEndRowAxis != nullptr) {
                rectEndRowId = rectEndRowAxis->id;
            }
            if (rectEndRowId.isNull()) {
                rectEndRowId = generate_id();
                std::string payload = "{\"pos\":" + std::to_string(rect.maxRow) +
                                      ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
                Operation op = makeRowInsertOp(*_workbook, rectEndRowId, payload);
                applyOperation(*_workbook, op);
            }

            // Create the new split range
            ID newRangeId = generate_id();
            std::ostringstream newRangePayload;
            newRangePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
            newRangePayload << "\"start_col_id\":\"" << rectStartColId.toString() << "\",";
            newRangePayload << "\"start_row_id\":\"" << rectStartRowId.toString() << "\",";
            newRangePayload << "\"end_col_id\":\"" << rectEndColId.toString() << "\",";
            newRangePayload << "\"end_row_id\":\"" << rectEndRowId.toString() << "\",";
            newRangePayload << "\"flags\":" << static_cast<int>(RangeFlags::STYLE) << "}";

            Operation newRangeOp = makeRangeAddOp(*_workbook, newRangeId, newRangePayload.str());
            applyOperation(*_workbook, newRangeOp);

            // Associate the OLD style with the new split range (preserving the style)
            std::ostringstream newStylePayload;
            newStylePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
            newStylePayload << "\"style_id\":\"" << splitOp.oldStyleId.toString() << "\"}";

            Operation newSetStyleOp =
                makeRangeSetStyleOp(*_workbook, newRangeId, newStylePayload.str());
            applyOperation(*_workbook, newSetStyleOp);
        }

        // Release the temporary reference now that all split ranges have been created
        // (setRangeStyleId added permanent refs for each new range)
        if (needsTempRef) {
            registry->release(splitOp.oldStyleId);
        }
    }

    // Create a new Range with RANGE_STYLE flag
    ID rangeId = generate_id();
    std::ostringstream rangePayload;
    rangePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
    rangePayload << "\"start_col_id\":\"" << startColId.toString() << "\",";
    rangePayload << "\"start_row_id\":\"" << startRowId.toString() << "\",";
    rangePayload << "\"end_col_id\":\"" << endColId.toString() << "\",";
    rangePayload << "\"end_row_id\":\"" << endRowId.toString() << "\",";
    rangePayload << "\"flags\":" << static_cast<int>(RangeFlags::STYLE) << "}";

    Operation rangeOp = makeRangeAddOp(*_workbook, rangeId, rangePayload.str());
    applyOperation(*_workbook, rangeOp);

    // Associate the style with the range
    std::ostringstream stylePayload;
    stylePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\",";
    stylePayload << "\"style_id\":\"" << styleId.toString() << "\"}";

    Operation setStyleOp = makeRangeSetStyleOp(*_workbook, rangeId, stylePayload.str());
    applyOperation(*_workbook, setStyleOp);

    // Clear redundant cell-level styles within the range (I2: Range style clears cell styles)
    // When applying a range style, remove matching properties from individual cells to avoid redundancy
    for (const auto& cellId : sheet->getCellIds()) {
        // Read style from workbook map
        const ID cellStyleId = _workbook->getStyleId(cellId);
        if (cellStyleId.isNull()) {
            continue;  // Cell has no style, skip
        }

        Cell* cell = _workbook->getCell(cellId);
        if (!cell) continue;

        // Check if cell is within the range bounds
        const Axis* cellCol = sheet->getColumn(cell->colId);
        const Axis* cellRow = sheet->getRow(cell->rowId);
        if (cellCol == nullptr || cellRow == nullptr) {
            continue;
        }

        const uint32_t cellColPos = cellCol->position;
        const uint32_t cellRowPos = cellRow->position;
        if (cellColPos < minCol || cellColPos > maxCol || cellRowPos < minRow || cellRowPos > maxRow) {
            continue;  // Cell is outside the range
        }

        // Get the cell's current style
        const CellStyle* cellStylePtr = _workbook->getStyle(cellStyleId);
        if (cellStylePtr == nullptr) {
            continue;
        }

        // Strip properties that match the range style
        CellStyle strippedStyle = stripMatchingStyleProperties(*cellStylePtr, style, styleJson);

        // If the stripped style is empty, clear the cell's styleId
        // Otherwise, create/find the stripped style and update the cell
        if (strippedStyle.isEmpty()) {
            // Clear the cell's style
            std::string clearPayload = "{\"style_id\":\"~\"}";
            Operation clearOp = makeCellSetStyleOp(*_workbook, cellId, clearPayload);
            applyOperation(*_workbook, clearOp);
        } else if (strippedStyle != *cellStylePtr) {
            // Style changed, need to update the cell
            // Use hash-based O(1) lookup for deduplication
            bool isNewStyle = false;
            ID newStyleId = _workbook->findOrRegisterStyle(strippedStyle, &isNewStyle);

            // Create STYLE_DEFINE operation for sync if this is a new style
            if (isNewStyle && _workbook->isCollaborating()) {
                std::string fullStyleJson = styleToJson(strippedStyle);
                Operation styleDefineOp = makeStyleDefineOp(*_workbook, newStyleId, fullStyleJson);
                applyOperation(*_workbook, styleDefineOp);
            }

            // Update the cell's styleId
            std::string updatePayload = "{\"style_id\":\"" + newStyleId.toString() + "\"}";
            Operation updateOp = makeCellSetStyleOp(*_workbook, cellId, updatePayload);
            applyOperation(*_workbook, updateOp);
        }
    }

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"rangeId\":\"" + rangeId.toString() + "\",\"styleId\":\"" + styleId.toString() + "\"}";
}

std::string CellsEngine::removeRangeStyle(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find style ranges at this position
    std::vector<Range*> styleRanges = sheet->getRangesAt(col, row, RangeFlags::STYLE);
    if (styleRanges.empty()) {
        return "{\"error\":\"No style range found at this position\"}";
    }

    // Remove the first style range found
    Range* range = styleRanges[0];
    std::ostringstream payload;
    payload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";

    Operation removeOp = makeRangeRemoveOp(*_workbook, range->id, payload.str());
    applyOperation(*_workbook, removeOp);

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true}";
}

// =============================================================================
// Effective Style Operations (Phase L)
// =============================================================================
// Computes the effective style for a cell or range, resolving the style hierarchy:
// 1. Cell's own style (highest priority)
// 2. Range styles (merged from all overlapping RANGE_STYLE ranges)
// 3. Column's default style
// 4. Row's default style
// 5. Default style (empty)

namespace {

// Helper to merge two CellStyles - overlay properties fill in properties not set in base.
// Used for CSS-like cascading where multiple sources contribute different properties.
CellStyle mergeEffectiveStyles(const CellStyle& base, const CellStyle& overlay) {
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

// Computes the effective style at a position, considering all style sources.
// Does not require a Cell object - works for empty cells too.
// Note: sheet is non-const because getCellAt/getRangesAt are non-const
CellStyle computeEffectiveStyleAt(Sheet& sheet, const Workbook& workbook,
                                   uint32_t colPos, uint32_t rowPos,
                                   ID colId, ID rowId) {
    CellStyle result;

    // Find cell at this position (may be null)
    Cell* cell = nullptr;
    if (!colId.isNull() && !rowId.isNull()) {
        cell = sheet.getCellAt(colId, rowId);
    }

    // Priority 1: Cell's own style (highest priority - properties set here take precedence)
    // Note: We start with cell style but continue to merge lower-priority styles
    // to fill in any properties not explicitly set at the cell level.
    // Read style from workbook map
    const ID cellStyleId = cell ? workbook.getStyleId(cell->id) : ID();
    if (cell && !cellStyleId.isNull()) {
        const CellStyle* cellStyle = workbook.getStyle(cellStyleId);
        if (cellStyle) {
            result = *cellStyle;  // Start with cell style as base
            // Don't return early - merge with range/column/row styles below
        }
    }

    // Priority 2: Range styles (merge all overlapping RANGE_STYLE ranges)
    // These fill in any properties not set by the cell style
    std::vector<Range*> styleRanges = sheet.getRangesAt(colPos, rowPos, RangeFlags::STYLE);
    for (Range* range : styleRanges) {
        ID rangeStyleId = sheet.getRangeStyleId(range->id);
        if (!rangeStyleId.isNull()) {
            const CellStyle* rangeStyle = workbook.getStyle(rangeStyleId);
            if (rangeStyle) {
                result = mergeEffectiveStyles(result, *rangeStyle);
            }
        }
    }

    // Priority 3: Column's default style
    if (!colId.isNull()) {
        const Axis* colAxis = sheet.getColumn(colId);
        if (colAxis != nullptr && colAxis->hasStyle()) {
            const ID colStyleId = workbook.getStyleId(colAxis->id);
            if (!colStyleId.isNull()) {
                const CellStyle* colStyle = workbook.getStyle(colStyleId);
                if (colStyle) {
                    result = mergeEffectiveStyles(result, *colStyle);
                }
            }
        }
    }

    // Priority 4: Row's default style
    if (!rowId.isNull()) {
        const Axis* rowAxis = sheet.getRow(rowId);
        if (rowAxis != nullptr && rowAxis->hasStyle()) {
            const ID rowStyleId = workbook.getStyleId(rowAxis->id);
            if (!rowStyleId.isNull()) {
                const CellStyle* rowStyle = workbook.getStyle(rowStyleId);
                if (rowStyle) {
                    result = mergeEffectiveStyles(result, *rowStyle);
                }
            }
        }
    }

    return result;
}

}  // namespace

std::string CellsEngine::getEffectiveCellStyle(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    // Find column and row IDs at positions
    ID colId, rowId;
    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }

    CellStyle effectiveStyle = computeEffectiveStyleAt(*sheet, *_workbook, col, row, colId, rowId);
    return styleToJson(effectiveStyle);
}

std::string CellsEngine::getEffectiveStyleForRange(uint32_t col1, uint32_t row1, uint32_t col2, uint32_t row2) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"style\":{},\"mixed\":{}}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"style\":{},\"mixed\":{}}";
    }

    const uint32_t minCol = std::min(col1, col2);
    const uint32_t maxCol = std::max(col1, col2);
    const uint32_t minRow = std::min(row1, row2);
    const uint32_t maxRow = std::max(row1, row2);

    // Build lookup tables for column/row IDs by position
    std::unordered_map<uint32_t, ID> colIdByPos;
    std::unordered_map<uint32_t, ID> rowIdByPos;
    for (const ID& id : sheet->getColumnIds()) {
        Axis* axis = sheet->getColumn(id);
        if (axis != nullptr && axis->position >= minCol && axis->position <= maxCol) {
            colIdByPos[axis->position] = id;
        }
    }
    for (const ID& id : sheet->getRowIds()) {
        Axis* axis = sheet->getRow(id);
        if (axis != nullptr && axis->position >= minRow && axis->position <= maxRow) {
            rowIdByPos[axis->position] = id;
        }
    }

    // Get style of first cell (anchor)
    ID firstColId = colIdByPos.count(minCol) > 0 ? colIdByPos[minCol] : ID();
    ID firstRowId = rowIdByPos.count(minRow) > 0 ? rowIdByPos[minRow] : ID();
    CellStyle firstStyle = computeEffectiveStyleAt(*sheet, *_workbook, minCol, minRow, firstColId, firstRowId);

    // Track which properties differ across the range
    bool mixedBold = false;
    bool mixedItalic = false;
    bool mixedUnderline = false;
    bool mixedWrapText = false;
    bool mixedBgColor = false;
    bool mixedTextColor = false;
    bool mixedFontFamily = false;
    bool mixedFontSize = false;
    bool mixedHAlign = false;
    bool mixedVAlign = false;

    // Check all cells in range
    for (uint32_t c = minCol; c <= maxCol; ++c) {
        for (uint32_t r = minRow; r <= maxRow; ++r) {
            if (c == minCol && r == minRow) continue;  // Skip anchor

            ID colId = colIdByPos.count(c) > 0 ? colIdByPos[c] : ID();
            ID rowId = rowIdByPos.count(r) > 0 ? rowIdByPos[r] : ID();
            CellStyle cellStyle = computeEffectiveStyleAt(*sheet, *_workbook, c, r, colId, rowId);

            // Compare each property
            if (cellStyle.bold != firstStyle.bold) mixedBold = true;
            if (cellStyle.italic != firstStyle.italic) mixedItalic = true;
            if (cellStyle.underline != firstStyle.underline) mixedUnderline = true;
            if (cellStyle.wrapText != firstStyle.wrapText) mixedWrapText = true;
            if (cellStyle.bgColor != firstStyle.bgColor) mixedBgColor = true;
            if (cellStyle.textColor != firstStyle.textColor) mixedTextColor = true;
            if (cellStyle.fontFamily != firstStyle.fontFamily) mixedFontFamily = true;
            if (cellStyle.fontSize != firstStyle.fontSize) mixedFontSize = true;
            if (cellStyle.hAlign != firstStyle.hAlign) mixedHAlign = true;
            if (cellStyle.vAlign != firstStyle.vAlign) mixedVAlign = true;
        }
    }

    // Build JSON response
    std::ostringstream ss;
    ss << "{\"style\":" << styleToJson(firstStyle) << ",\"mixed\":{";
    bool first = true;
    if (mixedBold) { ss << "\"bold\":true"; first = false; }
    if (mixedItalic) { if (!first) ss << ","; ss << "\"italic\":true"; first = false; }
    if (mixedUnderline) { if (!first) ss << ","; ss << "\"underline\":true"; first = false; }
    if (mixedWrapText) { if (!first) ss << ","; ss << "\"wrapText\":true"; first = false; }
    if (mixedBgColor) { if (!first) ss << ","; ss << "\"bgColor\":true"; first = false; }
    if (mixedTextColor) { if (!first) ss << ","; ss << "\"textColor\":true"; first = false; }
    if (mixedFontFamily) { if (!first) ss << ","; ss << "\"fontFamily\":true"; first = false; }
    if (mixedFontSize) { if (!first) ss << ","; ss << "\"fontSize\":true"; first = false; }
    if (mixedHAlign) { if (!first) ss << ","; ss << "\"hAlign\":true"; first = false; }
    if (mixedVAlign) { if (!first) ss << ","; ss << "\"vAlign\":true"; first = false; }
    ss << "}}";

    return ss.str();
}

}  // namespace cells::wasm
