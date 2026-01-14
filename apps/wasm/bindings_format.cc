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

#include "core/cells/crdt.h"
#include "core/cells/range.h"
#include "core/cells/format_code_formatter.h"
#include "core/cells/format_code_parser.h"
#include "core/cells/formula_functions.h"
#include "core/cells/id.h"
#include "core/cells/input_parser.h"
#include "core/cells/number_formatter.h"
#include "core/cells/operation.h"

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
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            colId = id;
            break;
        }
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
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            rowId = id;
            break;
        }
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
    for (const auto& [id, c] : sheet->cells) {
        if (c->colId == colId && c->rowId == rowId) {
            cellId = id;
            break;
        }
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

    std::string formatIdStr = cell->formatId.isNull() ? "~" : cell->formatId.toString();
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

    FormattedValue result =
        formatNumber(_formatRegistry, _workbook->getCustomFormats(), numericValue, cell->formatId);

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

// Helper to serialize CellStyle to JSON
std::string styleToJson(const CellStyle& style) {
    std::ostringstream ss;
    ss << "{";
    ss << "\"bold\":" << (style.bold ? "true" : "false");
    ss << ",\"italic\":" << (style.italic ? "true" : "false");
    ss << ",\"underline\":" << (style.underline ? "true" : "false");
    ss << ",\"bgColor\":\"" << jsonEscape(style.bgColor) << "\"";
    ss << ",\"textColor\":\"" << jsonEscape(style.textColor) << "\"";
    ss << ",\"fontFamily\":\"" << jsonEscape(style.fontFamily) << "\"";
    ss << ",\"fontSize\":" << static_cast<int>(style.fontSize);
    ss << ",\"hAlign\":\"";
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
    }
    ss << "\",\"vAlign\":\"";
    switch (style.vAlign) {
        case VerticalAlign::TOP:
            ss << "top";
            break;
        case VerticalAlign::MIDDLE:
            ss << "middle";
            break;
        case VerticalAlign::BOTTOM:
            ss << "bottom";
            break;
    }
    ss << "\"}";
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

// Helper to parse CellStyle from JSON
CellStyle parseStyleJson(const std::string& json) {
    CellStyle style;
    style.bold = extractBoolField(json, "bold", false);
    style.italic = extractBoolField(json, "italic", false);
    style.underline = extractBoolField(json, "underline", false);
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

    return style;
}

// Helper to merge style JSON into an existing style (only updates fields present in JSON)
CellStyle mergeStyleJson(const CellStyle& baseStyle, const std::string& json) {
    CellStyle style = baseStyle;

    // Only update fields that are actually present in the JSON
    if (hasJsonField(json, "bold")) {
        style.bold = extractBoolField(json, "bold", baseStyle.bold);
    }
    if (hasJsonField(json, "italic")) {
        style.italic = extractBoolField(json, "italic", baseStyle.italic);
    }
    if (hasJsonField(json, "underline")) {
        style.underline = extractBoolField(json, "underline", baseStyle.underline);
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
        style.fontSize = static_cast<uint8_t>(extractIntField(json, "fontSize", baseStyle.fontSize));
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

    return style;
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

    // Get existing style (if any) and merge with incoming JSON
    CellStyle baseStyle;  // Uses CellStyle defaults from model.h
    if (!cell->styleId.isNull()) {
        const CellStyle* existingStyle = _workbook->getStyle(cell->styleId);
        if (existingStyle) {
            baseStyle = *existingStyle;
        }
    }
    CellStyle style = mergeStyleJson(baseStyle, styleJson);

    ID styleId;
    if (!style.isEmpty()) {
        // Check if this style already exists
        const auto& existingStyles = _workbook->getStyles();
        for (const auto& [id, existingStyle] : existingStyles) {
            if (existingStyle == style) {
                styleId = id;
                break;
            }
        }

        // Create new style if not found
        if (styleId.isNull()) {
            styleId = generate_id();
            _workbook->registerStyle(styleId, style);

            // Create STYLE_DEFINE operation for sync
            if (_workbook->isCollaborating()) {
                // Store full merged style for sync (not partial styleJson)
                std::string fullStyleJson = styleToJson(style);
                Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
                applyOperation(*_workbook, styleOp);
            }
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
    CellStyle baseStyle;  // Uses CellStyle defaults from model.h
    ID existingColId, existingRowId;
    Cell* existingCell = nullptr;

    // Find existing column at position
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            existingColId = id;
            break;
        }
    }

    // Find existing row at position
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            existingRowId = id;
            break;
        }
    }

    // If both column and row exist, try to find the cell
    if (!existingColId.isNull() && !existingRowId.isNull()) {
        for (const auto& [id, c] : sheet->cells) {
            if (c->colId == existingColId && c->rowId == existingRowId) {
                existingCell = c.get();
                break;
            }
        }
    }

    // Get existing style if cell has one
    if (existingCell && !existingCell->styleId.isNull()) {
        const CellStyle* existingStyle = _workbook->getStyle(existingCell->styleId);
        if (existingStyle) {
            baseStyle = *existingStyle;
        }
    }

    // Merge incoming JSON with existing style
    CellStyle style = mergeStyleJson(baseStyle, styleJson);

    ID styleId;
    if (!style.isEmpty()) {
        // Check if this style already exists
        const auto& existingStyles = _workbook->getStyles();
        for (const auto& [id, existingStyle] : existingStyles) {
            if (existingStyle == style) {
                styleId = id;
                break;
            }
        }

        // Create new style if not found
        if (styleId.isNull()) {
            styleId = generate_id();
            _workbook->registerStyle(styleId, style);

            // Create STYLE_DEFINE operation for sync
            if (_workbook->isCollaborating()) {
                // Store full merged style for sync (not partial styleJson)
                std::string fullStyleJson = styleToJson(style);
                Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
                applyOperation(*_workbook, styleOp);
            }
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
        // Find the cell ID from the existing cell
        for (const auto& [id, c] : sheet->cells) {
            if (c.get() == existingCell) {
                cellId = id;
                break;
            }
        }
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

    if (cell->styleId.isNull()) {
        // Return empty/default style
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const CellStyle* style = _workbook->getStyle(cell->styleId);
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
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            colId = id;
            break;
        }
    }
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            rowId = id;
            break;
        }
    }

    if (colId.isNull() || rowId.isNull()) {
        // No axis at this position - return default style
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    // Find cell at this position
    Cell* cell = nullptr;
    for (const auto& [id, c] : sheet->cells) {
        if (c->colId == colId && c->rowId == rowId) {
            cell = c.get();
            break;
        }
    }

    if (!cell || cell->styleId.isNull()) {
        // No cell or no style - return default
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const CellStyle* style = _workbook->getStyle(cell->styleId);
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

    // Check if this style already exists
    const auto& existingStyles = _workbook->getStyles();
    for (const auto& [id, existingStyle] : existingStyles) {
        if (existingStyle == style) {
            return "{\"success\":true,\"styleId\":\"" + id.toString() + "\",\"existing\":true}";
        }
    }

    // Create new style
    ID styleId = generate_id();
    _workbook->registerStyle(styleId, style);

    // Create STYLE_DEFINE operation for sync
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
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == minCol) {
            startColId = id;
            break;
        }
    }
    if (startColId.isNull()) {
        startColId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(minCol) + ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation op = makeColInsertOp(*_workbook, startColId, payload);
        applyOperation(*_workbook, op);
    }

    // Find end column
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == maxCol) {
            endColId = id;
            break;
        }
    }
    if (endColId.isNull()) {
        endColId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(maxCol) + ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation op = makeColInsertOp(*_workbook, endColId, payload);
        applyOperation(*_workbook, op);
    }

    // Find start row
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == minRow) {
            startRowId = id;
            break;
        }
    }
    if (startRowId.isNull()) {
        startRowId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(minRow) + ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation op = makeRowInsertOp(*_workbook, startRowId, payload);
        applyOperation(*_workbook, op);
    }

    // Find end row
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == maxRow) {
            endRowId = id;
            break;
        }
    }
    if (endRowId.isNull()) {
        endRowId = generate_id();
        std::string payload =
            "{\"pos\":" + std::to_string(maxRow) + ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation op = makeRowInsertOp(*_workbook, endRowId, payload);
        applyOperation(*_workbook, op);
    }

    // Create or find the style
    CellStyle defaultStyle;
    CellStyle style = mergeStyleJson(defaultStyle, styleJson);

    ID styleId;
    if (!style.isEmpty()) {
        // Check if this style already exists
        const auto& existingStyles = _workbook->getStyles();
        for (const auto& [id, existingStyle] : existingStyles) {
            if (existingStyle == style) {
                styleId = id;
                break;
            }
        }

        // Create new style if not found
        if (styleId.isNull()) {
            styleId = generate_id();
            _workbook->registerStyle(styleId, style);

            // Create STYLE_DEFINE operation for sync
            if (_workbook->isCollaborating()) {
                std::string fullStyleJson = styleToJson(style);
                Operation styleOp = makeStyleDefineOp(*_workbook, styleId, fullStyleJson);
                applyOperation(*_workbook, styleOp);
            }
        }
    }

    if (styleId.isNull()) {
        return "{\"error\":\"Empty style\"}";
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

}  // namespace cells::wasm
