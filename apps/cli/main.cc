// Cells CLI - spreadsheet format converter
// Usage: cells -i <input> <output>

#include "options.h"

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using cells::cli::detect_format;
using cells::cli::Format;
using cells::cli::format_name;
using cells::cli::Options;

constexpr const char* kVersion = "0.0.1";

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
    auto ends_with_tsv = [](const std::string& s) {
        return s.size() >= 4 && s.substr(s.size() - 4) == ".tsv";
    };
    if (ends_with_tsv(opts.input_file) || ends_with_tsv(opts.output_file)) {
        if (opts.csv.delimiter == ",") {
            opts.csv.delimiter = "\t";
        }
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
    if (opts.output.verbose) {
        std::cout << "Input:  " << opts.input_file << " ("
                  << format_name(opts.input_format) << ")\n";
        std::cout << "Output: " << opts.output_file << " ("
                  << format_name(opts.output_format) << ")\n";
    }

    std::cout << "Converting: " << opts.input_file << " -> " << opts.output_file
              << "\n";

    return 0;
}
