#include "core/cells/serializer.h"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

#include "core/cells/format_buffer.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/operation.h"
#if !defined(CELLS_NO_COLLAB)
#include "core/cells/oplog.h"
#endif
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"
#include "core/cells/theme.h"

namespace cells {

namespace {

// Format a double using shortest exact representation (via std::to_chars)
void writeDouble(std::ostream& out, double value) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    out.write(buf, ptr - buf);
}

}  // namespace

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

    // Serialize theme (after header, before named ranges/sheets)
    serializeTheme(workbook, out);

    // NOTE: Formats are now content-addressed (like styles) and serialized directly
    // on entities as base64. No separate F lines needed.

    // NOTE: Styles are now content-addressed and serialized directly on entities
    // (no separate Y lines needed - the base64 content IS the identity)

    // Serialize named ranges (before sheets, as formulas may reference them)
    serializeNamedRanges(workbook, out);

    // Serialize each sheet
    for (const auto& sheet : workbook.sheets) {
        serializeSheet(workbook, *sheet, out);
    }

    // Serialize operation log if it has operations (skipped when compiled out)
#if !defined(CELLS_NO_COLLAB)
    const OpLog* oplog = workbook.getOpLog();
    if (oplog != nullptr && !oplog->empty()) {
        serializeOpLog(*oplog, out);
    }
#endif

    // Serialize durable peer knowledge (frontiers for offline rejoin)
    serializePeerKnowledge(workbook, out);
}

void Serializer::serializeHeader(const Workbook& workbook, std::ostream& out) const {
    // Document ID and name
    out << "D " << workbook.id.toString() << " \"" << escapeString(workbook.name) << "\"\n";
}

void Serializer::serializeTheme(const Workbook& workbook, std::ostream& out) const {
    const Theme* theme = workbook.getTheme();
    if (theme == nullptr) {
        return;
    }

    // Format: T "<name>" colors:<12 hex values> fonts:"<major>","<minor>"
    out << "T \"" << escapeString(theme->name) << "\" colors:";

    // 12 colors in index order (0-11), no # prefix, comma-separated
    for (size_t i = 0; i < 12; ++i) {
        if (i > 0) {
            out << ",";
        }
        const std::string& color = theme->colorScheme.colors[i];
        // Strip leading '#' if present
        if (!color.empty() && color[0] == '#') {
            out << color.substr(1);
        } else {
            out << color;
        }
    }

    out << " fonts:\"" << escapeString(theme->fontScheme.majorFont) << "\",\""
        << escapeString(theme->fontScheme.minorFont) << "\"\n";
}

void Serializer::serializeCustomFormats(const Workbook& /*workbook*/, std::ostream& /*out*/) const {
    // No-op: Formats are now content-addressed and serialized directly on entities.
    // This method is kept for API compatibility but does nothing.
    // Custom formats are encoded as base64 in fmt: properties on cells/axes.
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

    // Sheet view/format properties (only if non-default)
    // Format: V <key:value...>
    const bool hasViewProps = !sheet.showGridLines || sheet.zoomScale != 100 ||
                              sheet.freezeCol > 0 || sheet.freezeRow > 0 ||
                              sheet.defaultRowHeight > 0 || sheet.hasPageMargins;
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
        if (sheet.defaultRowHeight > 0) {
            out << " defaultRowHeight:";
            writeDouble(out, sheet.defaultRowHeight);
        }
        if (sheet.hasPageMargins) {
            out << " pageMargins:";
            writeDouble(out, sheet.pageMargins.left);
            out << ",";
            writeDouble(out, sheet.pageMargins.right);
            out << ",";
            writeDouble(out, sheet.pageMargins.top);
            out << ",";
            writeDouble(out, sheet.pageMargins.bottom);
            out << ",";
            writeDouble(out, sheet.pageMargins.header);
            out << ",";
            writeDouble(out, sheet.pageMargins.footer);
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
    // Format: RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [fmt:<base64>]
    // [sty:<base64>]
    for (const auto& item : ordered) {
        const Range* range = item.second;
        out << "RG " << range->id.toString() << " " << range->startColId.toString() << " "
            << range->startRowId.toString() << " " << range->endColId.toString() << " "
            << range->endRowId.toString() << " " << static_cast<int>(range->flags);

        // Add format content directly (content-addressed) if FORMAT flag is set
        if (range->hasFlag(RangeFlags::FORMAT) && range->format.has_value()) {
            out << " fmt:" << range->format->toBase64();
        }

        // Add style content directly (content-addressed) if STYLE flag is set
        if (range->hasFlag(RangeFlags::STYLE) && range->style.has_value()) {
            out << " sty:" << range->style->toBase64();
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

    // Original Excel-unit size (char-widths for columns, points for rows)
    if (axis.sizeOriginal > 0) {
        out << (axis.isColumn() ? " wo:" : " ho:");
        writeDouble(out, axis.sizeOriginal);
    }

    if (!axis.name.empty()) {
        out << " name:\"" << escapeString(axis.name) << "\"";
    }

    if (axis.hidden()) {
        out << " hidden:1";
    }

    // Axis style is stored in workbook._entityStyles map (content-addressed)
    if (axis.hasStyle()) {
        const StyleBuffer* styleBuf = workbook.getEntityStyle(axis.id);
        if (styleBuf != nullptr) {
            out << " sty:" << styleBuf->toBase64();
        }
    }

    // Axis format is stored in workbook._entityFormats map (content-addressed)
    if (axis.hasFormat()) {
        const FormatBuffer* formatBuf = workbook.getEntityFormat(axis.id);
        if (formatBuf != nullptr) {
            out << " fmt:" << formatBuf->toBase64();
        }
    }

    out << "\n";
}

void Serializer::serializeCell(const Workbook& workbook, const Cell& cell, const Sheet& sheet,
                               std::ostream& out) const {
    // Format: X <id> <col> <row> <type> <value> [fmt:<base64>] [sty:<base64>]
    out << "X " << cell.id.toString() << " " << cell.colId.toString() << " "
        << cell.rowId.toString() << " ";

    serializeCellValue(cell.value, cell, sheet, out);

    // Optional format property (content-addressed) - read from workbook map
    const FormatBuffer* formatBuf = workbook.getEntityFormat(cell.id);
    if (formatBuf != nullptr) {
        out << " fmt:" << formatBuf->toBase64();
    }

    // Optional style property (content-addressed) - read from workbook
    const StyleBuffer* styleBuf = workbook.getEntityStyle(cell.id);
    if (styleBuf != nullptr) {
        out << " sty:" << styleBuf->toBase64();
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

#if !defined(CELLS_NO_COLLAB)
void Serializer::serializeOpLog(const OpLog& oplog, std::ostream& out) const {
    // OpLog section header
    out << "#oplog\n";

    // Serialize each operation in HLC order
    const auto& operations = oplog.getAllOperations();
    for (const auto& op : operations) {
        serializeOperation(op, out);
    }
}
#endif

void Serializer::serializeOperation(const Operation& op, std::ostream& out) const {
    // Format: O <hlc> <op-type> <target-id> <payload-json>
    // Example: O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}
    out << "O " << op.hlc.toString() << " " << opTypeToString(op.type) << " "
        << op.target_id.toString() << " " << op.payload << "\n";
}

void Serializer::serializePeerKnowledge(const Workbook& workbook, std::ostream& out) const {
    const auto& knowledge = workbook.getPeerKnowledge();
    if (knowledge.empty()) {
        return;
    }

    // Format: P <peer_id> <hlc>
    // Example: P AbCdEf12 1705312200000.0.N3f8hJ2w
    out << "#peers\n";
    for (const auto& pair : knowledge) {
        if (pair.first.isNull()) {
            continue;
        }
        out << "P " << pair.first.toString() << " " << pair.second.toString() << "\n";
    }
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
