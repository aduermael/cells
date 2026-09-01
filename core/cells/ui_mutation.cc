#include "core/cells/ui_mutation.h"

#include <cstdio>
#include <cstdlib>

#include <string>

#include "core/cells/format_buffer.h"
#include "core/cells/input_parser.h"
#include "core/cells/model.h"
#include "core/cells/number_format.h"
#include "core/cells/operation.h"

namespace cells {
namespace {

std::string luaQuote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 32) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

std::string luaNumber(double n) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", n);
    return buf;
}

bool wholeStringNumber(const std::string& value, double* out) {
    if (value.empty()) {
        return false;
    }
    char* endptr = nullptr;
    const double n = strtod(value.c_str(), &endptr);
    if (endptr == nullptr || *endptr != '\0' || endptr == value.c_str()) {
        return false;
    }
    *out = n;
    return true;
}

std::string formatBufferFromId(const ID& formatId) {
    if (formatId.isNull()) {
        return {};
    }
    const ParsedFormatId parsed = parseFormatId(formatId.toString());
    if (!parsed.valid) {
        return {};
    }
    FormatBuffer buf;
    buf.setCategory(parsed.category);
    if (parsed.decimalPlaces > 0) {
        buf.setDecimals(parsed.decimalPlaces);
    }
    if (parsed.useThousandsSeparator) {
        buf.setThousandsSeparator(true);
    }
    if (!parsed.currencyCode.empty()) {
        buf.setCurrencySymbol(getCurrencySymbol(parsed.currencyCode));
    }
    return buf.toBase64();
}

Cell* cellAtPosition(Sheet& sheet, uint32_t col, uint32_t row) {
    Axis* colAxis = sheet.getColumnByPosition(col);
    Axis* rowAxis = sheet.getRowByPosition(row);
    if (colAxis == nullptr || rowAxis == nullptr) {
        return nullptr;
    }
    return sheet.getCellAt(colAxis->id, rowAxis->id);
}

UiCellWriteResult resultFromCell(Sheet& sheet, uint32_t col, uint32_t row, Workbook& workbook) {
    UiCellWriteResult out;
    Cell* cell = cellAtPosition(sheet, col, row);
    if (cell == nullptr) {
        out.error = "Cell not found after script";
        return out;
    }
    out.success = true;
    out.cellId = cell->id;
    const FormatBuffer* fmt = workbook.getEntityFormat(cell->id);
    if (fmt != nullptr) {
        out.formatBase64 = fmt->toBase64();
    }
    return out;
}

}  // namespace

std::string a1FromPosition(uint32_t col, uint32_t row) {
    return Sheet::positionToColumnName(col) + std::to_string(row + 1);
}

ScriptResult executeUiMutation(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                               const std::string& script) {
    sandbox.setContext(&workbook, &sheet);
    return sandbox.execute(script);
}

UiCellWriteResult uiWriteCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet, uint32_t col,
                              uint32_t row, const std::string& value, bool detectFormat) {
    const std::string a1 = a1FromPosition(col, row);
    std::string script = "setCell(" + luaQuote(a1) + ", ";

    auto emitString = [&](const std::string& s) { script += luaQuote(s); };

    if (!value.empty() && value[0] == '=') {
        emitString(value);
    } else if (value == "TRUE" || value == "true") {
        script += "true";
    } else if (value == "FALSE" || value == "false") {
        script += "false";
    } else if (detectFormat) {
        const ParsedInput parsed = parseUserInput(value);
        if (parsed.success && parsed.valueType == CellValueType::NUMBER) {
            script += luaNumber(parsed.numericValue);
            const std::string fmt = formatBufferFromId(parsed.formatId);
            if (!fmt.empty()) {
                script += ")\nsetFormat(" + luaQuote(a1) + ", " + luaQuote(fmt);
            }
        } else {
            emitString(value);
        }
    } else {
        double n = 0.0;
        if (wholeStringNumber(value, &n)) {
            script += luaNumber(n);
        } else {
            emitString(value);
        }
    }
    script += ")";

    const ScriptResult sr = executeUiMutation(sandbox, workbook, sheet, script);
    if (!sr.success) {
        UiCellWriteResult out;
        out.error = sr.error.empty() ? "Luau execution failed" : sr.error;
        return out;
    }
    return resultFromCell(sheet, col, row, workbook);
}

UiCellWriteResult uiWriteCellById(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                  const ID& cellId, const std::string& value, bool detectFormat) {
    Cell* cell = sheet.getCell(cellId);
    if (cell == nullptr) {
        UiCellWriteResult out;
        out.error = "Cell not found";
        return out;
    }
    const Axis* col = sheet.getColumn(cell->colId);
    const Axis* row = sheet.getRow(cell->rowId);
    if (col == nullptr || row == nullptr) {
        UiCellWriteResult out;
        out.error = "Cell axis not found";
        return out;
    }
    return uiWriteCell(sandbox, workbook, sheet, col->position, row->position, value, detectFormat);
}

UiCellWriteResult uiEnsureCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet, uint32_t col,
                               uint32_t row) {
    const std::string a1 = a1FromPosition(col, row);
    const std::string script = "getCell(" + luaQuote(a1) + ", {create = true})";
    const ScriptResult sr = executeUiMutation(sandbox, workbook, sheet, script);
    if (!sr.success) {
        UiCellWriteResult out;
        out.error = sr.error.empty() ? "Luau execution failed" : sr.error;
        return out;
    }
    return resultFromCell(sheet, col, row, workbook);
}

bool uiDeleteCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet, const ID& cellId,
                  std::string* error) {
    Cell* cell = sheet.getCell(cellId);
    if (cell == nullptr) {
        if (error != nullptr) {
            *error = "Cell not found";
        }
        return false;
    }
    const Axis* col = sheet.getColumn(cell->colId);
    const Axis* row = sheet.getRow(cell->rowId);
    if (col == nullptr || row == nullptr) {
        if (error != nullptr) {
            *error = "Cell axis not found";
        }
        return false;
    }
    const std::string a1 = a1FromPosition(col->position, row->position);
    const std::string script = "setCell(" + luaQuote(a1) + ", nil)";
    const ScriptResult sr = executeUiMutation(sandbox, workbook, sheet, script);
    if (!sr.success) {
        if (error != nullptr) {
            *error = sr.error.empty() ? "Luau execution failed" : sr.error;
        }
        return false;
    }
    return true;
}

ApplyResult uiApplyOperation(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                             const Operation& op) {
    sandbox.queueUiOperation(op);
    const ScriptResult sr = executeUiMutation(sandbox, workbook, sheet, "_applyUiOp()");
    if (!sr.success) {
        return ApplyResult::INVALID_PAYLOAD;
    }
    return sandbox.lastUiApplyResult();
}

ScriptResult uiFreezePanes(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet, int freezeCol,
                           int freezeRow) {
    if (freezeCol < 0) {
        freezeCol = 0;
    }
    if (freezeRow < 0) {
        freezeRow = 0;
    }
    return executeUiMutation(
        sandbox, workbook, sheet,
        "freezePanes(" + std::to_string(freezeCol) + ", " + std::to_string(freezeRow) + ")");
}

ScriptResult uiSetDocumentTitle(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                const std::string& title) {
    return executeUiMutation(sandbox, workbook, sheet, "setDocumentTitle(" + luaQuote(title) + ")");
}

ScriptResult uiMoveSheet(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet, int fromIndex,
                         int toIndex) {
    return executeUiMutation(
        sandbox, workbook, sheet,
        "moveSheet(" + std::to_string(fromIndex) + ", " + std::to_string(toIndex) + ")");
}

ScriptResult uiSetTheme(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                        const std::string& themeJson) {
    return executeUiMutation(sandbox, workbook, sheet, "setTheme(" + luaQuote(themeJson) + ")");
}

}  // namespace cells
