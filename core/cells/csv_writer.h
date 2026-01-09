// =============================================================================
// CSV Writer (CSV Export)
// =============================================================================
//
// Writes Sheet or Workbook data to CSV format following RFC 4180.
// Formula cells output computed values, not formulas.
//
// Key responsibilities:
// - Generate RFC 4180 compliant CSV output
// - Quote fields containing delimiters, quotes, or newlines
// - Escape quotes as double-quotes within quoted fields
// - Support configurable delimiters and line endings
// - Optionally include column names as header row
//
// Output rules (RFC 4180):
// - CRLF line endings (configurable)
// - Fields with special chars are double-quoted
// - Internal double-quotes escaped as ""
// - Last record may omit trailing CRLF
//
// Limitations:
// - CSV export only includes the first sheet (CSV has no multi-sheet concept).
//   Use XLSX for multi-sheet export.
// - Cell styles are NOT exported (bold, italic, colors, alignment, etc.).
//   CSV is a plain-text format with no style/formatting support.
//   Use XLSX format to preserve cell styles.
// - Number formats are NOT preserved. Numbers are exported as raw values.
//   Dates appear as serial numbers unless pre-formatted to strings.
//
// Dependencies: model.h
// Used by: bindings.cc (file export), CLI tools
//
// =============================================================================

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
    std::vector<std::string> warnings{};   // Non-fatal warnings (e.g., styles lost)
    bool stylesLost{false};                // True if styled cells were exported

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
