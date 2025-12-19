#ifndef CELLS_XLSX_READER_H_
#define CELLS_XLSX_READER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/cells/model.h"

namespace cells {

// Options for XLSX parsing
struct XLSXReadOptions {
    bool readFormulas{true};     // Detect formulas (mark cells as formula type)
    bool readFormulaText{true};  // Extract formula text (slower due to shared formula handling)
    bool readStyles{true};       // Read cell styles (bold, colors, etc.)
    bool readDimensions{true};   // Read column widths and row heights
    std::string sheetName{};     // Specific sheet to read (empty = all sheets)

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
// Uses Excelize (Go) to read Excel files into our Workbook model
class XLSXReader {
public:
    XLSXReader();
    explicit XLSXReader(XLSXReadOptions options);

    // Read XLSX from file path
    // Returns XLSXReadResult with workbook on success, error on failure
    XLSXReadResult readFile(const std::string& path);

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

// Convenience function for one-shot reading
XLSXReadResult readXLSX(const std::string& path);
XLSXReadResult readXLSX(const std::string& path, const XLSXReadOptions& options);

}  // namespace cells

#endif  // CELLS_XLSX_READER_H_
