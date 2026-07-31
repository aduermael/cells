// Cells CLI - spreadsheet format converter, sync client, and agent sessions
// Usage: cells -i <input> <output>
//        cells sync <url>
//        cells session start <url>

#include "cli_version.h"
#include "converter.h"
#include "options.h"
#include "output_spill.h"
#include "session_args.h"
#include "session_command.h"
#include "sync_args.h"
#include "sync_command.h"

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

void print_usage(const char* program_name) {
    std::cerr
        << "cells - spreadsheet CLI (Excel CLI equivalent for .xlsx/.csv/.zcd)\n"
        << "\n"
        << "Modes:\n"
        << "  Convert:  " << program_name << " [options] -i <input> <output>\n"
        << "  Create:   " << program_name << " [options] <output>\n"
        << "  Info:     " << program_name << " -I <file>\n"
        << "  Script:   " << program_name << " -i <input> [--script <file> | -e '<code>'] [output]\n"
        << "  Sync:     " << program_name << " sync <url> | sync --server <url>\n"
        << "  Session:  " << program_name << " session start|list|stop|exec|watch|status\n"
        << "\n"
        << "Formats (auto-detected from extension; override with -f / -t):\n"
        << "  .zcd      Native format (full fidelity)\n"
        << "  .xlsx     Excel 2007+\n"
        << "  .csv      Comma-separated values (single sheet, values)\n"
        << "  .tsv      Tab-separated values (auto-detected)\n"
        << "\n"
        << "Input/Output:\n"
        << "  -i <file>           Input workbook (omit to create empty)\n"
        << "  -f <format>         Force input format (zcd, csv, xlsx)\n"
        << "  -t <format>         Force output format (zcd, csv, xlsx)\n"
        << "  -I, --info          Show file information (no conversion)\n"
        << "  --version           Show version\n"
        << "  --help              Show this help\n"
        << "\n"
        << "CSV:\n"
        << "  --delimiter <char>  Delimiter (default: , ; tab for .tsv)\n"
        << "  --no-header         No header row\n"
        << "  --encoding <enc>    Character encoding (default: utf-8)\n"
        << "\n"
        << "XLSX:\n"
        << "  --sheet <name>      Import/export only this sheet\n"
        << "  --all-sheets        Export each sheet to a separate file\n"
        << "\n"
        << "Processing:\n"
        << "  --eval              Evaluate formulas before export\n"
        << "  --script <file>     Run script from file after load (before save)\n"
        << "  -e \"<code>\"         Run inline script after load (before save)\n"
        << "  -y                  Overwrite output without asking\n"
        << "  -q                  Quiet (no warnings)\n"
        << "  -v                  Verbose\n"
        << "  --time              Show processing time\n"
        << "\n"
        << "Scripting:\n"
        << "  Scripts use the Cells API (not free-form shell). Core globals include:\n"
        << "    getCell, setCell, getSheet, addSheet, selectSheet,\n"
        << "    setColumnWidth, setRowHeight, deleteRange, fillRange,\n"
        << "    setFormat, setStyle, print\n"
        << "  Full API reference: skill SCRIPTING.md (agent skill package) or project docs.\n"
        << "  Examples: setCell(\"A1\", 100)  |  getCell(\"A1\").value  |  setCell(\"B1\", \"=A1*2\")\n"
        << "\n"
        << "Sync (one-shot blocking listen):\n"
        << "  cells sync <url>                      Join room and log operations\n"
        << "  cells sync --server <url>             Same with explicit flag\n"
        << "  cells sync --server <url> --apply <f> Apply remote ops to workbook\n"
        << "  cells sync --server <url> --send <f>  Broadcast workbook as ops\n"
        << "  cells sync <url> --ops-only           Show only operations\n"
        << "\n"
        << "Session (long-running peer for agents; preferred multi-step path; pure JSON):\n"
        << "  cells session start <url> [--wait-seconds N] [--idle-minutes N] [--name NAME]\n"
        << "  cells session list | status <id> | stop <id>\n"
        << "  cells session exec <id> -e '<code>' | --script <file> [--force]\n"
        << "  cells session export <id> <path.(zcd|xlsx|csv)>\n"
        << "  cells session watch <id> [--duration SECS]\n"
        << "  cells session --help\n"
        << "  start waits for ONLINE/SYNCING (default 15s); fails if stuck CONNECTING.\n"
        << "\n"
        << "Examples:\n"
        << "  cells -i data.csv output.zcd\n"
        << "  cells -i budget.xlsx report.csv --eval\n"
        << "  cells -i data.tsv out.zcd\n"
        << "  cells -i data.txt --delimiter ';' out.zcd\n"
        << "  cells -i workbook.xlsx --sheet 'Q1' q1.csv\n"
        << "  cells -I data.zcd\n"
        << "  cells output.zcd\n"
        << "  cells out.xlsx -e 'setCell(\"A1\", 42)'\n"
        << "  cells -i data.xlsx output.csv --script transform.luau\n"
        << "  cells -i report.csv -e 'print(getCell(\"A1\").value)'\n"
        << "  cells -i input.xlsx output.csv -q -y\n"
        << "  cells session start 'https://cells.example.com/?room=abc123'\n"
        << "  cells session exec SID -e 'setCell(\"A1\", 42)'\n"
        << "  cells session export SID /tmp/room.xlsx\n"
        << "  cells session watch SID --duration 5\n"
        << "  cells session stop SID\n"
        << "  cells sync 'https://cells.example.com/?room=abc123'\n"
        << "\n"
        << "Notes:\n"
        << "  CSV export keeps the first sheet and formula results as values; use -q to\n"
        << "  silence fidelity warnings. Exit 0 on success, 1 on error.\n"
        << "  Agents: use session start/exec/watch for multi-command collab; sync is one-shot.\n";
}

void print_version() { std::cout << "cells " << cells::cli::cli_version() << "\n"; }

Format parse_format(std::string_view format_str) {
    if (format_str == "zcd") return Format::kZcd;
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
        if (arg == "--eval") {
            opts.evaluate_formulas = true;
            continue;
        }
        if (arg == "--script" && i + 1 < argc) {
            opts.script_file = argv[++i];
            continue;
        }
        if (arg == "-e" && i + 1 < argc) {
            opts.script_inline = argv[++i];
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

    // Allow missing input file in these cases:
    // 1. Output file specified (create empty workbook)
    // 2. Script specified (script-only mode)
    bool has_script = !opts.script_file.empty() || !opts.script_inline.empty();
    bool has_output = !opts.output_file.empty();

    if (opts.input_file.empty() && !has_output && !has_script) {
        std::cerr << "Error: Input file required (-i <file>), or specify output "
                  << "file to create empty workbook, or use --script/-e for "
                  << "script-only mode\n";
        return false;
    }

    // Auto-detect input format if not specified (skip if no input file)
    if (!opts.input_file.empty() && opts.input_format == Format::kUnknown) {
        opts.input_format = detect_format(opts.input_file);
        if (opts.input_format == Format::kUnknown) {
            std::cerr << "Error: Cannot detect input format from extension. "
                      << "Use -f to specify format.\n";
            return false;
        }
    }

    // Auto-detect output format if not specified (skip if no output file)
    if (!opts.output_file.empty() && opts.output_format == Format::kUnknown) {
        opts.output_format = detect_format(opts.output_file);
        if (opts.output_format == Format::kUnknown) {
            std::cerr << "Error: Cannot detect output format from extension. "
                      << "Use -t to specify format.\n";
            return false;
        }
    }

    // Auto-set tab delimiter for .tsv files
    if ((!opts.input_file.empty() && ends_with_tsv(opts.input_file)) ||
        (!opts.output_file.empty() && ends_with_tsv(opts.output_file))) {
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

// Calculate actual grid dimension by finding max position from axis IDs
size_t calc_grid_dimension(const cells::Workbook& workbook,
                           const std::unordered_set<cells::ID, cells::IDHash>& axis_ids,
                           bool is_column) {
    if (axis_ids.empty()) {
        return 0;
    }

    uint32_t max_position = 0;
    for (const auto& id : axis_ids) {
        const cells::Axis* axis = is_column ? workbook.getColumn(id) : workbook.getRow(id);
        if (axis && axis->position >= max_position) {
            max_position = axis->position + 1;
        }
    }

    return max_position;
}

// Show file information
int show_file_info(const Options& opts) {
    // Parse the file based on format
    std::unique_ptr<cells::Workbook> workbook;

    if (opts.input_format == Format::kZcd) {
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
        for (const auto& cellId : sheet->getCellIds()) {
            cells::Cell* cell = workbook->getCell(cellId);
            if (cell && cell->isFormula()) {
                formula_count++;
            }
        }
        size_t value_count = sheet->cellCount() - formula_count;

        // Tree characters (Unicode box-drawing)
        const char* branch = is_last ? "└─ " : "├─ ";
        const char* indent = is_last ? "   " : "│  ";

        // Calculate actual grid dimensions from max position
        size_t num_rows = calc_grid_dimension(*workbook, sheet->getRowIds(), false);
        size_t num_cols = calc_grid_dimension(*workbook, sheet->getColumnIds(), true);

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

}  // namespace

int main(int argc, char* argv[]) {
    // Session subcommand (daemon peer for agents) — pure JSON on stdout
    cells::cli::SessionParseResult session_parse = cells::cli::parse_session_args(argc, argv);
    if (session_parse.is_session) {
        if (!session_parse.ok) {
            std::cout << "{\"ok\":false,\"error\":\""
                      << cells::cli::json_escape(session_parse.error) << "\"}\n";
            return 1;
        }
        cells::cli::validate_session_options(session_parse);
        if (!session_parse.ok) {
            std::cout << "{\"ok\":false,\"error\":\""
                      << cells::cli::json_escape(session_parse.error) << "\"}\n";
            return 1;
        }
        return cells::cli::run_session_command(session_parse.options);
    }

    // Sync subcommand (one-shot blocking)
    cells::cli::SyncParseResult sync_parse = cells::cli::parse_sync_args(argc, argv);
    if (sync_parse.is_sync) {
        if (!sync_parse.ok) {
            std::cerr << "Error: " << sync_parse.error << "\n";
            return 1;
        }
        if (sync_parse.options.url.empty()) {
            std::cerr << "Error: URL required for sync command\n";
            std::cerr << "Usage: " << argv[0]
                      << " sync <url> | sync --server <url> [--apply <file>] [--send <file>]\n";
            return 1;
        }
        return cells::cli::run_sync_command(sync_parse.options);
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
            if (!opts.input_file.empty()) {
                std::cout << "Input:  " << opts.input_file << " ("
                          << format_name(opts.input_format) << ")\n";
            } else {
                std::cout << "Input:  (empty workbook)\n";
            }
            if (!opts.output_file.empty()) {
                std::cout << "Output: " << opts.output_file << " ("
                          << format_name(opts.output_format) << ")\n";
            } else {
                std::cout << "Output: (script-only mode)\n";
            }
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
                // Different messages for different modes
                if (opts.output_file.empty()) {
                    // Script-only mode
                    std::cout << "Script executed successfully\n";
                } else if (opts.input_file.empty()) {
                    // Created empty workbook
                    std::cout << "Created: " << opts.output_file << " ("
                              << conv_result.cells_converted << " cells)\n";
                } else {
                    // Normal conversion
                    std::cout << "Converted: " << opts.input_file << " -> " << opts.output_file
                              << " (" << conv_result.cells_converted << " cells)\n";
                }
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
