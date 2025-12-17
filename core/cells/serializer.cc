#include "core/cells/serializer.h"

#include <iomanip>
#include <sstream>
#include <vector>

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
    // Find head column (one with null prevId) and traverse in order
    std::vector<const Axis*> ordered;
    const Axis* head = nullptr;

    for (const auto& pair : sheet.columns) {
        if (pair.second->prevId.isNull()) {
            head = pair.second.get();
            break;
        }
    }

    // Traverse from head to tail
    const Axis* current = head;
    while (current != nullptr) {
        ordered.push_back(current);
        if (current->nextId.isNull()) {
            break;
        }
        auto it = sheet.columns.find(current->nextId);
        if (it == sheet.columns.end()) {
            break;
        }
        current = it->second.get();
    }

    // Serialize in order
    for (const Axis* col : ordered) {
        serializeAxis(*col, 'C', out);
    }
}

void Serializer::serializeRows(const Sheet& sheet, std::ostream& out) const {
    // Find head row (one with null prevId) and traverse in order
    std::vector<const Axis*> ordered;
    const Axis* head = nullptr;

    for (const auto& pair : sheet.rows) {
        if (pair.second->prevId.isNull()) {
            head = pair.second.get();
            break;
        }
    }

    // Traverse from head to tail
    const Axis* current = head;
    while (current != nullptr) {
        ordered.push_back(current);
        if (current->nextId.isNull()) {
            break;
        }
        auto it = sheet.rows.find(current->nextId);
        if (it == sheet.rows.end()) {
            break;
        }
        current = it->second.get();
    }

    // Serialize in order
    for (const Axis* row : ordered) {
        serializeAxis(*row, 'R', out);
    }
}

void Serializer::serializeCells(const Sheet& sheet, std::ostream& out) const {
    for (const auto& pair : sheet.cells) {
        serializeCell(*pair.second, out);
    }
}

void Serializer::serializeAxis(const Axis& axis, char prefix, std::ostream& out) const {
    // Format: C/R <id> <prev>[:<gap>] <next>[:<gap>] [props...]
    out << prefix << " " << axis.id.toString() << " ";

    // Prev link with gap before
    serializeLink(axis.prevId, axis.gapBefore, out);
    out << " ";

    // Next link with gap after
    serializeLink(axis.nextId, axis.gapAfter, out);

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

void Serializer::serializeLink(const ID& id, uint32_t gap, std::ostream& out) {
    if (id.isNull()) {
        out << "~";
    } else {
        out << id.toString();
    }

    // Only include gap if non-zero
    if (gap > 0) {
        out << ":" << gap;
    }
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

        case CellValueType::FORMULA:
            // Formula text is stored in value.raw or cell.formula->text
            if (cell.formula != nullptr && cell.formula->text != nullptr) {
                out << "\"" << escapeString(cell.formula->text) << "\"";
            } else {
                out << "\"" << escapeString(value.raw) << "\"";
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
