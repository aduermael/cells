#ifndef APPS_CLI_CONVERTER_H_
#define APPS_CLI_CONVERTER_H_

#include <memory>
#include <string>
#include <vector>

#include "core/cells/model.h"

#include "options.h"

namespace cells::cli {

// Warning generated during conversion
struct ConversionWarning {
    std::string message{};

    explicit ConversionWarning(std::string msg) : message(std::move(msg)) {}
};

// Result of a conversion operation
struct ConversionResult {
    bool success{false};
    std::string error{};  // Error message if success is false
    std::vector<ConversionWarning> warnings{};
    size_t cells_converted{0};  // Number of cells in the converted file

    [[nodiscard]] bool ok() const { return success; }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// Converter class: handles the read -> transform -> write pipeline
class Converter {
public:
    explicit Converter(const Options& options);

    // Run the conversion pipeline
    // Returns ConversionResult with success/error status and warnings
    ConversionResult convert();

private:
    const Options& options_;
    std::vector<ConversionWarning> warnings_;

    // Read input file into a Workbook
    // Returns nullptr on error (sets error_out)
    std::unique_ptr<Workbook> readInput(std::string& error_out);

    // Write workbook to output file
    // Returns false on error (sets error_out)
    bool writeOutput(const Workbook& workbook, std::string& error_out);

    // Read file contents from disk
    std::string readFileContents(const std::string& path, std::string& error_out);

    // Write contents to disk
    bool writeFileContents(const std::string& path, const std::string& content,
                           std::string& error_out);

    // Check if output file exists
    [[nodiscard]] bool outputFileExists() const;

    // Add a warning
    void addWarning(const std::string& message);

    // Generate warnings for feature loss during conversion
    void checkFeatureLoss(const Workbook& workbook);

    // Convert A1 formulas to UUID references (for XLSX import)
    void convertFormulasToUuid(Workbook& workbook);

    // Evaluate all formulas in the workbook and store computed values
    void evaluateFormulas(Workbook& workbook);

    // Execute a Luau script on the workbook
    // Returns false on error (sets error_out)
    bool executeScript(Workbook& workbook, std::string& error_out);

    // Handle --all-sheets mode (export each sheet to separate CSV)
    ConversionResult convertAllSheets();
};

}  // namespace cells::cli

#endif  // APPS_CLI_CONVERTER_H_
