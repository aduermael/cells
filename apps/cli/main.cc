// Cells CLI - spreadsheet format converter
// Usage: cells -i <input> <output>

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr const char* kVersion = "0.0.1";

struct Options {
    std::string input_file;
    std::string output_file;
    std::string input_format;   // -f: force input format
    std::string output_format;  // -t: force output format
    std::string delimiter = ",";
    std::string encoding = "utf-8";
    std::string sheet_name;
    bool no_header = false;
    bool all_sheets = false;
    bool overwrite = false;  // -y
    bool quiet = false;      // -q
    bool verbose = false;    // -v
    bool show_help = false;
    bool show_version = false;
};

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options] -i <input> <output>\n"
              << "\n"
              << "Input/Output:\n"
              << "  -i <file>           Input file (required)\n"
              << "  -f <format>         Force input format (cells, csv, xlsx)\n"
              << "  -t <format>         Force output format (cells, csv, xlsx)\n"
              << "\n"
              << "CSV Options:\n"
              << "  --delimiter <char>  CSV delimiter (default: ,)\n"
              << "  --no-header         CSV has no header row\n"
              << "  --encoding <enc>    Character encoding (default: utf-8)\n"
              << "\n"
              << "XLSX Options:\n"
              << "  --sheet <name>      Export only this sheet\n"
              << "  --all-sheets        Export all sheets\n"
              << "\n"
              << "Output Options:\n"
              << "  -y                  Overwrite output without asking\n"
              << "  -q                  Quiet mode (no warnings)\n"
              << "  -v                  Verbose output\n"
              << "\n"
              << "Info:\n"
              << "  --version           Show version\n"
              << "  --help              Show this help\n";
}

void print_version() { std::cout << "cells " << kVersion << "\n"; }

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
        if (arg == "-i" && i + 1 < argc) {
            opts.input_file = argv[++i];
            continue;
        }
        if (arg == "-f" && i + 1 < argc) {
            opts.input_format = argv[++i];
            continue;
        }
        if (arg == "-t" && i + 1 < argc) {
            opts.output_format = argv[++i];
            continue;
        }
        if (arg == "--delimiter" && i + 1 < argc) {
            opts.delimiter = argv[++i];
            continue;
        }
        if (arg == "--encoding" && i + 1 < argc) {
            opts.encoding = argv[++i];
            continue;
        }
        if (arg == "--sheet" && i + 1 < argc) {
            opts.sheet_name = argv[++i];
            continue;
        }
        if (arg == "--no-header") {
            opts.no_header = true;
            continue;
        }
        if (arg == "--all-sheets") {
            opts.all_sheets = true;
            continue;
        }
        if (arg == "-y") {
            opts.overwrite = true;
            continue;
        }
        if (arg == "-q") {
            opts.quiet = true;
            continue;
        }
        if (arg == "-v") {
            opts.verbose = true;
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

bool validate_options(const Options& opts) {
    if (opts.show_help || opts.show_version) {
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
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
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

    // TODO: Implement conversion pipeline
    std::cout << "Converting: " << opts.input_file << " -> " << opts.output_file
              << "\n";

    return 0;
}
