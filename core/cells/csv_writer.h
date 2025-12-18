#ifndef CELLS_CSV_WRITER_H_
#define CELLS_CSV_WRITER_H_

#include <optional>
#include <string>

#include "core/cells/model.h"

namespace cells {

// Options for CSV writing
struct CSVWriteOptions {
    char delimiter{','};       // Field delimiter (comma, tab, semicolon, etc.)
    bool includeHeader{true};  // Include column names as first row
    bool useCRLF{true};        // Use CRLF line endings (RFC 4180 standard)

    CSVWriteOptions() = default;
};

// Error information for CSV writing
struct CSVWriteError {
    std::string message{};  // Human-readable error message

    CSVWriteError() = default;
    explicit CSVWriteError(std::string msg) : message(std::move(msg)) {}

    [[nodiscard]] std::string toString() const { return "CSV write error: " + message; }
};

// Result of writing a CSV file
struct CSVWriteResult {
    std::string output{};                  // CSV content on success
    std::optional<CSVWriteError> error{};  // Present on failure

    [[nodiscard]] bool ok() const { return !error.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// CSV writer class
// Implements RFC 4180 CSV output:
// - Fields containing delimiter, quotes, or newlines are quoted
// - Quotes within fields are escaped as ""
// - Configurable delimiter (comma, tab, semicolon, etc.)
// - Formula cells output their computed value, not the formula
class CSVWriter {
public:
    CSVWriter();
    explicit CSVWriter(const CSVWriteOptions& options);

    // Write sheet to CSV string
    // Returns CSVWriteResult with output on success, error on failure
    CSVWriteResult write(const Sheet& sheet);

    // Write workbook to CSV string (uses first sheet only)
    // Returns CSVWriteResult with output on success, error on failure
    CSVWriteResult write(const Workbook& workbook);

    // Get/set options
    [[nodiscard]] const CSVWriteOptions& options() const { return options_; }
    void setOptions(const CSVWriteOptions& options) { options_ = options; }

private:
    CSVWriteOptions options_;

    // Get ordered column IDs by walking the linked list
    [[nodiscard]] std::vector<ID> getOrderedColumns(const Sheet& sheet) const;

    // Get ordered row IDs by walking the linked list
    [[nodiscard]] std::vector<ID> getOrderedRows(const Sheet& sheet) const;

    // Format a single cell value for CSV output
    [[nodiscard]] std::string formatValue(const CellValue& value) const;

    // Escape a field according to RFC 4180 rules
    [[nodiscard]] std::string escapeField(const std::string& field) const;

    // Check if a field needs quoting
    [[nodiscard]] bool needsQuoting(const std::string& field) const;

    // Get the line ending to use
    [[nodiscard]] std::string lineEnding() const;
};

// Convenience functions for one-shot writing
CSVWriteResult writeCSV(const Sheet& sheet);
CSVWriteResult writeCSV(const Sheet& sheet, const CSVWriteOptions& options);
CSVWriteResult writeCSV(const Workbook& workbook);
CSVWriteResult writeCSV(const Workbook& workbook, const CSVWriteOptions& options);

}  // namespace cells

#endif  // CELLS_CSV_WRITER_H_
