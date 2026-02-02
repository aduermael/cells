#ifndef APPS_CLI_OPTIONS_H_
#define APPS_CLI_OPTIONS_H_

#include <string>
#include <string_view>

namespace cells::cli {

// Supported file formats
enum class Format {
    kUnknown,
    kZcd,   // .zcd - native format (Zero-Conflict Document)
    kCsv,   // .csv - comma-separated values
    kXlsx,  // .xlsx - Excel 2007+
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
    bool show_time = false;  // --time: Show processing time
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

    // Processing options
    bool evaluate_formulas = false;  // --eval: Evaluate formulas before export

    // Info flags
    bool show_help = false;
    bool show_version = false;
    bool show_info = false;  // --info / -I: Show file information
};

// Detect format from file extension
// Returns kUnknown if extension is not recognized
inline Format detect_format(std::string_view filename) {
    // Find the last dot
    auto dot_pos = filename.rfind('.');
    if (dot_pos == std::string_view::npos) {
        return Format::kUnknown;
    }

    std::string_view ext = filename.substr(dot_pos);

    if (ext == ".zcd") return Format::kZcd;
    if (ext == ".csv") return Format::kCsv;
    if (ext == ".tsv") return Format::kCsv;  // TSV is CSV with tab delimiter
    if (ext == ".xlsx") return Format::kXlsx;

    return Format::kUnknown;
}

// Convert format enum to string for display
inline const char* format_name(Format fmt) {
    switch (fmt) {
        case Format::kZcd: return "zcd";
        case Format::kCsv: return "csv";
        case Format::kXlsx: return "xlsx";
        case Format::kUnknown: return "unknown";
    }
    return "unknown";
}

}  // namespace cells::cli

#endif  // APPS_CLI_OPTIONS_H_
