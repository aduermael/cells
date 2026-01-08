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

#include <iomanip>
#include <sstream>

#include "core/cells/crdt.h"
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

}  // namespace cells::wasm
