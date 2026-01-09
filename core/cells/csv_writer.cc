#include "core/cells/csv_writer.h"

#include <algorithm>
#include <sstream>

namespace cells {

CSVWriter::CSVWriter() = default;

CSVWriter::CSVWriter(const CSVWriteOptions& options) : options_(options) {}

std::vector<ID> CSVWriter::getOrderedColumns(const Sheet& sheet) const {
    // Collect all columns and sort by position
    std::vector<std::pair<uint32_t, ID>> columns;
    columns.reserve(sheet.columns.size());

    for (const auto& pair : sheet.columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }

    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(columns.size());
    for (const auto& col : columns) {
        result.push_back(col.second);
    }
    return result;
}

std::vector<ID> CSVWriter::getOrderedRows(const Sheet& sheet) const {
    // Collect all rows and sort by position
    std::vector<std::pair<uint32_t, ID>> rows;
    rows.reserve(sheet.rows.size());

    for (const auto& pair : sheet.rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }

    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(row.second);
    }
    return result;
}

std::string CSVWriter::formatValue(const CellValue& value) const {
    switch (value.type) {
        case CellValueType::BOOLEAN:
        case CellValueType::FORMULA_BOOLEAN:
            return value.raw == "1" || value.raw == "true" ? "true" : "false";

        case CellValueType::ERROR:
        case CellValueType::FORMULA_ERROR:
            return errorToString(value.error);

        case CellValueType::NUMBER:
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
        case CellValueType::STRING:
        case CellValueType::FORMULA:
        case CellValueType::FORMULA_NUMBER:
        case CellValueType::FORMULA_STRING:
        case CellValueType::FORMULA_EMPTY:
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

    // Track if any styled cells are being exported
    bool foundStyledCell = false;

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

                // Check if cell has styles that will be lost
                if (!foundStyledCell && !cell->styleId.isNull()) {
                    foundStyledCell = true;
                }
            }
            // Empty cell outputs empty field (nothing before the next delimiter)
        }
        oss << endl;
    }

    result.output = oss.str();

    // Set warning if styled cells were exported
    if (foundStyledCell) {
        result.stylesLost = true;
        result.warnings.push_back(
            "Cell styles (bold, colors, alignment, etc.) are not preserved in CSV format. "
            "Use XLSX format to preserve styles.");
    }

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
