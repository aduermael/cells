// =============================================================================
// Office.js Excel add-in host for QuickJS
// =============================================================================
//
// Native flush applies batched Range/Worksheet/Name commands to the real
// workbook via CRDT helpers (same path Luau uses). A JS bootstrap provides
// Office / Excel / OfficeExtension globals with load/sync semantics.
//
// =============================================================================

#include "core/cells/officejs_api.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/format_buffer.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/js_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/cells/ref_converter.h"
#include "core/cells/style_buffer.h"
#include "core/cells/style_types.h"

#include "quickjs.h"  // NOLINT(build/include_subdir)

namespace cells {
namespace {

JsSandbox* sandboxFrom(JSContext* ctx) {
    return static_cast<JsSandbox*>(JS_GetContextOpaque(ctx));
}

std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

std::string getStr(JSContext* ctx, JSValueConst obj, const char* prop) {
    const JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        JS_FreeValue(ctx, v);
        return "";
    }
    const char* s = JS_ToCString(ctx, v);
    std::string out = s != nullptr ? s : "";
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
    return out;
}

int32_t getInt(JSContext* ctx, JSValueConst obj, const char* prop, int32_t def = 0) {
    const JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    int32_t n = def;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        JS_ToInt32(ctx, &n, v);
    }
    JS_FreeValue(ctx, v);
    return n;
}

double getNumber(JSContext* ctx, JSValueConst obj, const char* prop, double def = 0) {
    const JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    double n = def;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        JS_ToFloat64(ctx, &n, v);
    }
    JS_FreeValue(ctx, v);
    return n;
}

bool getBool(JSContext* ctx, JSValueConst obj, const char* prop, bool def = false) {
    const JSValue v = JS_GetPropertyStr(ctx, obj, prop);
    bool out = def;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        out = JS_ToBool(ctx, v) != 0;
    }
    JS_FreeValue(ctx, v);
    return out;
}

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

JSValue throwOfficeError(JSContext* ctx, const char* code, const char* message) {
    const JSValue err = JS_NewError(ctx);
    const std::string full = std::string(code) + ": " + message;
    JS_SetPropertyStr(ctx, err, "name", JS_NewString(ctx, "OfficeExtension.Error"));
    JS_SetPropertyStr(ctx, err, "code", JS_NewString(ctx, code));
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, full.c_str()));
    return JS_Throw(ctx, err);
}

std::string normalizeColor(std::string color) {
    if (color.empty()) {
        return color;
    }
    std::string lower = color;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "yellow") {
        return "#FFFF00";
    }
    if (lower == "red") {
        return "#FF0000";
    }
    if (lower == "green") {
        return "#00FF00";
    }
    if (lower == "blue") {
        return "#0000FF";
    }
    if (lower == "black") {
        return "#000000";
    }
    if (lower == "white") {
        return "#FFFFFF";
    }
    if (lower == "orange") {
        return "#FFA500";
    }
    if (color[0] != '#' && color.size() == 6) {
        bool hex = true;
        for (const char c : color) {
            if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
                hex = false;
                break;
            }
        }
        if (hex) {
            return "#" + color;
        }
    }
    if (color.size() == 7 && color[0] == '#') {
        for (size_t i = 1; i < color.size(); i++) {
            color[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(color[i])));
        }
    }
    return color;
}

std::string colToName(int col) {
    if (col < 0) {
        return "A";
    }
    return Sheet::positionToColumnName(static_cast<uint32_t>(col));
}

std::string a1Address(int row, int col, int rowCount, int colCount) {
    const std::string start = colToName(col) + std::to_string(row + 1);
    if (rowCount <= 1 && colCount <= 1) {
        return start;
    }
    const std::string end = colToName(col + colCount - 1) + std::to_string(row + rowCount);
    return start + ":" + end;
}

Sheet* findSheet(JsSandbox* sandbox, const std::string& name,
                 const std::unordered_map<std::string, std::string>& idToName,
                 const std::string& sheetRef) {
    Workbook* wb = sandbox->workbook();
    if (wb == nullptr) {
        return nullptr;
    }
    std::string resolved = name;
    if (resolved.empty() && !sheetRef.empty()) {
        const auto it = idToName.find(sheetRef);
        if (it != idToName.end()) {
            resolved = it->second;
        }
    }
    if (resolved.empty()) {
        return sandbox->activeSheet();
    }
    Sheet* sheet = wb->getSheetByName(resolved);
    if (sheet != nullptr) {
        return sheet;
    }
    // Numeric index as string
    bool allDigit = !resolved.empty();
    for (const char c : resolved) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            allDigit = false;
            break;
        }
    }
    if (allDigit) {
        int idx = 0;
        for (const char c : resolved) {
            idx = idx * 10 + (c - '0');
        }
        if (idx >= 0 && static_cast<size_t>(idx) < wb->sheetCount()) {
            return wb->getSheetByIndex(static_cast<size_t>(idx));
        }
    }
    return nullptr;
}

void applyFormulaDeps(Workbook& workbook, Sheet& sheet, const ID& cellId, const ASTNode* ast) {
    DependencyGraph* depGraph = sheet.getDependencyGraph();
    if (depGraph == nullptr || ast == nullptr) {
        return;
    }
    depGraph->removeFormula(cellId);
    depGraph->addFormula(cellId, ast, [&sheet](const ID& cellIdArg) {
        const Cell* depCell = sheet.getCell(cellIdArg);
        if (depCell == nullptr) {
            return std::make_pair(static_cast<int32_t>(-1), static_cast<int32_t>(-1));
        }
        const Axis* depCol = sheet.getColumn(depCell->colId);
        const Axis* depRow = sheet.getRow(depCell->rowId);
        if (depCol == nullptr || depRow == nullptr) {
            return std::make_pair(static_cast<int32_t>(-1), static_cast<int32_t>(-1));
        }
        return std::make_pair(static_cast<int32_t>(depCol->position),
                              static_cast<int32_t>(depRow->position));
    });
    if (FormulaResolver::containsVolatileFunction(ast)) {
        depGraph->markVolatile(cellId);
    } else {
        depGraph->unmarkVolatile(cellId);
    }
    (void)workbook;
}

bool setCellRaw(Workbook& workbook, Sheet& sheet, int colIdx, int rowIdx,
                const std::string& payload, const ID& cellId) {
    const ID spillMasterBefore =
        sheet.getSpillMaster(sheet.getColumnByPosition(static_cast<uint32_t>(colIdx)) != nullptr
                                 ? sheet.getColumnByPosition(static_cast<uint32_t>(colIdx))->id
                                 : ID(),
                             sheet.getRowByPosition(static_cast<uint32_t>(rowIdx)) != nullptr
                                 ? sheet.getRowByPosition(static_cast<uint32_t>(rowIdx))->id
                                 : ID());
    const Operation op = makeCellSetOp(workbook, cellId, sheet.id, payload);
    applyOperation(workbook, op);
    markDirty(&sheet, cellId);
    std::vector<ID> changed = {cellId};
    if (!spillMasterBefore.isNull()) {
        markDirty(&sheet, spillMasterBefore);
        changed.push_back(spillMasterBefore);
    }
    cells::recalculate(&workbook, changed);
    cells::recalculateVolatile(&sheet);
    return true;
}

bool setCellFormula(Workbook& workbook, Sheet& sheet, int colIdx, int rowIdx,
                    const std::string& formula, Cell* existing, const ID& cellId,
                    const std::string& colIdStr, const std::string& rowIdStr) {
    FormulaParser parser(formula);
    auto ast = parser.parse();
    std::string payload;
    auto appendStyleFormat = [&payload, &workbook, &cellId, existing]() {
        if (existing == nullptr) {
            return;
        }
        if (existing->hasStyle()) {
            const StyleBuffer* sty = workbook.getEntityStyle(cellId);
            if (sty != nullptr) {
                payload += R"(,"sty":")" + sty->toBase64() + R"(")";
            }
        }
        if (existing->hasFormat()) {
            const FormatBuffer* fmt = workbook.getEntityFormat(cellId);
            if (fmt != nullptr) {
                payload += R"(,"fmt":")" + fmt->toBase64() + R"(")";
            }
        }
    };

    if (ast == nullptr || parser.hasErrors()) {
        RefConverter conv;
        conv.setContext(sheet);
        const std::string uuidFormula = conv.formulaToUuid(formula);
        payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" + colIdStr +
                  R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat();
        payload += "}";
        return setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
    }

    FormulaResolver resolver(workbook, sheet, workbook.getNamedRanges());
    const RequiredEntities required = resolver.getRequiredEntities(ast.get());
    for (const auto& pending : required.columns) {
        const std::string colPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
        const Operation colOp = makeColSetOp(workbook, pending.id, pending.sheetId, colPayload);
        applyOperation(workbook, colOp);
    }
    for (const auto& pending : required.rows) {
        const std::string rowPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
        const Operation rowOp = makeRowSetOp(workbook, pending.id, pending.sheetId, rowPayload);
        applyOperation(workbook, rowOp);
    }
    for (const auto& pending : required.cells) {
        const std::string cellPayload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" +
                                        pending.colId.toString() + "\",\"row\":\"" +
                                        pending.rowId.toString() + "\"}";
        const Operation cellOp = makeCellSetOp(workbook, pending.id, pending.sheetId, cellPayload);
        applyOperation(workbook, cellOp);
    }

    const ResolveResult resolveRes = resolver.resolve(ast.get());
    if (!resolveRes.success) {
        RefConverter conv;
        conv.setContext(sheet);
        const std::string uuidFormula = conv.formulaToUuid(formula);
        payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" + colIdStr +
                  R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat();
        payload += "}";
        return setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
    }

    const std::string uuidFormula = FormulaSerializer::serialize(ast.get());
    payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" + colIdStr +
              R"(","row":")" + rowIdStr + R"(")";
    appendStyleFormat();
    payload += "}";
    const bool ok = setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
    applyFormulaDeps(workbook, sheet, cellId, ast.get());
    return ok;
}

bool setCellFromJs(JSContext* ctx, Workbook& workbook, Sheet& sheet, int colIdx, int rowIdx,
                   JSValueConst value) {
    Axis* col = ensureColumnViaCrdt(workbook, sheet, static_cast<uint32_t>(colIdx));
    Axis* row = ensureRowViaCrdt(workbook, sheet, static_cast<uint32_t>(rowIdx));
    if (col == nullptr || row == nullptr) {
        return false;
    }
    Cell* existing = sheet.getCellAt(col->id, row->id);
    const ID cellId = existing != nullptr ? existing->id : generate_id();
    const std::string colIdStr = col->id.toString();
    const std::string rowIdStr = row->id.toString();

    auto appendStyleFormat = [&workbook, &cellId, existing](std::string& payload) {
        if (existing == nullptr) {
            return;
        }
        if (existing->hasStyle()) {
            const StyleBuffer* sty = workbook.getEntityStyle(cellId);
            if (sty != nullptr) {
                payload += R"(,"sty":")" + sty->toBase64() + R"(")";
            }
        }
        if (existing->hasFormat()) {
            const FormatBuffer* fmt = workbook.getEntityFormat(cellId);
            if (fmt != nullptr) {
                payload += R"(,"fmt":")" + fmt->toBase64() + R"(")";
            }
        }
    };

    if (JS_IsNull(value) || JS_IsUndefined(value)) {
        if (existing == nullptr) {
            return true;
        }
        DependencyGraph* depGraph = sheet.getDependencyGraph();
        if (depGraph != nullptr) {
            depGraph->removeFormula(cellId);
            depGraph->unmarkVolatile(cellId);
        }
        const Operation op = makeCellDeleteOp(workbook, cellId);
        applyOperation(workbook, op);
        markDirty(&sheet, cellId);
        const std::vector<ID> changed = {cellId};
        cells::recalculate(&workbook, changed);
        cells::recalculateVolatile(&sheet);
        return true;
    }

    if (JS_IsNumber(value)) {
        double num = 0;
        JS_ToFloat64(ctx, &num, value);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", num);
        std::string payload = R"({"t":"n","v":")" + std::string(buf) + R"(","col":")" + colIdStr +
                              R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat(payload);
        payload += "}";
        return setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
    }

    if (JS_IsBool(value)) {
        const int b = JS_ToBool(ctx, value);
        std::string payload = R"({"t":"b","v":")" + std::string(b != 0 ? "true" : "false") +
                              R"(","col":")" + colIdStr + R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat(payload);
        payload += "}";
        return setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
    }

    const char* s = JS_ToCString(ctx, value);
    if (s == nullptr) {
        return false;
    }
    const std::string str = s;
    JS_FreeCString(ctx, s);
    if (!str.empty() && str[0] == '=') {
        return setCellFormula(workbook, sheet, colIdx, rowIdx, str, existing, cellId, colIdStr,
                              rowIdStr);
    }
    std::string payload = R"({"t":"s","v":")" + jsonEscape(str) + R"(","col":")" + colIdStr +
                          R"(","row":")" + rowIdStr + R"(")";
    appendStyleFormat(payload);
    payload += "}";
    return setCellRaw(workbook, sheet, colIdx, rowIdx, payload, cellId);
}

JSValue cellValueToJs(JSContext* ctx, Workbook& workbook, Sheet& sheet, Cell* cell) {
    if (cell == nullptr) {
        return JS_NewString(ctx, "");
    }
    if (cell->isFormula()) {
        evaluateCell(&sheet, cell);
    }
    (void)workbook;
    if (cell->hasError()) {
        return JS_NewString(ctx, cell->value.raw.c_str());
    }
    switch (cell->value.type) {
        case CellValueType::NUMBER:
        case CellValueType::FORMULA_NUMBER:
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
            return JS_NewFloat64(ctx, cell->value.asNumber());
        case CellValueType::BOOLEAN:
        case CellValueType::FORMULA_BOOLEAN:
            return JS_NewBool(ctx, cell->value.asBoolean() ? 1 : 0);
        case CellValueType::STRING:
        case CellValueType::FORMULA_STRING:
            return JS_NewString(ctx, cell->value.asString().c_str());
        default:
            if (cell->value.raw.empty()) {
                return JS_NewString(ctx, "");
            }
            return JS_NewString(ctx, cell->value.raw.c_str());
    }
}

std::string cellFormulaDisplay(Workbook& workbook, Sheet& sheet, Cell* cell) {
    if (cell == nullptr || !cell->isFormula()) {
        return "";
    }
    Formula* f = cell->getFormula();
    if (f == nullptr || f->ast == nullptr) {
        return cell->value.raw.empty() ? "" : cell->value.raw;
    }
    FormulaDisplayConverter conv(sheet, &workbook);
    return conv.toDisplayString(f->ast);
}

bool mergeStyle(Workbook& workbook, Sheet& sheet, int colIdx, int rowIdx,
                const std::function<void(CellStyle&)>& apply) {
    Cell* cell = ensureCellAtPositionViaCrdt(workbook, sheet, static_cast<uint32_t>(colIdx),
                                             static_cast<uint32_t>(rowIdx));
    if (cell == nullptr) {
        return false;
    }
    CellStyle style;
    const StyleBuffer* existing = workbook.getEntityStyle(cell->id);
    if (existing != nullptr) {
        style = existing->toCellStyle();
    }
    apply(style);
    const StyleBuffer buf = StyleBuffer::fromCellStyle(style);
    const Operation op = makeCellSetStyleOp(workbook, cell->id, buf);
    applyOperation(workbook, op);
    return true;
}

JSValue loadRange(JSContext* ctx, JsSandbox* sandbox, Sheet& sheet, int row, int col, int rowCount,
                  int colCount, JSValueConst props) {
    Workbook* wb = sandbox->workbook();
    JSValue out = JS_NewObject(ctx);
    auto wants = [&](const char* name) -> bool {
        if (JS_IsUndefined(props) || JS_IsNull(props)) {
            return true;
        }
        const int isArr = JS_IsArray(ctx, props);
        if (isArr > 0) {
            JSValue lenv = JS_GetPropertyStr(ctx, props, "length");
            uint32_t n = 0;
            JS_ToUint32(ctx, &n, lenv);
            JS_FreeValue(ctx, lenv);
            for (uint32_t i = 0; i < n; i++) {
                JSValue item = JS_GetPropertyUint32(ctx, props, i);
                const char* s = JS_ToCString(ctx, item);
                const bool match =
                    s != nullptr && (std::strcmp(s, name) == 0 || std::strcmp(s, "*") == 0 ||
                                     std::strstr(s, name) != nullptr);
                JS_FreeCString(ctx, s);
                JS_FreeValue(ctx, item);
                if (match) {
                    return true;
                }
            }
            return false;
        }
        return true;
    };

    const int rc = std::max(rowCount, 1);
    const int cc = std::max(colCount, 1);

    if (wants("values")) {
        JSValue rows = JS_NewArray(ctx);
        for (int r = 0; r < rc; r++) {
            JSValue cols = JS_NewArray(ctx);
            for (int c = 0; c < cc; c++) {
                Cell* cell = sheet.getCellAtPosition(static_cast<uint32_t>(col + c),
                                                     static_cast<uint32_t>(row + r));
                JS_SetPropertyUint32(ctx, cols, static_cast<uint32_t>(c),
                                     cellValueToJs(ctx, *wb, sheet, cell));
            }
            JS_SetPropertyUint32(ctx, rows, static_cast<uint32_t>(r), cols);
        }
        JS_SetPropertyStr(ctx, out, "values", rows);
    }
    if (wants("formulas")) {
        JSValue rows = JS_NewArray(ctx);
        for (int r = 0; r < rc; r++) {
            JSValue cols = JS_NewArray(ctx);
            for (int c = 0; c < cc; c++) {
                Cell* cell = sheet.getCellAtPosition(static_cast<uint32_t>(col + c),
                                                     static_cast<uint32_t>(row + r));
                std::string f = cellFormulaDisplay(*wb, sheet, cell);
                if (f.empty()) {
                    JS_SetPropertyUint32(ctx, cols, static_cast<uint32_t>(c),
                                         cellValueToJs(ctx, *wb, sheet, cell));
                } else {
                    JS_SetPropertyUint32(ctx, cols, static_cast<uint32_t>(c),
                                         JS_NewString(ctx, f.c_str()));
                }
            }
            JS_SetPropertyUint32(ctx, rows, static_cast<uint32_t>(r), cols);
        }
        JS_SetPropertyStr(ctx, out, "formulas", rows);
    }
    if (wants("numberFormat")) {
        JSValue rows = JS_NewArray(ctx);
        for (int r = 0; r < rc; r++) {
            JSValue cols = JS_NewArray(ctx);
            for (int c = 0; c < cc; c++) {
                Cell* cell = sheet.getCellAtPosition(static_cast<uint32_t>(col + c),
                                                     static_cast<uint32_t>(row + r));
                std::string fmt = "General";
                if (cell != nullptr) {
                    const FormatBuffer* fb = wb->getEntityFormat(cell->id);
                    if (fb != nullptr) {
                        fmt = fb->toFormatCode();
                        if (fmt.empty()) {
                            fmt = "General";
                        }
                    }
                }
                JS_SetPropertyUint32(ctx, cols, static_cast<uint32_t>(c),
                                     JS_NewString(ctx, fmt.c_str()));
            }
            JS_SetPropertyUint32(ctx, rows, static_cast<uint32_t>(r), cols);
        }
        JS_SetPropertyStr(ctx, out, "numberFormat", rows);
    }
    if (wants("address") || wants("rowCount") || wants("columnCount") || wants("rowIndex") ||
        wants("columnIndex")) {
        const std::string addr = sheet.name + "!" + a1Address(row, col, rc, cc);
        JS_SetPropertyStr(ctx, out, "address", JS_NewString(ctx, addr.c_str()));
        JS_SetPropertyStr(ctx, out, "rowCount", JS_NewInt32(ctx, rc));
        JS_SetPropertyStr(ctx, out, "columnCount", JS_NewInt32(ctx, cc));
        JS_SetPropertyStr(ctx, out, "rowIndex", JS_NewInt32(ctx, row));
        JS_SetPropertyStr(ctx, out, "columnIndex", JS_NewInt32(ctx, col));
    }
    if (wants("fillColor") || wants("color") || wants("format/fill/color")) {
        Cell* cell =
            sheet.getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
        std::string color;
        if (cell != nullptr) {
            const StyleBuffer* sb = wb->getEntityStyle(cell->id);
            if (sb != nullptr) {
                color = sb->toCellStyle().bgColor;
            }
        }
        JS_SetPropertyStr(ctx, out, "fillColor", JS_NewString(ctx, color.c_str()));
    }
    if (wants("font") || wants("format/font")) {
        Cell* cell =
            sheet.getCellAtPosition(static_cast<uint32_t>(col), static_cast<uint32_t>(row));
        JSValue font = JS_NewObject(ctx);
        CellStyle style;
        if (cell != nullptr) {
            const StyleBuffer* sb = wb->getEntityStyle(cell->id);
            if (sb != nullptr) {
                style = sb->toCellStyle();
            }
        }
        JS_SetPropertyStr(ctx, font, "bold", JS_NewBool(ctx, style.bold ? 1 : 0));
        JS_SetPropertyStr(ctx, font, "italic", JS_NewBool(ctx, style.italic ? 1 : 0));
        JS_SetPropertyStr(ctx, font, "underline", JS_NewBool(ctx, style.underline ? 1 : 0));
        JS_SetPropertyStr(ctx, font, "name", JS_NewString(ctx, style.fontFamily.c_str()));
        JS_SetPropertyStr(ctx, font, "size", JS_NewInt32(ctx, style.fontSize));
        JS_SetPropertyStr(ctx, font, "color", JS_NewString(ctx, style.textColor.c_str()));
        JS_SetPropertyStr(ctx, out, "font", font);
    }
    JS_SetPropertyStr(ctx, out, "name", JS_NewString(ctx, sheet.name.c_str()));
    return out;
}

struct NamedAddr {
    Sheet* sheet{nullptr};
    int row{0};
    int col{0};
    int rowCount{1};
    int colCount{1};
};

bool resolveNamedItem(Workbook& workbook, Sheet* hint, const std::string& name, NamedAddr* out) {
    NamedRangeRegistry* reg = workbook.getNamedRanges();
    if (reg == nullptr) {
        return false;
    }
    const NamedRange* nr = nullptr;
    if (hint != nullptr) {
        nr = reg->resolve(name, hint->id);
    }
    if (nr == nullptr) {
        for (const NamedRange* n : reg->getWorkbookScoped()) {
            if (n->name == name) {
                nr = n;
                break;
            }
        }
    }
    if (nr == nullptr) {
        return false;
    }
    Sheet* sheet = workbook.getSheet(nr->target.sheetId);
    if (sheet == nullptr) {
        sheet = hint;
    }
    if (sheet == nullptr && !workbook.sheets.empty()) {
        sheet = workbook.sheets[0].get();
    }
    if (sheet == nullptr) {
        return false;
    }
    out->sheet = sheet;
    auto posOf = [&](const ID& cellId, int* r, int* c) -> bool {
        Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            return false;
        }
        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            return false;
        }
        *c = static_cast<int>(col->position);
        *r = static_cast<int>(row->position);
        return true;
    };
    if (nr->target.type == NamedRangeTarget::Type::CELL) {
        if (!posOf(nr->target.id1, &out->row, &out->col)) {
            return false;
        }
        out->rowCount = 1;
        out->colCount = 1;
        return true;
    }
    if (nr->target.type == NamedRangeTarget::Type::RANGE) {
        int r1 = 0;
        int c1 = 0;
        int r2 = 0;
        int c2 = 0;
        if (!posOf(nr->target.id1, &r1, &c1) || !posOf(nr->target.id2, &r2, &c2)) {
            return false;
        }
        out->row = std::min(r1, r2);
        out->col = std::min(c1, c2);
        out->rowCount = std::abs(r2 - r1) + 1;
        out->colCount = std::abs(c2 - c1) + 1;
        return true;
    }
    if (nr->target.type == NamedRangeTarget::Type::COLUMN) {
        const Axis* col = sheet->getColumn(nr->target.id1);
        if (col == nullptr) {
            return false;
        }
        out->col = static_cast<int>(col->position);
        out->row = 0;
        out->rowCount = 1;
        out->colCount = 1;
        return true;
    }
    if (nr->target.type == NamedRangeTarget::Type::ROW) {
        const Axis* row = sheet->getRow(nr->target.id1);
        if (row == nullptr) {
            return false;
        }
        out->row = static_cast<int>(row->position);
        out->col = 0;
        out->rowCount = 1;
        out->colCount = 1;
        return true;
    }
    return false;
}

void ensureAxesInRect(Workbook& workbook, Sheet& sheet, int col, int row, int colCount,
                      int rowCount) {
    for (int c = 0; c < colCount; c++) {
        (void)ensureColumnViaCrdt(workbook, sheet, static_cast<uint32_t>(col + c));
    }
    for (int r = 0; r < rowCount; r++) {
        (void)ensureRowViaCrdt(workbook, sheet, static_cast<uint32_t>(row + r));
    }
}

void clearCellAt(Workbook& workbook, Sheet& sheet, int colIdx, int rowIdx) {
    Cell* cell =
        sheet.getCellAtPosition(static_cast<uint32_t>(colIdx), static_cast<uint32_t>(rowIdx));
    if (cell == nullptr) {
        return;
    }
    DependencyGraph* depGraph = sheet.getDependencyGraph();
    if (depGraph != nullptr) {
        depGraph->removeFormula(cell->id);
        depGraph->unmarkVolatile(cell->id);
    }
    const ID cellId = cell->id;
    applyOperation(workbook, makeCellDeleteOp(workbook, cellId));
    markDirty(&sheet, cellId);
    const std::vector<ID> changed = {cellId};
    cells::recalculate(&workbook, changed);
    cells::recalculateVolatile(&sheet);
}

void copyStyleFormat(Workbook& workbook, Cell* src, Cell* dest) {
    if (src == nullptr || dest == nullptr) {
        return;
    }
    const StyleBuffer* style = workbook.getEntityStyle(src->id);
    if (style != nullptr) {
        applyOperation(workbook, makeCellSetStyleOp(workbook, dest->id, *style));
    }
    const FormatBuffer* fmt = workbook.getEntityFormat(src->id);
    if (fmt != nullptr) {
        applyOperation(workbook, makeCellSetFormatOp(workbook, dest->id, *fmt));
    }
}

void copyCellAt(JSContext* ctx, Workbook& workbook, Sheet& srcSheet, int srcCol, int srcRow,
                Sheet& destSheet, int destCol, int destRow, bool copyValues, bool copyFormulas,
                bool copyFormats) {
    Cell* src =
        srcSheet.getCellAtPosition(static_cast<uint32_t>(srcCol), static_cast<uint32_t>(srcRow));
    if (src == nullptr) {
        if (copyValues) {
            clearCellAt(workbook, destSheet, destCol, destRow);
        }
        return;
    }
    if (copyFormulas && src->isFormula()) {
        const std::string formula = cellFormulaDisplay(workbook, srcSheet, src);
        Axis* col = ensureColumnViaCrdt(workbook, destSheet, static_cast<uint32_t>(destCol));
        Axis* row = ensureRowViaCrdt(workbook, destSheet, static_cast<uint32_t>(destRow));
        if (col != nullptr && row != nullptr) {
            Cell* existing = destSheet.getCellAt(col->id, row->id);
            const ID cellId = existing != nullptr ? existing->id : generate_id();
            setCellFormula(workbook, destSheet, destCol, destRow, formula, existing, cellId,
                           col->id.toString(), row->id.toString());
        }
    } else if (copyValues) {
        JSValue v = cellValueToJs(ctx, workbook, srcSheet, src);
        setCellFromJs(ctx, workbook, destSheet, destCol, destRow, v);
        JS_FreeValue(ctx, v);
    }
    if (copyFormats) {
        Cell* dest = destSheet.getCellAtPosition(static_cast<uint32_t>(destCol),
                                                 static_cast<uint32_t>(destRow));
        if (dest == nullptr) {
            dest = ensureCellAtPositionViaCrdt(workbook, destSheet, static_cast<uint32_t>(destCol),
                                               static_cast<uint32_t>(destRow));
        }
        copyStyleFormat(workbook, src, dest);
    }
}

int maxUsedRowInCols(Sheet& sheet, int col, int colCount) {
    int maxRow = -1;
    for (const ID& id : sheet.getCellIds()) {
        Cell* cell = sheet.getCell(id);
        if (cell == nullptr) {
            continue;
        }
        const Axis* c = sheet.getColumn(cell->colId);
        const Axis* r = sheet.getRow(cell->rowId);
        if (c == nullptr || r == nullptr) {
            continue;
        }
        const int cpos = static_cast<int>(c->position);
        if (cpos >= col && cpos < col + colCount) {
            maxRow = std::max(maxRow, static_cast<int>(r->position));
        }
    }
    return maxRow;
}

int maxUsedColInRows(Sheet& sheet, int row, int rowCount) {
    int maxCol = -1;
    for (const ID& id : sheet.getCellIds()) {
        Cell* cell = sheet.getCell(id);
        if (cell == nullptr) {
            continue;
        }
        const Axis* c = sheet.getColumn(cell->colId);
        const Axis* r = sheet.getRow(cell->rowId);
        if (c == nullptr || r == nullptr) {
            continue;
        }
        const int rpos = static_cast<int>(r->position);
        if (rpos >= row && rpos < row + rowCount) {
            maxCol = std::max(maxCol, static_cast<int>(c->position));
        }
    }
    return maxCol;
}

bool computeUsedRange(Sheet& sheet, int clipRow, int clipCol, int clipRowCount, int clipColCount,
                      bool clip, int* row, int* col, int* rowCount, int* colCount) {
    int minR = 0x7fffffff;
    int minC = 0x7fffffff;
    int maxR = -1;
    int maxC = -1;
    const int clipRowEnd = clipRow + clipRowCount;
    const int clipColEnd = clipCol + clipColCount;
    for (const ID& id : sheet.getCellIds()) {
        Cell* cell = sheet.getCell(id);
        if (cell == nullptr) {
            continue;
        }
        const Axis* c = sheet.getColumn(cell->colId);
        const Axis* r = sheet.getRow(cell->rowId);
        if (c == nullptr || r == nullptr) {
            continue;
        }
        const int cpos = static_cast<int>(c->position);
        const int rpos = static_cast<int>(r->position);
        if (clip) {
            if (cpos < clipCol || cpos >= clipColEnd || rpos < clipRow || rpos >= clipRowEnd) {
                continue;
            }
        }
        minC = std::min(minC, cpos);
        minR = std::min(minR, rpos);
        maxC = std::max(maxC, cpos);
        maxR = std::max(maxR, rpos);
    }
    if (maxR < 0) {
        return false;
    }
    *row = minR;
    *col = minC;
    *rowCount = maxR - minR + 1;
    *colCount = maxC - minC + 1;
    return true;
}

bool mergeCells(Workbook& workbook, Sheet& sheet, int col, int row, int colCount, int rowCount) {
    if (colCount <= 1 && rowCount <= 1) {
        return true;
    }
    ensureAxesInRect(workbook, sheet, col, row, colCount, rowCount);
    Axis* startCol = sheet.getColumnByPosition(static_cast<uint32_t>(col));
    Axis* startRow = sheet.getRowByPosition(static_cast<uint32_t>(row));
    Axis* endCol = sheet.getColumnByPosition(static_cast<uint32_t>(col + colCount - 1));
    Axis* endRow = sheet.getRowByPosition(static_cast<uint32_t>(row + rowCount - 1));
    if (startCol == nullptr || startRow == nullptr || endCol == nullptr || endRow == nullptr) {
        return false;
    }
    const ID rangeId = generate_id();
    std::string payload = "{\"startCol\":\"" + startCol->id.toString() + "\",\"startRow\":\"" +
                          startRow->id.toString() + "\",\"endCol\":\"" + endCol->id.toString() +
                          "\",\"endRow\":\"" + endRow->id.toString() +
                          "\",\"flags\":" + std::to_string(static_cast<int>(RangeFlags::MERGE)) +
                          "}";
    applyOperation(workbook, makeRangeSetOp(workbook, rangeId, payload));
    return true;
}

void unmergeCells(Workbook& workbook, Sheet& sheet, int col, int row, int colCount, int rowCount) {
    std::vector<ID> seen;
    for (int r = 0; r < rowCount; r++) {
        for (int c = 0; c < colCount; c++) {
            const std::vector<Range*> merges = sheet.getRangesAt(
                static_cast<uint32_t>(col + c), static_cast<uint32_t>(row + r), RangeFlags::MERGE);
            for (Range* mergeRange : merges) {
                if (mergeRange == nullptr) {
                    continue;
                }
                bool already = false;
                for (const ID& id : seen) {
                    if (id == mergeRange->id) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }
                seen.push_back(mergeRange->id);
                const std::string payload = "{\"sheet\":\"" + sheet.id.toString() + "\"}";
                applyOperation(workbook, makeRangeDeleteOp(workbook, mergeRange->id, payload));
            }
        }
    }
}

TextAlign parseHAlign(const std::string& raw) {
    const std::string s = toLowerCopy(raw);
    if (s == "center" || s == "centercrossselection" || s == "centercontinuous") {
        return TextAlign::CENTER;
    }
    if (s == "left") {
        return TextAlign::LEFT;
    }
    if (s == "right") {
        return TextAlign::RIGHT;
    }
    if (s == "justify" || s == "distributed") {
        return TextAlign::JUSTIFY;
    }
    return TextAlign::GENERAL;
}

VerticalAlign parseVAlign(const std::string& raw) {
    const std::string s = toLowerCopy(raw);
    if (s == "top") {
        return VerticalAlign::TOP;
    }
    if (s == "center" || s == "middle") {
        return VerticalAlign::MIDDLE;
    }
    return VerticalAlign::BOTTOM;
}

BorderStyle parseBorderStyle(const std::string& raw) {
    const std::string s = toLowerCopy(raw);
    if (s.empty() || s == "none" || s == "0") {
        return BorderStyle::NONE;
    }
    if (s == "dash" || s == "dashed") {
        return BorderStyle::DASHED;
    }
    if (s == "dot" || s == "dotted") {
        return BorderStyle::DOTTED;
    }
    if (s == "double") {
        return BorderStyle::DOUBLE;
    }
    if (s == "hair" || s == "hairline") {
        return BorderStyle::HAIR;
    }
    if (s == "medium") {
        return BorderStyle::MEDIUM;
    }
    if (s == "thick") {
        return BorderStyle::THICK;
    }
    if (s == "mediumdashed") {
        return BorderStyle::MEDIUM_DASHED;
    }
    if (s == "dashdot") {
        return BorderStyle::DASH_DOT;
    }
    if (s == "mediumdashdot") {
        return BorderStyle::MEDIUM_DASH_DOT;
    }
    if (s == "dashdotdot") {
        return BorderStyle::DASH_DOT_DOT;
    }
    if (s == "mediumdashdotdot") {
        return BorderStyle::MEDIUM_DASH_DOT_DOT;
    }
    if (s == "slantdashdot") {
        return BorderStyle::SLANT_DASH_DOT;
    }
    return BorderStyle::THIN;
}

enum class BorderEdgeKind : std::uint8_t {
    Top,
    Bottom,
    Left,
    Right,
    InsideVertical,
    InsideHorizontal,
    DiagonalDown,
    DiagonalUp,
    Unknown
};

BorderEdgeKind parseBorderEdge(const std::string& raw) {
    const std::string s = toLowerCopy(raw);
    if (s == "edgetop" || s == "top" || s == "0") {
        return BorderEdgeKind::Top;
    }
    if (s == "edgebottom" || s == "bottom" || s == "2") {
        return BorderEdgeKind::Bottom;
    }
    if (s == "edgeleft" || s == "left" || s == "1") {
        return BorderEdgeKind::Left;
    }
    if (s == "edgeright" || s == "right" || s == "3") {
        return BorderEdgeKind::Right;
    }
    if (s == "insidevertical" || s == "5") {
        return BorderEdgeKind::InsideVertical;
    }
    if (s == "insidehorizontal" || s == "6") {
        return BorderEdgeKind::InsideHorizontal;
    }
    if (s == "diagonaldown" || s == "7") {
        return BorderEdgeKind::DiagonalDown;
    }
    if (s == "diagonalup" || s == "8") {
        return BorderEdgeKind::DiagonalUp;
    }
    return BorderEdgeKind::Unknown;
}

void applyBorderEdge(CellStyle& style, BorderEdgeKind edge, BorderStyle bstyle,
                     const std::string& color) {
    BorderEdge be;
    be.style = bstyle;
    be.color = color;
    switch (edge) {
        case BorderEdgeKind::Top:
            style.border.top = be;
            style.setDefined(DEFINED_BORDER_TOP);
            break;
        case BorderEdgeKind::Bottom:
            style.border.bottom = be;
            style.setDefined(DEFINED_BORDER_BOTTOM);
            break;
        case BorderEdgeKind::Left:
            style.border.left = be;
            style.setDefined(DEFINED_BORDER_LEFT);
            break;
        case BorderEdgeKind::Right:
            style.border.right = be;
            style.setDefined(DEFINED_BORDER_RIGHT);
            break;
        default:
            break;
    }
}

bool addNamedItem(Workbook& workbook, Sheet* sheet, const std::string& name, int row, int col,
                  int rowCount, int colCount) {
    if (sheet == nullptr || name.empty()) {
        return false;
    }
    NamedRangeRegistry* reg = workbook.getNamedRanges();
    if (reg == nullptr) {
        return false;
    }
    ensureAxesInRect(workbook, *sheet, col, row, rowCount, colCount);
    Cell* a = ensureCellAtPositionViaCrdt(workbook, *sheet, static_cast<uint32_t>(col),
                                          static_cast<uint32_t>(row));
    if (a == nullptr) {
        return false;
    }
    NamedRangeTarget target;
    if (rowCount <= 1 && colCount <= 1) {
        target = NamedRangeTarget::cell(a->id, sheet->id);
    } else {
        Cell* b =
            ensureCellAtPositionViaCrdt(workbook, *sheet, static_cast<uint32_t>(col + colCount - 1),
                                        static_cast<uint32_t>(row + rowCount - 1));
        if (b == nullptr) {
            return false;
        }
        target = NamedRangeTarget::range(a->id, b->id, sheet->id);
    }
    std::string payload =
        "{\"name\":\"" + jsonEscape(name) +
        "\",\"scope\":\"W\",\"scopeSheetId\":\"-\",\"targetType\":\"" +
        std::string(target.type == NamedRangeTarget::Type::CELL ? "CELL" : "RANGE") +
        "\",\"id1\":\"" + target.id1.toString() + "\",\"id2\":\"" +
        (target.id2.isNull() ? "-" : target.id2.toString()) + "\",\"targetSheetId\":\"" +
        sheet->id.toString() + "\"}";
    applyOperation(workbook, makeNamedRangeSetOp(workbook, payload));
    return true;
}

JSValue jsFlush(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    JsSandbox* sandbox = sandboxFrom(ctx);
    if (sandbox == nullptr || sandbox->workbook() == nullptr) {
        return throwOfficeError(ctx, "InvalidArgument", "no workbook context");
    }
    if (argc < 1) {
        return throwOfficeError(ctx, "InvalidArgument", "flush requires a command list");
    }
    Workbook& workbook = *sandbox->workbook();
    JSValueConst cmds = argv[0];
    JSValue lenv = JS_GetPropertyStr(ctx, cmds, "length");
    uint32_t n = 0;
    JS_ToUint32(ctx, &n, lenv);
    JS_FreeValue(ctx, lenv);

    JSValue loads = JS_NewObject(ctx);
    std::unordered_map<std::string, std::string> idToName;

    for (uint32_t i = 0; i < n; i++) {
        JSValue cmd = JS_GetPropertyUint32(ctx, cmds, i);
        const std::string op = getStr(ctx, cmd, "op");
        const std::string oid = getStr(ctx, cmd, "id");
        const std::string sheetName = getStr(ctx, cmd, "sheet");
        const std::string sheetRef = getStr(ctx, cmd, "sheetRef");
        const std::string named = getStr(ctx, cmd, "namedItem");

        auto resolveSheet = [&]() -> Sheet* {
            if (!named.empty()) {
                NamedAddr addr;
                if (!resolveNamedItem(workbook, sandbox->activeSheet(), named, &addr) ||
                    addr.sheet == nullptr) {
                    return nullptr;
                }
                return addr.sheet;
            }
            return findSheet(sandbox, sheetName, idToName, sheetRef);
        };

        if (op == "addSheet") {
            std::string name = getStr(ctx, cmd, "name");
            if (name.empty()) {
                name = "Sheet" + std::to_string(workbook.sheetCount() + 1);
            }
            if (workbook.getSheetByName(name) != nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "InvalidArgument", "sheet already exists");
            }
            const ID sheetId = generate_id();
            const std::string payload = R"({"name":")" + jsonEscape(name) + R"("})";
            const Operation sop = makeSheetSetOp(workbook, sheetId, payload);
            applyOperation(workbook, sop);
            Sheet* created = workbook.getSheet(sheetId);
            if (created == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "GeneralException", "failed to create sheet");
            }
            if (!oid.empty()) {
                idToName[oid] = created->name;
                JSValue rec = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, rec, "name", JS_NewString(ctx, created->name.c_str()));
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            }
        } else if (op == "activateSheet") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            sandbox->setActiveSheet(sheet);
        } else if (op == "renameSheet") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const std::string newName = getStr(ctx, cmd, "newName");
            const std::string payload = R"({"name":")" + jsonEscape(newName) + R"("})";
            const Operation sop = makeSheetSetOp(workbook, sheet->id, payload);
            applyOperation(workbook, sop);
            if (!oid.empty()) {
                idToName[oid] = newName;
            }
        } else if (op == "setValues" || op == "setFormulas") {
            Sheet* sheet = resolveSheet();
            NamedAddr namedAddr;
            int row = getInt(ctx, cmd, "row");
            int col = getInt(ctx, cmd, "col");
            if (!named.empty() &&
                resolveNamedItem(workbook, sandbox->activeSheet(), named, &namedAddr)) {
                sheet = namedAddr.sheet;
                row = namedAddr.row;
                col = namedAddr.col;
            }
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            JSValue values = JS_GetPropertyStr(ctx, cmd, "values");
            JSValue lenRows = JS_GetPropertyStr(ctx, values, "length");
            uint32_t nrows = 0;
            JS_ToUint32(ctx, &nrows, lenRows);
            JS_FreeValue(ctx, lenRows);
            for (uint32_t r = 0; r < nrows; r++) {
                JSValue rowv = JS_GetPropertyUint32(ctx, values, r);
                uint32_t ncols = 1;
                if (JS_IsArray(ctx, rowv) > 0) {
                    JSValue lenCols = JS_GetPropertyStr(ctx, rowv, "length");
                    JS_ToUint32(ctx, &ncols, lenCols);
                    JS_FreeValue(ctx, lenCols);
                    for (uint32_t c = 0; c < ncols; c++) {
                        JSValue cellv = JS_GetPropertyUint32(ctx, rowv, c);
                        setCellFromJs(ctx, workbook, *sheet, col + static_cast<int>(c),
                                      row + static_cast<int>(r), cellv);
                        JS_FreeValue(ctx, cellv);
                    }
                } else {
                    setCellFromJs(ctx, workbook, *sheet, col, row + static_cast<int>(r), rowv);
                }
                JS_FreeValue(ctx, rowv);
            }
            JS_FreeValue(ctx, values);
        } else if (op == "setNumberFormat") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            JSValue formats = JS_GetPropertyStr(ctx, cmd, "values");
            auto applyCode = [&](int r, int c, const std::string& code) {
                Cell* cell =
                    ensureCellAtPositionViaCrdt(workbook, *sheet, static_cast<uint32_t>(col + c),
                                                static_cast<uint32_t>(row + r));
                if (cell == nullptr) {
                    return;
                }
                auto parsed = FormatBuffer::fromFormatCode(code);
                if (parsed.has_value()) {
                    const Operation fop = makeCellSetFormatOp(workbook, cell->id, *parsed);
                    applyOperation(workbook, fop);
                }
            };
            if (JS_IsString(formats)) {
                const char* fs = JS_ToCString(ctx, formats);
                const std::string code = fs != nullptr ? fs : "General";
                JS_FreeCString(ctx, fs);
                for (int r = 0; r < rowCount; r++) {
                    for (int c = 0; c < colCount; c++) {
                        applyCode(r, c, code);
                    }
                }
            } else {
                for (int r = 0; r < rowCount; r++) {
                    JSValue rowv = JS_GetPropertyUint32(ctx, formats, static_cast<uint32_t>(r));
                    for (int c = 0; c < colCount; c++) {
                        JSValue cellv =
                            JS_IsArray(ctx, rowv) > 0
                                ? JS_GetPropertyUint32(ctx, rowv, static_cast<uint32_t>(c))
                                : JS_DupValue(ctx, rowv);
                        const char* fs = JS_ToCString(ctx, cellv);
                        std::string code = fs != nullptr ? fs : "General";
                        JS_FreeCString(ctx, fs);
                        JS_FreeValue(ctx, cellv);
                        applyCode(r, c, code);
                    }
                    JS_FreeValue(ctx, rowv);
                }
            }
            JS_FreeValue(ctx, formats);
        } else if (op == "setFill") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const std::string color = normalizeColor(getStr(ctx, cmd, "color"));
            for (int r = 0; r < rowCount; r++) {
                for (int c = 0; c < colCount; c++) {
                    mergeStyle(workbook, *sheet, col + c, row + r, [&](CellStyle& style) {
                        style.bgColor = color;
                        style.setDefined(DEFINED_BGCOLOR);
                    });
                }
            }
        } else if (op == "setFont") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            JSValue font = JS_GetPropertyStr(ctx, cmd, "font");
            for (int r = 0; r < rowCount; r++) {
                for (int c = 0; c < colCount; c++) {
                    mergeStyle(workbook, *sheet, col + c, row + r, [&](CellStyle& style) {
                        JSValue v = JS_GetPropertyStr(ctx, font, "bold");
                        if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
                            style.bold = JS_ToBool(ctx, v) != 0;
                            style.setDefined(DEFINED_BOLD);
                        }
                        JS_FreeValue(ctx, v);
                        v = JS_GetPropertyStr(ctx, font, "italic");
                        if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
                            style.italic = JS_ToBool(ctx, v) != 0;
                            style.setDefined(DEFINED_ITALIC);
                        }
                        JS_FreeValue(ctx, v);
                        v = JS_GetPropertyStr(ctx, font, "underline");
                        if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
                            style.underline = JS_ToBool(ctx, v) != 0;
                            style.setDefined(DEFINED_UNDERLINE);
                        }
                        JS_FreeValue(ctx, v);
                        v = JS_GetPropertyStr(ctx, font, "name");
                        if (JS_IsString(v)) {
                            const char* nm = JS_ToCString(ctx, v);
                            style.fontFamily = nm != nullptr ? nm : "";
                            JS_FreeCString(ctx, nm);
                            style.setDefined(DEFINED_FONTFAMILY);
                        }
                        JS_FreeValue(ctx, v);
                        v = JS_GetPropertyStr(ctx, font, "size");
                        if (JS_IsNumber(v)) {
                            int32_t sz = 0;
                            JS_ToInt32(ctx, &sz, v);
                            style.fontSize = static_cast<uint8_t>(sz);
                            style.setDefined(DEFINED_FONTSIZE);
                        }
                        JS_FreeValue(ctx, v);
                        v = JS_GetPropertyStr(ctx, font, "color");
                        if (JS_IsString(v)) {
                            const char* cl = JS_ToCString(ctx, v);
                            style.textColor = normalizeColor(cl != nullptr ? cl : "");
                            JS_FreeCString(ctx, cl);
                            style.setDefined(DEFINED_TEXTCOLOR);
                        }
                        JS_FreeValue(ctx, v);
                    });
                }
            }
            JS_FreeValue(ctx, font);
        } else if (op == "clear") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const std::string applyTo = getStr(ctx, cmd, "applyTo");
            const bool contents = applyTo.empty() || applyTo == "All" || applyTo == "Contents" ||
                                  applyTo == "0" || applyTo == "2";
            const bool formats = applyTo.empty() || applyTo == "All" || applyTo == "Formats" ||
                                 applyTo == "0" || applyTo == "1";
            for (int r = 0; r < rowCount; r++) {
                for (int c = 0; c < colCount; c++) {
                    Cell* cell = sheet->getCellAtPosition(static_cast<uint32_t>(col + c),
                                                          static_cast<uint32_t>(row + r));
                    if (cell == nullptr) {
                        continue;
                    }
                    if (contents) {
                        setCellFromJs(ctx, workbook, *sheet, col + c, row + r, JS_NULL);
                    }
                    if (formats && cell != nullptr) {
                        Cell* again = sheet->getCellAtPosition(static_cast<uint32_t>(col + c),
                                                               static_cast<uint32_t>(row + r));
                        if (again != nullptr) {
                            const Operation cop = makeCellClearStyleOp(workbook, again->id);
                            applyOperation(workbook, cop);
                            const Operation fop = makeCellClearFormatOp(workbook, again->id);
                            applyOperation(workbook, fop);
                        }
                    }
                }
            }
        } else if (op == "loadRange") {
            Sheet* sheet = resolveSheet();
            int row = getInt(ctx, cmd, "row");
            int col = getInt(ctx, cmd, "col");
            int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            // getUsedRange / getSelectedRange resolve bounds into loads[id] first;
            // a following load() still carries the placeholder A1 coords.
            if (!oid.empty()) {
                const JSValue prev = JS_GetPropertyStr(ctx, loads, oid.c_str());
                if (JS_IsObject(prev)) {
                    const JSValue prevRow = JS_GetPropertyStr(ctx, prev, "row");
                    if (!JS_IsUndefined(prevRow) && !JS_IsNull(prevRow)) {
                        row = getInt(ctx, prev, "row");
                        col = getInt(ctx, prev, "col");
                        rowCount = std::max(getInt(ctx, prev, "rowCount", 1), 1);
                        colCount = std::max(getInt(ctx, prev, "colCount", 1), 1);
                    }
                    JS_FreeValue(ctx, prevRow);
                }
                JS_FreeValue(ctx, prev);
            }
            if (!named.empty()) {
                NamedAddr addr;
                if (!resolveNamedItem(workbook, sandbox->activeSheet(), named, &addr)) {
                    JS_FreeValue(ctx, cmd);
                    JS_FreeValue(ctx, loads);
                    return throwOfficeError(ctx, "ItemNotFound", "Named item not found");
                }
                sheet = addr.sheet;
                row = addr.row;
                col = addr.col;
                rowCount = addr.rowCount;
                colCount = addr.colCount;
            }
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            JSValue props = JS_GetPropertyStr(ctx, cmd, "properties");
            JSValue rec = loadRange(ctx, sandbox, *sheet, row, col, rowCount, colCount, props);
            JS_FreeValue(ctx, props);
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "loadSheets") {
            JSValue rec = JS_NewObject(ctx);
            JSValue items = JS_NewArray(ctx);
            for (size_t s = 0; s < workbook.sheetCount(); s++) {
                Sheet* sh = workbook.getSheetByIndex(s);
                JSValue item = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, item, "name",
                                  JS_NewString(ctx, sh != nullptr ? sh->name.c_str() : ""));
                JS_SetPropertyStr(ctx, item, "position", JS_NewInt32(ctx, static_cast<int32_t>(s)));
                JS_SetPropertyUint32(ctx, items, static_cast<uint32_t>(s), item);
            }
            JS_SetPropertyStr(ctx, rec, "items", items);
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "loadWorksheet") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            JSValue rec = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rec, "name", JS_NewString(ctx, sheet->name.c_str()));
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "loadNamedItem") {
            NamedAddr addr;
            if (!resolveNamedItem(workbook, sandbox->activeSheet(), named, &addr)) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Named item not found");
            }
            JSValue rec = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rec, "name", JS_NewString(ctx, named.c_str()));
            const std::string addrStr = addr.sheet->name + "!" +
                                        a1Address(addr.row, addr.col, addr.rowCount, addr.colCount);
            JS_SetPropertyStr(ctx, rec, "address", JS_NewString(ctx, addrStr.c_str()));
            JS_SetPropertyStr(ctx, rec, "sheet", JS_NewString(ctx, addr.sheet->name.c_str()));
            JS_SetPropertyStr(ctx, rec, "row", JS_NewInt32(ctx, addr.row));
            JS_SetPropertyStr(ctx, rec, "col", JS_NewInt32(ctx, addr.col));
            JS_SetPropertyStr(ctx, rec, "rowCount", JS_NewInt32(ctx, addr.rowCount));
            JS_SetPropertyStr(ctx, rec, "colCount", JS_NewInt32(ctx, addr.colCount));
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "getSelectedRange") {
            Sheet* sheet = sandbox->activeSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            JSValue rec = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rec, "sheet", JS_NewString(ctx, sheet->name.c_str()));
            JS_SetPropertyStr(ctx, rec, "row", JS_NewInt32(ctx, 0));
            JS_SetPropertyStr(ctx, rec, "col", JS_NewInt32(ctx, 0));
            JS_SetPropertyStr(ctx, rec, "rowCount", JS_NewInt32(ctx, 1));
            JS_SetPropertyStr(ctx, rec, "colCount", JS_NewInt32(ctx, 1));
            JS_SetPropertyStr(ctx, rec, "address",
                              JS_NewString(ctx, (sheet->name + "!A1").c_str()));
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "merge") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            if (!mergeCells(workbook, *sheet, col, row, colCount, rowCount)) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "GeneralException", "Failed to merge range");
            }
        } else if (op == "unmerge") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            unmergeCells(workbook, *sheet, getInt(ctx, cmd, "col"), getInt(ctx, cmd, "row"),
                         std::max(getInt(ctx, cmd, "colCount", 1), 1),
                         std::max(getInt(ctx, cmd, "rowCount", 1), 1));
        } else if (op == "copyFrom") {
            Sheet* destSheet = resolveSheet();
            if (destSheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            Sheet* srcSheet = findSheet(sandbox, getStr(ctx, cmd, "srcSheet"), idToName, "");
            if (srcSheet == nullptr) {
                srcSheet = destSheet;
            }
            const int destRow = getInt(ctx, cmd, "row");
            const int destCol = getInt(ctx, cmd, "col");
            const int srcRow = getInt(ctx, cmd, "srcRow");
            const int srcCol = getInt(ctx, cmd, "srcCol");
            const int srcRowCount = std::max(getInt(ctx, cmd, "srcRowCount", 1), 1);
            const int srcColCount = std::max(getInt(ctx, cmd, "srcColCount", 1), 1);
            const std::string copyType = toLowerCopy(getStr(ctx, cmd, "copyType"));
            const bool copyValues = copyType.empty() || copyType == "all" || copyType == "values";
            const bool copyFormulas =
                copyType.empty() || copyType == "all" || copyType == "formulas";
            const bool copyFormats = copyType.empty() || copyType == "all" || copyType == "formats";
            const bool skipBlanks = getBool(ctx, cmd, "skipBlanks");
            const bool transpose = getBool(ctx, cmd, "transpose");
            for (int r = 0; r < srcRowCount; r++) {
                for (int c = 0; c < srcColCount; c++) {
                    Cell* src = srcSheet->getCellAtPosition(static_cast<uint32_t>(srcCol + c),
                                                            static_cast<uint32_t>(srcRow + r));
                    if (skipBlanks && src == nullptr) {
                        continue;
                    }
                    const int dr = transpose ? destRow + c : destRow + r;
                    const int dc = transpose ? destCol + r : destCol + c;
                    copyCellAt(ctx, workbook, *srcSheet, srcCol + c, srcRow + r, *destSheet, dc, dr,
                               copyValues, copyFormulas, copyFormats);
                }
            }
        } else if (op == "insert") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const std::string shift = toLowerCopy(getStr(ctx, cmd, "shift"));
            const bool right = shift == "right";
            if (right) {
                const int last = maxUsedColInRows(*sheet, row, rowCount);
                for (int c = last; c >= col; c--) {
                    for (int r = 0; r < rowCount; r++) {
                        copyCellAt(ctx, workbook, *sheet, c, row + r, *sheet, c + colCount, row + r,
                                   true, true, true);
                        clearCellAt(workbook, *sheet, c, row + r);
                    }
                }
            } else {
                const int last = maxUsedRowInCols(*sheet, col, colCount);
                for (int r = last; r >= row; r--) {
                    for (int c = 0; c < colCount; c++) {
                        copyCellAt(ctx, workbook, *sheet, col + c, r, *sheet, col + c, r + rowCount,
                                   true, true, true);
                        clearCellAt(workbook, *sheet, col + c, r);
                    }
                }
            }
        } else if (op == "deleteCells") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const std::string shift = toLowerCopy(getStr(ctx, cmd, "shift"));
            const bool left = shift == "left";
            if (left) {
                const int last = maxUsedColInRows(*sheet, row, rowCount);
                for (int c = col; c <= last - colCount; c++) {
                    for (int r = 0; r < rowCount; r++) {
                        copyCellAt(ctx, workbook, *sheet, c + colCount, row + r, *sheet, c, row + r,
                                   true, true, true);
                    }
                }
                for (int c = std::max(col, last - colCount + 1); c <= last; c++) {
                    for (int r = 0; r < rowCount; r++) {
                        clearCellAt(workbook, *sheet, c, row + r);
                    }
                }
            } else {
                const int last = maxUsedRowInCols(*sheet, col, colCount);
                for (int r = row; r <= last - rowCount; r++) {
                    for (int c = 0; c < colCount; c++) {
                        copyCellAt(ctx, workbook, *sheet, col + c, r + rowCount, *sheet, col + c, r,
                                   true, true, true);
                    }
                }
                for (int r = std::max(row, last - rowCount + 1); r <= last; r++) {
                    for (int c = 0; c < colCount; c++) {
                        clearCellAt(workbook, *sheet, col + c, r);
                    }
                }
            }
        } else if (op == "getUsedRange") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const bool clip = getBool(ctx, cmd, "clip");
            int row = 0;
            int col = 0;
            int rowCount = 1;
            int colCount = 1;
            if (!computeUsedRange(*sheet, getInt(ctx, cmd, "row"), getInt(ctx, cmd, "col"),
                                  std::max(getInt(ctx, cmd, "rowCount", 1), 1),
                                  std::max(getInt(ctx, cmd, "colCount", 1), 1), clip, &row, &col,
                                  &rowCount, &colCount)) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet has no used range");
            }
            JSValue rec = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rec, "sheet", JS_NewString(ctx, sheet->name.c_str()));
            JS_SetPropertyStr(ctx, rec, "row", JS_NewInt32(ctx, row));
            JS_SetPropertyStr(ctx, rec, "col", JS_NewInt32(ctx, col));
            JS_SetPropertyStr(ctx, rec, "rowCount", JS_NewInt32(ctx, rowCount));
            JS_SetPropertyStr(ctx, rec, "colCount", JS_NewInt32(ctx, colCount));
            const std::string addr = sheet->name + "!" + a1Address(row, col, rowCount, colCount);
            JS_SetPropertyStr(ctx, rec, "address", JS_NewString(ctx, addr.c_str()));
            if (!oid.empty()) {
                JS_SetPropertyStr(ctx, loads, oid.c_str(), rec);
            } else {
                JS_FreeValue(ctx, rec);
            }
        } else if (op == "addNamedItem") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                sheet = sandbox->activeSheet();
            }
            const std::string name = getStr(ctx, cmd, "name");
            if (!addNamedItem(workbook, sheet, name, getInt(ctx, cmd, "row"),
                              getInt(ctx, cmd, "col"), std::max(getInt(ctx, cmd, "rowCount", 1), 1),
                              std::max(getInt(ctx, cmd, "colCount", 1), 1))) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "InvalidArgument", "Failed to add named item");
            }
        } else if (op == "setColumnWidth") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int col = getInt(ctx, cmd, "col");
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const auto width = static_cast<uint32_t>(std::max(getNumber(ctx, cmd, "value"), 0.0));
            for (int c = 0; c < colCount; c++) {
                (void)setColumnWidthByPosition(workbook, *sheet, static_cast<uint32_t>(col + c),
                                               width);
            }
        } else if (op == "setRowHeight") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const auto height = static_cast<uint32_t>(std::max(getNumber(ctx, cmd, "value"), 0.0));
            for (int r = 0; r < rowCount; r++) {
                (void)setRowHeightByPosition(workbook, *sheet, static_cast<uint32_t>(row + r),
                                             height);
            }
        } else if (op == "setHAlign" || op == "setVAlign" || op == "setWrapText") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const std::string value = getStr(ctx, cmd, "value");
            const bool wrap = getBool(ctx, cmd, "value");
            for (int r = 0; r < rowCount; r++) {
                for (int c = 0; c < colCount; c++) {
                    mergeStyle(workbook, *sheet, col + c, row + r, [&](CellStyle& style) {
                        if (op == "setHAlign") {
                            style.hAlign = parseHAlign(value);
                            style.setDefined(DEFINED_HALIGN);
                        } else if (op == "setVAlign") {
                            style.vAlign = parseVAlign(value);
                            style.setDefined(DEFINED_VALIGN);
                        } else {
                            style.wrapText = wrap;
                            style.setDefined(DEFINED_WRAPTEXT);
                        }
                    });
                }
            }
        } else if (op == "setBorder") {
            Sheet* sheet = resolveSheet();
            if (sheet == nullptr) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "ItemNotFound", "Worksheet not found");
            }
            const int row = getInt(ctx, cmd, "row");
            const int col = getInt(ctx, cmd, "col");
            const int rowCount = std::max(getInt(ctx, cmd, "rowCount", 1), 1);
            const int colCount = std::max(getInt(ctx, cmd, "colCount", 1), 1);
            const BorderEdgeKind edge = parseBorderEdge(getStr(ctx, cmd, "edge"));
            if (edge == BorderEdgeKind::Unknown || edge == BorderEdgeKind::DiagonalDown ||
                edge == BorderEdgeKind::DiagonalUp) {
                JS_FreeValue(ctx, cmd);
                JS_FreeValue(ctx, loads);
                return throwOfficeError(ctx, "NotImplemented",
                                        "Not supported: Excel.RangeBorder diagonal");
            }
            std::string styleStr = getStr(ctx, cmd, "style");
            std::string color = normalizeColor(getStr(ctx, cmd, "color"));
            const std::string weight = toLowerCopy(getStr(ctx, cmd, "weight"));
            BorderStyle bstyle = parseBorderStyle(styleStr);
            if (!weight.empty()) {
                if (weight == "hairline") {
                    bstyle = BorderStyle::HAIR;
                } else if (weight == "thin") {
                    bstyle = BorderStyle::THIN;
                } else if (weight == "medium") {
                    bstyle = BorderStyle::MEDIUM;
                } else if (weight == "thick") {
                    bstyle = BorderStyle::THICK;
                }
            }
            if (bstyle == BorderStyle::NONE && (!styleStr.empty() || !color.empty()) &&
                toLowerCopy(styleStr) != "none") {
                bstyle = BorderStyle::THIN;
            }
            auto applyAt = [&](int c, int r, BorderEdgeKind e) {
                mergeStyle(workbook, *sheet, c, r,
                           [&](CellStyle& style) { applyBorderEdge(style, e, bstyle, color); });
            };
            if (edge == BorderEdgeKind::InsideVertical) {
                for (int r = 0; r < rowCount; r++) {
                    for (int c = 0; c < colCount; c++) {
                        if (c < colCount - 1) {
                            applyAt(col + c, row + r, BorderEdgeKind::Right);
                        }
                        if (c > 0) {
                            applyAt(col + c, row + r, BorderEdgeKind::Left);
                        }
                    }
                }
            } else if (edge == BorderEdgeKind::InsideHorizontal) {
                for (int r = 0; r < rowCount; r++) {
                    for (int c = 0; c < colCount; c++) {
                        if (r < rowCount - 1) {
                            applyAt(col + c, row + r, BorderEdgeKind::Bottom);
                        }
                        if (r > 0) {
                            applyAt(col + c, row + r, BorderEdgeKind::Top);
                        }
                    }
                }
            } else {
                for (int r = 0; r < rowCount; r++) {
                    for (int c = 0; c < colCount; c++) {
                        applyAt(col + c, row + r, edge);
                    }
                }
            }
        } else if (op == "notSupported") {
            const std::string feature = getStr(ctx, cmd, "feature");
            const std::string msg =
                "Not supported: " + (feature.empty() ? std::string("this API") : feature);
            JS_FreeValue(ctx, cmd);
            JS_FreeValue(ctx, loads);
            return throwOfficeError(ctx, "NotImplemented", msg.c_str());
        } else if (!op.empty()) {
            const std::string msg = "Not supported: " + op;
            JS_FreeValue(ctx, cmd);
            JS_FreeValue(ctx, loads);
            return throwOfficeError(ctx, "NotImplemented", msg.c_str());
        }

        JS_FreeValue(ctx, cmd);
    }

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "loads", loads);
    return result;
}

JSValue jsPrint(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    JsSandbox* sandbox = sandboxFrom(ctx);
    if (sandbox == nullptr) {
        return JS_UNDEFINED;
    }
    std::string line;
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            line += " ";
        }
        const char* s = JS_ToCString(ctx, argv[i]);
        if (s != nullptr) {
            line += s;
        }
        JS_FreeCString(ctx, s);
    }
    line += "\n";
    sandbox->appendOutput(line);
    return JS_UNDEFINED;
}

JSValue jsSheetNames(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                     JSValueConst* /*argv*/) {
    JsSandbox* sandbox = sandboxFrom(ctx);
    JSValue arr = JS_NewArray(ctx);
    if (sandbox == nullptr || sandbox->workbook() == nullptr) {
        return arr;
    }
    Workbook& wb = *sandbox->workbook();
    for (size_t i = 0; i < wb.sheetCount(); i++) {
        Sheet* sh = wb.getSheetByIndex(i);
        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                             JS_NewString(ctx, sh != nullptr ? sh->name.c_str() : ""));
    }
    return arr;
}

JSValue jsActiveSheet(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                      JSValueConst* /*argv*/) {
    JsSandbox* sandbox = sandboxFrom(ctx);
    Sheet* sheet = sandbox != nullptr ? sandbox->activeSheet() : nullptr;
    if (sheet == nullptr) {
        return JS_NewString(ctx, "Sheet1");
    }
    return JS_NewString(ctx, sheet->name.c_str());
}

constexpr const char kBootstrap[] = R"OFFICEJS(
(function (native) {
  'use strict';
  delete globalThis.__cellsNative;

  function parseLoad(arg) {
    if (arg == null) return ['*'];
    if (Array.isArray(arg)) return arg.map(String);
    if (typeof arg === 'object') return Object.keys(arg).filter(function (k) { return arg[k]; });
    return String(arg).split(',').map(function (s) { return s.trim(); }).filter(Boolean);
  }

  function parseA1(ref) {
    var m = /^([A-Za-z]+)([0-9]+)$/.exec(String(ref || ''));
    if (!m) return { col: 0, row: 0 };
    var col = 0;
    var letters = m[1].toUpperCase();
    for (var i = 0; i < letters.length; i++) col = col * 26 + (letters.charCodeAt(i) - 64);
    return { col: col - 1, row: parseInt(m[2], 10) - 1 };
  }

  function parseAddress(address) {
    var a = String(address || 'A1');
    var sheet = '';
    var bang = a.lastIndexOf('!');
    if (bang >= 0) {
      sheet = a.slice(0, bang).replace(/^'+|'+$/g, '');
      a = a.slice(bang + 1);
    }
    a = a.replace(/\$/g, '');
    var parts = a.split(':');
    var p1 = parseA1(parts[0]);
    var p2 = parts.length > 1 ? parseA1(parts[1]) : p1;
    var row = Math.min(p1.row, p2.row);
    var col = Math.min(p1.col, p2.col);
    return {
      sheet: sheet,
      row: row,
      col: col,
      rowCount: Math.abs(p2.row - p1.row) + 1,
      colCount: Math.abs(p2.col - p1.col) + 1
    };
  }

  function OfficeError(code, message) {
    var e = new Error((code ? code + ': ' : '') + (message || code));
    e.name = 'OfficeExtension.Error';
    e.code = code;
    return e;
  }

  function enqueueNotSupported(context, feature) {
    context._enqueue({ op: 'notSupported', feature: feature || 'this API' });
  }

  function Dummy(context, feature) {
    this.context = context;
    this._feature = feature || 'Excel.Unknown';
    return wrapHostObject(this, 'Unknown');
  }
  Dummy.prototype.load = function () { return this; };
  Dummy.prototype.getRange = function () {
    enqueueNotSupported(this.context, this._feature);
    return new Range(this.context, native.activeSheet(), 0, 0, 1, 1);
  };
  Dummy.prototype.add = function () {
    enqueueNotSupported(this.context, this._feature + '.add');
    return this;
  };
  Dummy.prototype.getItem = function () {
    enqueueNotSupported(this.context, this._feature + '.getItem');
    return this;
  };
  Dummy.prototype.getItemOrNullObject = Dummy.prototype.getItem;
  Dummy.prototype.delete = function () {
    enqueueNotSupported(this.context, this._feature + '.delete');
    return this;
  };
  Dummy.prototype.set = function () {
    enqueueNotSupported(this.context, this._feature + '.set');
    return this;
  };
  Dummy.prototype.clear = function () {
    enqueueNotSupported(this.context, this._feature + '.clear');
    return this;
  };
  Dummy.prototype.protect = function () {
    enqueueNotSupported(this.context, this._feature + '.protect');
    return this;
  };
  Dummy.prototype.unprotect = function () {
    enqueueNotSupported(this.context, this._feature + '.unprotect');
    return this;
  };

  function TableCollection(context) {
    this.context = context;
    return wrapHostObject(this, 'TableCollection');
  }
  TableCollection.prototype.add = function () {
    enqueueNotSupported(this.context, 'Excel.TableCollection.add');
    return new Dummy(this.context, 'Excel.Table');
  };
  TableCollection.prototype.getItem = function () {
    enqueueNotSupported(this.context, 'Excel.TableCollection.getItem');
    return new Dummy(this.context, 'Excel.Table');
  };
  TableCollection.prototype.getItemOrNullObject = TableCollection.prototype.getItem;
  TableCollection.prototype.load = function () { return this; };

  function ChartCollection(context) {
    this.context = context;
    return wrapHostObject(this, 'ChartCollection');
  }
  ChartCollection.prototype.add = function () {
    enqueueNotSupported(this.context, 'Excel.ChartCollection.add');
    return new Dummy(this.context, 'Excel.Chart');
  };
  ChartCollection.prototype.getItem = function () {
    enqueueNotSupported(this.context, 'Excel.ChartCollection.getItem');
    return new Dummy(this.context, 'Excel.Chart');
  };
  ChartCollection.prototype.getItemOrNullObject = ChartCollection.prototype.getItem;
  ChartCollection.prototype.load = function () { return this; };

  function WorksheetProtection(context) {
    this.context = context;
    return wrapHostObject(this, 'WorksheetProtection');
  }
  WorksheetProtection.prototype.protect = function () {
    enqueueNotSupported(this.context, 'Excel.WorksheetProtection.protect');
  };
  WorksheetProtection.prototype.unprotect = function () {
    enqueueNotSupported(this.context, 'Excel.WorksheetProtection.unprotect');
  };
  WorksheetProtection.prototype.load = function () { return this; };

  function RequestContext() {
    this._queue = [];
    this._objects = {};
    this._nextId = 1;
    this.workbook = new Workbook(this);
  }
  RequestContext.prototype._id = function (obj) {
    if (!obj._oid) {
      obj._oid = 'o' + (this._nextId++);
      this._objects[obj._oid] = obj;
    }
    return obj._oid;
  };
  RequestContext.prototype._enqueue = function (cmd) {
    this._queue.push(cmd);
  };
  RequestContext.prototype.sync = function () {
    var self = this;
    return Promise.resolve().then(function () {
      var result = native.flush(self._queue);
      self._queue = [];
      if (result && result.loads) {
        var ids = Object.keys(result.loads);
        for (var i = 0; i < ids.length; i++) {
          var obj = self._objects[ids[i]];
          if (obj && obj._applyLoad) obj._applyLoad(result.loads[ids[i]]);
        }
      }
      return self;
    });
  };

  function Workbook(context) {
    this.context = context;
    this.worksheets = new WorksheetCollection(context);
    this.names = new NamedItemCollection(context);
    this.tables = new TableCollection(context);
    this.protection = new WorksheetProtection(context);
    return wrapHostObject(this, 'Workbook');
  }
  Workbook.prototype.getSelectedRange = function () {
    var r = new Range(this.context, native.activeSheet(), 0, 0, 1, 1);
    this.context._enqueue({ op: 'getSelectedRange', id: this.context._id(r) });
    return r;
  };
  Workbook.prototype.getActiveCell = function () {
    return this.getSelectedRange();
  };
  Workbook.prototype.getRange = function (address) {
    var p = parseAddress(address);
    var sheet = p.sheet || native.activeSheet();
    return new Range(this.context, sheet, p.row, p.col, p.rowCount, p.colCount);
  };

  function WorksheetCollection(context) {
    this.context = context;
    this._loaded = {};
    this._pending = [];
    this.context._id(this);
    return wrapHostObject(this, 'WorksheetCollection');
  }
  Object.defineProperty(WorksheetCollection.prototype, 'items', {
    get: function () { return this._loaded.items || []; }
  });
  WorksheetCollection.prototype.getActiveWorksheet = function () {
    return new Worksheet(this.context, native.activeSheet());
  };
  WorksheetCollection.prototype.getItem = function (name) {
    if (typeof name === 'number') return this.getItemAt(name);
    return new Worksheet(this.context, String(name));
  };
  WorksheetCollection.prototype.getItemAt = function (index) {
    var names = native.sheetNames().concat(this._pending);
    var n = names[index];
    if (n == null) return new Worksheet(this.context, String(index));
    return new Worksheet(this.context, n);
  };
  WorksheetCollection.prototype.add = function (name) {
    var names = native.sheetNames().concat(this._pending);
    var n = name == null || name === '' ? null : String(name);
    if (!n) {
      var i = names.length + 1;
      n = 'Sheet' + i;
      while (names.indexOf(n) >= 0) { i++; n = 'Sheet' + i; }
    }
    this._pending.push(n);
    var ws = new Worksheet(this.context, n);
    this.context._enqueue({ op: 'addSheet', name: n, id: this.context._id(ws) });
    return ws;
  };
  WorksheetCollection.prototype.load = function (props) {
    this.context._enqueue({ op: 'loadSheets', id: this.context._id(this), properties: parseLoad(props) });
    return this;
  };
  WorksheetCollection.prototype._applyLoad = function (data) {
    if (!data) return;
    this._loaded.items = (data.items || []).map(function (info) {
      return new Worksheet(this.context, info.name);
    }, this);
  };

  function Worksheet(context, name) {
    this.context = context;
    this._name = name;
    this._loaded = {};
    this.tables = new TableCollection(context);
    this.charts = new ChartCollection(context);
    this.protection = new WorksheetProtection(context);
    this.names = new NamedItemCollection(context);
    this.context._id(this);
    return wrapHostObject(this, 'Worksheet');
  }
  Object.defineProperty(Worksheet.prototype, 'name', {
    get: function () { return this._loaded.name != null ? this._loaded.name : this._name; },
    set: function (v) {
      var nn = String(v);
      this.context._enqueue({ op: 'renameSheet', sheet: this._name, newName: nn, id: this._oid });
      this._name = nn;
    }
  });
  Worksheet.prototype.getRange = function (address) {
    var p = parseAddress(address);
    return new Range(this.context, this._name, p.row, p.col, p.rowCount, p.colCount);
  };
  Worksheet.prototype.getRangeByIndexes = function (startRow, startCol, rowCount, colCount) {
    return new Range(this.context, this._name, startRow|0, startCol|0, rowCount|0 || 1, colCount|0 || 1);
  };
  Worksheet.prototype.getCell = function (row, column) {
    return new Range(this.context, this._name, row|0, column|0, 1, 1);
  };
  Worksheet.prototype.getUsedRange = function () {
    var r = new Range(this.context, this._name, 0, 0, 1, 1);
    this.context._enqueue({ op: 'getUsedRange', sheet: this._name, id: this.context._id(r) });
    return r;
  };
  Worksheet.prototype.getUsedRangeOrNullObject = function () {
    return this.getUsedRange();
  };
  Worksheet.prototype.activate = function () {
    this.context._enqueue({ op: 'activateSheet', sheet: this._name, id: this._oid });
  };
  Worksheet.prototype.delete = function () {
    enqueueNotSupported(this.context, 'Excel.Worksheet.delete');
  };
  Worksheet.prototype.calculate = function () {
    enqueueNotSupported(this.context, 'Excel.Worksheet.calculate');
  };
  Worksheet.prototype.copy = function () {
    enqueueNotSupported(this.context, 'Excel.Worksheet.copy');
    return this;
  };
  Worksheet.prototype.load = function (props) {
    this.context._enqueue({ op: 'loadWorksheet', sheet: this._name, id: this._oid, properties: parseLoad(props) });
    return this;
  };
  Worksheet.prototype._applyLoad = function (data) {
    if (!data) return;
    if (data.name != null) {
      this._loaded.name = data.name;
      this._name = data.name;
    }
  };

  function Range(context, sheet, row, col, rowCount, colCount, namedItem) {
    this.context = context;
    this._sheet = sheet;
    this._row = row|0;
    this._col = col|0;
    this._rowCount = rowCount|0 || 1;
    this._colCount = colCount|0 || 1;
    this._named = namedItem || '';
    this._loaded = {};
    this.format = new RangeFormat(this);
    this.context._id(this);
    return wrapHostObject(this, 'Range');
  }
  Range.prototype._base = function (extra) {
    var cmd = extra || {};
    cmd.sheet = this._sheet;
    cmd.row = this._row;
    cmd.col = this._col;
    cmd.rowCount = this._rowCount;
    cmd.colCount = this._colCount;
    cmd.id = this._oid;
    if (this._named) cmd.namedItem = this._named;
    return cmd;
  };
  Object.defineProperty(Range.prototype, 'values', {
    get: function () { return this._loaded.values; },
    set: function (v) { this.context._enqueue(this._base({ op: 'setValues', values: v })); }
  });
  Object.defineProperty(Range.prototype, 'formulas', {
    get: function () { return this._loaded.formulas; },
    set: function (v) { this.context._enqueue(this._base({ op: 'setFormulas', values: v })); }
  });
  Object.defineProperty(Range.prototype, 'numberFormat', {
    get: function () { return this._loaded.numberFormat; },
    set: function (v) { this.context._enqueue(this._base({ op: 'setNumberFormat', values: v })); }
  });
  Object.defineProperty(Range.prototype, 'address', { get: function () { return this._loaded.address; } });
  Object.defineProperty(Range.prototype, 'rowCount', { get: function () { return this._loaded.rowCount != null ? this._loaded.rowCount : this._rowCount; } });
  Object.defineProperty(Range.prototype, 'columnCount', { get: function () { return this._loaded.columnCount != null ? this._loaded.columnCount : this._colCount; } });
  Object.defineProperty(Range.prototype, 'rowIndex', { get: function () { return this._loaded.rowIndex != null ? this._loaded.rowIndex : this._row; } });
  Object.defineProperty(Range.prototype, 'columnIndex', { get: function () { return this._loaded.columnIndex != null ? this._loaded.columnIndex : this._col; } });
  Range.prototype.getCell = function (row, column) {
    return new Range(this.context, this._sheet, this._row + (row|0), this._col + (column|0), 1, 1, this._named);
  };
  Range.prototype.getRange = function (address) {
    var p = parseAddress(address);
    return new Range(this.context, this._sheet, this._row + p.row, this._col + p.col, p.rowCount, p.colCount, this._named);
  };
  Range.prototype.getOffsetRange = function (rowOffset, columnOffset) {
    return new Range(this.context, this._sheet, this._row + (rowOffset|0), this._col + (columnOffset|0), this._rowCount, this._colCount, this._named);
  };
  Range.prototype.getResizedRange = function (deltaRows, deltaColumns) {
    var rc = this._rowCount + (deltaRows|0);
    var cc = this._colCount + (deltaColumns|0);
    if (rc < 1) rc = 1;
    if (cc < 1) cc = 1;
    return new Range(this.context, this._sheet, this._row, this._col, rc, cc, this._named);
  };
  Range.prototype.getEntireRow = function () {
    return new Range(this.context, this._sheet, this._row, 0, this._rowCount, 1, this._named);
  };
  Range.prototype.getEntireColumn = function () {
    return new Range(this.context, this._sheet, 0, this._col, 1, this._colCount, this._named);
  };
  Range.prototype.getRow = function (row) {
    return new Range(this.context, this._sheet, this._row + (row|0), this._col, 1, this._colCount, this._named);
  };
  Range.prototype.getColumn = function (column) {
    return new Range(this.context, this._sheet, this._row, this._col + (column|0), this._rowCount, 1, this._named);
  };
  Range.prototype.getUsedRange = function () {
    var r = new Range(this.context, this._sheet, this._row, this._col, this._rowCount, this._colCount, this._named);
    this.context._enqueue(r._base({ op: 'getUsedRange', clip: true }));
    return r;
  };
  Range.prototype.merge = function () {
    this.context._enqueue(this._base({ op: 'merge' }));
  };
  Range.prototype.unmerge = function () {
    this.context._enqueue(this._base({ op: 'unmerge' }));
  };
  Range.prototype.copyFrom = function (source, copyType, skipBlanks, transpose) {
    var cmd = this._base({
      op: 'copyFrom',
      copyType: copyType == null ? 'All' : String(copyType),
      skipBlanks: !!skipBlanks,
      transpose: !!transpose
    });
    if (source && typeof source === 'object') {
      cmd.srcSheet = source._sheet;
      cmd.srcRow = source._row;
      cmd.srcCol = source._col;
      cmd.srcRowCount = source._rowCount;
      cmd.srcColCount = source._colCount;
    } else {
      var p = parseAddress(source);
      cmd.srcSheet = p.sheet || this._sheet;
      cmd.srcRow = p.row;
      cmd.srcCol = p.col;
      cmd.srcRowCount = p.rowCount;
      cmd.srcColCount = p.colCount;
    }
    this.context._enqueue(cmd);
  };
  Range.prototype.insert = function (shift) {
    this.context._enqueue(this._base({ op: 'insert', shift: shift == null ? 'Down' : String(shift) }));
  };
  Range.prototype.delete = function (shift) {
    this.context._enqueue(this._base({ op: 'deleteCells', shift: shift == null ? 'Up' : String(shift) }));
  };
  Range.prototype.select = function () {};
  Range.prototype.set = function (props) {
    if (!props) return this;
    if (props.values != null) this.values = props.values;
    if (props.formulas != null) this.formulas = props.formulas;
    if (props.numberFormat != null) this.numberFormat = props.numberFormat;
    if (props.format) {
      var f = props.format;
      if (f.fill && f.fill.color != null) this.format.fill.color = f.fill.color;
      if (f.font) {
        if (f.font.bold != null) this.format.font.bold = f.font.bold;
        if (f.font.italic != null) this.format.font.italic = f.font.italic;
        if (f.font.underline != null) this.format.font.underline = f.font.underline;
        if (f.font.name != null) this.format.font.name = f.font.name;
        if (f.font.size != null) this.format.font.size = f.font.size;
        if (f.font.color != null) this.format.font.color = f.font.color;
      }
      if (f.columnWidth != null) this.format.columnWidth = f.columnWidth;
      if (f.rowHeight != null) this.format.rowHeight = f.rowHeight;
      if (f.horizontalAlignment != null) this.format.horizontalAlignment = f.horizontalAlignment;
      if (f.verticalAlignment != null) this.format.verticalAlignment = f.verticalAlignment;
      if (f.wrapText != null) this.format.wrapText = f.wrapText;
    }
    return this;
  };
  Range.prototype.autoFill = function () { enqueueNotSupported(this.context, 'Excel.Range.autoFill'); };
  Range.prototype.calculate = function () { enqueueNotSupported(this.context, 'Excel.Range.calculate'); };
  Range.prototype.find = function () {
    enqueueNotSupported(this.context, 'Excel.Range.find');
    return new Dummy(this.context, 'Excel.Range');
  };
  Range.prototype.moveTo = function () { enqueueNotSupported(this.context, 'Excel.Range.moveTo'); };
  Range.prototype.clear = function (applyTo) {
    var a = applyTo;
    if (a && typeof a === 'object' && a.toString) a = String(a);
    this.context._enqueue(this._base({ op: 'clear', applyTo: a == null ? 'All' : String(a) }));
  };
  Range.prototype.load = function (props) {
    this.context._enqueue(this._base({ op: 'loadRange', properties: parseLoad(props) }));
    return this;
  };
  Range.prototype._applyLoad = function (data) {
    if (!data) return;
    var keys = Object.keys(data);
    for (var i = 0; i < keys.length; i++) this._loaded[keys[i]] = data[keys[i]];
    if (data.fillColor != null && this.format && this.format.fill) this.format.fill._loaded.color = data.fillColor;
    if (data.font && this.format && this.format.font) this.format.font._loaded = data.font;
    if (data.sheet) this._sheet = data.sheet;
    if (data.row != null) this._row = data.row;
    if (data.col != null) this._col = data.col;
    if (data.rowCount != null) this._rowCount = data.rowCount;
    if (data.colCount != null) this._colCount = data.colCount;
  };

  function RangeFormat(range) {
    this._range = range;
    this._loaded = {};
    this.fill = new RangeFill(range);
    this.font = new RangeFont(range);
    this.borders = new RangeBorderCollection(range);
    return wrapHostObject(this, 'RangeFormat');
  }
  function defFormat(prop, op) {
    Object.defineProperty(RangeFormat.prototype, prop, {
      get: function () { return this._loaded[prop]; },
      set: function (v) {
        var r = this._range;
        var cmd = r._base({ op: op });
        cmd.value = v;
        r.context._enqueue(cmd);
        this._loaded[prop] = v;
      }
    });
  }
  defFormat('columnWidth', 'setColumnWidth');
  defFormat('rowHeight', 'setRowHeight');
  defFormat('horizontalAlignment', 'setHAlign');
  defFormat('verticalAlignment', 'setVAlign');
  defFormat('wrapText', 'setWrapText');
  RangeFormat.prototype.autofitColumns = function () {
    enqueueNotSupported(this._range.context, 'Excel.RangeFormat.autofitColumns');
  };
  RangeFormat.prototype.autofitRows = function () {
    enqueueNotSupported(this._range.context, 'Excel.RangeFormat.autofitRows');
  };
  RangeFormat.prototype.load = function () { return this; };

  function RangeBorderCollection(range) {
    this._range = range;
    return wrapHostObject(this, 'RangeBorderCollection');
  }
  RangeBorderCollection.prototype.getItem = function (index) {
    return new RangeBorder(this._range, String(index));
  };
  RangeBorderCollection.prototype.getItemOrNullObject = RangeBorderCollection.prototype.getItem;
  RangeBorderCollection.prototype.load = function () { return this; };

  function RangeBorder(range, index) {
    this._range = range;
    this._index = index;
    this._loaded = {};
    return wrapHostObject(this, 'RangeBorder');
  }
  function defBorder(prop) {
    Object.defineProperty(RangeBorder.prototype, prop, {
      get: function () { return this._loaded[prop]; },
      set: function (v) {
        var r = this._range;
        var cmd = r._base({ op: 'setBorder', edge: this._index });
        cmd[prop] = v;
        r.context._enqueue(cmd);
        this._loaded[prop] = v;
      }
    });
  }
  defBorder('style');
  defBorder('color');
  defBorder('weight');
  RangeBorder.prototype.load = function () { return this; };

  function RangeFill(range) {
    this._range = range;
    this._loaded = {};
    return wrapHostObject(this, 'RangeFill');
  }
  Object.defineProperty(RangeFill.prototype, 'color', {
    get: function () { return this._loaded.color; },
    set: function (v) {
      var r = this._range;
      r.context._enqueue(r._base({ op: 'setFill', color: String(v) }));
    }
  });
  RangeFill.prototype.load = function (props) {
    var r = this._range;
    r.context._enqueue(r._base({ op: 'loadRange', properties: parseLoad(props).concat(['fillColor']) }));
    return this;
  };

  function RangeFont(range) {
    this._range = range;
    this._loaded = {};
    return wrapHostObject(this, 'RangeFont');
  }
  function defFont(prop, key) {
    Object.defineProperty(RangeFont.prototype, prop, {
      get: function () { return this._loaded[prop]; },
      set: function (v) {
        var font = {};
        font[key || prop] = v;
        var r = this._range;
        r.context._enqueue(r._base({ op: 'setFont', font: font }));
      }
    });
  }
  defFont('bold');
  defFont('italic');
  defFont('underline');
  defFont('name');
  defFont('size');
  defFont('color');
  RangeFont.prototype.load = function (props) {
    var r = this._range;
    r.context._enqueue(r._base({ op: 'loadRange', properties: parseLoad(props).concat(['font']) }));
    return this;
  };

  function NamedItemCollection(context) {
    this.context = context;
    return wrapHostObject(this, 'NamedItemCollection');
  }
  NamedItemCollection.prototype.getItem = function (name) {
    return new NamedItem(this.context, String(name));
  };
  NamedItemCollection.prototype.getItemOrNullObject = NamedItemCollection.prototype.getItem;
  NamedItemCollection.prototype.add = function (name, reference, comment) {
    var item = new NamedItem(this.context, String(name));
    var cmd = {
      op: 'addNamedItem',
      name: String(name),
      comment: comment == null ? '' : String(comment),
      id: this.context._id(item)
    };
    if (reference && typeof reference === 'object' && reference._row != null) {
      cmd.sheet = reference._sheet;
      cmd.row = reference._row;
      cmd.col = reference._col;
      cmd.rowCount = reference._rowCount;
      cmd.colCount = reference._colCount;
    } else {
      var p = parseAddress(String(reference || 'A1'));
      cmd.sheet = p.sheet;
      cmd.row = p.row;
      cmd.col = p.col;
      cmd.rowCount = p.rowCount;
      cmd.colCount = p.colCount;
    }
    this.context._enqueue(cmd);
    return item;
  };

  function NamedItem(context, name) {
    this.context = context;
    this._name = name;
    this._loaded = {};
    this.context._id(this);
    return wrapHostObject(this, 'NamedItem');
  }
  Object.defineProperty(NamedItem.prototype, 'name', {
    get: function () { return this._loaded.name != null ? this._loaded.name : this._name; }
  });
  NamedItem.prototype.getRange = function () {
    return new Range(this.context, '', 0, 0, 1, 1, this._name);
  };
  NamedItem.prototype.delete = function () {
    enqueueNotSupported(this.context, 'Excel.NamedItem.delete');
  };
  NamedItem.prototype.load = function (props) {
    this.context._enqueue({ op: 'loadNamedItem', namedItem: this._name, id: this._oid, properties: parseLoad(props) });
    return this;
  };
  NamedItem.prototype._applyLoad = function (data) {
    if (!data) return;
    this._loaded = data;
  };

  function isStubbableMethodName(prop) {
    if (typeof prop !== 'string') return false;
    if (prop.charAt(0) === '_') return false;
    return prop !== 'then' && prop !== 'toJSON' && prop !== 'toString' && prop !== 'valueOf' &&
           prop !== 'constructor' && prop !== 'inspect' && prop !== 'prototype';
  }
  // Instance proxy (not prototype): a prototype Proxy re-enters on getter/setter
  // lookup and overflows the WASM JS stack ("Maximum call stack size exceeded").
  function wrapHostObject(obj, typeName) {
    if (obj == null || obj.__cellsWrapped) return obj;
    obj.__cellsWrapped = true;
    return new Proxy(obj, {
      get: function (target, prop, receiver) {
        if (typeof prop === 'string' && isStubbableMethodName(prop) && !(prop in target)) {
          return function () {
            var ctx = target.context;
            if (!ctx && target._range) ctx = target._range.context;
            if (ctx) enqueueNotSupported(ctx, 'Excel.' + typeName + '.' + prop);
            return receiver;
          };
        }
        return Reflect.get(target, prop, receiver);
      }
    });
  }

  var Excel = {
    run: function (arg1, arg2) {
      var batch;
      var context;
      if (typeof arg1 === 'function') {
        batch = arg1;
        context = new RequestContext();
      } else if (typeof arg2 === 'function') {
        context = arg1 && arg1.workbook ? arg1 : new RequestContext();
        batch = arg2;
      } else if (arg1 && typeof arg1.batch === 'function') {
        batch = arg1.batch;
        context = new RequestContext();
      } else {
        return Promise.reject(OfficeError('InvalidArgument', 'Excel.run requires a batch function'));
      }
      return Promise.resolve()
        .then(function () { return batch(context); })
        .then(function (value) {
          return context.sync().then(function () { return value === undefined ? context : value; });
        });
    },
    RequestContext: RequestContext,
    Workbook: Workbook,
    Worksheet: Worksheet,
    WorksheetCollection: WorksheetCollection,
    Range: Range,
    NamedItem: NamedItem,
    ClearApplyTo: { All: 'All', Formats: 'Formats', Contents: 'Contents', Hyperlinks: 'Hyperlinks' },
    RangeUnderlineStyle: { None: 'None', Single: 'Single', Double: 'Double' },
    InsertShiftDirection: { Down: 'Down', Right: 'Right' },
    DeleteShiftDirection: { Up: 'Up', Left: 'Left' },
    HorizontalAlignment: { General: 'General', Left: 'Left', Center: 'Center', Right: 'Right', Fill: 'Fill', Justify: 'Justify', CenterAcrossSelection: 'CenterAcrossSelection', Distributed: 'Distributed' },
    VerticalAlignment: { Top: 'Top', Center: 'Center', Bottom: 'Bottom', Justify: 'Justify', Distributed: 'Distributed' },
    BorderIndex: { EdgeTop: 'EdgeTop', EdgeBottom: 'EdgeBottom', EdgeLeft: 'EdgeLeft', EdgeRight: 'EdgeRight', InsideVertical: 'InsideVertical', InsideHorizontal: 'InsideHorizontal', DiagonalDown: 'DiagonalDown', DiagonalUp: 'DiagonalUp' },
    BorderLineStyle: { None: 'None', Continuous: 'Continuous', Dash: 'Dash', DashDot: 'DashDot', DashDotDot: 'DashDotDot', Dot: 'Dot', Double: 'Double', SlantDashDot: 'SlantDashDot' },
    BorderWeight: { Hairline: 'Hairline', Thin: 'Thin', Medium: 'Medium', Thick: 'Thick' },
    RangeCopyType: { All: 'All', Formulas: 'Formulas', Values: 'Values', Formats: 'Formats' },
    ChartType: { ColumnClustered: 'ColumnClustered', Line: 'Line', Pie: 'Pie', BarClustered: 'BarClustered', Area: 'Area', XYScatter: 'XYScatter' }
  };

  var info = { host: 'Excel', platform: 'OfficeOnline' };
  var readyPromise = Promise.resolve(info);
  var Office = {
    HostType: { Excel: 'Excel', Word: 'Word', PowerPoint: 'PowerPoint', Outlook: 'Outlook', OneNote: 'OneNote' },
    PlatformType: { PC: 'PC', OfficeOnline: 'OfficeOnline', Mac: 'Mac', iOS: 'iOS', Android: 'Android' },
    context: { host: 'Excel', platform: 'OfficeOnline' },
    onReady: function (cb) {
      if (typeof cb === 'function') return readyPromise.then(cb);
      return readyPromise;
    },
    initialize: null
  };

  var OfficeExtension = {
    ClientRequestContext: RequestContext,
    ClientObject: function () {},
    Error: OfficeError,
    ErrorCodes: {
      itemNotFound: 'ItemNotFound',
      invalidArgument: 'InvalidArgument',
      generalException: 'GeneralException',
      propertyNotLoaded: 'PropertyNotLoaded',
      notImplemented: 'NotImplemented'
    }
  };

  globalThis.Excel = Excel;
  globalThis.Office = Office;
  globalThis.OfficeExtension = OfficeExtension;
  if (typeof globalThis.console === 'undefined') {
    globalThis.console = {};
  }
  globalThis.console.log = function () {
    var parts = [];
    for (var i = 0; i < arguments.length; i++) {
      var v = arguments[i];
      parts.push(typeof v === 'object' ? JSON.stringify(v) : String(v));
    }
    native.print(parts.join(' '));
  };
  globalThis.console.info = globalThis.console.log;
  globalThis.console.warn = globalThis.console.log;
  globalThis.console.error = globalThis.console.log;
})(globalThis.__cellsNative);
)OFFICEJS";

}  // namespace

void registerOfficeJsHost(JSContext* ctx, JsSandbox* /*sandbox*/) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue native = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, native, "flush", JS_NewCFunction(ctx, jsFlush, "flush", 1));
    JS_SetPropertyStr(ctx, native, "print", JS_NewCFunction(ctx, jsPrint, "print", 1));
    JS_SetPropertyStr(ctx, native, "sheetNames",
                      JS_NewCFunction(ctx, jsSheetNames, "sheetNames", 0));
    JS_SetPropertyStr(ctx, native, "activeSheet",
                      JS_NewCFunction(ctx, jsActiveSheet, "activeSheet", 0));
    JS_SetPropertyStr(ctx, global, "__cellsNative", native);
    JS_FreeValue(ctx, global);

    JSValue ret =
        JS_Eval(ctx, kBootstrap, std::strlen(kBootstrap), "<officejs>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, ret);
        return;
    }
    JS_FreeValue(ctx, ret);
}

}  // namespace cells
