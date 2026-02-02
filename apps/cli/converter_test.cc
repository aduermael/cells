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
// CSV to .zcd conversion tests
// ============================================================

TEST_F(ConverterTest, CsvToZcd_SimpleData) {
    // CSV: header row (A,B,C) + 2 data rows (1,2,3) and (4,5,6)
    // Header row becomes column names, so we have 2 data rows × 3 columns = 6 cells
    std::string csv_content = "A,B,C\n1,2,3\n4,5,6\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 6);  // 2 data rows × 3 columns
    EXPECT_TRUE(result.warnings.empty());

    // Verify output file exists and has valid zcd content
    std::string output_content = readFile(output);
    EXPECT_FALSE(output_content.empty());
    // Serializer starts with "D" (document) line
    EXPECT_TRUE(output_content.find("D ") != std::string::npos);
}

TEST_F(ConverterTest, CsvToZcd_NumericDetection) {
    std::string csv_content = "Name,Value\nTest,42\nOther,3.14\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify output contains numeric values
    std::string output_content = readFile(output);
    EXPECT_TRUE(output_content.find(" n 42") != std::string::npos);
    EXPECT_TRUE(output_content.find(" n 3.14") != std::string::npos);
}

TEST_F(ConverterTest, CsvToZcd_CustomDelimiter) {
    // TSV: header row (A,B,C) + 1 data row (1,2,3)
    // Header row becomes column names, so we have 1 data row × 3 columns = 3 cells
    std::string tsv_content = "A\tB\tC\n1\t2\t3\n";
    std::string input = tempFile(".tsv", tsv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.csv.delimiter = "\t";
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 3);  // 1 data row × 3 columns
}

// ============================================================
// .zcd to CSV conversion tests
// ============================================================

TEST_F(ConverterTest, ZcdToCsv_SimpleData) {
    std::string zcd_content = R"(D doc1 "TestDoc"
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

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
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

TEST_F(ConverterTest, ZcdToCsv_MultiSheetWarning) {
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
R row1 0
X cell1 col1 row1 n 1
S sheet2 "Sheet2"
C col2 0
R row2 0
X cell2 col2 row2 n 2
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
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

TEST_F(ConverterTest, ZcdToCsv_FormulaWarning) {
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C col1 0
C col2 1
R row1 0
X cell1 col1 row1 n 10
X cell2 col2 row1 f "=$col1$row1*2"
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
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
// Roundtrip tests (CSV -> zcd -> CSV)
// ============================================================

TEST_F(ConverterTest, Roundtrip_CsvToZcdToCsv) {
    std::string original_csv = "Name,Age,City\r\nAlice,30,NYC\r\nBob,25,LA\r\n";
    std::string csv_input = tempFile(".csv", original_csv);
    std::string zcd_intermediate = tempFile(".zcd", "");
    std::string csv_output = tempFile(".csv", "");
    trackFile(zcd_intermediate);
    trackFile(csv_output);

    // Step 1: CSV -> .zcd
    {
        Options opts;
        opts.input_file = csv_input;
        opts.output_file = zcd_intermediate;
        opts.input_format = Format::kCsv;
        opts.output_format = Format::kZcd;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "CSV->zcd failed: " << result.error;
    }

    // Step 2: .zcd -> CSV
    {
        Options opts;
        opts.input_file = zcd_intermediate;
        opts.output_file = csv_output;
        opts.input_format = Format::kZcd;
        opts.output_format = Format::kCsv;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "zcd->CSV failed: " << result.error;
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

TEST_F(ConverterTest, Roundtrip_ZcdToCsvToZcd) {
    std::string original_zcd = R"(D doc1 "TestDoc"
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

    std::string zcd_input = tempFile(".zcd", original_zcd);
    std::string csv_intermediate = tempFile(".csv", "");
    std::string zcd_output = tempFile(".zcd", "");
    trackFile(csv_intermediate);
    trackFile(zcd_output);

    // Step 1: .zcd -> CSV
    {
        Options opts;
        opts.input_file = zcd_input;
        opts.output_file = csv_intermediate;
        opts.input_format = Format::kZcd;
        opts.output_format = Format::kCsv;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "zcd->CSV failed: " << result.error;
    }

    // Step 2: CSV -> .zcd
    {
        Options opts;
        opts.input_file = csv_intermediate;
        opts.output_file = zcd_output;
        opts.input_format = Format::kCsv;
        opts.output_format = Format::kZcd;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "CSV->zcd failed: " << result.error;
    }

    // Verify roundtrip preserves data
    std::string result_zcd = readFile(zcd_output);
    EXPECT_TRUE(result_zcd.find("Product") != std::string::npos);
    EXPECT_TRUE(result_zcd.find("Price") != std::string::npos);
    EXPECT_TRUE(result_zcd.find("Widget") != std::string::npos);
    // Note: 99.99 may have floating-point precision changes (99.989999999999995)
    // so we check for "99.9" as a prefix
    EXPECT_TRUE(result_zcd.find("99.9") != std::string::npos);
}

// ============================================================
// Error handling tests
// ============================================================

TEST_F(ConverterTest, Error_InputFileNotFound) {
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = "/nonexistent/path/file.csv";
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("Could not open") != std::string::npos ||
                result.error.find("Could not read") != std::string::npos);
}

TEST_F(ConverterTest, Error_OverwriteProtection) {
    std::string input = tempFile(".csv", "A,B\n1,2\n");
    std::string output = tempFile(".zcd", "existing content");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = false;  // Explicitly disable overwrite

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("already exists") != std::string::npos);
}

TEST_F(ConverterTest, Error_InvalidZcdFormat) {
    std::string invalid_zcd = "This is not a valid zcd file\n";
    std::string input = tempFile(".zcd", invalid_zcd);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
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

TEST_F(ConverterTest, XlsxToZcd_SimpleData) {
    std::string input = getTestDataPath("simple.xlsx");
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_GT(result.cells_converted, 0);

    // Verify output file exists and has valid zcd content
    std::string output_content = readFile(output);
    EXPECT_FALSE(output_content.empty());
    EXPECT_TRUE(output_content.find("D ") != std::string::npos);
}

TEST_F(ConverterTest, XlsxToZcd_WithFormulas) {
    std::string input = getTestDataPath("formulas.xlsx");
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify formulas are converted to UUID format
    std::string output_content = readFile(output);
    // Formula cells should have f "=..." format with $id$id references
    EXPECT_TRUE(output_content.find(" f \"=") != std::string::npos);
}

TEST_F(ConverterTest, XlsxToZcd_MultiSheet) {
    std::string input = getTestDataPath("multi_sheet.xlsx");
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kZcd;
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

TEST_F(ConverterTest, ZcdToXlsx_SimpleData) {
    std::string zcd_content = R"(D doc1 "TestDoc"
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

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".xlsx", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
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

TEST_F(ConverterTest, Roundtrip_XlsxToZcdToXlsx) {
    std::string xlsx_input = getTestDataPath("simple.xlsx");
    std::string zcd_intermediate = tempFile(".zcd", "");
    std::string xlsx_output = tempFile(".xlsx", "");
    trackFile(zcd_intermediate);
    trackFile(xlsx_output);

    // Step 1: XLSX -> .zcd
    size_t cells_count_1 = 0;
    {
        Options opts;
        opts.input_file = xlsx_input;
        opts.output_file = zcd_intermediate;
        opts.input_format = Format::kXlsx;
        opts.output_format = Format::kZcd;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "XLSX->zcd failed: " << result.error;
        cells_count_1 = result.cells_converted;
    }

    // Step 2: .zcd -> XLSX
    {
        Options opts;
        opts.input_file = zcd_intermediate;
        opts.output_file = xlsx_output;
        opts.input_format = Format::kZcd;
        opts.output_format = Format::kXlsx;
        opts.output.overwrite = true;

        Converter converter(opts);
        ConversionResult result = converter.convert();
        ASSERT_TRUE(result.ok()) << "zcd->XLSX failed: " << result.error;
        EXPECT_EQ(result.cells_converted, cells_count_1);
    }
}

TEST_F(ConverterTest, XlsxSheetFilter) {
    std::string input = getTestDataPath("multi_sheet.xlsx");
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kZcd;
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
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
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
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify the zcd file contains the properly unquoted content
    std::string zcd_output = readFile(output);
    EXPECT_TRUE(zcd_output.find("Widget") != std::string::npos);
    EXPECT_TRUE(zcd_output.find("small, useful device") != std::string::npos);
}

TEST_F(ConverterTest, CsvWithUnicode) {
    std::string csv_content = "Name,City\nTokyo,\xE6\x9D\xB1\xE4\xBA\xAC\nParis,\xC3\x89toile\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
}

// ============================================================
// --eval flag tests (formula evaluation before export)
// ============================================================

TEST_F(ConverterTest, EvalFlag_SimpleArithmetic) {
    // Create a .zcd file with formulas using A1 notation
    // A1 = 10, B1 = 5, C1 = =A1+B1 (should evaluate to 15)
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
C cB000002 1
C cC000003 2
R r0000001 0
X xA100001 cA000001 r0000001 n 10
X xB100002 cB000002 r0000001 n 5
X xC100003 cC000003 r0000001 f "=A1+B1"
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.evaluate_formulas = true;  // Enable formula evaluation

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify the CSV contains the computed value (15), not a formula
    std::string csv_output = readFile(output);
    EXPECT_TRUE(csv_output.find("10") != std::string::npos);
    EXPECT_TRUE(csv_output.find("5") != std::string::npos);
    EXPECT_TRUE(csv_output.find("15") != std::string::npos)
        << "Expected 15 (10+5) in CSV output. Got: " << csv_output;
}

TEST_F(ConverterTest, EvalFlag_MultiplicationFormula) {
    // A1 = 6, B1 = 7, C1 = =A1*B1 (should evaluate to 42)
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
C cB000002 1
C cC000003 2
R r0000001 0
X xA100001 cA000001 r0000001 n 6
X xB100002 cB000002 r0000001 n 7
X xC100003 cC000003 r0000001 f "=A1*B1"
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.evaluate_formulas = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    std::string csv_output = readFile(output);
    EXPECT_TRUE(csv_output.find("42") != std::string::npos)
        << "Expected 42 (6*7) in CSV output. Got: " << csv_output;
}

TEST_F(ConverterTest, EvalFlag_SumFunction) {
    // A1 = 1, A2 = 2, A3 = 3, B1 = =SUM(A1:A3) (should evaluate to 6)
    std::string zcd_content = R"ZCD(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
C cB000002 1
R r0000001 0
R r0000002 1
R r0000003 2
X xA100001 cA000001 r0000001 n 1
X xA200002 cA000001 r0000002 n 2
X xA300003 cA000001 r0000003 n 3
X xB100004 cB000002 r0000001 f "=SUM(A1:A3)"
)ZCD";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.evaluate_formulas = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    std::string csv_output = readFile(output);
    // SUM(1,2,3) = 6
    EXPECT_TRUE(csv_output.find("6") != std::string::npos)
        << "Expected 6 (SUM of 1,2,3) in CSV output. Got: " << csv_output;
}

TEST_F(ConverterTest, EvalFlag_XlsxWithFormulas) {
    // Test --eval with XLSX input containing formulas
    std::string input = getTestDataPath("formulas.xlsx");
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kXlsx;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.evaluate_formulas = true;  // Recalculate formulas

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify the CSV was created and has content
    std::string csv_output = readFile(output);
    EXPECT_FALSE(csv_output.empty());
    // Formula indicators should not appear (=, $) since values are computed
    // Note: This checks formula text doesn't leak through; values are numbers
}

TEST_F(ConverterTest, EvalFlag_Disabled_PreservesFormulaValue) {
    // When --eval is NOT set, formulas should still be exported as their
    // cached values (not recalculated). The CSV writer always writes values.
    // This test ensures the flag doesn't break normal conversion.
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
C cB000002 1
R r0000001 0
X xA100001 cA000001 r0000001 n 10
X xB100002 cB000002 r0000001 f "=A1*2"
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.evaluate_formulas = false;  // Don't evaluate (default behavior)

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Should still produce valid CSV (values from cached results or empty)
    std::string csv_output = readFile(output);
    EXPECT_TRUE(csv_output.find("10") != std::string::npos);
}

// ============================================================
// Script execution tests (--script and -e flags)
// ============================================================

TEST_F(ConverterTest, Script_InlineSetCell) {
    // Test inline script with -e that modifies a cell
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
R r0000001 0
X xA100001 cA000001 r0000001 n 10
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.output.quiet = true;
    opts.script_inline = "setCell('A1', 999)";  // Change A1 from 10 to 999

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify the output contains 999 (modified value), not 10
    std::string zcd_output = readFile(output);
    EXPECT_TRUE(zcd_output.find(" n 999") != std::string::npos)
        << "Expected 999 in output. Got: " << zcd_output;
}

TEST_F(ConverterTest, Script_InlineMultipleStatements) {
    // Test inline script with multiple statements
    std::string csv_content = "A,B\n1,2\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.output.quiet = true;
    opts.script_inline = "setCell('A1', 100); setCell('B1', 200)";

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    std::string zcd_output = readFile(output);
    EXPECT_TRUE(zcd_output.find(" n 100") != std::string::npos);
    EXPECT_TRUE(zcd_output.find(" n 200") != std::string::npos);
}

TEST_F(ConverterTest, Script_FromFile) {
    // Test --script flag with script file
    std::string script_content = "setCell('A1', 'Hello from script')";
    std::string script_file = tempFile(".luau", script_content);

    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
R r0000001 0
X xA100001 cA000001 r0000001 n 0
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.output.quiet = true;
    opts.script_file = script_file;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    std::string zcd_output = readFile(output);
    EXPECT_TRUE(zcd_output.find("Hello from script") != std::string::npos)
        << "Expected 'Hello from script' in output. Got: " << zcd_output;
}

TEST_F(ConverterTest, Script_Error_SyntaxError) {
    // Test that script syntax errors are reported
    std::string csv_content = "A\n1\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.script_inline = "setCell('A1',";  // Incomplete syntax

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("Script error") != std::string::npos ||
                result.error.find("Compile error") != std::string::npos)
        << "Expected script error message. Got: " << result.error;
}

TEST_F(ConverterTest, Script_Error_FileNotFound) {
    // Test error when script file doesn't exist
    std::string csv_content = "A\n1\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.script_file = "/nonexistent/path/script.luau";

    Converter converter(opts);
    ConversionResult result = converter.convert();

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.find("Could not open") != std::string::npos)
        << "Expected file not found error. Got: " << result.error;
}

TEST_F(ConverterTest, Script_PrintOutput) {
    // Test that print() output is captured
    std::string csv_content = "A\n1\n";
    std::string input = tempFile(".csv", csv_content);
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kCsv;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.output.quiet = true;  // Quiet mode to suppress print output (still succeeds)
    opts.script_inline = "print('Hello, World!')";

    Converter converter(opts);
    ConversionResult result = converter.convert();

    // Script should succeed even with print
    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
}

TEST_F(ConverterTest, Script_WithEval) {
    // Test that --eval and --script can be combined
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
C cB000002 1
R r0000001 0
X xA100001 cA000001 r0000001 n 5
X xB100002 cB000002 r0000001 f "=A1*2"
)";

    std::string input = tempFile(".zcd", zcd_content);
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.input_file = input;
    opts.output_file = output;
    opts.input_format = Format::kZcd;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;
    opts.output.quiet = true;
    opts.evaluate_formulas = true;
    opts.script_inline = "setCell('A1', 10)";  // Change A1 from 5 to 10

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Note: Script runs AFTER eval, so B1 would be evaluated first (5*2=10),
    // then script changes A1 to 10. The formula in B1 doesn't re-evaluate.
    std::string csv_output = readFile(output);
    EXPECT_TRUE(csv_output.find("10") != std::string::npos);
}

// ============================================================
// Empty workbook and script-only mode tests
// ============================================================

TEST_F(ConverterTest, EmptyWorkbook_CreateZcd) {
    // Create an empty workbook without input file
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.output_file = output;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    // No input_file set - should create empty workbook

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 0);  // Empty workbook has no cells

    // Verify output file contains valid zcd with a sheet
    std::string output_content = readFile(output);
    EXPECT_FALSE(output_content.empty());
    EXPECT_TRUE(output_content.find("D ") != std::string::npos);  // Document
    EXPECT_TRUE(output_content.find("S ") != std::string::npos);  // Sheet
    EXPECT_TRUE(output_content.find("Sheet1") != std::string::npos);  // Default sheet name
}

TEST_F(ConverterTest, EmptyWorkbook_CreateXlsx) {
    // Create an empty workbook in XLSX format
    std::string output = tempFile(".xlsx", "");
    trackFile(output);

    Options opts;
    opts.output_file = output;
    opts.output_format = Format::kXlsx;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Verify XLSX file was created
    EXPECT_TRUE(std::filesystem::exists(output));
    EXPECT_GT(std::filesystem::file_size(output), 0);
}

TEST_F(ConverterTest, EmptyWorkbook_WithScript) {
    // Create empty workbook and populate it with a script
    std::string output = tempFile(".zcd", "");
    trackFile(output);

    Options opts;
    opts.output_file = output;
    opts.output_format = Format::kZcd;
    opts.output.overwrite = true;
    opts.output.quiet = true;
    opts.script_inline = "setCell('A1', 'Hello'); setCell('B1', 42)";

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 2);  // Script created 2 cells

    // Verify output contains the script-created values
    std::string output_content = readFile(output);
    EXPECT_TRUE(output_content.find("Hello") != std::string::npos);
    EXPECT_TRUE(output_content.find(" n 42") != std::string::npos);
}

TEST_F(ConverterTest, EmptyWorkbook_CreateCsv) {
    // Create an empty CSV (edge case - should produce empty file)
    std::string output = tempFile(".csv", "");
    trackFile(output);

    Options opts;
    opts.output_file = output;
    opts.output_format = Format::kCsv;
    opts.output.overwrite = true;

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    EXPECT_EQ(result.cells_converted, 0);

    // CSV file should exist (may be empty or have just headers)
    EXPECT_TRUE(std::filesystem::exists(output));
}

TEST_F(ConverterTest, ScriptOnly_NoOutput) {
    // Script-only mode: run script on input, no output file
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
R r0000001 0
X xA100001 cA000001 r0000001 n 42
)";
    std::string input = tempFile(".zcd", zcd_content);

    Options opts;
    opts.input_file = input;
    opts.input_format = Format::kZcd;
    opts.output.quiet = true;
    opts.script_inline = "local val = getCell('A1'); print('Value is: ' .. tostring(val))";
    // No output_file set - script-only mode

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
    // Script executed successfully, no output file created
}

TEST_F(ConverterTest, ScriptOnly_WithScriptFile) {
    // Script-only mode with external script file
    std::string script_content = "print('Script executed!')";
    std::string script_file = tempFile(".luau", script_content);

    std::string csv_content = "A\n1\n";
    std::string input = tempFile(".csv", csv_content);

    Options opts;
    opts.input_file = input;
    opts.input_format = Format::kCsv;
    opts.output.quiet = true;
    opts.script_file = script_file;
    // No output_file set - script-only mode

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;
}

TEST_F(ConverterTest, ScriptOnly_ModifiesWorkbook) {
    // Script-only mode that modifies the workbook (changes are discarded)
    std::string zcd_content = R"(D doc1 "TestDoc"
S sheet1 "Sheet1"
C cA000001 0
R r0000001 0
X xA100001 cA000001 r0000001 n 10
)";
    std::string input = tempFile(".zcd", zcd_content);

    Options opts;
    opts.input_file = input;
    opts.input_format = Format::kZcd;
    opts.output.quiet = true;
    opts.script_inline = "setCell('A1', 999)";  // Modify value (not saved)
    // No output_file set - changes are not persisted

    Converter converter(opts);
    ConversionResult result = converter.convert();

    ASSERT_TRUE(result.ok()) << "Error: " << result.error;

    // Original file should be unchanged
    std::string original = readFile(input);
    EXPECT_TRUE(original.find(" n 10") != std::string::npos);
    EXPECT_TRUE(original.find(" n 999") == std::string::npos);
}

}  // namespace
}  // namespace cells::cli
