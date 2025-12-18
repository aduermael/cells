#ifndef CELLS_XLSX_WRITER_H_
#define CELLS_XLSX_WRITER_H_

#include <optional>
#include <string>
#include <vector>

#include "core/cells/model.h"

namespace cells {

// Options for XLSX writing
struct XLSXWriteOptions {
    bool writeFormulas{true};    // Write formulas (vs computed values only)
    bool writeDimensions{true};  // Write column widths and row heights

    XLSXWriteOptions() = default;
};

// Error information for XLSX writing
struct XLSXWriteError {
    std::string message{};  // Human-readable error message
    std::string sheet{};    // Sheet name where error occurred (if applicable)

    XLSXWriteError() = default;
    explicit XLSXWriteError(std::string msg) : message(std::move(msg)) {}
    XLSXWriteError(std::string msg, std::string sheet)
        : message(std::move(msg)), sheet(std::move(sheet)) {}

    [[nodiscard]] std::string toString() const;
};

// Result of writing an XLSX file
struct XLSXWriteResult {
    std::optional<XLSXWriteError> error{};  // Present on failure
    std::vector<std::string> warnings{};    // Non-fatal issues encountered
    size_t cellsWritten{0};                 // Number of cells written

    [[nodiscard]] bool ok() const { return !error.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// XLSX writer class
// Uses OpenXLSX to write our Workbook model to Excel files
class XLSXWriter {
public:
    XLSXWriter();
    explicit XLSXWriter(XLSXWriteOptions options);

    // Write workbook to XLSX file
    // Returns XLSXWriteResult with success status
    XLSXWriteResult writeFile(const Workbook& workbook, const std::string& path);

    // Get/set options
    [[nodiscard]] const XLSXWriteOptions& options() const { return options_; }
    void setOptions(const XLSXWriteOptions& options) { options_ = options; }

private:
    XLSXWriteOptions options_;
    std::vector<std::string> warnings_;

    // Add a warning message
    void addWarning(const std::string& msg);

    // Reset writer state for new write operation
    void reset();

    // Get ordered column IDs by position
    [[nodiscard]] std::vector<ID> getOrderedColumns(const Sheet& sheet) const;

    // Get ordered row IDs by position
    [[nodiscard]] std::vector<ID> getOrderedRows(const Sheet& sheet) const;

    // Convert column index (0-based) to Excel column letter (A, B, ..., Z, AA, ...)
    [[nodiscard]] static std::string columnIndexToLetter(size_t index);

    // Convert our formula (UUID refs) to A1 notation
    // For now, just strips the formula - full conversion will be in Phase 8
    [[nodiscard]] std::string convertFormula(const std::string& formula, const Sheet& sheet,
                                             const std::vector<ID>& columnIds,
                                             const std::vector<ID>& rowIds) const;
};

// Convenience functions for one-shot writing
XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path);
XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path,
                          const XLSXWriteOptions& options);

}  // namespace cells

#endif  // CELLS_XLSX_WRITER_H_
