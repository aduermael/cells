#include "core/cells/csv_writer.h"

#include <sstream>

namespace cells {

CSVWriter::CSVWriter() = default;

CSVWriter::CSVWriter(const CSVWriteOptions& options) : options_(options) {}

std::vector<ID> CSVWriter::getOrderedColumns(const Sheet& sheet) const {
    std::vector<ID> columns;

    ID colId = sheet.firstCol;
    while (!colId.isNull()) {
        columns.push_back(colId);
        auto it = sheet.columns.find(colId);
        if (it == sheet.columns.end()) {
            break;
        }
        colId = it->second->nextId;
    }

    return columns;
}

std::vector<ID> CSVWriter::getOrderedRows(const Sheet& sheet) const {
    std::vector<ID> rows;

    ID rowId = sheet.firstRow;
    while (!rowId.isNull()) {
        rows.push_back(rowId);
        auto it = sheet.rows.find(rowId);
        if (it == sheet.rows.end()) {
            break;
        }
        rowId = it->second->nextId;
    }

    return rows;
}

std::string CSVWriter::formatValue(const CellValue& value) const {
    switch (value.type) {
        case CellValueType::BOOLEAN:
            return value.raw == "1" || value.raw == "true" ? "true" : "false";

        case CellValueType::ERROR:
            return errorToString(value.error);

        case CellValueType::NUMBER:
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
        case CellValueType::STRING:
        case CellValueType::FORMULA:
            // For formulas, output the cached computed value (stored in raw)
            return value.raw;
    }

    return value.raw;
}

bool CSVWriter::needsQuoting(const std::string& field) const {
    for (char const c : field) {
        if (c == options_.delimiter || c == '"' || c == '\r' || c == '\n') {
            return true;
        }
    }
    return false;
}

std::string CSVWriter::escapeField(const std::string& field) const {
    if (!needsQuoting(field)) {
        return field;
    }

    // Quote the field and escape any quotes within it
    std::string result;
    result.reserve(field.size() + 2);  // At minimum, add surrounding quotes
    result.push_back('"');

    for (char const c : field) {
        if (c == '"') {
            result.push_back('"');  // Escape quote by doubling it
        }
        result.push_back(c);
    }

    result.push_back('"');
    return result;
}

std::string CSVWriter::lineEnding() const {
    return options_.useCRLF ? "\r\n" : "\n";
}

CSVWriteResult CSVWriter::write(const Sheet& sheet) {
    CSVWriteResult result;

    // Get ordered columns and rows
    std::vector<ID> columns = getOrderedColumns(sheet);
    std::vector<ID> const rows = getOrderedRows(sheet);

    if (columns.empty()) {
        // Empty sheet - return empty output
        return result;
    }

    std::ostringstream oss;
    const std::string endl = lineEnding();

    // Write header row if enabled
    if (options_.includeHeader) {
        for (size_t c = 0; c < columns.size(); c++) {
            if (c > 0) {
                oss << options_.delimiter;
            }

            // Get column name
            auto it = sheet.columns.find(columns[c]);
            if (it != sheet.columns.end() && !it->second->name.empty()) {
                oss << escapeField(it->second->name);
            } else {
                // Generate default column name (A, B, ..., Z, AA, AB, ...)
                std::string name;
                size_t n = c;
                do {
                    name.insert(name.begin(), static_cast<char>('A' + (n % 26)));
                    n = n / 26;
                    if (n > 0) {
                        n--;  // Adjust for 1-based "digits"
                    }
                } while (n > 0 && name.size() < 3);  // Limit to 3 chars

                // Only use generated name if column has no custom name
                oss << escapeField(name);
            }
        }
        oss << endl;
    }

    // Write data rows
    for (const ID& rowId : rows) {
        for (size_t c = 0; c < columns.size(); c++) {
            if (c > 0) {
                oss << options_.delimiter;
            }

            // Find cell at (column, row)
            const Cell* cell = const_cast<Sheet&>(sheet).getCellAt(columns[c], rowId);
            if (cell != nullptr) {
                std::string const value = formatValue(cell->value);
                oss << escapeField(value);
            }
            // Empty cell outputs empty field (nothing before the next delimiter)
        }
        oss << endl;
    }

    result.output = oss.str();
    return result;
}

CSVWriteResult CSVWriter::write(const Workbook& workbook) {
    if (workbook.sheets.empty()) {
        CSVWriteResult result;
        result.error = CSVWriteError("Workbook has no sheets");
        return result;
    }

    // Write first sheet only (CSV doesn't support multiple sheets)
    return write(*workbook.sheets[0]);
}

// Convenience functions
CSVWriteResult writeCSV(const Sheet& sheet) {
    CSVWriter writer;
    return writer.write(sheet);
}

CSVWriteResult writeCSV(const Sheet& sheet, const CSVWriteOptions& options) {
    CSVWriter writer(options);
    return writer.write(sheet);
}

CSVWriteResult writeCSV(const Workbook& workbook) {
    CSVWriter writer;
    return writer.write(workbook);
}

CSVWriteResult writeCSV(const Workbook& workbook, const CSVWriteOptions& options) {
    CSVWriter writer(options);
    return writer.write(workbook);
}

}  // namespace cells
