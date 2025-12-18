// Converter implementation for cells CLI

#include "converter.h"

#include <fstream>
#include <sstream>

#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/parser.h"
#include "core/cells/serializer.h"

namespace cells::cli {

Converter::Converter(const Options& options) : options_(options) {}

ConversionResult Converter::convert() {
    ConversionResult result;

    // Check if output file exists and we're not allowed to overwrite
    if (!options_.output.overwrite && outputFileExists()) {
        result.error =
            "Output file already exists: " + options_.output_file + " (use -y to overwrite)";
        return result;
    }

    // Read input file
    std::string error;
    auto workbook = readInput(error);
    if (!workbook) {
        result.error = error;
        return result;
    }

    // Check for feature loss and generate warnings
    checkFeatureLoss(*workbook);

    // Write output file
    if (!writeOutput(*workbook, error)) {
        result.error = error;
        return result;
    }

    // Calculate total cells
    size_t total_cells = 0;
    for (const auto& sheet : workbook->sheets) {
        total_cells += sheet->cellCount();
    }

    result.success = true;
    result.warnings = std::move(warnings_);
    result.cells_converted = total_cells;
    return result;
}

std::unique_ptr<Workbook> Converter::readInput(std::string& error_out) {
    // Read file contents
    std::string content = readFileContents(options_.input_file, error_out);
    if (content.empty() && !error_out.empty()) {
        return nullptr;
    }

    switch (options_.input_format) {
        case Format::kCells: {
            ParseResult result = parse(content);
            if (!result.ok()) {
                error_out = result.error->toString();
                return nullptr;
            }
            return std::move(result.workbook);
        }

        case Format::kCsv: {
            CSVReadOptions csv_opts;
            if (!options_.csv.delimiter.empty()) {
                csv_opts.delimiter = options_.csv.delimiter[0];
            }
            csv_opts.hasHeader = options_.csv.has_header;
            csv_opts.autoDetectTypes = true;

            CSVReadResult result = readCSV(content, csv_opts);
            if (!result.ok()) {
                error_out = result.error->toString();
                return nullptr;
            }
            return std::move(result.workbook);
        }

        case Format::kXlsx:
            error_out = "XLSX format not yet supported";
            return nullptr;

        case Format::kUnknown:
            error_out = "Unknown input format";
            return nullptr;
    }

    error_out = "Unexpected input format";
    return nullptr;
}

bool Converter::writeOutput(const Workbook& workbook, std::string& error_out) {
    std::string content;

    switch (options_.output_format) {
        case Format::kCells: {
            Serializer serializer;
            content = serializer.serialize(workbook);
            break;
        }

        case Format::kCsv: {
            CSVWriteOptions csv_opts;
            if (!options_.csv.delimiter.empty()) {
                csv_opts.delimiter = options_.csv.delimiter[0];
            }
            csv_opts.includeHeader = options_.csv.has_header;

            CSVWriteResult result = writeCSV(workbook, csv_opts);
            if (!result.ok()) {
                error_out = result.error->toString();
                return false;
            }
            content = std::move(result.output);
            break;
        }

        case Format::kXlsx:
            error_out = "XLSX format not yet supported";
            return false;

        case Format::kUnknown:
            error_out = "Unknown output format";
            return false;
    }

    return writeFileContents(options_.output_file, content, error_out);
}

std::string Converter::readFileContents(const std::string& path, std::string& error_out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error_out = "Could not open input file: " + path;
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    if (file.bad()) {
        error_out = "Error reading input file: " + path;
        return "";
    }

    return ss.str();
}

bool Converter::writeFileContents(const std::string& path, const std::string& content,
                                  std::string& error_out) {
    std::ofstream file(path);
    if (!file.is_open()) {
        error_out = "Could not open output file: " + path;
        return false;
    }

    file << content;

    if (file.bad()) {
        error_out = "Error writing output file: " + path;
        return false;
    }

    return true;
}

bool Converter::outputFileExists() const {
    std::ifstream file(options_.output_file);
    return file.good();
}

void Converter::addWarning(const std::string& message) {
    warnings_.emplace_back(message);
}

void Converter::checkFeatureLoss(const Workbook& workbook) {
    // Check for feature loss when converting to CSV
    if (options_.output_format == Format::kCsv) {
        // Multiple sheets warning
        if (workbook.sheetCount() > 1) {
            addWarning("CSV format doesn't support multiple sheets. Only \"" +
                       workbook.sheets[0]->name + "\" will be exported.");
        }

        // Formula warning - check if any cells have formulas
        bool has_formulas = false;
        for (const auto& sheet : workbook.sheets) {
            for (const auto& [id, cell] : sheet->cells) {
                if (cell->isFormula()) {
                    has_formulas = true;
                    break;
                }
            }
            if (has_formulas) break;
        }
        if (has_formulas) {
            addWarning("Formulas will be exported as computed values only.");
        }
    }
}

}  // namespace cells::cli
