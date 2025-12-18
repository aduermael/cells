#ifndef APPS_CLI_OPTIONS_H_
#define APPS_CLI_OPTIONS_H_

#include <string>

namespace cells::cli {

// Supported file formats
enum class Format {
    kUnknown,
    kCells,  // .cells - native format
    kCsv,    // .csv - comma-separated values
    kXlsx,   // .xlsx - Excel 2007+
};

// CSV-specific options
struct CsvOptions {
    std::string delimiter = ",";
    std::string encoding = "utf-8";
    bool has_header = true;
};

// XLSX-specific options
struct XlsxOptions {
    std::string sheet_name;  // Export only this sheet
    bool all_sheets = false; // Export all sheets to separate files
};

// Output behavior options
struct OutputOptions {
    bool overwrite = false;  // -y: Overwrite without asking
    bool quiet = false;      // -q: Suppress warnings
    bool verbose = false;    // -v: Verbose output
};

// Complete CLI options
struct Options {
    // Input/output files
    std::string input_file;
    std::string output_file;

    // Format overrides (empty = auto-detect from extension)
    Format input_format = Format::kUnknown;
    Format output_format = Format::kUnknown;

    // Format-specific options
    CsvOptions csv;
    XlsxOptions xlsx;
    OutputOptions output;

    // Info flags
    bool show_help = false;
    bool show_version = false;
};

}  // namespace cells::cli

#endif  // APPS_CLI_OPTIONS_H_
