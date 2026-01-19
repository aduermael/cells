#include "core/cells/serializer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"

namespace cells {

// --- String escaping ---

std::string escapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size());

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
                break;
        }
    }

    return result;
}

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
        case BorderStyle::NONE:
        default:
            return "none";
    }
}

// Helper to convert string to BorderStyle enum
BorderStyle stringToBorderStyle(const std::string& str) {
    if (str == "thin") {
        return BorderStyle::THIN;
    }
    if (str == "medium") {
        return BorderStyle::MEDIUM;
    }
    if (str == "thick") {
        return BorderStyle::THICK;
    }
    if (str == "dashed") {
        return BorderStyle::DASHED;
    }
    if (str == "dotted") {
        return BorderStyle::DOTTED;
    }
    if (str == "double") {
        return BorderStyle::DOUBLE;
    }
    if (str == "hair") {
        return BorderStyle::HAIR;
    }
    if (str == "mediumDashed") {
        return BorderStyle::MEDIUM_DASHED;
    }
    if (str == "dashDot") {
        return BorderStyle::DASH_DOT;
    }
    if (str == "mediumDashDot") {
        return BorderStyle::MEDIUM_DASH_DOT;
    }
    if (str == "dashDotDot") {
        return BorderStyle::DASH_DOT_DOT;
    }
    if (str == "mediumDashDotDot") {
        return BorderStyle::MEDIUM_DASH_DOT_DOT;
    }
    if (str == "slantDashDot") {
        return BorderStyle::SLANT_DASH_DOT;
    }
    return BorderStyle::NONE;
}

// --- Serializer ---

Serializer::Serializer() = default;

std::string Serializer::serialize(const Workbook& workbook) const {
    std::ostringstream ss;
    serialize(workbook, ss);
    return ss.str();
}

void Serializer::serialize(const Workbook& workbook, std::ostream& out) const {
    serializeHeader(workbook, out);

    // Serialize custom formats (before sheets, as cells may reference them)
    serializeCustomFormats(workbook, out);

    // Serialize styles (before sheets, as cells may reference them)
    serializeStyles(workbook, out);

    // Serialize named ranges (before sheets, as formulas may reference them)
    serializeNamedRanges(workbook, out);

    // Serialize each sheet
    for (const auto& sheet : workbook.sheets) {
        serializeSheet(workbook, *sheet, out);
    }

    // Serialize operation log if it has operations
    const OpLog* oplog = workbook.getOpLog();
    if (oplog != nullptr && !oplog->empty()) {
        serializeOpLog(*oplog, out);
    }
}

void Serializer::serializeHeader(const Workbook& workbook, std::ostream& out) const {
    // Document ID and name
    out << "D " << workbook.id.toString() << " \"" << escapeString(workbook.name) << "\"\n";
}

void Serializer::serializeCustomFormats(const Workbook& workbook, std::ostream& out) const {
    const auto& customFormats = workbook.getCustomFormats();
    if (customFormats.empty()) {
        return;
    }

    // Sort formats by ID for deterministic output
    std::vector<std::pair<std::string, std::string>> ordered;
    ordered.reserve(customFormats.size());

    for (const auto& [formatId, formatCode] : customFormats) {
        ordered.emplace_back(formatId.toString(), formatCode);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Output format definitions
    // Format: F <format-id> "<format-code>"
    for (const auto& [idStr, formatCode] : ordered) {
        out << "F " << idStr << " \"" << escapeString(formatCode) << "\"\n";
    }
}

void Serializer::serializeStyles(const Workbook& workbook, std::ostream& out) const {
    const auto& styles = workbook.getStyles();
    if (styles.empty()) {
        return;
    }

    // Sort styles by ID for deterministic output
    std::vector<std::pair<std::string, const CellStyle*>> ordered;
    ordered.reserve(styles.size());

    for (const auto& [styleId, style] : styles) {
        ordered.emplace_back(styleId.toString(), &style);
    }

    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Output style definitions
    // Format: Y <style-id> <json-props>
    // Properties are serialized based on defined flags (source of truth)
    for (const auto& [idStr, style] : ordered) {
        out << "Y " << idStr << " {";
        bool first = true;

        // Helper lambda to add comma separator
        auto addComma = [&first, &out]() {
            if (!first) {
                out << ",";
            }
            first = false;
        };

        // Serialize properties based on defined flags
        if (style->isDefined(DEFINED_BOLD)) {
            addComma();
            out << "\"bold\":" << (style->bold ? "true" : "false");
        }
        if (style->isDefined(DEFINED_ITALIC)) {
            addComma();
            out << "\"italic\":" << (style->italic ? "true" : "false");
        }
        if (style->isDefined(DEFINED_UNDERLINE)) {
            addComma();
            out << "\"underline\":" << (style->underline ? "true" : "false");
        }
        if (style->isDefined(DEFINED_WRAPTEXT)) {
            addComma();
            out << "\"wrapText\":" << (style->wrapText ? "true" : "false");
        }
        if (style->isDefined(DEFINED_BGCOLOR)) {
            addComma();
            out << "\"bgColor\":\"" << escapeString(style->bgColor) << "\"";
        }
        if (style->isDefined(DEFINED_TEXTCOLOR)) {
            addComma();
            out << "\"textColor\":\"" << escapeString(style->textColor) << "\"";
        }
        if (style->isDefined(DEFINED_FONTFAMILY)) {
            addComma();
            out << "\"fontFamily\":\"" << escapeString(style->fontFamily) << "\"";
        }
        if (style->isDefined(DEFINED_FONTSIZE)) {
            addComma();
            out << "\"fontSize\":" << static_cast<int>(style->fontSize);
        }
        if (style->isDefined(DEFINED_HALIGN)) {
            addComma();
            out << "\"hAlign\":\"";
            switch (style->hAlign) {
                case TextAlign::LEFT:
                    out << "left";
                    break;
                case TextAlign::CENTER:
                    out << "center";
                    break;
                case TextAlign::RIGHT:
                    out << "right";
                    break;
                case TextAlign::JUSTIFY:
                    out << "justify";
                    break;
                case TextAlign::GENERAL:
                    out << "general";
                    break;
            }
            out << "\"";
        }
        if (style->isDefined(DEFINED_VALIGN)) {
            addComma();
            out << "\"vAlign\":\"";
            switch (style->vAlign) {
                case VerticalAlign::TOP:
                    out << "top";
                    break;
                case VerticalAlign::MIDDLE:
                    out << "middle";
                    break;
                case VerticalAlign::BOTTOM:
                    out << "bottom";
                    break;
            }
            out << "\"";
        }
        // Border edges - serialize individually if defined
        if (style->isDefined(DEFINED_BORDER_TOP) || style->isDefined(DEFINED_BORDER_RIGHT) ||
            style->isDefined(DEFINED_BORDER_BOTTOM) || style->isDefined(DEFINED_BORDER_LEFT)) {
            addComma();
            out << "\"border\":{";
            bool borderFirst = true;
            auto addBorderComma = [&borderFirst, &out]() {
                if (!borderFirst) {
                    out << ",";
                }
                borderFirst = false;
            };
            if (style->isDefined(DEFINED_BORDER_TOP)) {
                addBorderComma();
                out << "\"top\":{\"style\":\"" << borderStyleToString(style->border.top.style)
                    << "\"";
                if (!style->border.top.color.empty()) {
                    out << ",\"color\":\"" << escapeString(style->border.top.color) << "\"";
                }
                out << "}";
            }
            if (style->isDefined(DEFINED_BORDER_RIGHT)) {
                addBorderComma();
                out << "\"right\":{\"style\":\"" << borderStyleToString(style->border.right.style)
                    << "\"";
                if (!style->border.right.color.empty()) {
                    out << ",\"color\":\"" << escapeString(style->border.right.color) << "\"";
                }
                out << "}";
            }
            if (style->isDefined(DEFINED_BORDER_BOTTOM)) {
                addBorderComma();
                out << "\"bottom\":{\"style\":\"" << borderStyleToString(style->border.bottom.style)
                    << "\"";
                if (!style->border.bottom.color.empty()) {
                    out << ",\"color\":\"" << escapeString(style->border.bottom.color) << "\"";
                }
                out << "}";
            }
            if (style->isDefined(DEFINED_BORDER_LEFT)) {
                addBorderComma();
                out << "\"left\":{\"style\":\"" << borderStyleToString(style->border.left.style)
                    << "\"";
                if (!style->border.left.color.empty()) {
                    out << ",\"color\":\"" << escapeString(style->border.left.color) << "\"";
                }
                out << "}";
            }
            out << "}";
        }
        out << "}\n";
    }
}

void Serializer::serializeNamedRanges(const Workbook& workbook, std::ostream& out) const {
    const NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry == nullptr) {
        return;
    }

    const std::vector<const NamedRange*> allRanges = registry->getAll();
    if (allRanges.empty()) {
        return;
    }

    // Copy to vector of values for deterministic sorting (avoids pointer comparison)
    std::vector<NamedRange> rangesCopy;
    rangesCopy.reserve(allRanges.size());
    for (const NamedRange* nr : allRanges) {
        rangesCopy.push_back(*nr);
    }

    // Sort named ranges by name for deterministic output
    std::sort(rangesCopy.begin(), rangesCopy.end(),
              [](const NamedRange& a, const NamedRange& b) { return a.name < b.name; });

    // Output named range definitions
    // Format: N "<name>" <scope:W|S> <scope-sheet-id|-> <target-type> <target-data>
    // Target types: CELL, RANGE, COLUMN, ROW, COLUMN_RANGE, ROW_RANGE
    // Target data format depends on type:
    //   CELL: <cell-id> <sheet-id>
    //   RANGE: <id1> <id2> <sheet-id>
    //   COLUMN/ROW: <axis-id> <sheet-id>
    //   COLUMN_RANGE/ROW_RANGE: <id1> <id2> <sheet-id>
    for (const NamedRange& nr : rangesCopy) {
        out << "N \"" << escapeString(nr.name) << "\" ";

        // Scope
        if (nr.scope == NamedRangeScope::WORKBOOK) {
            out << "W -";
        } else {
            out << "S " << nr.scopeSheetId.toString();
        }

        // Target type and data
        const NamedRangeTarget& target = nr.target;
        switch (target.type) {
            case NamedRangeTarget::Type::CELL:
                out << " CELL " << target.id1.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
            case NamedRangeTarget::Type::RANGE:
                out << " RANGE " << target.id1.toString() << " " << target.id2.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
            case NamedRangeTarget::Type::COLUMN:
                out << " COLUMN " << target.id1.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
            case NamedRangeTarget::Type::ROW:
                out << " ROW " << target.id1.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
            case NamedRangeTarget::Type::COLUMN_RANGE:
                out << " COLUMN_RANGE " << target.id1.toString() << " " << target.id2.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
            case NamedRangeTarget::Type::ROW_RANGE:
                out << " ROW_RANGE " << target.id1.toString() << " " << target.id2.toString();
                out << " " << (target.sheetId.isNull() ? "-" : target.sheetId.toString());
                break;
        }
        out << "\n";
    }
}

void Serializer::serializeSheet(const Workbook& workbook, const Sheet& sheet,
                                std::ostream& out) const {
    // Sheet ID and name
    out << "S " << sheet.id.toString() << " \"" << escapeString(sheet.name) << "\"\n";

    // Sheet view properties (only if non-default)
    // Format: V <key:value...>
    // showGridLines is true by default, only output if false
    // zoomScale is 100 by default, only output if different
    // freezeCol/freezeRow are 0 by default, only output if non-zero
    const bool hasViewProps = !sheet.showGridLines || sheet.zoomScale != 100 ||
                              sheet.freezeCol > 0 || sheet.freezeRow > 0;
    if (hasViewProps) {
        out << "V";
        if (!sheet.showGridLines) {
            out << " showGridLines:0";
        }
        if (sheet.zoomScale != 100) {
            out << " zoomScale:" << sheet.zoomScale;
        }
        if (sheet.freezeCol > 0) {
            out << " freezeCol:" << sheet.freezeCol;
        }
        if (sheet.freezeRow > 0) {
            out << " freezeRow:" << sheet.freezeRow;
        }
        out << "\n";
    }

    // Columns, rows, cells, ranges
    serializeColumns(workbook, sheet, out);
    serializeRows(workbook, sheet, out);
    serializeCells(workbook, sheet, out);
    serializeRanges(sheet, out);
}

void Serializer::serializeColumns(const Workbook& workbook, const Sheet& sheet,
                                  std::ostream& out) const {
    // Collect columns for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Axis*>> ordered;
    ordered.reserve(sheet.columnCount());

    for (const ID& colId : sheet.getColumnIds()) {
        const Axis* axis = sheet.getColumn(colId);
        if (axis) {
            ordered.emplace_back(axis->id.toString(), axis);
        }
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeAxis(workbook, *item.second, 'C', out);
    }
}

void Serializer::serializeRows(const Workbook& workbook, const Sheet& sheet,
                               std::ostream& out) const {
    // Collect rows for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Axis*>> ordered;
    ordered.reserve(sheet.rowCount());

    for (const ID& rowId : sheet.getRowIds()) {
        const Axis* axis = sheet.getRow(rowId);
        if (axis) {
            ordered.emplace_back(axis->id.toString(), axis);
        }
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeAxis(workbook, *item.second, 'R', out);
    }
}

void Serializer::serializeCells(const Workbook& workbook, const Sheet& sheet,
                                std::ostream& out) const {
    // Collect cells for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Cell*>> ordered;
    ordered.reserve(sheet.getCellIds().size());

    for (const ID& cellId : sheet.getCellIds()) {
        const Cell* cell = workbook.getCell(cellId);
        if (cell) {
            ordered.emplace_back(cell->id.toString(), cell);
        }
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeCell(workbook, *item.second, sheet, out);
    }
}

void Serializer::serializeRanges(const Sheet& sheet, std::ostream& out) const {
    // Collect ranges for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Range*>> ordered;
    const auto rangeIds = sheet.getRangeIds();  // Returns vector by value
    ordered.reserve(rangeIds.size());

    for (const ID& rangeId : rangeIds) {
        const Range* range = sheet.getRange(rangeId);
        if (range != nullptr) {
            ordered.emplace_back(range->id.toString(), range);
        }
    }

    // Sort alphabetically by UUID for deterministic output
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize each range
    // Format: RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [sty:<styleId>]
    for (const auto& item : ordered) {
        const Range* range = item.second;
        out << "RG " << range->id.toString() << " " << range->startColId.toString() << " "
            << range->startRowId.toString() << " " << range->endColId.toString() << " "
            << range->endRowId.toString() << " " << static_cast<int>(range->flags);

        // Add style reference if RANGE_STYLE flag is set
        if (range->hasFlag(RangeFlags::STYLE)) {
            const ID styleId = sheet.getRangeStyleId(range->id);
            if (!styleId.isNull()) {
                out << " sty:" << styleId.toString();
            }
        }

        out << "\n";
    }
}

void Serializer::serializeAxis(const Workbook& workbook, const Axis& axis, char prefix,
                               std::ostream& out) const {
    // Format: C/R <id> <position> [props...]
    out << prefix << " " << axis.id.toString() << " " << axis.position;

    // Optional properties (only if non-default)
    if (axis.isColumn() && axis.size != DEFAULT_COLUMN_WIDTH) {
        out << " w:" << axis.size;
    } else if (!axis.isColumn() && axis.size != DEFAULT_ROW_HEIGHT) {
        out << " h:" << axis.size;
    }

    if (!axis.name.empty()) {
        out << " name:\"" << escapeString(axis.name) << "\"";
    }

    if (axis.hidden()) {
        out << " hidden:1";
    }

    // Axis style is stored in workbook._styles map (not in Axis struct)
    if (axis.hasStyle()) {
        const ID styleId = workbook.getStyleId(axis.id);
        if (!styleId.isNull()) {
            out << " sty:" << styleId.toString();
        }
    }

    // Axis format is stored in workbook._formats map (not in Axis struct)
    if (axis.hasFormat()) {
        const ID formatId = workbook.getFormatId(axis.id);
        if (!formatId.isNull()) {
            out << " fmt:" << formatId.toString();
        }
    }

    out << "\n";
}

void Serializer::serializeCell(const Workbook& workbook, const Cell& cell, const Sheet& sheet,
                               std::ostream& out) const {
    // Format: X <id> <col> <row> <type> <value> [fmt:<formatId>] [sty:<styleId>]
    out << "X " << cell.id.toString() << " " << cell.colId.toString() << " "
        << cell.rowId.toString() << " ";

    serializeCellValue(cell.value, cell, sheet, out);

    // Optional format property (only if not null/default) - read from workbook map
    const ID formatId = workbook.getFormatId(cell.id);
    if (!formatId.isNull()) {
        out << " fmt:" << formatId.toString();
    }

    // Optional style property (only if not null/default) - read from workbook map
    const ID styleId = workbook.getStyleId(cell.id);
    if (!styleId.isNull()) {
        out << " sty:" << styleId.toString();
    }

    out << "\n";
}

void Serializer::serializeCellValue(const CellValue& value, const Cell& cell, const Sheet& sheet,
                                    std::ostream& out) const {
    const char typeChar = valueTypeToChar(value.type);
    out << typeChar << " ";

    switch (value.type) {
        case CellValueType::NUMBER: {
            // Use high precision for numbers to avoid loss
            const double num = value.asNumber();
            // Check if it's an integer
            if (num == static_cast<double>(static_cast<long long>(num))) {
                out << static_cast<long long>(num);
            } else {
                out << std::setprecision(17) << num;
            }
            break;
        }

        case CellValueType::STRING:
            out << "\"" << escapeString(value.raw) << "\"";
            break;

        // All formula types serialize the same way - output the formula text
        case CellValueType::FORMULA:
        case CellValueType::FORMULA_NUMBER:
        case CellValueType::FORMULA_STRING:
        case CellValueType::FORMULA_BOOLEAN:
        case CellValueType::FORMULA_ERROR:
        case CellValueType::FORMULA_EMPTY:
            // Shared formula subscriber: write =@masterUUID reference
            if (cell.isSharedFormula()) {
                const ID masterId = sheet.getSharedFormulaMaster(cell.id);
                if (!masterId.isNull()) {
                    out << "\"=@" << masterId.toString() << "\"";
                } else {
                    // Should not happen - subscriber without master
                    out << "\"=\"";
                }
            }
            // Master or regular formula: generate text from AST
            else if (cell.formula != nullptr && cell.formula->ast != nullptr) {
                out << "\"" << escapeString(FormulaSerializer::serialize(cell.formula->ast))
                    << "\"";
            } else {
                // Fallback for cells without AST (shouldn't happen)
                out << "\"=\"";
            }
            break;

        case CellValueType::BOOLEAN:
            out << (value.asBoolean() ? "true" : "false");
            break;

        case CellValueType::ERROR:
            // Error strings are not quoted
            out << errorToString(value.error);
            break;

        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
            // ISO 8601 format, stored as raw string (not quoted)
            out << value.raw;
            break;
    }
}

void Serializer::serializeOpLog(const OpLog& oplog, std::ostream& out) const {
    // OpLog section header
    out << "#oplog\n";

    // Serialize each operation in HLC order
    const auto& operations = oplog.getAllOperations();
    for (const auto& op : operations) {
        serializeOperation(op, out);
    }
}

void Serializer::serializeOperation(const Operation& op, std::ostream& out) const {
    // Format: O <hlc> <op-type> <target-id> <payload-json>
    // Example: O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}
    out << "O " << op.hlc.toString() << " " << opTypeToString(op.type) << " "
        << op.target_id.toString() << " " << op.payload << "\n";
}

// --- Convenience functions ---

std::string serialize(const Workbook& workbook) {
    const Serializer serializer;
    return serializer.serialize(workbook);
}

void serialize(const Workbook& workbook, std::ostream& out) {
    const Serializer serializer;
    serializer.serialize(workbook, out);
}

}  // namespace cells
