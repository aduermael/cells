// End-to-end tests for the CLI converter

#include "converter.h"

#include <cstdio>
#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

// Helper to create a temp file and return its path
std::string createTempFile(const std::string& suffix, const std::string& content) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path temp_file = temp_dir / ("cells_test_" + std::to_string(rand()) + suffix);

    std::ofstream out(temp_file);
    out << content;
    out.close();

    return temp_file.string();
}

// Helper to read a file
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Helper to delete a temp file
void deleteTempFile(const std::string& path) {
    std::filesystem::remove(path);
}

class ConverterTest : public ::testing::Test {
protected:
    void SetUp() override { temp_files_.clear(); }

    void TearDown() override {
        for (const auto& f : temp_files_) {
            deleteTempFile(f);
        }
    }

    // Create temp file and track for cleanup
    std::string tempFile(const std::string& suffix, const std::string& content = "") {
        std::string path = createTempFile(suffix, content);
        temp_files_.push_back(path);
        return path;
    }

    // Track output file for cleanup
    void trackFile(const std::string& path) { temp_files_.push_back(path); }

private:
    std::vector<std::string> temp_files_;
};

// ============================================================
// CSV to .cells conversion tests
// ============================================================

TEST_F(ConverterTest, CsvToCells_SimpleData) {
    // CSV: header row (A,B,C) + 2 data rows (1,2,3) and (4,5,6)
    // Header row becomes column names, so we have 2 data rows × 3 columns = 6 cells
    std::string csv_content = "A,B,C\n1,2,3\n4,5,6\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 6);  // 2 data rows × 3 columns
    EXPECT_TRUE(result.warnings.empty());

    // Verify output file exists and has valid cells content
    std::string output_content = readFile(output);
    EXPECT_FALSE(output_content.empty());
    // Serializer starts with "D" (document) line, not "#cells v1"
    EXPECT_TRUE(output_content.find("D ") != std::string::npos);
}

TEST_F(ConverterTest, CsvToCells_NumericDetection) {
    std::string csv_content = "Name,Value\nTest,42\nOther,3.14\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify output contains numeric values
    std::string output_content = readFile(output);
    EXPECT_TRUE(output_content.find(" n 42") != std::string::npos);
    EXPECT_TRUE(output_content.find(" n 3.14") != std::string::npos);
}

TEST_F(ConverterTest, CsvToCells_CustomDelimiter) {
    // TSV: header row (A,B,C) + 1 data row (1,2,3)
    // Header row becomes column names, so we have 1 data row × 3 columns = 3 cells
    std::string tsv_content = "A\tB\tC\n1\t2\t3\n";
    std::string input = tempFile(".tsv", tsv_content);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.csv.delimiter = "\t";
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 3);  // 1 data row × 3 columns
}

// ============================================================
// .cells to CSV conversion tests
// ============================================================

TEST_F(ConverterTest, CellsToCsv_SimpleData) {
    std::string cells_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
C col2 1
R row1 0
R row2 1
X cell1 col1 row1 s "Hello"
X cell2 col2 row1 n 42
X cell3 col1 row2 s "World"
X cell4 col2 row2 n 100
)";

    std::string input = tempFile(".cells", cells_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCells;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 4);

    // Verify CSV output
    std::string csv_output = readFile(output);
    EXPECT_TRUE(csv_output.find("Hello") != std::string::npos);
    EXPECT_TRUE(csv_output.find("42") != std::string::npos);
}

TEST_F(ConverterTest, CellsToCsv_MultiSheetWarning) {
    std::string cells_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
R row1 0
X cell1 col1 row1 n 1
S sheet2 "Sheet2"
C col2 0
R row2 0
X cell2 col2 row2 n 2
)";

    std::string input = tempFile(".cells", cells_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCells;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Should warn about multiple sheets
    EXPECT_FALSE(result.warnings.empty());
    bool found_warning = false;
    for (const auto& w : result.warnings) {
        if (w.message.find("multiple sheets") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_warning) << "Expected warning about multiple sheets";
}

TEST_F(ConverterTest, CellsToCsv_FormulaWarning) {
    std::string cells_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
C col2 1
R row1 0
X cell1 col1 row1 n 10
X cell2 col2 row1 f "=$col1$row1*2"
)";

    std::string input = tempFile(".cells", cells_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCells;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Should warn about formulas
    bool found_warning = false;
    for (const auto& w : result.warnings) {
        if (w.message.find("ormulas") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_warning) << "Expected warning about formulas";
}

// ============================================================
// Roundtrip tests (CSV -> cells -> CSV)
// ============================================================

TEST_F(ConverterTest, Roundtrip_CsvToCellsToCsv) {
    std::string original_csv = "Name,Age,City\r\nAlice,30,NYC\r\nBob,25,LA\r\n";
    std::string csv_input = tempFile(".csv", original_csv);
    std::string cells_intermediate = tempFile(".cells", "");
    std::string csv_output = tempFile(".csv", "");
    trackFile(cells_intermediate);
    trackFile(csv_output);

    // Step 1: CSV -> .cells
    {
        Options opts;
        opts.input_file = csv_input;
        opts.output_file = cells_intermediate;
        opts.input_format = Format::kCsv;
        opts.output_format = Format::kCells;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "CSV->cells failed: " << result.error;
    }

    // Step 2: .cells -> CSV
    {
        Options opts;
        opts.input_file = cells_intermediate;
        opts.output_file = csv_output;
        opts.input_format = Format::kCells;
        opts.output_format = Format::kCsv;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "cells->CSV failed: " << result.error;
    }

    // Verify roundtrip preserves data
    std::string result_csv = readFile(csv_output);
    EXPECT_TRUE(result_csv.find("Name") != std::string::npos);
    EXPECT_TRUE(result_csv.find("Alice") != std::string::npos);
    EXPECT_TRUE(result_csv.find("30") != std::string::npos);
    EXPECT_TRUE(result_csv.find("NYC") != std::string::npos);
    EXPECT_TRUE(result_csv.find("Bob") != std::string::npos);
    EXPECT_TRUE(result_csv.find("25") != std::string::npos);
    EXPECT_TRUE(result_csv.find("LA") != std::string::npos);
}

TEST_F(ConverterTest, Roundtrip_CellsToCsvToCells) {
    std::string original_cells = R"(D doc1 "TestDoc"
S sheet1 "Data"
C col1 0
C col2 1
R row1 0
R row2 1
X cell1 col1 row1 s "Product"
X cell2 col2 row1 s "Price"
X cell3 col1 row2 s "Widget"
X cell4 col2 row2 n 99.99
)";

    std::string cells_input = tempFile(".cells", original_cells);
    std::string csv_intermediate = tempFile(".csv", "");
    std::string cells_output = tempFile(".cells", "");
    trackFile(csv_intermediate);
    trackFile(cells_output);

    // Step 1: .cells -> CSV
    {
        Options opts;
        opts.input_file = cells_input;
        opts.output_file = csv_intermediate;
        opts.input_format = Format::kCells;
        opts.output_format = Format::kCsv;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "cells->CSV failed: " << result.error;
    }

    // Step 2: CSV -> .cells
    {
        Options opts;
        opts.input_file = csv_intermediate;
        opts.output_file = cells_output;
        opts.input_format = Format::kCsv;
        opts.output_format = Format::kCells;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "CSV->cells failed: " << result.error;
    }

    // Verify roundtrip preserves data
    std::string result_cells = readFile(cells_output);
    EXPECT_TRUE(result_cells.find("Product") != std::string::npos);
    EXPECT_TRUE(result_cells.find("Price") != std::string::npos);
    EXPECT_TRUE(result_cells.find("Widget") != std::string::npos);
    // Note: 99.99 may have floating-point precision changes (99.989999999999995)
    // so we check for "99.9" as a prefix
    EXPECT_TRUE(result_cells.find("99.9") != std::string::npos);
}

// ============================================================
// Error handling tests
// ============================================================

TEST_F(ConverterTest, Error_InputFileNotFound) {
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = "/nonexistent/path/file.csv";
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("Could not open") != std::string::npos ||
                result.error.find("Could not read") != std::string::npos);
}

TEST_F(ConverterTest, Error_OverwriteProtection) {
    std::string input = tempFile(".csv", "A,B\n1,2\n");
    std::string output = tempFile(".cells", "existing content");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = false;  // Explicitly disable overwrite

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("already exists") != std::string::npos);
}

TEST_F(ConverterTest, Error_InvalidCellsFormat) {
    std::string invalid_cells = "This is not a valid cells file\n";
    std::string input = tempFile(".cells", invalid_cells);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCells;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
}

// ============================================================
// XLSX conversion tests
// ============================================================

// Helper to get path to test XLSX files
std::string getTestDataPath(const std::string& filename) {
    // Check runfiles location first (for bazel test)
    std::string runfiles_path = "core/testdata/xlsx/" + filename;
    if (std::filesystem::exists(runfiles_path)) {
        return runfiles_path;
    }

    // Try relative path from workspace root
    std::string workspace_path = "../../../core/testdata/xlsx/" + filename;
    if (std::filesystem::exists(workspace_path)) {
        return workspace_path;
    }

    // Return default and let test fail with meaningful message
    return runfiles_path;
}

TEST_F(ConverterTest, XlsxToCells_SimpleData) {
    std::string input = getTestDataPath("simple.xlsx");
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_GT(result.cells_converted, 0);

    // Verify output file exists and has valid cells content
    std::string output_content = readFile(output);
    EXPECT_FALSE(output_content.empty());
    EXPECT_TRUE(output_content.find("D ") != std::string::npos);
}

TEST_F(ConverterTest, XlsxToCells_WithFormulas) {
    std::string input = getTestDataPath("formulas.xlsx");
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify formulas are converted to UUID format
    std::string output_content = readFile(output);
    // Formula cells should have f "=..." format with $id$id references
    EXPECT_TRUE(output_content.find(" f \"=") != std::string::npos);
}

TEST_F(ConverterTest, XlsxToCells_MultiSheet) {
    std::string input = getTestDataPath("multi_sheet.xlsx");
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify multiple sheets in output
    std::string output_content = readFile(output);
    // Should have multiple S (Sheet) entries
    size_t sheet_count = 0;
    size_t pos = 0;
    while ((pos = output_content.find("\nS ", pos)) != std::string::npos) {
        sheet_count++;
        pos++;
    }
    // Also check if it starts with S
    if (output_content.substr(0, 2) == "S ") {
        sheet_count++;
    }
    EXPECT_GE(sheet_count, 2) << "Expected multiple sheets in output";
}

TEST_F(ConverterTest, CellsToXlsx_SimpleData) {
    std::string cells_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
C col2 1
R row1 0
R row2 1
X cell1 col1 row1 s "Hello"
X cell2 col2 row1 n 42
X cell3 col1 row2 s "World"
X cell4 col2 row2 n 100
)";

    std::string input = tempFile(".cells", cells_content);
    std::string output = tempFile(".xlsx", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCells;
    opts.output_format = Format::kXlsx;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 4);

    // Verify XLSX file was created (it's a binary ZIP archive)
    EXPECT_TRUE(std::filesystem::exists(output));
    EXPECT_GT(std::filesystem::file_size(output), 0);
}

TEST_F(ConverterTest, XlsxToCsv_SimpleData) {
    std::string input = getTestDataPath("simple.xlsx");
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify CSV output
    std::string csv_output = readFile(output);
    EXPECT_FALSE(csv_output.empty());
    // Check for some expected content from simple.xlsx
    EXPECT_TRUE(csv_output.find(",") != std::string::npos);  // Has delimiter
}

TEST_F(ConverterTest, CsvToXlsx_SimpleData) {
    std::string csv_content = "Name,Age,City\nAlice,30,NYC\nBob,25,LA\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".xlsx", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kXlsx;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 6);  // 2 data rows × 3 columns

    // Verify XLSX file was created
    EXPECT_TRUE(std::filesystem::exists(output));
    EXPECT_GT(std::filesystem::file_size(output), 0);
}

TEST_F(ConverterTest, Roundtrip_XlsxToCellsToXlsx) {
    std::string xlsx_input = getTestDataPath("simple.xlsx");
    std::string cells_intermediate = tempFile(".cells", "");
    std::string xlsx_output = tempFile(".xlsx", "");
    trackFile(cells_intermediate);
    trackFile(xlsx_output);

    // Step 1: XLSX -> .cells
    size_t cells_count_1 = 0;
    {
        Options opts;
        opts.input_file = xlsx_input;
        opts.output_file = cells_intermediate;
        opts.input_format = Format::kXlsx;
        opts.output_format = Format::kCells;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "XLSX->cells failed: " << result.error;
        cells_count_1 = result.cells_converted;
    }

    // Step 2: .cells -> XLSX
    {
        Options opts;
        opts.input_file = cells_intermediate;
        opts.output_file = xlsx_output;
        opts.input_format = Format::kCells;
        opts.output_format = Format::kXlsx;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "cells->XLSX failed: " << result.error;
        EXPECT_EQ(result.cells_converted, cells_count_1);
    }
}

TEST_F(ConverterTest, XlsxSheetFilter) {
    std::string input = getTestDataPath("multi_sheet.xlsx");
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCells;
    opts.xlsx.sheet_name = "Sales";  // Filter to only "Sales" sheet
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify only one sheet in output
    std::string output_content = readFile(output);
    size_t sheet_count = 0;
    size_t pos = 0;
    while ((pos = output_content.find("\nS ", pos)) != std::string::npos) {
        sheet_count++;
        pos++;
    }
    if (output_content.substr(0, 2) == "S ") {
        sheet_count++;
    }
    EXPECT_EQ(sheet_count, 1) << "Expected only one sheet (filtered)";
}

TEST_F(ConverterTest, XlsxAllSheets) {
    std::string input = getTestDataPath("multi_sheet.xlsx");

    // Create a temp directory for output
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    std::filesystem::path output_dir = temp_dir / ("cells_test_allsheets_" + std::to_string(rand()));
    std::filesystem::create_directories(output_dir);

    Options opts;
    opts.input_file = input;
    opts.output_file = output_dir.string() + "/";
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCsv;
    opts.xlsx.all_sheets = true;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify multiple CSV files were created
    bool has_sales = std::filesystem::exists(output_dir / "Sales.csv");
    bool has_expenses = std::filesystem::exists(output_dir / "Expenses.csv");
    bool has_summary = std::filesystem::exists(output_dir / "Summary.csv");

    EXPECT_TRUE(has_sales) << "Expected Sales.csv to be created";
    EXPECT_TRUE(has_expenses) << "Expected Expenses.csv to be created";
    EXPECT_TRUE(has_summary) << "Expected Summary.csv to be created";

    // Verify content
    if (has_sales) {
        std::string sales_content = readFile((output_dir / "Sales.csv").string());
        EXPECT_TRUE(sales_content.find("Product") != std::string::npos ||
                    sales_content.find("Revenue") != std::string::npos);
    }

    // Cleanup
    std::filesystem::remove_all(output_dir);
}

// ============================================================
// Edge case tests
// ============================================================

TEST_F(ConverterTest, EmptyCsv) {
    std::string empty_csv = "";
    std::string input = tempFile(".csv", empty_csv);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    // Should handle empty CSV gracefully
    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 0);
}

TEST_F(ConverterTest, CsvWithQuotedFields) {
    std::string csv_content = R"("Name","Description"
"Widget","A small, useful device"
"Gadget","Has ""quotes"" inside"
)";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify the cells file contains the properly unquoted content
    std::string cells_output = readFile(output);
    EXPECT_TRUE(cells_output.find("Widget") != std::string::npos);
    EXPECT_TRUE(cells_output.find("small, useful device") != std::string::npos);
}

TEST_F(ConverterTest, CsvWithUnicode) {
    std::string csv_content = "Name,City\nTokyo,\xE6\x9D\xB1\xE4\xBA\xAC\nParis,\xC3\x89toile\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".cells", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kCells;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
}

}  // namespace
}  // namespace cells::cli
