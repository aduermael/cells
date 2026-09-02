// Converter implementation for cells CLI

#include "converter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "core/cells/csv_reader.h"
#include "core/cells/csv_writer.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/parser.h"
#include "core/cells/script_dispatch.h"
#include "core/cells/serializer.h"
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

#include "output_spill.h"

namespace fs = std::filesystem;

namespace cells::cli {

Converter::Converter(const Options& options) : options_(options) {}

ConversionResult Converter::convert() {
    ConversionResult result;

    // Handle --all-sheets mode (export each sheet to separate file)
    if (options_.xlsx.all_sheets && options_.output_format == Format::kCsv) {
        return convertAllSheets();
    }

    // Check if output file exists and we're not allowed to overwrite
    if (!options_.output_file.empty() && !options_.output.overwrite && outputFileExists()) {
        result.error =
            "Output file already exists: " + options_.output_file + " (use -y to overwrite)";
        return result;
    }

    // Read input file or create empty workbook
    std::string error;
    std::unique_ptr<Workbook> workbook;
    if (options_.input_file.empty()) {
        workbook = createEmptyWorkbook();
    } else {
        workbook = readInput(error);
        if (!workbook) {
            result.error = error;
            return result;
        }
    }

    // Check for feature loss and generate warnings
    checkFeatureLoss(*workbook);

    // Evaluate formulas if requested
    if (options_.evaluate_formulas) {
        evaluateFormulas(*workbook);
    }

    // Execute script if specified
    if (!options_.script_file.empty() || !options_.script_inline.empty()) {
        if (!executeScript(*workbook, error)) {
            result.error = error;
            return result;
        }
    }

    // Write output file (skip for script-only mode)
    if (!options_.output_file.empty()) {
        if (!writeOutput(*workbook, error)) {
            result.error = error;
            return result;
        }
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

ConversionResult Converter::convertAllSheets() {
    ConversionResult result;

    // Read input file
    std::string error;
    auto workbook = readInput(error);
    if (!workbook) {
        result.error = error;
        return result;
    }

    // Check for formula warnings
    bool has_formulas = false;
    for (const auto& sheet : workbook->sheets) {
        for (const auto& cellId : sheet->getCellIds()) {
            Cell* cell = workbook->getCell(cellId);
            if (cell && cell->isFormula()) {
                has_formulas = true;
                break;
            }
        }
        if (has_formulas)
            break;
    }
    if (has_formulas) {
        addWarning("Formulas will be exported as computed values only.");
    }

    // Determine output directory
    fs::path output_path(options_.output_file);
    fs::path output_dir;

    // Check if output is a directory or ends with separator
    if (fs::is_directory(output_path) || options_.output_file.back() == '/' ||
        options_.output_file.back() == '\\') {
        output_dir = output_path;
    } else {
        // Use parent directory
        output_dir = output_path.parent_path();
        if (output_dir.empty()) {
            output_dir = ".";
        }
    }

    // Create output directory if it doesn't exist
    std::error_code ec;
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir, ec);
        if (ec) {
            result.error =
                "Could not create directory: " + output_dir.string() + " (" + ec.message() + ")";
            return result;
        }
    }

    // CSV write options
    CSVWriteOptions csv_opts;
    if (!options_.csv.delimiter.empty()) {
        csv_opts.delimiter = options_.csv.delimiter[0];
    }
    csv_opts.includeHeader = options_.csv.has_header;

    // Export each sheet to a separate file
    size_t total_cells = 0;
    for (const auto& sheet : workbook->sheets) {
        // Create a temporary single-sheet workbook for CSV export
        Workbook temp_workbook(workbook->id, workbook->name);

        // We need to duplicate the sheet for the temp workbook
        // CSV writer only writes the first sheet anyway
        auto temp_sheet = std::make_unique<Sheet>(sheet->id, sheet->name);
        temp_sheet->setWorkbook(&temp_workbook);

        // Copy columns
        for (const auto& col_id : sheet->getColumnIds()) {
            const Axis* col = workbook->getColumn(col_id);
            if (!col)
                continue;
            auto new_col = std::make_unique<Axis>(col->id, true);
            new_col->position = col->position;
            new_col->size = col->size;
            new_col->name = col->name;
            temp_sheet->addColumn(std::move(new_col));
        }

        // Copy rows
        for (const auto& row_id : sheet->getRowIds()) {
            const Axis* row = workbook->getRow(row_id);
            if (!row)
                continue;
            auto new_row = std::make_unique<Axis>(row->id, false);
            new_row->position = row->position;
            new_row->size = row->size;
            temp_sheet->addRow(std::move(new_row));
        }

        // Copy cells (shallow copy of value is fine)
        for (const auto& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook->getCell(cellId);
            if (!cell)
                continue;
            auto new_cell = std::make_unique<Cell>(cell->id, cell->colId, cell->rowId);
            new_cell->value = cell->value;
            // Don't copy formula (CSV exports computed values only)
            temp_sheet->addCell(std::move(new_cell));
        }

        temp_workbook.sheets.push_back(std::move(temp_sheet));

        // Generate output filename
        fs::path sheet_file = output_dir / (sheet->name + ".csv");

        // Check if file exists
        if (!options_.output.overwrite && fs::exists(sheet_file)) {
            result.error =
                "Output file already exists: " + sheet_file.string() + " (use -y to overwrite)";
            return result;
        }

        // Write CSV
        CSVWriteResult csv_result = writeCSV(temp_workbook, csv_opts);
        if (!csv_result.ok() && csv_result.error.has_value()) {
            result.error =
                "Error writing " + sheet_file.string() + ": " + csv_result.error->toString();
            return result;
        }

        if (!writeFileContents(sheet_file.string(), csv_result.output, error)) {
            result.error = error;
            return result;
        }

        total_cells += sheet->cellCount();
    }

    result.success = true;
    result.warnings = std::move(warnings_);
    result.cells_converted = total_cells;
    return result;
}

std::unique_ptr<Workbook> Converter::readInput(std::string& error_out) {
    switch (options_.input_format) {
        case Format::kZcd: {
            // Read file contents for text-based formats
            const std::string content = readFileContents(options_.input_file, error_out);
            if (content.empty() && !error_out.empty()) {
                return nullptr;
            }

            ParseResult result = parse(content);
            if (!result.ok() && result.error.has_value()) {
                error_out = result.error->toString();
                return nullptr;
            }
            return std::move(result.workbook);
        }

        case Format::kCsv: {
            // Read file contents for text-based formats
            const std::string content = readFileContents(options_.input_file, error_out);
            if (content.empty() && !error_out.empty()) {
                return nullptr;
            }

            CSVReadOptions csv_opts;
            if (!options_.csv.delimiter.empty()) {
                csv_opts.delimiter = options_.csv.delimiter[0];
            }
            csv_opts.hasHeader = options_.csv.has_header;
            csv_opts.autoDetectTypes = true;

            CSVReadResult result = readCSV(content, csv_opts);
            if (!result.ok() && result.error.has_value()) {
                error_out = result.error->toString();
                return nullptr;
            }
            return std::move(result.workbook);
        }

        case Format::kXlsx: {
            // XLSX reader reads directly from file path
            XLSXReadOptions xlsx_opts;
            xlsx_opts.readFormulas = true;
            xlsx_opts.readDimensions = true;

            // Apply --sheet filter if specified
            if (!options_.xlsx.sheet_name.empty()) {
                xlsx_opts.sheetName = options_.xlsx.sheet_name;
            }

            XLSXReadResult result = readXLSX(options_.input_file, xlsx_opts);

            // Convert warnings
            for (const auto& warning : result.warnings) {
                addWarning(warning);
            }

            if (!result.ok() && result.error.has_value()) {
                error_out = result.error->toString();
                return nullptr;
            }

            convertFormulasToUuid(*result.workbook);

            return std::move(result.workbook);
        }

        case Format::kUnknown:
            error_out = "Unknown input format";
            return nullptr;
    }

    error_out = "Unexpected input format";
    return nullptr;
}

std::unique_ptr<Workbook> Converter::createEmptyWorkbook() {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());
    workbook->addSheet(std::move(sheet));
    return workbook;
}

bool Converter::writeOutput(const Workbook& workbook, std::string& error_out) {
    switch (options_.output_format) {
        case Format::kZcd: {
            const Serializer serializer;
            std::string content = serializer.serialize(workbook);
            return writeFileContents(options_.output_file, content, error_out);
        }

        case Format::kCsv: {
            CSVWriteOptions csv_opts;
            if (!options_.csv.delimiter.empty()) {
                csv_opts.delimiter = options_.csv.delimiter[0];
            }
            csv_opts.includeHeader = options_.csv.has_header;

            CSVWriteResult result = writeCSV(workbook, csv_opts);
            if (!result.ok() && result.error.has_value()) {
                error_out = result.error->toString();
                return false;
            }
            return writeFileContents(options_.output_file, result.output, error_out);
        }

        case Format::kXlsx: {
            // XLSX writer writes directly to file path
            XLSXWriteOptions xlsx_opts;
            xlsx_opts.writeFormulas = true;
            xlsx_opts.writeDimensions = true;

            XLSXWriteResult result = writeXLSX(workbook, options_.output_file, xlsx_opts);

            // Convert warnings
            for (const auto& warning : result.warnings) {
                addWarning(warning);
            }

            if (!result.ok() && result.error.has_value()) {
                error_out = result.error->toString();
                return false;
            }
            return true;
        }

        case Format::kUnknown:
            error_out = "Unknown output format";
            return false;
    }

    error_out = "Unexpected output format";
    return false;
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
            for (const auto& cellId : sheet->getCellIds()) {
                const Cell* cell = workbook.getCell(cellId);
                if (cell && cell->isFormula()) {
                    has_formulas = true;
                    break;
                }
            }
            if (has_formulas)
                break;
        }
        if (has_formulas) {
            addWarning("Formulas will be exported as computed values only.");
        }
    }

    // Check for feature loss when converting to XLSX
    if (options_.output_format == Format::kXlsx) {
        // Check for Excel row/column limits
        for (const auto& sheet : workbook.sheets) {
            size_t row_count = sheet->rowCount();
            size_t col_count = sheet->columnCount();

            if (row_count > 1048576) {
                addWarning("Sheet \"" + sheet->name + "\" has " + std::to_string(row_count) +
                           " rows (Excel limit: 1,048,576). Data may be truncated.");
            }
            if (col_count > 16384) {
                addWarning("Sheet \"" + sheet->name + "\" has " + std::to_string(col_count) +
                           " columns (Excel limit: 16,384). Data may be truncated.");
            }
        }
    }
}

void Converter::convertFormulasToUuid(Workbook& workbook) {
    resolveWorkbookFormulas(workbook);
}

void Converter::evaluateFormulas(Workbook& workbook) {
    // Evaluate all formula cells in each sheet using the recalculation engine
    for (auto& sheet : workbook.sheets) {
        for (const auto& cellId : sheet->getCellIds()) {
            Cell* cell = workbook.getCell(cellId);
            if (cell && cell->isFormula()) {
                cells::evaluateCell(sheet.get(), cell);
            }
        }
    }
}

bool Converter::executeScript(Workbook& workbook, std::string& error_out) {
    // Determine script content
    std::string script;

    if (!options_.script_file.empty()) {
        // Read script from file
        script = readFileContents(options_.script_file, error_out);
        if (script.empty() && !error_out.empty()) {
            return false;
        }
    } else if (!options_.script_inline.empty()) {
        // Use inline script
        script = options_.script_inline;
    } else {
        // No script to execute
        return true;
    }

    Sheet* sheet = workbook.sheets.empty() ? nullptr : workbook.sheets[0].get();
    const ScriptKind kind = detectScriptKind(options_.script_file, script);
    const ScriptResult result = executeUserScript(workbook, sheet, script, kind);

    // Print script output if any (unless quiet mode). Large payloads spill to
    // /tmp with a JSON pointer so agents do not ingest multi-megabyte stdout.
    if (!options_.output.quiet && !result.output.empty()) {
        SpillResult spill = maybe_spill_output(result.output);
        std::cout << spill.stdout_text;
        if (!spill.stdout_text.empty() && spill.stdout_text.back() != '\n') {
            std::cout << "\n";
        }
    }

    if (!result.success) {
        error_out = "Script error: " + result.error;
        return false;
    }

    return true;
}

std::unique_ptr<Workbook> createEmptyWorkbook() {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());
    workbook->addSheet(std::move(sheet));
    return workbook;
}

std::unique_ptr<Workbook> loadWorkbookFromFile(const std::string& path, std::string& error_out) {
    if (path.empty()) {
        return createEmptyWorkbook();
    }
    Options options;
    options.input_file = path;
    options.input_format = detect_format(path);
    if (options.input_format == Format::kUnknown) {
        error_out = "Cannot detect input format from extension: " + path;
        return nullptr;
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".tsv" &&
        options.csv.delimiter == ",") {
        options.csv.delimiter = "\t";
    }
    Converter converter(options);
    return converter.readInput(error_out);
}

bool saveWorkbookToFile(const Workbook& workbook, const std::string& path, std::string& error_out) {
    if (path.empty()) {
        error_out = "output path required";
        return false;
    }
    Options options;
    options.output_file = path;
    options.output_format = detect_format(path);
    options.output.overwrite = true;
    if (options.output_format == Format::kUnknown) {
        error_out = "Cannot detect output format from extension: " + path;
        return false;
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".tsv" &&
        options.csv.delimiter == ",") {
        options.csv.delimiter = "\t";
    }
    Converter converter(options);
    return converter.writeOutput(workbook, error_out);
}

}  // namespace cells::cli
