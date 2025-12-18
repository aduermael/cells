// Generate test XLSX files for xlsx_reader_test
// Run with: bazel run //core/testdata/xlsx:generate_test_files

#include <OpenXLSX/OpenXLSX.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

void createSimple(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/simple.xlsx", true);
    auto wks = doc.workbook().worksheet("Sheet1");

    // Header row
    wks.cell("A1").value() = "Name";
    wks.cell("B1").value() = "Age";
    wks.cell("C1").value() = "Score";

    // Data rows
    wks.cell("A2").value() = "Alice";
    wks.cell("B2").value() = 25;
    wks.cell("C2").value() = 95.5;

    wks.cell("A3").value() = "Bob";
    wks.cell("B3").value() = 30;
    wks.cell("C3").value() = 87.0;

    wks.cell("A4").value() = "Charlie";
    wks.cell("B4").value() = 35;
    wks.cell("C4").value() = 92.3;

    doc.save();
    doc.close();
    std::cout << "Created simple.xlsx\n";
}

void createFormulas(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/formulas.xlsx", true);
    auto wks = doc.workbook().worksheet("Sheet1");

    // Values
    wks.cell("A1").value() = 10;
    wks.cell("A2").value() = 20;
    wks.cell("A3").value() = 30;

    wks.cell("B1").value() = 5;
    wks.cell("B2").value() = 15;
    wks.cell("B3").value() = 25;

    // Formulas
    wks.cell("C1").formula() = "A1+B1";
    wks.cell("C2").formula() = "A2*B2";
    wks.cell("C3").formula() = "SUM(A1:A3)";

    wks.cell("D1").formula() = "A1/B1";
    wks.cell("D2").formula() = "AVERAGE(A1:A3)";

    doc.save();
    doc.close();
    std::cout << "Created formulas.xlsx\n";
}

void createMultiSheet(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/multi_sheet.xlsx", true);

    // First sheet - Sales
    auto sales = doc.workbook().worksheet("Sheet1");
    doc.workbook().worksheet("Sheet1").setName("Sales");
    sales.cell("A1").value() = "Product";
    sales.cell("B1").value() = "Revenue";
    sales.cell("A2").value() = "Widget";
    sales.cell("B2").value() = 1000;
    sales.cell("A3").value() = "Gadget";
    sales.cell("B3").value() = 2500;

    // Second sheet - Expenses
    doc.workbook().addWorksheet("Expenses");
    auto expenses = doc.workbook().worksheet("Expenses");
    expenses.cell("A1").value() = "Category";
    expenses.cell("B1").value() = "Amount";
    expenses.cell("A2").value() = "Rent";
    expenses.cell("B2").value() = 500;
    expenses.cell("A3").value() = "Supplies";
    expenses.cell("B3").value() = 200;

    // Third sheet - Summary (with formulas referencing other sheets)
    doc.workbook().addWorksheet("Summary");
    auto summary = doc.workbook().worksheet("Summary");
    summary.cell("A1").value() = "Total Revenue";
    summary.cell("A2").value() = "Total Expenses";
    summary.cell("A3").value() = "Net";
    // Note: Cross-sheet formulas may not be fully supported
    summary.cell("B1").value() = 3500;
    summary.cell("B2").value() = 700;
    summary.cell("B3").formula() = "B1-B2";

    doc.save();
    doc.close();
    std::cout << "Created multi_sheet.xlsx\n";
}

void createTypes(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/types.xlsx", true);
    auto wks = doc.workbook().worksheet("Sheet1");

    // Numbers
    wks.cell("A1").value() = "Numbers:";
    wks.cell("B1").value() = 42;
    wks.cell("C1").value() = 3.14159;
    wks.cell("D1").value() = -100;
    wks.cell("E1").value() = 0;

    // Strings
    wks.cell("A2").value() = "Strings:";
    wks.cell("B2").value() = "Hello";
    wks.cell("C2").value() = "World";
    wks.cell("D2").value() = "Special: \"quotes\" & <tags>";
    wks.cell("E2").value() = "";  // Empty string

    // Booleans
    wks.cell("A3").value() = "Booleans:";
    wks.cell("B3").value() = true;
    wks.cell("C3").value() = false;

    // Large numbers
    wks.cell("A4").value() = "Large:";
    wks.cell("B4").value() = 1234567890;
    wks.cell("C4").value() = 1.23e10;

    doc.save();
    doc.close();
    std::cout << "Created types.xlsx\n";
}

void createEmpty(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/empty.xlsx", true);
    // Don't add any cells
    doc.save();
    doc.close();
    std::cout << "Created empty.xlsx\n";
}

void createUnicode(const std::string& dir) {
    OpenXLSX::XLDocument doc;
    doc.create(dir + "/unicode.xlsx", true);
    auto wks = doc.workbook().worksheet("Sheet1");

    wks.cell("A1").value() = "Language";
    wks.cell("B1").value() = "Greeting";

    wks.cell("A2").value() = "Japanese";
    wks.cell("B2").value() = "こんにちは";

    wks.cell("A3").value() = "Chinese";
    wks.cell("B3").value() = "你好";

    wks.cell("A4").value() = "German";
    wks.cell("B4").value() = "Grüß Gott";

    wks.cell("A5").value() = "Emoji";
    wks.cell("B5").value() = "Hello 🌍!";

    doc.save();
    doc.close();
    std::cout << "Created unicode.xlsx\n";
}

int main(int argc, char* argv[]) {
    std::string dir = ".";
    if (argc > 1) {
        dir = argv[1];
    }

    // Use current directory or specified path
    std::filesystem::create_directories(dir);

    createSimple(dir);
    createFormulas(dir);
    createMultiSheet(dir);
    createTypes(dir);
    createEmpty(dir);
    createUnicode(dir);

    std::cout << "All test files created in: " << dir << "\n";
    return 0;
}
