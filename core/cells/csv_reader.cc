#include "core/cells/csv_reader.h"

#include <cctype>

#include <algorithm>
#include <charconv>
#include <sstream>

#include "core/cells/id.h"

namespace cells {

// UTF-8 BOM bytes: EF BB BF
constexpr unsigned char UTF8_BOM[] = {0xEF, 0xBB, 0xBF};
constexpr size_t UTF8_BOM_SIZE = 3;

std::string CSVReadError::toString() const {
    std::ostringstream oss;
    oss << "CSV error";
    if (line > 0) {
        oss << " at line " << line;
        if (column > 0) {
            oss << ", column " << column;
        }
    }
    oss << ": " << message;
    return oss.str();
}

CSVReader::CSVReader() = default;

CSVReader::CSVReader(const CSVReadOptions& options) : options_(options) {}

void CSVReader::reset() {
    lineNum_ = 0;
    errorMsg_.clear();
}

bool CSVReader::setError(const std::string& message) {
    errorMsg_ = message;
    return false;
}

bool CSVReader::setError(int line, const std::string& message) {
    lineNum_ = line;
    errorMsg_ = message;
    return false;
}

std::string_view CSVReader::skipBOM(std::string_view content) {
    if (content.size() >= UTF8_BOM_SIZE) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(content.data());
        if (bytes[0] == UTF8_BOM[0] && bytes[1] == UTF8_BOM[1] && bytes[2] == UTF8_BOM[2]) {
            return content.substr(UTF8_BOM_SIZE);
        }
    }
    return content;
}

bool CSVReader::looksLikeNumber(const std::string& s) {
    if (s.empty()) {
        return false;
    }

    // Try to parse as double
    double value = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);

    // Must consume entire string and succeed
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

bool CSVReader::looksLikeBoolean(const std::string& s) {
    // Case-insensitive check for true/false
    std::string lower;
    lower.reserve(s.size());
    for (char const c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower == "true" || lower == "false";
}

char CSVReader::detectDelimiter(std::string_view content) {
    // Skip BOM if present
    content = skipBOM(content);

    // Sample the first ~1000 characters (or less if content is shorter)
    constexpr size_t SAMPLE_SIZE = 1000;
    const std::string_view sample = content.substr(0, std::min(content.size(), SAMPLE_SIZE));

    // Count occurrences of potential delimiters outside of quoted fields
    int commaCount = 0;
    int semicolonCount = 0;
    int tabCount = 0;

    bool inQuote = false;
    for (size_t i = 0; i < sample.size(); ++i) {
        char const c = sample[i];

        if (c == '"') {
            // Handle escaped quotes ("") inside quoted fields
            if (inQuote && i + 1 < sample.size() && sample[i + 1] == '"') {
                ++i;  // Skip the escaped quote
            } else {
                inQuote = !inQuote;
            }
        } else if (!inQuote) {
            if (c == ',') {
                ++commaCount;
            } else if (c == ';') {
                ++semicolonCount;
            } else if (c == '\t') {
                ++tabCount;
            }
        }
    }

    // Return the delimiter with the highest count
    // Default to comma if no delimiters found or all equal
    if (semicolonCount > commaCount && semicolonCount >= tabCount) {
        return ';';
    }
    if (tabCount > commaCount && tabCount > semicolonCount) {
        return '\t';
    }
    return ',';  // Default to comma
}

CellValue CSVReader::detectValue(const std::string& raw, bool autoDetect) {
    if (!autoDetect || raw.empty()) {
        return CellValue(raw);
    }

    // Check for boolean
    if (looksLikeBoolean(raw)) {
        std::string lower;
        lower.reserve(raw.size());
        for (char const c : raw) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return CellValue(lower == "true");
    }

    // Check for number
    if (looksLikeNumber(raw)) {
        double value = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), value);
        return CellValue(value);
    }

    // Default to string
    return CellValue(raw);
}

std::optional<std::string> CSVReader::parseField(std::string_view& content) {
    if (content.empty()) {
        return "";
    }

    // Check if this is a quoted field
    if (content[0] == '"') {
        // Quoted field - RFC 4180 section 2.7
        std::string result;
        content.remove_prefix(1);  // Skip opening quote

        while (!content.empty()) {
            if (content[0] == '"') {
                if (content.size() > 1 && content[1] == '"') {
                    // Escaped quote ("") -> single quote
                    result.push_back('"');
                    content.remove_prefix(2);
                } else {
                    // End of quoted field
                    content.remove_prefix(1);  // Skip closing quote
                    return result;
                }
            } else {
                result.push_back(content[0]);
                content.remove_prefix(1);
            }
        }

        // Unterminated quote
        setError(lineNum_, "Unterminated quoted field");
        return std::nullopt;
    }

    // Unquoted field - read until delimiter, newline, or end
    std::string result;
    while (!content.empty()) {
        char const c = content[0];
        if (c == options_.delimiter || c == '\r' || c == '\n') {
            break;
        }
        result.push_back(c);
        content.remove_prefix(1);
    }

    return result;
}

std::optional<std::vector<std::string>> CSVReader::parseRecord(std::string_view& content) {
    std::vector<std::string> fields;

    while (!content.empty()) {
        // Parse next field
        auto field = parseField(content);
        if (!field.has_value()) {
            return std::nullopt;  // Error already set
        }
        fields.push_back(std::move(*field));

        // Check what follows the field
        if (content.empty()) {
            break;  // End of content
        }

        char const next = content[0];
        if (next == options_.delimiter) {
            content.remove_prefix(1);  // Skip delimiter, continue parsing
        } else if (next == '\r') {
            content.remove_prefix(1);  // Skip CR
            if (!content.empty() && content[0] == '\n') {
                content.remove_prefix(1);  // Skip LF (CRLF)
            }
            break;  // End of record
        } else if (next == '\n') {
            content.remove_prefix(1);  // Skip LF
            break;                     // End of record
        }
    }

    return fields;
}

CSVReadResult CSVReader::read(const std::string& content) {
    return read(std::string_view(content));
}

CSVReadResult CSVReader::read(std::string_view content) {
    reset();
    CSVReadResult result;

    // Auto-detect delimiter if enabled
    if (options_.autoDetectDelimiter) {
        options_.delimiter = detectDelimiter(content);
    }

    // Skip BOM if present
    content = skipBOM(content);

    // Create workbook and sheet
    auto workbook = std::make_unique<Workbook>(generate_id(), "Imported");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    // Set workbook pointer early so addCell() can delegate to workbook storage
    sheet->setWorkbook(workbook.get());

    // Parse all records
    std::vector<std::vector<std::string>> records;
    while (!content.empty()) {
        lineNum_++;

        // Skip empty lines
        if (content[0] == '\r' || content[0] == '\n') {
            if (content[0] == '\r' && content.size() > 1 && content[1] == '\n') {
                content.remove_prefix(2);
            } else {
                content.remove_prefix(1);
            }
            continue;
        }

        auto record = parseRecord(content);
        if (!record.has_value()) {
            result.error = CSVReadError(lineNum_, errorMsg_);
            return result;
        }

        // Skip completely empty records
        if (record->empty() || (record->size() == 1 && (*record)[0].empty())) {
            continue;
        }

        records.push_back(std::move(*record));
    }

    if (records.empty()) {
        // Empty CSV - return empty workbook
        workbook->addSheet(std::move(sheet));
        result.workbook = std::move(workbook);
        return result;
    }

    // Determine number of columns (max across all rows)
    size_t numCols = 0;
    for (const auto& record : records) {
        numCols = std::max(numCols, record.size());
    }

    // Create columns
    std::vector<ID> colIds;
    colIds.reserve(numCols);

    for (size_t c = 0; c < numCols; c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = static_cast<uint32_t>(c);
        colIds.push_back(col->id);

        // Set column name from header if available
        if (options_.hasHeader && !records.empty() && c < records[0].size()) {
            col->name = records[0][c];
        }

        sheet->addColumn(std::move(col));
    }

    // Determine data rows (skip header if present)
    size_t const startRow = options_.hasHeader ? 1 : 0;
    size_t const numRows = records.size() - startRow;

    if (numRows == 0) {
        // Only header, no data
        workbook->addSheet(std::move(sheet));
        result.workbook = std::move(workbook);
        return result;
    }

    // Create rows
    std::vector<ID> rowIds;
    rowIds.reserve(numRows);

    for (size_t r = 0; r < numRows; r++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(r);
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Create cells
    size_t cellsLoaded = 0;
    size_t const totalEstimate = numRows * numCols;  // Upper bound estimate
    for (size_t r = 0; r < numRows; r++) {
        const auto& record = records[startRow + r];
        for (size_t c = 0; c < record.size(); c++) {
            const std::string& fieldValue = record[c];

            // Skip empty cells
            if (fieldValue.empty()) {
                continue;
            }

            auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowIds[r]);
            cell->value = detectValue(fieldValue, options_.autoDetectTypes);
            sheet->addCell(std::move(cell));
            ++cellsLoaded;

            // Call progress callback periodically
            if (options_.progressCallback && cellsLoaded % options_.progressInterval == 0) {
                options_.progressCallback(cellsLoaded, totalEstimate);
            }
        }
    }

    // Final progress callback
    if (options_.progressCallback) {
        options_.progressCallback(cellsLoaded, cellsLoaded);
    }

    workbook->addSheet(std::move(sheet));
    result.workbook = std::move(workbook);
    return result;
}

// Convenience functions
CSVReadResult readCSV(std::string_view content) {
    CSVReader reader;
    return reader.read(content);
}

CSVReadResult readCSV(std::string_view content, const CSVReadOptions& options) {
    CSVReader reader(options);
    return reader.read(content);
}

}  // namespace cells
