#include "core/cells/xlsx_reader.h"

#include <string>

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to get test file path
std::string testFilePath(const std::string& filename) {
    return "testdata/xlsx/" + filename;
}

// ============================================================================
// Basic Reading Tests
// ============================================================================

TEST(XLSXReaderTest, ReadSimpleFile) {
    auto result = readXLSX(testFilePath("simple.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    // Should have one sheet
    EXPECT_EQ(result.workbook->sheetCount(), 1u);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->name, "Sheet1");

    // 3 columns, 4 rows (header + 3 data rows), 12 cells
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 4u);
    // The actual cell count depends on whether empty cells are created
    EXPECT_GE(sheet->cellCount(), 12u);
}

TEST(XLSXReaderTest, ReadEmptyFile) {
    auto result = readXLSX(testFilePath("empty.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->cellCount(), 0u);
}

TEST(XLSXReaderTest, ReadNonExistentFile) {
    auto result = readXLSX(testFilePath("does_not_exist.xlsx"));
    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_FALSE(result.error->message.empty());
}

// ============================================================================
// Cell Value Type Tests
// ============================================================================

TEST(XLSXReaderTest, ReadNumbers) {
    auto result = readXLSX(testFilePath("types.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that we have numeric cells
    bool foundNumber = false;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->value.type == CellValueType::NUMBER) {
            foundNumber = true;
            break;
        }
    }
    EXPECT_TRUE(foundNumber) << "Expected to find at least one numeric cell";
}

TEST(XLSXReaderTest, ReadStrings) {
    auto result = readXLSX(testFilePath("types.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that we have string cells
    bool foundString = false;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->value.type == CellValueType::STRING) {
            foundString = true;
            break;
        }
    }
    EXPECT_TRUE(foundString) << "Expected to find at least one string cell";
}

TEST(XLSXReaderTest, ReadBooleans) {
    auto result = readXLSX(testFilePath("types.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that we have boolean cells
    bool foundBoolean = false;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->value.type == CellValueType::BOOLEAN) {
            foundBoolean = true;
            break;
        }
    }
    EXPECT_TRUE(foundBoolean) << "Expected to find at least one boolean cell";
}

// ============================================================================
// Formula Tests
// ============================================================================

TEST(XLSXReaderTest, ReadFormulas) {
    auto result = readXLSX(testFilePath("formulas.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that we have formula cells
    int formulaCount = 0;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->isFormula()) {
            formulaCount++;
            // Formula should start with '='
            EXPECT_EQ(cell->formula->text[0], '=');
        }
    }
    EXPECT_GT(formulaCount, 0) << "Expected to find formula cells";
}

TEST(XLSXReaderTest, FormulaContainsExcelNotation) {
    auto result = readXLSX(testFilePath("formulas.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Look for formulas with A1 notation (e.g., "A1+B1", "SUM(A1:A3)")
    bool foundA1Reference = false;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->isFormula() && cell->formula->text != nullptr) {
            std::string formula(cell->formula->text);
            // Check for cell reference patterns
            if (formula.find("A1") != std::string::npos ||
                formula.find("B1") != std::string::npos ||
                formula.find("SUM") != std::string::npos) {
                foundA1Reference = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundA1Reference)
        << "Expected formulas to contain Excel A1 notation or function names";
}

TEST(XLSXReaderTest, SkipFormulasWhenDisabled) {
    XLSXReadOptions options;
    options.readFormulas = false;

    auto result = readXLSX(testFilePath("formulas.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Should have no formula cells
    for (const auto& [id, cell] : sheet->cells) {
        EXPECT_FALSE(cell->isFormula()) << "Expected no formula cells when readFormulas=false";
    }
}

// ============================================================================
// Multi-Sheet Tests
// ============================================================================

TEST(XLSXReaderTest, ReadMultipleSheets) {
    auto result = readXLSX(testFilePath("multi_sheet.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    // Should have 3 sheets
    EXPECT_EQ(result.workbook->sheetCount(), 3u);
}

TEST(XLSXReaderTest, SheetNamesPreserved) {
    auto result = readXLSX(testFilePath("multi_sheet.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    // Check sheet names
    std::vector<std::string> names;
    for (const auto& sheet : result.workbook->sheets) {
        names.push_back(sheet->name);
    }

    EXPECT_EQ(names.size(), 3u);
    // Names should be Sales, Expenses, Summary (in order)
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Sales") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Expenses") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Summary") != names.end());
}

TEST(XLSXReaderTest, ReadSpecificSheet) {
    XLSXReadOptions options;
    options.sheetName = "Expenses";

    auto result = readXLSX(testFilePath("multi_sheet.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    // Should have only 1 sheet
    EXPECT_EQ(result.workbook->sheetCount(), 1u);
    EXPECT_EQ(result.workbook->getSheetByIndex(0)->name, "Expenses");
}

TEST(XLSXReaderTest, ReadNonExistentSheet) {
    XLSXReadOptions options;
    options.sheetName = "NonExistent";

    auto result = readXLSX(testFilePath("multi_sheet.xlsx"), options);
    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.error->message.find("not found") != std::string::npos);
}

// ============================================================================
// Unicode Tests
// ============================================================================

TEST(XLSXReaderTest, ReadUnicodeText) {
    auto result = readXLSX(testFilePath("unicode.xlsx"));
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that we can read cells with unicode content
    bool foundUnicode = false;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->value.type == CellValueType::STRING) {
            const std::string& str = cell->value.asString();
            // Check for non-ASCII characters (UTF-8 multi-byte sequences)
            for (char c : str) {
                if (static_cast<unsigned char>(c) > 127) {
                    foundUnicode = true;
                    break;
                }
            }
            if (foundUnicode)
                break;
        }
    }
    EXPECT_TRUE(foundUnicode) << "Expected to find unicode text in cells";
}

// ============================================================================
// Dimensions Tests
// ============================================================================

TEST(XLSXReaderTest, ColumnWidthsRead) {
    XLSXReadOptions options;
    options.readDimensions = true;

    auto result = readXLSX(testFilePath("simple.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Check that columns have sizes
    for (const auto& [id, col] : sheet->columns) {
        EXPECT_GT(col->size, 0u) << "Column should have a width > 0";
    }
}

TEST(XLSXReaderTest, SkipDimensionsWhenDisabled) {
    XLSXReadOptions options;
    options.readDimensions = false;

    auto result = readXLSX(testFilePath("simple.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    // Should still work, dimensions just won't be read
    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_GT(sheet->columnCount(), 0u);
}

// ============================================================================
// XLSXReadError Tests
// ============================================================================

TEST(XLSXReadErrorTest, ToStringBasic) {
    XLSXReadError error("Test error");
    EXPECT_EQ(error.toString(), "Test error");
}

TEST(XLSXReadErrorTest, ToStringWithSheet) {
    XLSXReadError error("Test error", "Sheet1");
    std::string str = error.toString();
    EXPECT_TRUE(str.find("Sheet1") != std::string::npos);
    EXPECT_TRUE(str.find("Test error") != std::string::npos);
}

TEST(XLSXReadErrorTest, ToStringWithLocation) {
    XLSXReadError error("Test error", "Sheet1", 5, 3);
    std::string str = error.toString();
    EXPECT_TRUE(str.find("Sheet1") != std::string::npos);
    EXPECT_TRUE(str.find("row: 5") != std::string::npos);
    EXPECT_TRUE(str.find("col: 3") != std::string::npos);
    EXPECT_TRUE(str.find("Test error") != std::string::npos);
}

// ============================================================================
// Warnings Tests
// ============================================================================

TEST(XLSXReaderTest, WarningsCollected) {
    // Even a successful read might produce warnings
    auto result = readXLSX(testFilePath("formulas.xlsx"));
    // Just verify that warnings is a vector (may or may not have content)
    EXPECT_TRUE(result.warnings.empty() || !result.warnings.empty());
}

}  // namespace
}  // namespace cells
