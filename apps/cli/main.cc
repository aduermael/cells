// Cells CLI - spreadsheet format converter and viewer
// Usage: cells -i <input> <output>
//        cells serve <file>

#include "converter.h"
#include "options.h"
#include "server.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "core/cells/csv_reader.h"
#include "core/cells/parser.h"
#include "core/cells/xlsx_reader.h"

namespace {

using cells::cli::ConversionResult;
using cells::cli::Converter;
using cells::cli::detect_format;
using cells::cli::Format;
using cells::cli::format_name;
using cells::cli::Options;
using cells::cli::Server;
using cells::cli::ServerOptions;

constexpr const char* kVersion = "0.0.1";

void print_usage(const char* program_name) {
    std::cerr << "cells - spreadsheet format converter and viewer\n"
              << "\n"
              << "Usage: " << program_name << " [options] -i <input> <output>\n"
              << "       " << program_name << " -I <file>     (info mode)\n"
              << "       " << program_name << " serve <file>  (web viewer)\n"
              << "\n"
              << "Convert between spreadsheet formats (.cells, .csv, .xlsx).\n"
              << "Format is auto-detected from file extension.\n"
              << "\n"
              << "Supported Formats:\n"
              << "  .cells    Native format (preserves all features)\n"
              << "  .csv      Comma-separated values (single sheet, values only)\n"
              << "  .tsv      Tab-separated values (auto-detected)\n"
              << "  .xlsx     Excel 2007+ format\n"
              << "\n"
              << "Input/Output:\n"
              << "  -i <file>           Input file (required)\n"
              << "  -f <format>         Force input format (cells, csv, xlsx)\n"
              << "  -t <format>         Force output format (cells, csv, xlsx)\n"
              << "\n"
              << "CSV Options:\n"
              << "  --delimiter <char>  CSV delimiter (default: , or tab for .tsv)\n"
              << "  --no-header         CSV has no header row\n"
              << "  --encoding <enc>    Character encoding (default: utf-8)\n"
              << "\n"
              << "XLSX Options:\n"
              << "  --sheet <name>      Export/import only this sheet\n"
              << "  --all-sheets        Export all sheets to separate files\n"
              << "\n"
              << "Output Options:\n"
              << "  -y                  Overwrite output without asking\n"
              << "  -q                  Quiet mode (no warnings)\n"
              << "  -v                  Verbose output\n"
              << "  --time              Show processing time\n"
              << "\n"
              << "Web Viewer:\n"
              << "  serve <file>        Start HTTP server to view spreadsheet\n"
              << "  --port <port>       Server port (default: 8888)\n"
              << "  --open              Open browser automatically\n"
              << "\n"
              << "Info:\n"
              << "  -I, --info          Show file information (no conversion)\n"
              << "  --version           Show version\n"
              << "  --help              Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  # Basic conversion\n"
              << "  cells -i data.csv output.cells\n"
              << "  cells -i budget.xlsx report.csv\n"
              << "  cells -i legacy.csv modern.xlsx\n"
              << "\n"
              << "  # CSV with custom delimiter\n"
              << "  cells -i data.tsv output.cells          # Auto-detects tab\n"
              << "  cells -i data.txt --delimiter ';' out.cells\n"
              << "\n"
              << "  # XLSX sheet selection\n"
              << "  cells -i workbook.xlsx --sheet 'Q1' q1.csv\n"
              << "  cells -i workbook.xlsx --all-sheets reports/\n"
              << "\n"
              << "  # File inspection\n"
              << "  cells -I data.cells                     # Show file info\n"
              << "  cells -I budget.xlsx --sheet 'Summary'  # Info for one sheet\n"
              << "\n"
              << "  # Scripting\n"
              << "  cells -i input.xlsx output.csv -q -y    # Quiet, overwrite\n"
              << "  cells -i data.csv out.xlsx --time       # Show timing\n"
              << "\n"
              << "  # Web viewer\n"
              << "  cells serve budget.xlsx                 # Start viewer on :8888\n"
              << "  cells serve data.cells --port 9000      # Custom port\n"
              << "  cells serve report.xlsx --open          # Open browser\n"
              << "\n"
              << "Feature Preservation:\n"
              << "  When converting to CSV, formulas become values and only the\n"
              << "  first sheet is exported. Warnings are shown for lost features\n"
              << "  unless -q (quiet) is specified.\n"
              << "\n"
              << "Exit Codes:\n"
              << "  0   Success\n"
              << "  1   Error (invalid arguments, file not found, parse error)\n";
}

void print_version() { std::cout << "cells " << kVersion << "\n"; }

Format parse_format(std::string_view format_str) {
    if (format_str == "cells") return Format::kCells;
    if (format_str == "csv") return Format::kCsv;
    if (format_str == "xlsx") return Format::kXlsx;
    return Format::kUnknown;
}

// Returns true on success, false on error
bool parse_args(int argc, char* argv[], Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help") {
            opts.show_help = true;
            return true;
        }
        if (arg == "--version") {
            opts.show_version = true;
            return true;
        }
        if (arg == "--info" || arg == "-I") {
            opts.show_info = true;
            continue;
        }
        if (arg == "-i" && i + 1 < argc) {
            opts.input_file = argv[++i];
            continue;
        }
        if (arg == "-f" && i + 1 < argc) {
            opts.input_format = parse_format(argv[++i]);
            if (opts.input_format == Format::kUnknown) {
                std::cerr << "Error: Unknown input format: " << argv[i] << "\n";
                return false;
            }
            continue;
        }
        if (arg == "-t" && i + 1 < argc) {
            opts.output_format = parse_format(argv[++i]);
            if (opts.output_format == Format::kUnknown) {
                std::cerr << "Error: Unknown output format: " << argv[i] << "\n";
                return false;
            }
            continue;
        }
        if (arg == "--delimiter" && i + 1 < argc) {
            opts.csv.delimiter = argv[++i];
            continue;
        }
        if (arg == "--encoding" && i + 1 < argc) {
            opts.csv.encoding = argv[++i];
            continue;
        }
        if (arg == "--sheet" && i + 1 < argc) {
            opts.xlsx.sheet_name = argv[++i];
            continue;
        }
        if (arg == "--no-header") {
            opts.csv.has_header = false;
            continue;
        }
        if (arg == "--all-sheets") {
            opts.xlsx.all_sheets = true;
            continue;
        }
        if (arg == "-y") {
            opts.output.overwrite = true;
            continue;
        }
        if (arg == "-q") {
            opts.output.quiet = true;
            continue;
        }
        if (arg == "-v") {
            opts.output.verbose = true;
            continue;
        }
        if (arg == "--time") {
            opts.output.show_time = true;
            continue;
        }
        // Positional argument (output file)
        if (!arg.empty() && arg[0] != '-') {
            if (opts.output_file.empty()) {
                opts.output_file = arg;
            } else {
                std::cerr << "Error: Unexpected argument: " << arg << "\n";
                return false;
            }
            continue;
        }
        // Unknown option
        std::cerr << "Error: Unknown option: " << arg << "\n";
        return false;
    }
    return true;
}

bool validate_options(Options& opts) {
    if (opts.show_help || opts.show_version) {
        return true;
    }

    // Helper to detect .tsv files for auto-setting tab delimiter
    auto ends_with_tsv = [](const std::string& s) {
        return s.size() >= 4 && s.substr(s.size() - 4) == ".tsv";
    };

    // Info mode: allow positional arg as input if -i wasn't used
    if (opts.show_info) {
        if (opts.input_file.empty() && !opts.output_file.empty()) {
            opts.input_file = opts.output_file;
            opts.output_file.clear();
        }
        if (opts.input_file.empty()) {
            std::cerr << "Error: Input file required (-i <file>)\n";
            return false;
        }
        // Auto-detect input format if not specified
        if (opts.input_format == Format::kUnknown) {
            opts.input_format = detect_format(opts.input_file);
        }
        // Auto-set tab delimiter for .tsv files
        if (ends_with_tsv(opts.input_file) && opts.csv.delimiter == ",") {
            opts.csv.delimiter = "\t";
        }
        return true;
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: Input file required (-i <file>)\n";
        return false;
    }

    if (opts.output_file.empty()) {
        std::cerr << "Error: Output file required\n";
        return false;
    }

    // Auto-detect input format if not specified
    if (opts.input_format == Format::kUnknown) {
        opts.input_format = detect_format(opts.input_file);
        if (opts.input_format == Format::kUnknown) {
            std::cerr << "Error: Cannot detect input format from extension. "
                      << "Use -f to specify format.\n";
            return false;
        }
    }

    // Auto-detect output format if not specified
    if (opts.output_format == Format::kUnknown) {
        opts.output_format = detect_format(opts.output_file);
        if (opts.output_format == Format::kUnknown) {
            std::cerr << "Error: Cannot detect output format from extension. "
                      << "Use -t to specify format.\n";
            return false;
        }
    }

    // Auto-set tab delimiter for .tsv files
    if (ends_with_tsv(opts.input_file) || ends_with_tsv(opts.output_file)) {
        if (opts.csv.delimiter == ",") {
            opts.csv.delimiter = "\t";
        }
    }

    return true;
}

// Read file contents into a string
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Calculate actual grid dimension by finding max position
size_t calc_grid_dimension(
    const std::unordered_map<cells::ID, std::unique_ptr<cells::Axis>, cells::IDHash>& axes) {
    if (axes.empty()) {
        return 0;
    }

    uint32_t max_position = 0;
    for (const auto& pair : axes) {
        if (pair.second->position >= max_position) {
            max_position = pair.second->position + 1;
        }
    }

    return max_position;
}

// Show file information
int show_file_info(const Options& opts) {
    // Parse the file based on format
    std::unique_ptr<cells::Workbook> workbook;

    if (opts.input_format == Format::kCells) {
        // Read file content for text-based formats
        std::string content = read_file(opts.input_file);
        if (content.empty()) {
            std::cerr << "Error: Could not read file: " << opts.input_file << "\n";
            return 1;
        }

        cells::ParseResult result = cells::parse(content);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    } else if (opts.input_format == Format::kCsv) {
        // Read file content for text-based formats
        std::string content = read_file(opts.input_file);
        if (content.empty()) {
            std::cerr << "Error: Could not read file: " << opts.input_file << "\n";
            return 1;
        }
        // Build CSV options from CLI options
        cells::CSVReadOptions csv_opts;
        if (!opts.csv.delimiter.empty()) {
            csv_opts.delimiter = opts.csv.delimiter[0];
        }
        csv_opts.hasHeader = opts.csv.has_header;
        csv_opts.autoDetectTypes = true;

        cells::CSVReadResult result = cells::readCSV(content, csv_opts);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    } else if (opts.input_format == Format::kXlsx) {
        // XLSX reader reads from file path directly
        cells::XLSXReadOptions xlsx_opts;
        xlsx_opts.readFormulas = true;      // Need formulas for counting
        xlsx_opts.readFormulaText = false;  // Don't need formula text
        xlsx_opts.readDimensions = false;   // Skip dimension reading for --info (faster)

        // Apply --sheet filter if specified
        if (!opts.xlsx.sheet_name.empty()) {
            xlsx_opts.sheetName = opts.xlsx.sheet_name;
        }

        cells::XLSXReadResult result = cells::readXLSX(opts.input_file, xlsx_opts);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    } else {
        std::cerr << "Error: Unsupported format for --info\n";
        return 1;
    }

    // Calculate statistics per sheet
    size_t total_values = 0;
    size_t total_formulas = 0;
    size_t sheet_count = workbook->sheetCount();

    for (size_t i = 0; i < sheet_count; ++i) {
        const auto& sheet = workbook->sheets[i];
        bool is_last = (i == sheet_count - 1);

        size_t formula_count = 0;
        for (const auto& [id, cell] : sheet->cells) {
            if (cell->isFormula()) {
                formula_count++;
            }
        }
        size_t value_count = sheet->cellCount() - formula_count;

        // Tree characters (Unicode box-drawing)
        const char* branch = is_last ? "└─ " : "├─ ";
        const char* indent = is_last ? "   " : "│  ";

        // Calculate actual grid dimensions from max position
        size_t num_rows = calc_grid_dimension(sheet->rows);
        size_t num_cols = calc_grid_dimension(sheet->columns);

        std::cout << branch << sheet->name << "\n";
        std::cout << indent << num_rows << (num_rows == 1 ? " row x " : " rows x ")
                  << num_cols << (num_cols == 1 ? " column" : " columns") << "\n";
        std::cout << indent << value_count
                  << (value_count == 1 ? " value, " : " values, ") << formula_count
                  << (formula_count == 1 ? " formula" : " formulas") << "\n";

        total_values += value_count;
        total_formulas += formula_count;
    }

    std::cout << sheet_count << (sheet_count == 1 ? " sheet, " : " sheets, ")
              << total_values << (total_values == 1 ? " value, " : " values, ")
              << total_formulas << (total_formulas == 1 ? " formula" : " formulas")
              << "\n";

    return 0;
}

// Run serve command
int run_serve(int argc, char* argv[]) {
    // Parse serve-specific args: serve <file> [--port N] [--open] [-v]
    std::string input_file;
    ServerOptions server_opts;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--port" && i + 1 < argc) {
            server_opts.port = static_cast<uint16_t>(std::stoi(argv[++i]));
            continue;
        }
        if (arg == "--open") {
            server_opts.open_browser = true;
            continue;
        }
        if (arg == "-v") {
            server_opts.verbose = true;
            continue;
        }
        if (arg == "--help") {
            std::cout << "Usage: cells serve <file> [--port N] [--open] [-v]\n"
                      << "\n"
                      << "Start an HTTP server to view a spreadsheet in the browser.\n"
                      << "\n"
                      << "Options:\n"
                      << "  --port <port>  Server port (default: 8888)\n"
                      << "  --open         Open browser automatically\n"
                      << "  -v             Verbose output\n";
            return 0;
        }
        // Positional argument (input file)
        if (!arg.empty() && arg[0] != '-') {
            if (input_file.empty()) {
                input_file = std::string(arg);
            } else {
                std::cerr << "Error: Unexpected argument: " << arg << "\n";
                return 1;
            }
            continue;
        }
        std::cerr << "Error: Unknown option: " << arg << "\n";
        return 1;
    }

    if (input_file.empty()) {
        std::cerr << "Error: Input file required\n";
        std::cerr << "Usage: cells serve <file> [--port N] [--open]\n";
        return 1;
    }

    // Detect format and load file
    Format format = detect_format(input_file);
    if (format == Format::kUnknown) {
        std::cerr << "Error: Cannot detect format from extension\n";
        return 1;
    }

    std::unique_ptr<cells::Workbook> workbook;

    if (format == Format::kCells) {
        std::string content = read_file(input_file);
        if (content.empty()) {
            std::cerr << "Error: Could not read file: " << input_file << "\n";
            return 1;
        }
        cells::ParseResult result = cells::parse(content);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    } else if (format == Format::kCsv) {
        std::string content = read_file(input_file);
        if (content.empty()) {
            std::cerr << "Error: Could not read file: " << input_file << "\n";
            return 1;
        }
        cells::CSVReadOptions csv_opts;
        cells::CSVReadResult result = cells::readCSV(content, csv_opts);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    } else if (format == Format::kXlsx) {
        cells::XLSXReadOptions xlsx_opts;
        xlsx_opts.readFormulas = true;
        cells::XLSXReadResult result = cells::readXLSX(input_file, xlsx_opts);
        if (!result.ok()) {
            std::cerr << "Error: " << result.error->toString() << "\n";
            return 1;
        }
        workbook = std::move(result.workbook);
    }

    if (!workbook || workbook->sheetCount() == 0) {
        std::cerr << "Error: No sheets found in file\n";
        return 1;
    }

    if (server_opts.verbose) {
        std::cout << "Loaded: " << input_file << " (" << workbook->sheetCount() << " sheet"
                  << (workbook->sheetCount() != 1 ? "s" : "") << ")\n";
    }

    // Start server
    Server server(std::move(workbook));
    return server.run(server_opts);
}

}  // namespace

int main(int argc, char* argv[]) {
    // Check for serve command first
    if (argc >= 2 && std::string_view(argv[1]) == "serve") {
        return run_serve(argc, argv);
    }

    Options opts;

    if (!parse_args(argc, argv, opts)) {
        print_usage(argv[0]);
        return 1;
    }

    if (!validate_options(opts)) {
        print_usage(argv[0]);
        return 1;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.show_version) {
        print_version();
        return 0;
    }

    // Start timing after arg parsing
    auto start_time = std::chrono::steady_clock::now();
    int result = 0;

    if (opts.show_info) {
        result = show_file_info(opts);
    } else {
        // Run conversion pipeline
        if (opts.output.verbose) {
            std::cout << "Input:  " << opts.input_file << " ("
                      << format_name(opts.input_format) << ")\n";
            std::cout << "Output: " << opts.output_file << " ("
                      << format_name(opts.output_format) << ")\n";
        }

        Converter converter(opts);
        ConversionResult conv_result = converter.convert();

        // Print warnings (unless quiet mode)
        if (!opts.output.quiet) {
            for (const auto& warning : conv_result.warnings) {
                std::cerr << "Warning: " << warning.message << "\n";
            }
        }

        if (!conv_result.ok()) {
            std::cerr << "Error: " << conv_result.error << "\n";
            result = 1;
        } else {
            if (!opts.output.quiet) {
                std::cout << "Converted: " << opts.input_file << " -> " << opts.output_file
                          << " (" << conv_result.cells_converted << " cells)\n";
            }
        }
    }

    // Show timing if requested
    if (opts.output.show_time) {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        if (duration.count() >= 1000000) {
            std::cout << "Time: "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                                 .count() /
                             1000.0
                      << "s\n";
        } else if (duration.count() >= 1000) {
            std::cout << "Time: " << duration.count() / 1000.0 << "ms\n";
        } else {
            std::cout << "Time: " << duration.count() << "us\n";
        }
    }

    return result;
}
