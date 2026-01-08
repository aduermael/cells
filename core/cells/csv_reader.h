// =============================================================================
// CSV Reader (CSV Import)
// =============================================================================
//
// Parses CSV files into our Workbook model following RFC 4180.
// Supports various delimiters and auto-detection of value types.
//
// Key responsibilities:
// - Parse RFC 4180 compliant CSV (with extensions)
// - Handle quoted fields with escaped quotes ("" -> ")
// - Auto-detect numeric, boolean, and date values
// - Support configurable delimiters (comma, tab, semicolon)
// - Handle UTF-8 BOM
//
// RFC 4180 features:
// - CRLF line endings (also accepts LF-only)
// - Quoted fields may contain delimiters and newlines
// - Double-quote escaping within quoted fields
//
// Extensions:
// - Optional header row detection
// - Auto-type detection (numbers, booleans)
// - Configurable field delimiter
//
// Dependencies: model.h
// Used by: bindings.cc (file import), CLI tools
//
// =============================================================================

#ifndef CELLS_CSV_READER_H_
#define CELLS_CSV_READER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/cells/model.h"

namespace cells {

// Options for CSV parsing
struct CSVReadOptions {
    char delimiter{','};         // Field delimiter (comma, tab, semicolon, etc.)
    bool hasHeader{true};        // First row is header (used for column names)
    bool autoDetectTypes{true};  // Auto-detect numeric vs string values

    CSVReadOptions() = default;
};

// Error information for CSV parsing
struct CSVReadError {
    int line{0};            // 1-based line number
    int column{0};          // 1-based column (0 if not applicable)
    std::string message{};  // Human-readable error message

    CSVReadError() = default;
    CSVReadError(int line, std::string msg) : line(line), message(std::move(msg)) {}
    CSVReadError(int line, int col, std::string msg)
        : line(line), column(col), message(std::move(msg)) {}

    [[nodiscard]] std::string toString() const;
};

// Result of parsing a CSV file
struct CSVReadResult {
    std::unique_ptr<Workbook> workbook{};  // Non-null on success
    std::optional<CSVReadError> error{};   // Present on failure

    [[nodiscard]] bool ok() const { return workbook != nullptr && !error.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// CSV reader class
// Implements RFC 4180 CSV parsing with extensions:
// - Configurable delimiter (comma, tab, semicolon, etc.)
// - UTF-8 BOM detection and handling
// - Auto-detection of numeric values
// - Quoted fields with escaped quotes ("" -> ")
class CSVReader {
public:
    CSVReader();
    explicit CSVReader(const CSVReadOptions& options);

    // Parse CSV from string content
    // Returns CSVReadResult with workbook on success, error on failure
    CSVReadResult read(const std::string& content);
    CSVReadResult read(std::string_view content);

    // Get/set options
    [[nodiscard]] const CSVReadOptions& options() const { return options_; }
    void setOptions(const CSVReadOptions& options) { options_ = options; }

private:
    CSVReadOptions options_;

    // Internal state
    int lineNum_{0};
    std::string errorMsg_;

    // Reset reader state for new parse
    void reset();

    // Set error and return false
    bool setError(const std::string& message);
    bool setError(int line, const std::string& message);

    // Skip UTF-8 BOM if present, returns updated view
    static std::string_view skipBOM(std::string_view content);

    // Parse a single CSV record (line)
    // Returns vector of field values, or nullopt on error
    std::optional<std::vector<std::string>> parseRecord(std::string_view& content);

    // Parse a single field (handles quoted and unquoted)
    // Updates content to point past the field
    // Returns field value, or nullopt on error
    std::optional<std::string> parseField(std::string_view& content);

    // Detect cell value type and create appropriate CellValue
    static CellValue detectValue(const std::string& raw, bool autoDetect);

    // Check if string looks like a number
    static bool looksLikeNumber(const std::string& s);

    // Check if string looks like a boolean
    static bool looksLikeBoolean(const std::string& s);
};

// Convenience functions for one-shot parsing
CSVReadResult readCSV(std::string_view content);
CSVReadResult readCSV(std::string_view content, const CSVReadOptions& options);

}  // namespace cells

#endif  // CELLS_CSV_READER_H_
