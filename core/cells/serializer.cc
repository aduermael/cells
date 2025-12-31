#include "core/cells/serializer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "core/cells/formula_serializer.h"

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

void Serializer::serializeSheet(const Sheet& sheet, std::ostream& out) const {
    // Sheet ID and name
    out << "S " << sheet.id.toString() << " \"" << escapeString(sheet.name) << "\"\n";

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
        serializeCell(*item.second, out);
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

    out << "\n";
}

void Serializer::serializeCell(const Cell& cell, std::ostream& out) const {
    // Format: X <id> <col> <row> <type> <value>
    out << "X " << cell.id.toString() << " " << cell.colId.toString() << " "
        << cell.rowId.toString() << " ";

    serializeCellValue(cell.value, cell, out);

    out << "\n";
}

void Serializer::serializeCellValue(const CellValue& value, const Cell& cell,
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
            if (cell.sharedFormulaRef != nullptr) {
                out << "\"=@" << cell.sharedFormulaRef->id.toString() << "\"";
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
