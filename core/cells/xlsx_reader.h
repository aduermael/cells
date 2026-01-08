// =============================================================================
// XLSX Reader (Excel Import)
// =============================================================================
//
// Reads Microsoft Excel .xlsx files into our Workbook model.
// Uses miniz for ZIP decompression and pugixml for XML parsing.
//
// Key responsibilities:
// - Extract and parse XLSX package (ZIP containing XML files)
// - Read worksheets, shared strings, styles, and workbook metadata
// - Convert A1 references to UUID-based references
// - Support progress callbacks for large files
// - Handle shared formulas and array formulas
//
// Supported features:
// - Multiple worksheets
// - Cell values (numbers, strings, dates, booleans)
// - Formulas (converted to UUID format)
// - Column widths and row heights
// - Custom number formats
//
// Dependencies: model.h
// Used by: bindings.cc (file import), CLI tools
//
// =============================================================================

#ifndef CELLS_XLSX_READER_H_
#define CELLS_XLSX_READER_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/cells/model.h"

namespace cells {

// Progress callback for XLSX parsing
// Parameters: cellsLoaded (current count), totalEstimate (0 if unknown)
using XLSXProgressCallback = std::function<void(size_t cellsLoaded, size_t totalEstimate)>;

// Options for XLSX parsing
struct XLSXReadOptions {
    bool readFormulas{true};     // Detect formulas (mark cells as formula type)
    bool readFormulaText{true};  // Extract formula text (slower due to shared formula handling)
    bool readStyles{true};       // Read cell styles (bold, colors, etc.)
    bool readDimensions{true};   // Read column widths and row heights
    std::string sheetName{};     // Specific sheet to read (empty = all sheets)
    XLSXProgressCallback progressCallback{};  // Optional progress callback
    size_t progressInterval{500};             // Call progress callback every N cells

    XLSXReadOptions() = default;
};

// Error information for XLSX parsing
struct XLSXReadError {
    std::string message{};  // Human-readable error message
    std::string sheet{};    // Sheet name where error occurred (if applicable)
    int row{0};             // 1-based row number (0 if not applicable)
    int col{0};             // 1-based column number (0 if not applicable)

    XLSXReadError() = default;
    explicit XLSXReadError(std::string msg) : message(std::move(msg)) {}
    XLSXReadError(std::string msg, std::string sheet)
        : message(std::move(msg)), sheet(std::move(sheet)) {}
    XLSXReadError(std::string msg, std::string sheet, int row, int col)
        : message(std::move(msg)), sheet(std::move(sheet)), row(row), col(col) {}

    [[nodiscard]] std::string toString() const;
};

// Result of parsing an XLSX file
struct XLSXReadResult {
    std::unique_ptr<Workbook> workbook{};  // Non-null on success
    std::optional<XLSXReadError> error{};  // Present on failure
    std::vector<std::string> warnings{};   // Non-fatal issues encountered

    [[nodiscard]] bool ok() const { return workbook != nullptr && !error.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// XLSX reader class
// Reads Excel files into our Workbook model using miniz and pugixml
class XLSXReader {
public:
    XLSXReader();
    explicit XLSXReader(XLSXReadOptions options);

    // Read XLSX from file path
    // Returns XLSXReadResult with workbook on success, error on failure
    XLSXReadResult readFile(const std::string& path);

    // Read XLSX from memory buffer
    // Returns XLSXReadResult with workbook on success, error on failure
    XLSXReadResult readFromMemory(const char* data, size_t size);

    // Get/set options
    [[nodiscard]] const XLSXReadOptions& options() const { return options_; }
    void setOptions(const XLSXReadOptions& options) { options_ = options; }

private:
    XLSXReadOptions options_;
    std::vector<std::string> warnings_;

    // Add a warning message
    void addWarning(const std::string& msg);

    // Reset reader state for new parse
    void reset();
};

// Convenience functions for one-shot reading
XLSXReadResult readXLSX(const std::string& path);
XLSXReadResult readXLSX(const std::string& path, const XLSXReadOptions& options);
XLSXReadResult readXLSXFromMemory(const char* data, size_t size);
XLSXReadResult readXLSXFromMemory(const char* data, size_t size, const XLSXReadOptions& options);

}  // namespace cells

#endif  // CELLS_XLSX_READER_H_
