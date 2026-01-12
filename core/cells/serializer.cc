#include "core/cells/serializer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"

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
        serializeSheet(*sheet, out);
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
    for (const auto& [idStr, style] : ordered) {
        out << "Y " << idStr << " {";
        out << "\"bold\":" << (style->bold ? "true" : "false");
        out << ",\"italic\":" << (style->italic ? "true" : "false");
        out << ",\"underline\":" << (style->underline ? "true" : "false");
        if (!style->bgColor.empty()) {
            out << ",\"bgColor\":\"" << escapeString(style->bgColor) << "\"";
        }
        if (!style->textColor.empty()) {
            out << ",\"textColor\":\"" << escapeString(style->textColor) << "\"";
        }
        if (!style->fontFamily.empty()) {
            out << ",\"fontFamily\":\"" << escapeString(style->fontFamily) << "\"";
        }
        if (style->fontSize > 0) {
            out << ",\"fontSize\":" << static_cast<int>(style->fontSize);
        }
        // Horizontal alignment
        switch (style->hAlign) {
            case TextAlign::CENTER:
                out << ",\"hAlign\":\"center\"";
                break;
            case TextAlign::RIGHT:
                out << ",\"hAlign\":\"right\"";
                break;
            case TextAlign::JUSTIFY:
                out << ",\"hAlign\":\"justify\"";
                break;
            default:
                break;  // LEFT is default, omit
        }
        // Vertical alignment
        switch (style->vAlign) {
            case VerticalAlign::TOP:
                out << ",\"vAlign\":\"top\"";
                break;
            case VerticalAlign::MIDDLE:
                out << ",\"vAlign\":\"middle\"";
                break;
            default:
                break;  // BOTTOM is default, omit
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

void Serializer::serializeSheet(const Sheet& sheet, std::ostream& out) const {
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

    // Columns, rows, cells
    serializeColumns(sheet, out);
    serializeRows(sheet, out);
    serializeCells(sheet, out);
}

void Serializer::serializeColumns(const Sheet& sheet, std::ostream& out) const {
    // Collect columns for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Axis*>> ordered;
    ordered.reserve(sheet.columns.size());

    for (const auto& pair : sheet.columns) {
        const Axis* axis = pair.second.get();
        ordered.emplace_back(axis->id.toString(), axis);
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeAxis(*item.second, 'C', out);
    }
}

void Serializer::serializeRows(const Sheet& sheet, std::ostream& out) const {
    // Collect rows for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Axis*>> ordered;
    ordered.reserve(sheet.rows.size());

    for (const auto& pair : sheet.rows) {
        const Axis* axis = pair.second.get();
        ordered.emplace_back(axis->id.toString(), axis);
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeAxis(*item.second, 'R', out);
    }
}

void Serializer::serializeCells(const Sheet& sheet, std::ostream& out) const {
    // Collect cells for alphabetical ordering by UUID
    std::vector<std::pair<std::string, const Cell*>> ordered;
    ordered.reserve(sheet.cells.size());

    for (const auto& pair : sheet.cells) {
        const Cell* cell = pair.second.get();
        ordered.emplace_back(cell->id.toString(), cell);
    }

    // Sort alphabetically by UUID (required for deterministic shared formula masters)
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Serialize in order
    for (const auto& item : ordered) {
        serializeCell(*item.second, sheet, out);
    }
}

void Serializer::serializeAxis(const Axis& axis, char prefix, std::ostream& out) const {
    // Format: C/R <id> <position> [props...]
    out << prefix << " " << axis.id.toString() << " " << axis.position;

    // Optional properties (only if non-default)
    if (axis.isColumn && axis.size != DEFAULT_COLUMN_WIDTH) {
        out << " w:" << axis.size;
    } else if (!axis.isColumn && axis.size != DEFAULT_ROW_HEIGHT) {
        out << " h:" << axis.size;
    }

    if (!axis.name.empty()) {
        out << " name:\"" << escapeString(axis.name) << "\"";
    }

    if (axis.hidden) {
        out << " hidden:1";
    }

    if (!axis.defaultStyleId.isNull()) {
        out << " sty:" << axis.defaultStyleId.toString();
    }

    out << "\n";
}

void Serializer::serializeCell(const Cell& cell, const Sheet& sheet, std::ostream& out) const {
    // Format: X <id> <col> <row> <type> <value> [fmt:<formatId>] [sty:<styleId>]
    out << "X " << cell.id.toString() << " " << cell.colId.toString() << " "
        << cell.rowId.toString() << " ";

    serializeCellValue(cell.value, cell, sheet, out);

    // Optional format property (only if not null/default)
    if (!cell.formatId.isNull()) {
        out << " fmt:" << cell.formatId.toString();
    }

    // Optional style property (only if not null/default)
    if (!cell.styleId.isNull()) {
        out << " sty:" << cell.styleId.toString();
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
