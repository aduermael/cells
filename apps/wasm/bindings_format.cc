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

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include "core/cells/cell_style_presets.h"
#include "core/cells/crdt.h"
#include "core/cells/format_buffer.h"
#include "core/cells/format_code_formatter.h"
#include "core/cells/format_code_parser.h"
#include "core/cells/formula_functions.h"
#include "core/cells/id.h"
#include "core/cells/input_parser.h"
#include "core/cells/number_formatter.h"
#include "core/cells/operation.h"
#include "core/cells/range.h"
#include "core/cells/range_index.h"
#include "core/cells/style_buffer.h"
#include "core/log/include/Logger.h"

#include "apps/wasm/bindings.h"

namespace cells::wasm {

namespace {

// =============================================================================
// Format JSON parsing helpers
// =============================================================================

// Helper to convert string to NumberFormatCategory
NumberFormatCategory stringToCategoryInternal(const std::string& str) {
    if (str == "GENERAL" || str == "general")
        return NumberFormatCategory::GENERAL;
    if (str == "NUMBER" || str == "number")
        return NumberFormatCategory::NUMBER;
    if (str == "CURRENCY" || str == "currency")
        return NumberFormatCategory::CURRENCY;
    if (str == "ACCOUNTING" || str == "accounting")
        return NumberFormatCategory::ACCOUNTING;
    if (str == "PERCENTAGE" || str == "percentage")
        return NumberFormatCategory::PERCENTAGE;
    if (str == "DATE" || str == "date")
        return NumberFormatCategory::DATE;
    if (str == "TIME" || str == "time")
        return NumberFormatCategory::TIME;
    if (str == "DATE_TIME" || str == "dateTime" || str == "date_time")
        return NumberFormatCategory::DATE_TIME;
    if (str == "SCIENTIFIC" || str == "scientific")
        return NumberFormatCategory::SCIENTIFIC;
    if (str == "FRACTION" || str == "fraction")
        return NumberFormatCategory::FRACTION;
    if (str == "TEXT" || str == "text")
        return NumberFormatCategory::TEXT;
    if (str == "CUSTOM" || str == "custom")
        return NumberFormatCategory::CUSTOM;
    return NumberFormatCategory::GENERAL;
}

// Parse format JSON into FormatBuffer
// Accepts:
// {"category":"NUMBER","decimals":2,"separator":true,"currency":"$","formatCode":"#,##0.00"} Also
// accepts: {"base64":"..."} to parse from existing base64 format All fields are optional.
FormatBuffer parseFormatJson(const std::string& json) {
    FormatBuffer format;

    // Check for base64 format first (for copy/paste operations)
    std::string base64 = extractPayloadField(json, "base64");
    if (!base64.empty()) {
        // Parse from base64 and return immediately
        auto parsed = FormatBuffer::fromBase64(base64);
        if (parsed.has_value()) {
            return parsed.value();
        }
        // Invalid base64, return empty format
        return FormatBuffer();
    }

    // Parse category
    std::string category = extractPayloadField(json, "category");
    if (!category.empty()) {
        format.setCategory(stringToCategoryInternal(category));
    }

    // Parse decimals
    if (json.find("\"decimals\":") != std::string::npos) {
        // Extract integer value
        size_t pos = json.find("\"decimals\":");
        if (pos != std::string::npos) {
            pos += 11;  // length of "\"decimals\":"
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
                pos++;
            int value = 0;
            while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
                value = value * 10 + (json[pos] - '0');
                pos++;
            }
            format.setDecimals(static_cast<uint8_t>(std::min(value, 15)));
        }
    }

    // Parse separator (thousands separator)
    if (json.find("\"separator\":") != std::string::npos) {
        size_t pos = json.find("\"separator\":");
        if (pos != std::string::npos) {
            pos += 12;  // length of "\"separator\":"
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
                pos++;
            bool value = (json.substr(pos, 4) == "true");
            format.setThousandsSeparator(value);
        }
    }

    // Parse currency symbol
    std::string currency = extractPayloadField(json, "currency");
    if (!currency.empty()) {
        format.setCurrencySymbol(currency);
    }

    // Parse custom format code
    std::string formatCode = extractPayloadField(json, "formatCode");
    if (!formatCode.empty()) {
        format.setCustomFormatCode(formatCode);
    }

    return format;
}

// Convert FormatBuffer to JSON for returning to JS
std::string formatBufferToJson(const FormatBuffer& format) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;

    // Category
    if (format.hasCategory()) {
        ss << "\"category\":\"" << formatCategoryToString(format.getCategory()) << "\"";
        first = false;
    }

    // Decimals
    if (format.hasDecimals()) {
        if (!first)
            ss << ",";
        ss << "\"decimals\":" << static_cast<int>(format.getDecimals());
        first = false;
    }

    // Thousands separator
    if (format.hasThousandsSeparator()) {
        if (!first)
            ss << ",";
        ss << "\"separator\":" << (format.getThousandsSeparator() ? "true" : "false");
        first = false;
    }

    // Currency symbol
    if (format.hasCurrencySymbol()) {
        if (!first)
            ss << ",";
        ss << "\"currency\":\"" << jsonEscape(format.getCurrencySymbol()) << "\"";
        first = false;
    }

    // Custom format code
    if (format.hasCustomFormatCode()) {
        if (!first)
            ss << ",";
        ss << "\"formatCode\":\"" << jsonEscape(format.getCustomFormatCode()) << "\"";
        first = false;
    }

    // Always include the generated format code for display/formatting
    if (!first)
        ss << ",";
    ss << "\"effectiveFormatCode\":\"" << jsonEscape(format.toFormatCode()) << "\"";

    // Include base64 encoding for reference
    ss << ",\"base64\":\"" << format.toBase64() << "\"";

    ss << "}";
    return ss.str();
}

}  // namespace

std::string CellsEngine::setCellFormat(const std::string& cellIdStr,
                                       const std::string& formatJson) {
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

    // Parse format properties from JSON
    FormatBuffer format = parseFormatJson(formatJson);

    // Apply format or clear if empty
    if (!format.isEmpty()) {
        Operation op = makeCellSetFormatOp(*_workbook, cellId, format);
        applyOperation(*_workbook, op);
    } else {
        Operation op = makeCellClearFormatOp(*_workbook, cellId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::setCellFormatAt(uint32_t col, uint32_t row,
                                         const std::string& formatJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Parse format properties from JSON
    FormatBuffer format = parseFormatJson(formatJson);

    // Shared core ensure path (same as Luau setFormat)
    bool colCreated = false;
    bool rowCreated = false;
    bool cellCreated = false;
    Cell* cell = ensureCellAtPositionViaCrdt(*_workbook, *sheet, col, row, &colCreated, &rowCreated,
                                             &cellCreated);
    if (cell == nullptr) {
        return "{\"error\":\"Failed to ensure cell\"}";
    }
    const ID cellId = cell->id;
    const ID colId = cell->colId;
    const ID rowId = cell->rowId;

    // Apply format or clear if empty
    if (!format.isEmpty()) {
        Operation op = makeCellSetFormatOp(*_workbook, cellId, format);
        applyOperation(*_workbook, op);
    } else {
        Operation op = makeCellClearFormatOp(*_workbook, cellId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    if (cellCreated) {
        _viewportIndex.onCellAdded(cell);
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::getAvailableFormats() {
    // Return a static list of predefined format templates
    // Each template includes the format properties needed to create a FormatBuffer
    std::ostringstream ss;
    ss << "[";

    // Helper lambda to add a format entry
    auto addFormat = [&ss](bool& first, const char* category, int decimals, bool separator,
                           const char* currency, const char* formatCode, const char* name) {
        if (!first)
            ss << ",";
        first = false;
        ss << "{";
        ss << "\"category\":\"" << category << "\"";
        ss << ",\"decimals\":" << decimals;
        ss << ",\"separator\":" << (separator ? "true" : "false");
        if (currency && currency[0]) {
            ss << ",\"currency\":\"" << currency << "\"";
        }
        ss << ",\"formatCode\":\"" << formatCode << "\"";
        ss << ",\"name\":\"" << name << "\"";
        ss << "}";
    };

    bool first = true;

    // General
    addFormat(first, "GENERAL", 0, false, "", "General", "General");

    // Number formats
    addFormat(first, "NUMBER", 0, false, "", "0", "Number (0 decimals)");
    addFormat(first, "NUMBER", 2, false, "", "0.00", "Number (2 decimals)");
    addFormat(first, "NUMBER", 2, true, "", "#,##0.00", "Number with separator");

    // Currency formats
    addFormat(first, "CURRENCY", 2, true, "$", "$#,##0.00", "Currency (USD)");
    addFormat(first, "CURRENCY", 2, true, "€", "€#,##0.00", "Currency (EUR)");
    addFormat(first, "CURRENCY", 2, true, "£", "£#,##0.00", "Currency (GBP)");
    addFormat(first, "CURRENCY", 0, true, "¥", "¥#,##0", "Currency (JPY)");

    // Percentage formats
    addFormat(first, "PERCENTAGE", 0, false, "", "0%", "Percent (0 decimals)");
    addFormat(first, "PERCENTAGE", 2, false, "", "0.00%", "Percent (2 decimals)");

    // Date formats
    addFormat(first, "DATE", 0, false, "", "m/d/yyyy", "Date (Short)");
    addFormat(first, "DATE", 0, false, "", "mmmm d, yyyy", "Date (Long)");

    // Time formats
    addFormat(first, "TIME", 0, false, "", "h:mm AM/PM", "Time (12-hour)");
    addFormat(first, "TIME", 0, false, "", "h:mm:ss", "Time (24-hour)");

    // Scientific notation
    addFormat(first, "SCIENTIFIC", 2, false, "", "0.00E+00", "Scientific");

    // Text
    addFormat(first, "TEXT", 0, false, "", "@", "Text");

    ss << "]";
    return ss.str();
}

std::string CellsEngine::createCustomFormat(const std::string& formatCode) {
    // With content-addressed formats, we simply parse the format code into a FormatBuffer
    // and return its JSON representation. No need to register it in the workbook.
    auto validationError = validateFormatCode(formatCode);
    if (validationError) {
        return "{\"error\":\"" + jsonEscape(*validationError) + "\"}";
    }

    // Parse format code into FormatBuffer
    auto maybeFormat = FormatBuffer::fromFormatCode(formatCode);
    if (!maybeFormat.has_value()) {
        // If fromFormatCode fails, create a format with just the custom code
        FormatBuffer format;
        format.setCategory(NumberFormatCategory::CUSTOM);
        format.setCustomFormatCode(formatCode);
        return "{\"success\":true,\"format\":" + formatBufferToJson(format) + "}";
    }

    return "{\"success\":true,\"format\":" + formatBufferToJson(*maybeFormat) + "}";
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

    // Get format from content-addressed storage
    const FormatBuffer* format = _workbook->getEntityFormat(cell->id);
    if (format == nullptr || format->isEmpty()) {
        // No format - return empty JSON (GENERAL format)
        return "{\"category\":\"GENERAL\",\"effectiveFormatCode\":\"General\",\"base64\":\"\"}";
    }

    return formatBufferToJson(*format);
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

std::string CellsEngine::formatCellValue(double value, const std::string& formatJson) {
    // Parse format properties from JSON
    FormatBuffer format = parseFormatJson(formatJson);

    // Get the format code and use it to format the value
    std::string formatCode = format.toFormatCode();
    FormatCodeResult result = cells::formatWithCode(value, formatCode);

    std::ostringstream ss;
    if (!result.success) {
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

    // Get format from content-addressed storage
    const FormatBuffer* format = _workbook->getEntityFormat(cell->id);
    std::string formatCode = "General";
    if (format != nullptr && !format->isEmpty()) {
        formatCode = format->toFormatCode();
    }

    FormatCodeResult result = cells::formatWithCode(numericValue, formatCode);

    std::ostringstream ss;
    if (!result.success) {
        ss << "{\"error\":\"" << jsonEscape(result.errorMessage) << "\"}";
    } else {
        ss << "{\"text\":\"" << jsonEscape(result.text) << "\"}";
    }
    return ss.str();
}

std::string CellsEngine::getFormatDetails(const std::string& formatInput) {
    // Accept either:
    // 1. Base64-encoded FormatBuffer (e.g., "AQQI")
    // 2. JSON format properties (e.g., {"category":"NUMBER","decimals":2})
    // Returns format properties as JSON

    // Try to parse as base64 first
    auto maybeFormat = FormatBuffer::fromBase64(formatInput);
    if (maybeFormat.has_value()) {
        return formatBufferToJson(*maybeFormat);
    }

    // Try to parse as JSON
    if (formatInput.find('{') != std::string::npos) {
        FormatBuffer format = parseFormatJson(formatInput);
        return formatBufferToJson(format);
    }

    // Empty or invalid input - return GENERAL format
    return "{\"category\":\"GENERAL\",\"effectiveFormatCode\":\"General\",\"base64\":\"\"}";
}

std::string CellsEngine::makeFormatId(const std::string& category, int decimals, bool separator,
                                      const std::string& currency) {
    // Create a FormatBuffer with the given properties and return its JSON
    FormatBuffer format;

    // Set category
    format.setCategory(stringToCategoryInternal(category));

    // Set decimals
    format.setDecimals(static_cast<uint8_t>(std::min(std::max(decimals, 0), 15)));

    // Set thousands separator
    format.setThousandsSeparator(separator);

    // Set currency symbol
    if (!currency.empty()) {
        format.setCurrencySymbol(currency);
    }

    return "{\"format\":" + formatBufferToJson(format) + "}";
}

// ============================================================================
// Cell style operations
// ============================================================================

namespace {

// Helper to convert BorderStyle enum to string
std::string borderStyleToString(BorderStyle style) {
    switch (style) {
        case BorderStyle::THIN:
            return "thin";
        case BorderStyle::MEDIUM:
            return "medium";
        case BorderStyle::THICK:
            return "thick";
        case BorderStyle::DASHED:
            return "dashed";
        case BorderStyle::DOTTED:
            return "dotted";
        case BorderStyle::DOUBLE:
            return "double";
        case BorderStyle::HAIR:
            return "hair";
        case BorderStyle::MEDIUM_DASHED:
            return "mediumDashed";
        case BorderStyle::DASH_DOT:
            return "dashDot";
        case BorderStyle::MEDIUM_DASH_DOT:
            return "mediumDashDot";
        case BorderStyle::DASH_DOT_DOT:
            return "dashDotDot";
        case BorderStyle::MEDIUM_DASH_DOT_DOT:
            return "mediumDashDotDot";
        case BorderStyle::SLANT_DASH_DOT:
            return "slantDashDot";
        default:
            return "none";
    }
}

// Helper to convert string to BorderStyle enum
BorderStyle stringToBorderStyle(const std::string& str) {
    if (str == "thin")
        return BorderStyle::THIN;
    if (str == "medium")
        return BorderStyle::MEDIUM;
    if (str == "thick")
        return BorderStyle::THICK;
    if (str == "dashed")
        return BorderStyle::DASHED;
    if (str == "dotted")
        return BorderStyle::DOTTED;
    if (str == "double")
        return BorderStyle::DOUBLE;
    if (str == "hair")
        return BorderStyle::HAIR;
    if (str == "mediumDashed")
        return BorderStyle::MEDIUM_DASHED;
    if (str == "dashDot")
        return BorderStyle::DASH_DOT;
    if (str == "mediumDashDot")
        return BorderStyle::MEDIUM_DASH_DOT;
    if (str == "dashDotDot")
        return BorderStyle::DASH_DOT_DOT;
    if (str == "mediumDashDotDot")
        return BorderStyle::MEDIUM_DASH_DOT_DOT;
    if (str == "slantDashDot")
        return BorderStyle::SLANT_DASH_DOT;
    return BorderStyle::NONE;
}

// Helper to serialize a border edge to JSON
void serializeBorderEdge(std::ostringstream& ss, const std::string& name, const BorderEdge& edge,
                         bool& first) {
    if (edge.hasValue()) {
        if (!first)
            ss << ",";
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

    // Serialize properties based on defined flags (source of truth)
    // This allows explicitly set default values (e.g., bold=false) to be preserved
    if (style.isDefined(DEFINED_BOLD)) {
        if (!first)
            ss << ",";
        ss << "\"bold\":" << (style.bold ? "true" : "false");
        first = false;
    }
    if (style.isDefined(DEFINED_ITALIC)) {
        if (!first)
            ss << ",";
        ss << "\"italic\":" << (style.italic ? "true" : "false");
        first = false;
    }
    if (style.isDefined(DEFINED_UNDERLINE)) {
        if (!first)
            ss << ",";
        ss << "\"underline\":" << (style.underline ? "true" : "false");
        first = false;
    }
    if (style.isDefined(DEFINED_WRAPTEXT)) {
        if (!first)
            ss << ",";
        ss << "\"wrapText\":" << (style.wrapText ? "true" : "false");
        first = false;
    }
    if (style.isDefined(DEFINED_BGCOLOR)) {
        if (!first)
            ss << ",";
        ss << "\"bgColor\":\"" << jsonEscape(style.bgColor) << "\"";
        first = false;
    }
    if (style.hasBgThemeColor()) {
        if (!first)
            ss << ",";
        ss << "\"bgThemeIndex\":" << static_cast<int>(style.bgThemeIndex);
        first = false;
        if (style.bgThemeTint != 0.0) {
            ss << ",\"bgThemeTint\":" << style.bgThemeTint;
        }
    }
    if (style.isDefined(DEFINED_TEXTCOLOR)) {
        if (!first)
            ss << ",";
        ss << "\"textColor\":\"" << jsonEscape(style.textColor) << "\"";
        first = false;
    }
    if (style.hasTextThemeColor()) {
        if (!first)
            ss << ",";
        ss << "\"textThemeIndex\":" << static_cast<int>(style.textThemeIndex);
        first = false;
        if (style.textThemeTint != 0.0) {
            ss << ",\"textThemeTint\":" << style.textThemeTint;
        }
    }
    if (style.isDefined(DEFINED_FONTFAMILY)) {
        if (!first)
            ss << ",";
        ss << "\"fontFamily\":\"" << jsonEscape(style.fontFamily) << "\"";
        first = false;
    }
    if (style.isDefined(DEFINED_FONTSIZE)) {
        if (!first)
            ss << ",";
        ss << "\"fontSize\":" << static_cast<int>(style.fontSize);
        first = false;
    }
    if (style.isDefined(DEFINED_HALIGN)) {
        if (!first)
            ss << ",";
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
            case TextAlign::GENERAL:
                ss << "general";
                break;
        }
        ss << "\"";
        first = false;
    }
    if (style.isDefined(DEFINED_VALIGN)) {
        if (!first)
            ss << ",";
        ss << "\"vAlign\":\"";
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
        ss << "\"";
        first = false;
    }
    // Include border edges that are defined
    bool hasBorder = style.isDefined(DEFINED_BORDER_TOP) || style.isDefined(DEFINED_BORDER_RIGHT) ||
                     style.isDefined(DEFINED_BORDER_BOTTOM) || style.isDefined(DEFINED_BORDER_LEFT);
    if (hasBorder) {
        if (!first)
            ss << ",";
        ss << "\"border\":{";
        bool borderFirst = true;
        if (style.isDefined(DEFINED_BORDER_TOP)) {
            serializeBorderEdge(ss, "top", style.border.top, borderFirst);
        }
        if (style.isDefined(DEFINED_BORDER_RIGHT)) {
            serializeBorderEdge(ss, "right", style.border.right, borderFirst);
        }
        if (style.isDefined(DEFINED_BORDER_BOTTOM)) {
            serializeBorderEdge(ss, "bottom", style.border.bottom, borderFirst);
        }
        if (style.isDefined(DEFINED_BORDER_LEFT)) {
            serializeBorderEdge(ss, "left", style.border.left, borderFirst);
        }
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

// Helper to extract a double field from JSON
double extractDoubleField(const std::string& json, const std::string& key, double defaultValue) {
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
    // Extract the number portion as a substring and convert
    size_t start = pos;
    if (json[pos] == '-')
        pos++;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9'))
        pos++;
    if (pos < json.size() && json[pos] == '.') {
        pos++;
        while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9'))
            pos++;
    }
    if (pos == start)
        return defaultValue;
    return std::stod(json.substr(start, pos - start));
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
        if (json[braceEnd] == '{')
            braceCount++;
        else if (json[braceEnd] == '}')
            braceCount--;
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
        if (borderJson[edgeBraceEnd] == '{')
            edgeBraceCount++;
        else if (borderJson[edgeBraceEnd] == '}')
            edgeBraceCount--;
        edgeBraceEnd++;
    }

    std::string edgeJson = borderJson.substr(edgeBraceStart, edgeBraceEnd - edgeBraceStart);

    // Extract style and color from edge JSON
    std::string styleStr = extractPayloadField(edgeJson, "style");
    edge.style = stringToBorderStyle(styleStr);
    edge.color = extractPayloadField(edgeJson, "color");

    return edge;
}

// Merges style JSON into an existing style in-place (only updates fields present in JSON)
// Sets the defined flag for each property that is present in the JSON
void mergeStyleJson(CellStyle& style, const std::string& json) {
    // Only update fields that are actually present in the JSON
    // Set the defined flag for each property present in the JSON
    if (hasJsonField(json, "bold")) {
        style.bold = extractBoolField(json, "bold", style.bold);
        style.setDefined(DEFINED_BOLD);
    }
    if (hasJsonField(json, "italic")) {
        style.italic = extractBoolField(json, "italic", style.italic);
        style.setDefined(DEFINED_ITALIC);
    }
    if (hasJsonField(json, "underline")) {
        style.underline = extractBoolField(json, "underline", style.underline);
        style.setDefined(DEFINED_UNDERLINE);
    }
    if (hasJsonField(json, "wrapText")) {
        style.wrapText = extractBoolField(json, "wrapText", style.wrapText);
        style.setDefined(DEFINED_WRAPTEXT);
    }
    if (hasJsonField(json, "bgThemeIndex")) {
        // Theme color reference — clear direct color and indexed
        style.bgThemeIndex = static_cast<int8_t>(extractIntField(json, "bgThemeIndex", -1));
        style.bgThemeTint = extractDoubleField(json, "bgThemeTint", 0.0);
        style.bgColor.clear();
        style.bgIndexedColor = -1;
        style.setDefined(DEFINED_BGCOLOR);
    } else if (hasJsonField(json, "bgColor")) {
        // Direct hex color — clear theme and indexed refs
        style.bgColor = extractPayloadField(json, "bgColor");
        style.bgThemeIndex = -1;
        style.bgThemeTint = 0.0;
        style.bgIndexedColor = -1;
        style.setDefined(DEFINED_BGCOLOR);
    }
    if (hasJsonField(json, "textThemeIndex")) {
        // Theme color reference — clear direct color and indexed
        style.textThemeIndex = static_cast<int8_t>(extractIntField(json, "textThemeIndex", -1));
        style.textThemeTint = extractDoubleField(json, "textThemeTint", 0.0);
        style.textColor.clear();
        style.textIndexedColor = -1;
        style.setDefined(DEFINED_TEXTCOLOR);
    } else if (hasJsonField(json, "textColor")) {
        // Direct hex color — clear theme and indexed refs
        style.textColor = extractPayloadField(json, "textColor");
        style.textThemeIndex = -1;
        style.textThemeTint = 0.0;
        style.textIndexedColor = -1;
        style.setDefined(DEFINED_TEXTCOLOR);
    }
    if (hasJsonField(json, "fontFamily")) {
        style.fontFamily = extractPayloadField(json, "fontFamily");
        style.setDefined(DEFINED_FONTFAMILY);
    }
    if (hasJsonField(json, "fontSize")) {
        style.fontSize = static_cast<uint8_t>(extractIntField(json, "fontSize", style.fontSize));
        style.setDefined(DEFINED_FONTSIZE);
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
        style.setDefined(DEFINED_HALIGN);
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
        style.setDefined(DEFINED_VALIGN);
    }
    // Merge border properties if present
    if (hasJsonField(json, "border")) {
        // Extract the border JSON substring to check which edges are present
        std::string borderKey = "\"border\":";
        size_t borderPos = json.find(borderKey);
        if (borderPos != std::string::npos) {
            size_t braceStart = json.find('{', borderPos + borderKey.length());
            if (braceStart != std::string::npos) {
                int braceCount = 1;
                size_t braceEnd = braceStart + 1;
                while (braceEnd < json.size() && braceCount > 0) {
                    if (json[braceEnd] == '{')
                        braceCount++;
                    else if (json[braceEnd] == '}')
                        braceCount--;
                    braceEnd++;
                }
                std::string borderJson = json.substr(braceStart, braceEnd - braceStart);

                // Only update edges that are explicitly specified in the JSON
                // This allows setting style to "none" to remove borders
                if (hasJsonField(borderJson, "top")) {
                    style.border.top = extractBorderEdge(json, "top");
                    style.setDefined(DEFINED_BORDER_TOP);
                }
                if (hasJsonField(borderJson, "right")) {
                    style.border.right = extractBorderEdge(json, "right");
                    style.setDefined(DEFINED_BORDER_RIGHT);
                }
                if (hasJsonField(borderJson, "bottom")) {
                    style.border.bottom = extractBorderEdge(json, "bottom");
                    style.setDefined(DEFINED_BORDER_BOTTOM);
                }
                if (hasJsonField(borderJson, "left")) {
                    style.border.left = extractBorderEdge(json, "left");
                    style.setDefined(DEFINED_BORDER_LEFT);
                }
            }
        }
    }
}

// Helper to merge two CellStyles - newStyle properties override baseStyle properties.
// Used for exact match case: when new range matches existing range, merge their styles.
// Sets defined flags for properties being merged from newStyle.
CellStyle mergeStyles(const CellStyle& baseStyle, const CellStyle& newStyle,
                      const std::string& newStyleJson) {
    CellStyle result = baseStyle;

    // Only merge properties that are actually set in the new style (present in JSON)
    // Set defined flag for each property being merged
    if (hasJsonField(newStyleJson, "bold")) {
        result.bold = newStyle.bold;
        result.setDefined(DEFINED_BOLD);
    }
    if (hasJsonField(newStyleJson, "italic")) {
        result.italic = newStyle.italic;
        result.setDefined(DEFINED_ITALIC);
    }
    if (hasJsonField(newStyleJson, "underline")) {
        result.underline = newStyle.underline;
        result.setDefined(DEFINED_UNDERLINE);
    }
    if (hasJsonField(newStyleJson, "wrapText")) {
        result.wrapText = newStyle.wrapText;
        result.setDefined(DEFINED_WRAPTEXT);
    }
    if (hasJsonField(newStyleJson, "bgThemeIndex") || hasJsonField(newStyleJson, "bgColor")) {
        result.bgColor = newStyle.bgColor;
        result.bgThemeIndex = newStyle.bgThemeIndex;
        result.bgThemeTint = newStyle.bgThemeTint;
        result.bgIndexedColor = newStyle.bgIndexedColor;
        result.setDefined(DEFINED_BGCOLOR);
    }
    if (hasJsonField(newStyleJson, "textThemeIndex") || hasJsonField(newStyleJson, "textColor")) {
        result.textColor = newStyle.textColor;
        result.textThemeIndex = newStyle.textThemeIndex;
        result.textThemeTint = newStyle.textThemeTint;
        result.textIndexedColor = newStyle.textIndexedColor;
        result.setDefined(DEFINED_TEXTCOLOR);
    }
    if (hasJsonField(newStyleJson, "fontFamily")) {
        result.fontFamily = newStyle.fontFamily;
        result.setDefined(DEFINED_FONTFAMILY);
    }
    if (hasJsonField(newStyleJson, "fontSize")) {
        result.fontSize = newStyle.fontSize;
        result.setDefined(DEFINED_FONTSIZE);
    }
    if (hasJsonField(newStyleJson, "hAlign")) {
        result.hAlign = newStyle.hAlign;
        result.setDefined(DEFINED_HALIGN);
    }
    if (hasJsonField(newStyleJson, "vAlign")) {
        result.vAlign = newStyle.vAlign;
        result.setDefined(DEFINED_VALIGN);
    }
    if (hasJsonField(newStyleJson, "border")) {
        result.border = newStyle.border;
        // Copy border defined flags from newStyle
        if (newStyle.isDefined(DEFINED_BORDER_TOP))
            result.setDefined(DEFINED_BORDER_TOP);
        if (newStyle.isDefined(DEFINED_BORDER_RIGHT))
            result.setDefined(DEFINED_BORDER_RIGHT);
        if (newStyle.isDefined(DEFINED_BORDER_BOTTOM))
            result.setDefined(DEFINED_BORDER_BOTTOM);
        if (newStyle.isDefined(DEFINED_BORDER_LEFT))
            result.setDefined(DEFINED_BORDER_LEFT);
    }

    return result;
}

// Helper to strip conflicting properties from a style based on what's set in another style's JSON.
// Used for contained case: when existing range is fully inside new range, strip conflicting props.
// Returns the stripped style and a bool indicating if the style is now empty.
// Clears the defined flag for each stripped property.
std::pair<CellStyle, bool> stripConflictingProperties(const CellStyle& existingStyle,
                                                      const std::string& newStyleJson) {
    CellStyle result = existingStyle;

    // Strip properties that are set in the new style JSON
    // Clear the defined flag for each stripped property
    if (hasJsonField(newStyleJson, "bold")) {
        result.bold = false;  // Reset to default
        result.clearDefined(DEFINED_BOLD);
    }
    if (hasJsonField(newStyleJson, "italic")) {
        result.italic = false;
        result.clearDefined(DEFINED_ITALIC);
    }
    if (hasJsonField(newStyleJson, "underline")) {
        result.underline = false;
        result.clearDefined(DEFINED_UNDERLINE);
    }
    if (hasJsonField(newStyleJson, "wrapText")) {
        result.wrapText = false;
        result.clearDefined(DEFINED_WRAPTEXT);
    }
    if (hasJsonField(newStyleJson, "bgColor")) {
        result.bgColor = "";  // Reset to default
        result.clearDefined(DEFINED_BGCOLOR);
    }
    if (hasJsonField(newStyleJson, "textColor")) {
        result.textColor = "";
        result.clearDefined(DEFINED_TEXTCOLOR);
    }
    if (hasJsonField(newStyleJson, "fontFamily")) {
        result.fontFamily = "";
        result.clearDefined(DEFINED_FONTFAMILY);
    }
    if (hasJsonField(newStyleJson, "fontSize")) {
        result.fontSize = 0;
        result.clearDefined(DEFINED_FONTSIZE);
    }
    if (hasJsonField(newStyleJson, "hAlign")) {
        result.hAlign = TextAlign::GENERAL;
        result.clearDefined(DEFINED_HALIGN);
    }
    if (hasJsonField(newStyleJson, "vAlign")) {
        result.vAlign = VerticalAlign::BOTTOM;
        result.clearDefined(DEFINED_VALIGN);
    }
    if (hasJsonField(newStyleJson, "border")) {
        result.border = CellBorder();  // Reset to default (no borders)
        result.clearDefined(DEFINED_BORDER_TOP);
        result.clearDefined(DEFINED_BORDER_RIGHT);
        result.clearDefined(DEFINED_BORDER_BOTTOM);
        result.clearDefined(DEFINED_BORDER_LEFT);
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
        if (!first)
            ss << ",";
        ss << "\"bold\":true";
        first = false;
    }
    if (style.italic) {
        if (!first)
            ss << ",";
        ss << "\"italic\":true";
        first = false;
    }
    if (style.underline) {
        if (!first)
            ss << ",";
        ss << "\"underline\":true";
        first = false;
    }
    if (style.wrapText) {
        if (!first)
            ss << ",";
        ss << "\"wrapText\":true";
        first = false;
    }
    if (!style.bgColor.empty()) {
        if (!first)
            ss << ",";
        ss << "\"bgColor\":\"" << style.bgColor << "\"";
        first = false;
    }
    if (!style.textColor.empty()) {
        if (!first)
            ss << ",";
        ss << "\"textColor\":\"" << style.textColor << "\"";
        first = false;
    }
    if (!style.fontFamily.empty()) {
        if (!first)
            ss << ",";
        ss << "\"fontFamily\":\"" << style.fontFamily << "\"";
        first = false;
    }
    if (style.fontSize != 0) {
        if (!first)
            ss << ",";
        ss << "\"fontSize\":" << static_cast<int>(style.fontSize);
        first = false;
    }
    if (style.hAlign != TextAlign::GENERAL) {
        if (!first)
            ss << ",";
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
        if (!first)
            ss << ",";
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
        if (!first)
            ss << ",";
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
CellStyle stripMatchingStyleProperties(const CellStyle& cellStyle,
                                       const CellStyle& /* rangeStyle */,
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

    // Get existing style (if any) and merge with incoming JSON
    CellStyle style;
    const StyleBuffer* existingStyle = _workbook->getEntityStyle(cell->id);
    if (existingStyle != nullptr) {
        style = existingStyle->toCellStyle();
    }
    mergeStyleJson(style, styleJson);

    // Convert to content-addressed StyleBuffer and emit operation
    // Note: CellStyle.isEmpty() checks if any properties are defined, but
    // StyleBuffer.isEmpty() checks if any properties have actual values.
    // When setting borders to "none", CellStyle has defined bits set but
    // StyleBuffer is empty (no actual border values).
    if (!style.isEmpty()) {
        StyleBuffer styleBuffer = StyleBuffer::fromCellStyle(style);
        if (!styleBuffer.isEmpty()) {
            Operation op = makeCellSetStyleOp(*_workbook, cellId, styleBuffer);
            applyOperation(*_workbook, op);
        } else {
            // CellStyle has defined properties but StyleBuffer is empty
            // This happens when borders are set to "none" - clear the style
            Operation op = makeCellClearStyleOp(*_workbook, cellId);
            applyOperation(*_workbook, op);
        }
    } else {
        Operation op = makeCellClearStyleOp(*_workbook, cellId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

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

    // Shared core ensure path (same as Luau setStyle)
    bool colCreated = false;
    bool rowCreated = false;
    bool cellCreated = false;
    Cell* cell = ensureCellAtPositionViaCrdt(*_workbook, *sheet, col, row, &colCreated, &rowCreated,
                                             &cellCreated);
    if (cell == nullptr) {
        return "{\"error\":\"Failed to ensure cell\"}";
    }
    const ID cellId = cell->id;
    const ID colId = cell->colId;
    const ID rowId = cell->rowId;

    // Start from existing style (merge semantics)
    CellStyle style;
    if (cell->hasStyle()) {
        const StyleBuffer* existingStyle = _workbook->getEntityStyle(cell->id);
        if (existingStyle != nullptr) {
            style = existingStyle->toCellStyle();
        }
    }
    mergeStyleJson(style, styleJson);

    // Convert to content-addressed StyleBuffer and emit operation
    // Note: CellStyle.isEmpty() checks if any properties are defined, but
    // StyleBuffer.isEmpty() checks if any properties have actual values.
    // When setting borders to "none", CellStyle has defined bits set but
    // StyleBuffer is empty (no actual border values).
    if (!style.isEmpty()) {
        StyleBuffer styleBuffer = StyleBuffer::fromCellStyle(style);
        if (!styleBuffer.isEmpty()) {
            Operation op = makeCellSetStyleOp(*_workbook, cellId, styleBuffer);
            applyOperation(*_workbook, op);
        } else {
            // CellStyle has defined properties but StyleBuffer is empty
            // This happens when borders are set to "none" - clear the style
            Operation op = makeCellClearStyleOp(*_workbook, cellId);
            applyOperation(*_workbook, op);
        }
    } else {
        Operation op = makeCellClearStyleOp(*_workbook, cellId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    if (cellCreated) {
        _viewportIndex.onCellAdded(cell);
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

    const StyleBuffer* style = _workbook->getEntityStyle(cell->id);
    if (style != nullptr) {
        return styleToJson(style->toCellStyle());
    }

    CellStyle defaultStyle;
    return styleToJson(defaultStyle);
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
    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (colAxis == nullptr || rowAxis == nullptr) {
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    // Find cell at this position
    Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
    if (cell == nullptr) {
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const StyleBuffer* style = _workbook->getEntityStyle(cell->id);
    if (style != nullptr) {
        return styleToJson(style->toCellStyle());
    }

    CellStyle defaultStyle;
    return styleToJson(defaultStyle);
}

std::string CellsEngine::getAvailableStyles() {
    if (!_workbook) {
        return "[]";
    }

    // Content-addressed styles: return entity styles (unique styles used by entities)
    std::ostringstream ss;
    ss << "[";

    const auto& entityStyles = _workbook->getEntityStyles();
    bool first = true;
    for (const auto& [entityId, styleBuf] : entityStyles) {
        if (!first) {
            ss << ",";
        }
        first = false;

        const CellStyle style = styleBuf.toCellStyle();
        ss << "{\"entityId\":\"" << entityId.toString() << "\",\"style\":" << styleToJson(style)
           << "}";
    }

    ss << "]";
    return ss.str();
}

// =============================================================================
// Range Style Operations
// =============================================================================

std::string CellsEngine::setRangeStyle(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                       uint32_t endRow, const std::string& styleJson) {
    // Delegate to setRangeStyleOnSheet with active sheet
    return setRangeStyleOnSheet(_activeSheetIndex, startCol, startRow, endCol, endRow, styleJson);
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

    Operation removeOp = makeRangeDeleteOp(*_workbook, range->id, payload.str());
    applyOperation(*_workbook, removeOp);

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true}";
}

std::string CellsEngine::setRangeStyleOnSheet(uint32_t sheetIndex, uint32_t startCol,
                                              uint32_t startRow, uint32_t endCol, uint32_t endRow,
                                              const std::string& styleJson) {
    if (!_workbook || sheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"Invalid sheet index\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(sheetIndex);
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
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(minCol) + "}";
        Operation op = makeColSetOp(*_workbook, startColId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find end column
    Axis* endColAxis = sheet->getColumnByPosition(maxCol);
    if (endColAxis != nullptr) {
        endColId = endColAxis->id;
    }
    if (endColId.isNull()) {
        endColId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(maxCol) + "}";
        Operation op = makeColSetOp(*_workbook, endColId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find start row
    Axis* startRowAxis = sheet->getRowByPosition(minRow);
    if (startRowAxis != nullptr) {
        startRowId = startRowAxis->id;
    }
    if (startRowId.isNull()) {
        startRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(minRow) + "}";
        Operation op = makeRowSetOp(*_workbook, startRowId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find end row
    Axis* endRowAxis = sheet->getRowByPosition(maxRow);
    if (endRowAxis != nullptr) {
        endRowId = endRowAxis->id;
    }
    if (endRowId.isNull()) {
        endRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(maxRow) + "}";
        Operation op = makeRowSetOp(*_workbook, endRowId, sheet->id, payload);
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
        overlappingRanges =
            rangeIndex->queryRange(minCol, minRow, maxCol, maxRow, RangeFlags::STYLE);
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

        PositionRect existingRect{std::min(existingStartCol->position, existingEndCol->position),
                                  std::min(existingStartRow->position, existingEndRow->position),
                                  std::max(existingStartCol->position, existingEndCol->position),
                                  std::max(existingStartRow->position, existingEndRow->position)};

        // Case 1: EXACT MATCH - merge styles into existing range
        if (existingRect == newRect) {
            // Get existing style from range's StyleBuffer
            const StyleBuffer* existingStyleBuf = existingRange->getStyle();
            CellStyle existingStyleValue;
            if (existingStyleBuf != nullptr) {
                existingStyleValue = existingStyleBuf->toCellStyle();
            }

            // Merge style properties into existing style
            CellStyle mergedStyle = mergeStyles(existingStyleValue, style, styleJson);

            // If merged style is empty (all defaults), delete the range instead
            if (mergedStyle.isEmpty()) {
                std::ostringstream removePayload;
                removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
                Operation removeOp =
                    makeRangeDeleteOp(*_workbook, existingRange->id, removePayload.str());
                applyOperation(*_workbook, removeOp);

                broadcastPendingOperations();

                rebuildViewportIndex();
                notifyListeners(ChangeType::CELL_CHANGED);

                return "{\"success\":true,\"rangeId\":\"" + existingRange->id.toString() +
                       "\",\"deleted\":true}";
            }

            // Convert to content-addressed StyleBuffer and update the range
            StyleBuffer mergedStyleBuf = StyleBuffer::fromCellStyle(mergedStyle);
            Operation updateOp = makeRangeSetStyleOp(*_workbook, existingRange->id, mergedStyleBuf);
            applyOperation(*_workbook, updateOp);

            broadcastPendingOperations();

            rebuildViewportIndex();
            notifyListeners(ChangeType::CELL_CHANGED);

            // Return early - no new range needed, we merged into existing
            return "{\"success\":true,\"rangeId\":\"" + existingRange->id.toString() +
                   "\",\"merged\":true}";
        }
    }

    // After checking for exact matches, now validate the style for new range creation.
    // If the style is empty (all defaults like bold:false), we can't create a new range.
    // The only way to "clear" a style is to have an exact-match range that gets merged
    // to empty (handled above with range deletion).
    if (style.isEmpty()) {
        // Style is empty (all defaults) and no exact-match range was found.
        // This can happen when trying to "clear" a style on a range that doesn't exist.
        return "{\"error\":\"Empty style\"}";
    }

    // Convert to content-addressed StyleBuffer for the new range
    StyleBuffer newStyleBuf = StyleBuffer::fromCellStyle(style);

    // Track operations to perform after iteration (can't modify while iterating)
    struct SplitOperation {
        ID oldRangeId;
        StyleBuffer styleToPreserve;  // Style to apply to split ranges
        std::vector<PositionRect> newRects;
    };
    std::vector<SplitOperation> splitOps;

    struct ContainedOperation {
        ID rangeId;
        StyleBuffer strippedStyle;  // Style with conflicting props removed
        bool deleteRange;           // True if stripped style is empty
    };
    std::vector<ContainedOperation> containedOps;

    for (Range* existingRange : overlappingRanges) {
        if (!existingRange || !existingRange->hasFlag(RangeFlags::STYLE)) {
            continue;
        }

        // Get the existing range's style
        const StyleBuffer* existingStyleBuf = existingRange->getStyle();
        if (existingStyleBuf == nullptr) {
            continue;
        }
        CellStyle existingStyle = existingStyleBuf->toCellStyle();

        // Check if the existing style has conflicting properties with the style being applied
        std::string existingStyleJson = getStylePropertiesJson(existingStyle);
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

        PositionRect existingRect{std::min(existingStartCol->position, existingEndCol->position),
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
            auto [strippedCellStyle, isEmpty] =
                stripConflictingProperties(existingStyle, styleJson);
            ContainedOperation op;
            op.rangeId = existingRange->id;
            op.strippedStyle = StyleBuffer::fromCellStyle(strippedCellStyle);
            op.deleteRange = isEmpty;
            containedOps.push_back(std::move(op));
            continue;  // Don't also split
        }

        // Case 3: PARTIAL OVERLAP - split the existing range
        std::vector<PositionRect> splitRects = subtractRectangle(existingRect, newRect);

        SplitOperation op;
        op.oldRangeId = existingRange->id;
        op.styleToPreserve = *existingStyleBuf;  // Copy the style to preserve it
        op.newRects = std::move(splitRects);
        splitOps.push_back(std::move(op));
    }

    // Execute contained operations: update or delete ranges
    for (const ContainedOperation& containedOp : containedOps) {
        if (containedOp.deleteRange) {
            // Style is now empty, delete the range
            std::ostringstream removePayload;
            removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
            Operation removeOp =
                makeRangeDeleteOp(*_workbook, containedOp.rangeId, removePayload.str());
            applyOperation(*_workbook, removeOp);
        } else {
            // Update the range with the stripped style
            Operation updateOp =
                makeRangeSetStyleOp(*_workbook, containedOp.rangeId, containedOp.strippedStyle);
            applyOperation(*_workbook, updateOp);
        }
    }

    // Execute split operations: delete old ranges, create new split ranges
    for (const SplitOperation& splitOp : splitOps) {
        // Delete the old range
        std::ostringstream removePayload;
        removePayload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";
        Operation removeOp = makeRangeDeleteOp(*_workbook, splitOp.oldRangeId, removePayload.str());
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
                // Note: size is omitted to use local default (sizeSet=false)
                std::string payload = "{\"pos\":" + std::to_string(rect.minCol) + "}";
                Operation op = makeColSetOp(*_workbook, rectStartColId, sheet->id, payload);
                applyOperation(*_workbook, op);
            }

            // Find end column
            Axis* rectEndColAxis = sheet->getColumnByPosition(rect.maxCol);
            if (rectEndColAxis != nullptr) {
                rectEndColId = rectEndColAxis->id;
            }
            if (rectEndColId.isNull()) {
                rectEndColId = generate_id();
                // Note: size is omitted to use local default (sizeSet=false)
                std::string payload = "{\"pos\":" + std::to_string(rect.maxCol) + "}";
                Operation op = makeColSetOp(*_workbook, rectEndColId, sheet->id, payload);
                applyOperation(*_workbook, op);
            }

            // Find start row
            Axis* rectStartRowAxis = sheet->getRowByPosition(rect.minRow);
            if (rectStartRowAxis != nullptr) {
                rectStartRowId = rectStartRowAxis->id;
            }
            if (rectStartRowId.isNull()) {
                rectStartRowId = generate_id();
                // Note: size is omitted to use local default (sizeSet=false)
                std::string payload = "{\"pos\":" + std::to_string(rect.minRow) + "}";
                Operation op = makeRowSetOp(*_workbook, rectStartRowId, sheet->id, payload);
                applyOperation(*_workbook, op);
            }

            // Find end row
            Axis* rectEndRowAxis = sheet->getRowByPosition(rect.maxRow);
            if (rectEndRowAxis != nullptr) {
                rectEndRowId = rectEndRowAxis->id;
            }
            if (rectEndRowId.isNull()) {
                rectEndRowId = generate_id();
                // Note: size is omitted to use local default (sizeSet=false)
                std::string payload = "{\"pos\":" + std::to_string(rect.maxRow) + "}";
                Operation op = makeRowSetOp(*_workbook, rectEndRowId, sheet->id, payload);
                applyOperation(*_workbook, op);
            }

            // Create the new split range
            ID newRangeId = generate_id();
            std::ostringstream newRangePayload;
            newRangePayload << "{\"startCol\":\"" << rectStartColId.toString() << "\",";
            newRangePayload << "\"startRow\":\"" << rectStartRowId.toString() << "\",";
            newRangePayload << "\"endCol\":\"" << rectEndColId.toString() << "\",";
            newRangePayload << "\"endRow\":\"" << rectEndRowId.toString() << "\",";
            newRangePayload << "\"flags\":" << static_cast<int>(RangeFlags::STYLE) << "}";

            Operation newRangeOp = makeRangeSetOp(*_workbook, newRangeId, newRangePayload.str());
            applyOperation(*_workbook, newRangeOp);

            // Apply the preserved style to the new split range
            Operation newSetStyleOp =
                makeRangeSetStyleOp(*_workbook, newRangeId, splitOp.styleToPreserve);
            applyOperation(*_workbook, newSetStyleOp);
        }
    }

    // Create a new Range with RANGE_STYLE flag
    ID rangeId = generate_id();
    std::ostringstream rangePayload;
    rangePayload << "{\"startCol\":\"" << startColId.toString() << "\",";
    rangePayload << "\"startRow\":\"" << startRowId.toString() << "\",";
    rangePayload << "\"endCol\":\"" << endColId.toString() << "\",";
    rangePayload << "\"endRow\":\"" << endRowId.toString() << "\",";
    rangePayload << "\"flags\":" << static_cast<int>(RangeFlags::STYLE) << "}";

    Operation rangeOp = makeRangeSetOp(*_workbook, rangeId, rangePayload.str());
    applyOperation(*_workbook, rangeOp);

    // Associate the style with the range
    Operation setStyleOp = makeRangeSetStyleOp(*_workbook, rangeId, newStyleBuf);
    applyOperation(*_workbook, setStyleOp);

    // Clear redundant cell-level styles within the range (I2: Range style clears cell styles)
    // When applying a range style, remove matching properties from individual cells to avoid
    // redundancy
    for (const auto& cellId : sheet->getCellIds()) {
        // Read style from entity (content-addressed)
        const StyleBuffer* cellStyleBuf = _workbook->getEntityStyle(cellId);
        if (cellStyleBuf == nullptr) {
            continue;  // Cell has no style, skip
        }

        Cell* cell = _workbook->getCell(cellId);
        if (!cell)
            continue;

        // Check if cell is within the range bounds
        const Axis* cellCol = sheet->getColumn(cell->colId);
        const Axis* cellRow = sheet->getRow(cell->rowId);
        if (cellCol == nullptr || cellRow == nullptr) {
            continue;
        }

        const uint32_t cellColPos = cellCol->position;
        const uint32_t cellRowPos = cellRow->position;
        if (cellColPos < minCol || cellColPos > maxCol || cellRowPos < minRow ||
            cellRowPos > maxRow) {
            continue;  // Cell is outside the range
        }

        // Get the cell's current style
        const CellStyle cellStyle = cellStyleBuf->toCellStyle();

        // Strip properties that match the range style
        CellStyle strippedStyle = stripMatchingStyleProperties(cellStyle, style, styleJson);

        // If the stripped style is empty, clear the cell's style
        // Otherwise, update the cell with the stripped style (content-addressed)
        if (strippedStyle.isEmpty()) {
            // Clear the cell's style using content-addressed operation
            Operation clearOp = makeCellClearStyleOp(*_workbook, cellId);
            applyOperation(*_workbook, clearOp);
        } else if (strippedStyle != cellStyle) {
            // Style changed, need to update the cell with new content-addressed style
            StyleBuffer strippedBuf = StyleBuffer::fromCellStyle(strippedStyle);
            Operation updateOp = makeCellSetStyleOp(*_workbook, cellId, strippedBuf);
            applyOperation(*_workbook, updateOp);
        }
    }

    broadcastPendingOperations();

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"rangeId\":\"" + rangeId.toString() + "\"}";
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
// The defined flag is the source of truth - only defined properties participate in merges.
CellStyle mergeEffectiveStyles(const CellStyle& base, const CellStyle& overlay) {
    CellStyle result = base;

    // Merge boolean properties (overlay wins if overlay is defined and base is not)
    if (overlay.isDefined(DEFINED_BOLD) && !result.isDefined(DEFINED_BOLD)) {
        result.bold = overlay.bold;
        result.setDefined(DEFINED_BOLD);
    }
    if (overlay.isDefined(DEFINED_ITALIC) && !result.isDefined(DEFINED_ITALIC)) {
        result.italic = overlay.italic;
        result.setDefined(DEFINED_ITALIC);
    }
    if (overlay.isDefined(DEFINED_UNDERLINE) && !result.isDefined(DEFINED_UNDERLINE)) {
        result.underline = overlay.underline;
        result.setDefined(DEFINED_UNDERLINE);
    }
    if (overlay.isDefined(DEFINED_WRAPTEXT) && !result.isDefined(DEFINED_WRAPTEXT)) {
        result.wrapText = overlay.wrapText;
        result.setDefined(DEFINED_WRAPTEXT);
    }

    // Merge string properties (overlay wins if overlay is defined and base is not)
    if (overlay.isDefined(DEFINED_BGCOLOR) && !result.isDefined(DEFINED_BGCOLOR)) {
        result.bgColor = overlay.bgColor;
        result.setDefined(DEFINED_BGCOLOR);
    }
    if (overlay.isDefined(DEFINED_TEXTCOLOR) && !result.isDefined(DEFINED_TEXTCOLOR)) {
        result.textColor = overlay.textColor;
        result.setDefined(DEFINED_TEXTCOLOR);
    }
    if (overlay.isDefined(DEFINED_FONTFAMILY) && !result.isDefined(DEFINED_FONTFAMILY)) {
        result.fontFamily = overlay.fontFamily;
        result.setDefined(DEFINED_FONTFAMILY);
    }

    // Merge numeric properties (overlay wins if overlay is defined and base is not)
    if (overlay.isDefined(DEFINED_FONTSIZE) && !result.isDefined(DEFINED_FONTSIZE)) {
        result.fontSize = overlay.fontSize;
        result.setDefined(DEFINED_FONTSIZE);
    }

    // Merge alignment (overlay wins if overlay is defined and base is not)
    if (overlay.isDefined(DEFINED_HALIGN) && !result.isDefined(DEFINED_HALIGN)) {
        result.hAlign = overlay.hAlign;
        result.setDefined(DEFINED_HALIGN);
    }
    if (overlay.isDefined(DEFINED_VALIGN) && !result.isDefined(DEFINED_VALIGN)) {
        result.vAlign = overlay.vAlign;
        result.setDefined(DEFINED_VALIGN);
    }

    // Merge borders (each edge individually, overlay wins if defined and base is not)
    if (overlay.isDefined(DEFINED_BORDER_TOP) && !result.isDefined(DEFINED_BORDER_TOP)) {
        result.border.top = overlay.border.top;
        result.setDefined(DEFINED_BORDER_TOP);
    }
    if (overlay.isDefined(DEFINED_BORDER_RIGHT) && !result.isDefined(DEFINED_BORDER_RIGHT)) {
        result.border.right = overlay.border.right;
        result.setDefined(DEFINED_BORDER_RIGHT);
    }
    if (overlay.isDefined(DEFINED_BORDER_BOTTOM) && !result.isDefined(DEFINED_BORDER_BOTTOM)) {
        result.border.bottom = overlay.border.bottom;
        result.setDefined(DEFINED_BORDER_BOTTOM);
    }
    if (overlay.isDefined(DEFINED_BORDER_LEFT) && !result.isDefined(DEFINED_BORDER_LEFT)) {
        result.border.left = overlay.border.left;
        result.setDefined(DEFINED_BORDER_LEFT);
    }

    return result;
}

// Computes the effective style at a position, considering all style sources.
// Does not require a Cell object - works for empty cells too.
// Note: sheet is non-const because getCellAt/getRangesAt are non-const
//
// Style Priority Order (highest to lowest):
// 1. Cell style - Properties set directly on the cell always win
// 2. Range styles - Applied via RANGE_STYLE ranges (merged in application order)
// 3. Column style - Default formatting for the entire column
// 4. Row style - Default formatting for the entire row
//
// This cascading allows users to set column-wide defaults that can be overridden
// by specific ranges or individual cells. Column styles take precedence over row
// styles at intersections, following common spreadsheet UX patterns.
CellStyle computeEffectiveStyleAt(Sheet& sheet, const Workbook& workbook, uint32_t colPos,
                                  uint32_t rowPos, ID colId, ID rowId) {
    CellStyle result;

    // Find cell at this position (may be null)
    Cell* cell = nullptr;
    if (!colId.isNull() && !rowId.isNull()) {
        cell = sheet.getCellAt(colId, rowId);
    }

    // Priority 1: Cell's own style (highest priority - properties set here take precedence)
    if (cell != nullptr) {
        const StyleBuffer* cellStyle = workbook.getEntityStyle(cell->id);
        if (cellStyle != nullptr) {
            result = cellStyle->toCellStyle();
        }
    }

    // Priority 2: Range styles (merge all overlapping RANGE_STYLE ranges)
    std::vector<Range*> styleRanges = sheet.getRangesAt(colPos, rowPos, RangeFlags::STYLE);
    for (Range* range : styleRanges) {
        const StyleBuffer* rangeStyleBuf = range->getStyle();
        if (rangeStyleBuf != nullptr) {
            CellStyle rangeStyle = rangeStyleBuf->toCellStyle();
            result = mergeEffectiveStyles(result, rangeStyle);
        }
    }

    // Priority 3: Column's default style
    if (!colId.isNull()) {
        const Axis* colAxis = sheet.getColumn(colId);
        if (colAxis != nullptr && colAxis->hasStyle()) {
            const StyleBuffer* colStyle = workbook.getEntityStyle(colAxis->id);
            if (colStyle != nullptr) {
                result = mergeEffectiveStyles(result, colStyle->toCellStyle());
            }
        }
    }

    // Priority 4: Row's default style
    if (!rowId.isNull()) {
        const Axis* rowAxis = sheet.getRow(rowId);
        if (rowAxis != nullptr && rowAxis->hasStyle()) {
            const StyleBuffer* rowStyle = workbook.getEntityStyle(rowAxis->id);
            if (rowStyle != nullptr) {
                result = mergeEffectiveStyles(result, rowStyle->toCellStyle());
            }
        }
    }

    // Resolve theme/indexed color references to hex
    const Theme* theme = workbook.getTheme();
    if (result.hasBgThemeColor()) {
        result.bgColor = resolveThemeColor(theme, result.bgThemeIndex, result.bgThemeTint);
    } else if (result.hasBgIndexedColor()) {
        result.bgColor = resolveIndexedColor(result.bgIndexedColor);
    }
    if (result.hasTextThemeColor()) {
        result.textColor = resolveThemeColor(theme, result.textThemeIndex, result.textThemeTint);
    } else if (result.hasTextIndexedColor()) {
        result.textColor = resolveIndexedColor(result.textIndexedColor);
    }
    if (result.hasFontTheme()) {
        std::string resolved = resolveThemeFont(theme, result.fontThemeIndex);
        if (!resolved.empty()) {
            result.fontFamily = resolved;
        }
    }
    auto resolveBorderColor = [&](BorderEdge& edge) {
        if (edge.themeIndex >= 0) {
            edge.color = resolveThemeColor(theme, edge.themeIndex, edge.themeTint);
        } else if (edge.indexedColor >= 0) {
            edge.color = resolveIndexedColor(edge.indexedColor);
        }
    };
    resolveBorderColor(result.border.top);
    resolveBorderColor(result.border.right);
    resolveBorderColor(result.border.bottom);
    resolveBorderColor(result.border.left);

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

std::string CellsEngine::getEffectiveStyleForRange(uint32_t col1, uint32_t row1, uint32_t col2,
                                                   uint32_t row2) {
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
    CellStyle firstStyle =
        computeEffectiveStyleAt(*sheet, *_workbook, minCol, minRow, firstColId, firstRowId);

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
            if (c == minCol && r == minRow)
                continue;  // Skip anchor

            ID colId = colIdByPos.count(c) > 0 ? colIdByPos[c] : ID();
            ID rowId = rowIdByPos.count(r) > 0 ? rowIdByPos[r] : ID();
            CellStyle cellStyle = computeEffectiveStyleAt(*sheet, *_workbook, c, r, colId, rowId);

            // Compare each property
            if (cellStyle.bold != firstStyle.bold)
                mixedBold = true;
            if (cellStyle.italic != firstStyle.italic)
                mixedItalic = true;
            if (cellStyle.underline != firstStyle.underline)
                mixedUnderline = true;
            if (cellStyle.wrapText != firstStyle.wrapText)
                mixedWrapText = true;
            if (cellStyle.bgColor != firstStyle.bgColor)
                mixedBgColor = true;
            if (cellStyle.textColor != firstStyle.textColor)
                mixedTextColor = true;
            if (cellStyle.fontFamily != firstStyle.fontFamily)
                mixedFontFamily = true;
            if (cellStyle.fontSize != firstStyle.fontSize)
                mixedFontSize = true;
            if (cellStyle.hAlign != firstStyle.hAlign)
                mixedHAlign = true;
            if (cellStyle.vAlign != firstStyle.vAlign)
                mixedVAlign = true;
        }
    }

    // Build JSON response
    std::ostringstream ss;
    ss << "{\"style\":" << styleToJson(firstStyle) << ",\"mixed\":{";
    bool first = true;
    if (mixedBold) {
        ss << "\"bold\":true";
        first = false;
    }
    if (mixedItalic) {
        if (!first)
            ss << ",";
        ss << "\"italic\":true";
        first = false;
    }
    if (mixedUnderline) {
        if (!first)
            ss << ",";
        ss << "\"underline\":true";
        first = false;
    }
    if (mixedWrapText) {
        if (!first)
            ss << ",";
        ss << "\"wrapText\":true";
        first = false;
    }
    if (mixedBgColor) {
        if (!first)
            ss << ",";
        ss << "\"bgColor\":true";
        first = false;
    }
    if (mixedTextColor) {
        if (!first)
            ss << ",";
        ss << "\"textColor\":true";
        first = false;
    }
    if (mixedFontFamily) {
        if (!first)
            ss << ",";
        ss << "\"fontFamily\":true";
        first = false;
    }
    if (mixedFontSize) {
        if (!first)
            ss << ",";
        ss << "\"fontSize\":true";
        first = false;
    }
    if (mixedHAlign) {
        if (!first)
            ss << ",";
        ss << "\"hAlign\":true";
        first = false;
    }
    if (mixedVAlign) {
        if (!first)
            ss << ",";
        ss << "\"vAlign\":true";
        first = false;
    }
    ss << "}}";

    return ss.str();
}

// ============================================================================
// Axis Style Operations - Set/Get styles for entire columns or rows
// ============================================================================

std::string CellsEngine::setColumnStyle(uint32_t colPosition, const std::string& styleJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find existing column at position
    Axis* existingCol = sheet->getColumnByPosition(colPosition);
    ID colId;
    bool colCreated = false;

    if (existingCol != nullptr) {
        colId = existingCol->id;
    } else {
        // Create column if it doesn't exist
        colId = generate_id();
        colCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(colPosition) + "}";
        Operation colOp = makeColSetOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    // Get existing style if column has one
    CellStyle style;
    Axis* col = sheet->getColumn(colId);
    if (col != nullptr && col->hasStyle()) {
        const StyleBuffer* existingStyle = _workbook->getEntityStyle(col->id);
        if (existingStyle != nullptr) {
            style = existingStyle->toCellStyle();
        }
    }

    // Merge incoming JSON with existing style
    mergeStyleJson(style, styleJson);

    // Convert to content-addressed StyleBuffer and emit operation
    if (!style.isEmpty()) {
        StyleBuffer styleBuffer = StyleBuffer::fromCellStyle(style);
        if (!styleBuffer.isEmpty()) {
            Operation op = makeAxisSetStyleOp(*_workbook, colId, styleBuffer);
            applyOperation(*_workbook, op);
        } else {
            // Style properties resolved to empty - clear the style
            Operation op = makeAxisClearStyleOp(*_workbook, colId);
            applyOperation(*_workbook, op);
        }
    } else {
        Operation op = makeAxisClearStyleOp(*_workbook, colId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, colPosition, DEFAULT_COLUMN_WIDTH);
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::setRowStyle(uint32_t rowPosition, const std::string& styleJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find existing row at position
    Axis* existingRow = sheet->getRowByPosition(rowPosition);
    ID rowId;
    bool rowCreated = false;

    if (existingRow != nullptr) {
        rowId = existingRow->id;
    } else {
        // Create row if it doesn't exist
        rowId = generate_id();
        rowCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(rowPosition) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Get existing style if row has one
    CellStyle style;
    Axis* row = sheet->getRow(rowId);
    if (row != nullptr && row->hasStyle()) {
        const StyleBuffer* existingStyle = _workbook->getEntityStyle(row->id);
        if (existingStyle != nullptr) {
            style = existingStyle->toCellStyle();
        }
    }

    // Merge incoming JSON with existing style
    mergeStyleJson(style, styleJson);

    // Convert to content-addressed StyleBuffer and emit operation
    if (!style.isEmpty()) {
        StyleBuffer styleBuffer = StyleBuffer::fromCellStyle(style);
        if (!styleBuffer.isEmpty()) {
            Operation op = makeAxisSetStyleOp(*_workbook, rowId, styleBuffer);
            applyOperation(*_workbook, op);
        } else {
            // Style properties resolved to empty - clear the style
            Operation op = makeAxisClearStyleOp(*_workbook, rowId);
            applyOperation(*_workbook, op);
        }
    } else {
        Operation op = makeAxisClearStyleOp(*_workbook, rowId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, rowPosition, DEFAULT_ROW_HEIGHT);
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::getColumnStyle(uint32_t colPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    Axis* col = sheet->getColumnByPosition(colPosition);
    if (col == nullptr || !col->hasStyle()) {
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const StyleBuffer* style = _workbook->getEntityStyle(col->id);
    if (style != nullptr) {
        return styleToJson(style->toCellStyle());
    }

    CellStyle defaultStyle;
    return styleToJson(defaultStyle);
}

std::string CellsEngine::getRowStyle(uint32_t rowPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    Axis* row = sheet->getRowByPosition(rowPosition);
    if (row == nullptr || !row->hasStyle()) {
        CellStyle defaultStyle;
        return styleToJson(defaultStyle);
    }

    const StyleBuffer* style = _workbook->getEntityStyle(row->id);
    if (style != nullptr) {
        return styleToJson(style->toCellStyle());
    }

    CellStyle defaultStyle;
    return styleToJson(defaultStyle);
}

std::string CellsEngine::getColumnFormat(uint32_t colPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    Axis* col = sheet->getColumnByPosition(colPosition);
    if (col == nullptr || !col->hasFormat()) {
        return "{}";
    }

    const FormatBuffer* format = _workbook->getEntityFormat(col->id);
    if (format != nullptr && !format->isEmpty()) {
        // Return format properties as JSON
        std::ostringstream json;
        json << "{";
        json << "\"category\":\"" << formatCategoryToString(format->getCategory()) << "\"";
        if (format->hasDecimals()) {
            json << ",\"decimals\":" << static_cast<int>(format->getDecimals());
        }
        if (format->hasThousandsSeparator()) {
            json << ",\"separator\":" << (format->getThousandsSeparator() ? "true" : "false");
        }
        if (format->hasCurrencySymbol()) {
            json << ",\"currency\":\"" << format->getCurrencySymbol() << "\"";
        }
        if (format->hasCustomFormatCode()) {
            // Escape quotes in format code
            std::string code = format->getCustomFormatCode();
            std::string escaped;
            for (char c : code) {
                if (c == '"')
                    escaped += "\\\"";
                else if (c == '\\')
                    escaped += "\\\\";
                else
                    escaped += c;
            }
            json << ",\"formatCode\":\"" << escaped << "\"";
        }
        json << ",\"base64\":\"" << format->toBase64() << "\"";
        json << "}";
        return json.str();
    }

    return "{}";
}

std::string CellsEngine::getRowFormat(uint32_t rowPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{}";
    }

    Axis* row = sheet->getRowByPosition(rowPosition);
    if (row == nullptr || !row->hasFormat()) {
        return "{}";
    }

    const FormatBuffer* format = _workbook->getEntityFormat(row->id);
    if (format != nullptr && !format->isEmpty()) {
        // Return format properties as JSON
        std::ostringstream json;
        json << "{";
        json << "\"category\":\"" << formatCategoryToString(format->getCategory()) << "\"";
        if (format->hasDecimals()) {
            json << ",\"decimals\":" << static_cast<int>(format->getDecimals());
        }
        if (format->hasThousandsSeparator()) {
            json << ",\"separator\":" << (format->getThousandsSeparator() ? "true" : "false");
        }
        if (format->hasCurrencySymbol()) {
            json << ",\"currency\":\"" << format->getCurrencySymbol() << "\"";
        }
        if (format->hasCustomFormatCode()) {
            // Escape quotes in format code
            std::string code = format->getCustomFormatCode();
            std::string escaped;
            for (char c : code) {
                if (c == '"')
                    escaped += "\\\"";
                else if (c == '\\')
                    escaped += "\\\\";
                else
                    escaped += c;
            }
            json << ",\"formatCode\":\"" << escaped << "\"";
        }
        json << ",\"base64\":\"" << format->toBase64() << "\"";
        json << "}";
        return json.str();
    }

    return "{}";
}

// ============================================================================
// Axis Format Operations (column/row formats)
// ============================================================================

std::string CellsEngine::setColumnFormat(uint32_t colPosition, const std::string& formatJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find existing column at position
    Axis* existingCol = sheet->getColumnByPosition(colPosition);
    ID colId;
    bool colCreated = false;

    if (existingCol != nullptr) {
        colId = existingCol->id;
    } else {
        // Create column if it doesn't exist
        colId = generate_id();
        colCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(colPosition) + "}";
        Operation colOp = makeColSetOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    // Parse format JSON and create FormatBuffer
    FormatBuffer format = parseFormatJson(formatJson);

    // Apply format to column
    if (!format.isEmpty()) {
        Operation op = makeAxisSetFormatOp(*_workbook, colId, format);
        applyOperation(*_workbook, op);
    } else {
        // Empty format - clear the format
        Operation op = makeAxisClearFormatOp(*_workbook, colId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, colPosition, DEFAULT_COLUMN_WIDTH);
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::setRowFormat(uint32_t rowPosition, const std::string& formatJson) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find existing row at position
    Axis* existingRow = sheet->getRowByPosition(rowPosition);
    ID rowId;
    bool rowCreated = false;

    if (existingRow != nullptr) {
        rowId = existingRow->id;
    } else {
        // Create row if it doesn't exist
        rowId = generate_id();
        rowCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(rowPosition) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Parse format JSON and create FormatBuffer
    FormatBuffer format = parseFormatJson(formatJson);

    // Apply format to row
    if (!format.isEmpty()) {
        Operation op = makeAxisSetFormatOp(*_workbook, rowId, format);
        applyOperation(*_workbook, op);
    } else {
        // Empty format - clear the format
        Operation op = makeAxisClearFormatOp(*_workbook, rowId);
        applyOperation(*_workbook, op);
    }

    broadcastPendingOperations();

    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, rowPosition, DEFAULT_ROW_HEIGHT);
    }

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::clearColumnFormat(uint32_t colPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    Axis* col = sheet->getColumnByPosition(colPosition);
    if (col == nullptr) {
        return "{\"success\":true}";  // No column, nothing to clear
    }

    Operation op = makeAxisClearFormatOp(*_workbook, col->id);
    applyOperation(*_workbook, op);

    broadcastPendingOperations();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::clearRowFormat(uint32_t rowPosition) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    Axis* row = sheet->getRowByPosition(rowPosition);
    if (row == nullptr) {
        return "{\"success\":true}";  // No row, nothing to clear
    }

    Operation op = makeAxisClearFormatOp(*_workbook, row->id);
    applyOperation(*_workbook, op);

    broadcastPendingOperations();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

// ============================================================================
// Range Format Operations
// ============================================================================

std::string CellsEngine::setRangeFormat(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                        uint32_t endRow, const std::string& formatJson) {
    // Delegate to setRangeFormatOnSheet with active sheet
    return setRangeFormatOnSheet(_activeSheetIndex, startCol, startRow, endCol, endRow, formatJson);
}

std::string CellsEngine::setRangeFormatOnSheet(uint32_t sheetIndex, uint32_t startCol,
                                               uint32_t startRow, uint32_t endCol, uint32_t endRow,
                                               const std::string& formatJson) {
    if (!_workbook || sheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"Invalid sheet index\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(sheetIndex);
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
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(minCol) + "}";
        Operation op = makeColSetOp(*_workbook, startColId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find end column
    Axis* endColAxis = sheet->getColumnByPosition(maxCol);
    if (endColAxis != nullptr) {
        endColId = endColAxis->id;
    }
    if (endColId.isNull()) {
        endColId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(maxCol) + "}";
        Operation op = makeColSetOp(*_workbook, endColId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find start row
    Axis* startRowAxis = sheet->getRowByPosition(minRow);
    if (startRowAxis != nullptr) {
        startRowId = startRowAxis->id;
    }
    if (startRowId.isNull()) {
        startRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(minRow) + "}";
        Operation op = makeRowSetOp(*_workbook, startRowId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Find end row
    Axis* endRowAxis = sheet->getRowByPosition(maxRow);
    if (endRowAxis != nullptr) {
        endRowId = endRowAxis->id;
    }
    if (endRowId.isNull()) {
        endRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string payload = "{\"pos\":" + std::to_string(maxRow) + "}";
        Operation op = makeRowSetOp(*_workbook, endRowId, sheet->id, payload);
        applyOperation(*_workbook, op);
    }

    // Parse format JSON
    FormatBuffer format = parseFormatJson(formatJson);

    // Create range insert payload with FORMAT flag
    std::ostringstream insertPayload;
    insertPayload << "{\"startCol\":\"" << startColId.toString() << "\""
                  << ",\"startRow\":\"" << startRowId.toString() << "\""
                  << ",\"endCol\":\"" << endColId.toString() << "\""
                  << ",\"endRow\":\"" << endRowId.toString() << "\""
                  << ",\"flags\":" << static_cast<int>(RangeFlags::FORMAT) << "}";

    ID rangeId = generate_id();
    Operation rangeOp = makeRangeSetOp(*_workbook, rangeId, insertPayload.str());
    applyOperation(*_workbook, rangeOp);

    // Set format on the range
    if (!format.isEmpty()) {
        Operation formatOp = makeRangeSetFormatOp(*_workbook, rangeId, format);
        applyOperation(*_workbook, formatOp);
    }

    broadcastPendingOperations();

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"range_id\":\"" + rangeId.toString() + "\"}";
}

std::string CellsEngine::removeRangeFormat(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find format ranges at this position
    std::vector<Range*> formatRanges = sheet->getRangesAt(col, row, RangeFlags::FORMAT);
    if (formatRanges.empty()) {
        return "{\"error\":\"No format range found at this position\"}";
    }

    // Remove the first format range found
    Range* range = formatRanges[0];
    std::ostringstream payload;
    payload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";

    Operation removeOp = makeRangeDeleteOp(*_workbook, range->id, payload.str());
    applyOperation(*_workbook, removeOp);

    broadcastPendingOperations();

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true}";
}

// ============================================================================
// Cell style presets
// ============================================================================

std::string CellsEngine::getCellStylePresets() {
    auto presets = cells::getBuiltinCellStylePresets();
    const Theme* theme = _workbook ? _workbook->getTheme() : nullptr;

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& preset : presets) {
        if (!first) {
            ss << ",";
        }
        first = false;

        // Resolve theme colors to hex for preview rendering
        CellStyle resolved = cells::resolvePresetPreviewColors(preset.style, theme);

        ss << "{\"name\":\"" << jsonEscape(preset.name) << "\",\"category\":\""
           << jsonEscape(preset.category) << "\",\"style\":" << styleToJson(resolved);

        if (!preset.formatCode.empty()) {
            ss << ",\"formatCode\":\"" << jsonEscape(preset.formatCode) << "\"";
        }

        ss << "}";
    }
    ss << "]";
    return ss.str();
}

}  // namespace cells::wasm
