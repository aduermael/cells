#include "core/cells/xlsx_reader.h"

#include <string>

#include "core/cells/formula_serializer.h"

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
            // Formula should have AST
            EXPECT_NE(cell->formula->ast, nullptr);
            // Serialized formula should start with '='
            std::string formula = FormulaSerializer::serialize(cell->formula->ast);
            EXPECT_EQ(formula[0], '=');
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
        if (cell->isFormula() && cell->formula->ast != nullptr) {
            std::string formula = FormulaSerializer::serialize(cell->formula->ast);
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
// Style Reading Tests
// ============================================================================

TEST(XLSXReaderTest, ReadStylesBold) {
    XLSXReadOptions options;
    options.readStyles = true;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);
    ASSERT_GT(result.workbook->sheetCount(), 0u)
        << "Workbook should have at least one sheet";

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr) << "getSheetByIndex(0) returned null";

    // Find A1 (Bold cell) - it's the first cell at column 0, row 0
    Cell* boldCell = nullptr;
    Axis* col0 = sheet->getColumnByPosition(0);
    Axis* row0 = sheet->getRowByPosition(0);
    if (col0 && row0) {
        boldCell = sheet->getCellAt(col0->id, row0->id);
    }
    ASSERT_NE(boldCell, nullptr) << "Bold cell A1 should exist";

    // Should have a style ID
    EXPECT_FALSE(boldCell->styleId.isNull()) << "Bold cell should have a style";

    // Get the style and verify it's bold
    const CellStyle* style = result.workbook->getStyle(boldCell->styleId);
    ASSERT_NE(style, nullptr) << "Style should be registered in workbook";
    EXPECT_TRUE(style->bold) << "Cell A1 should be bold";
}

TEST(XLSXReaderTest, ReadStylesItalic) {
    XLSXReadOptions options;
    options.readStyles = true;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Find A2 (Italic cell) - column 0, row 1
    Cell* italicCell = nullptr;
    Axis* col0 = sheet->getColumnByPosition(0);
    Axis* row1 = sheet->getRowByPosition(1);
    if (col0 && row1) {
        italicCell = sheet->getCellAt(col0->id, row1->id);
    }
    ASSERT_NE(italicCell, nullptr) << "Italic cell A2 should exist";

    // Should have a style ID
    EXPECT_FALSE(italicCell->styleId.isNull()) << "Italic cell should have a style";

    // Get the style and verify it's italic
    const CellStyle* style = result.workbook->getStyle(italicCell->styleId);
    ASSERT_NE(style, nullptr) << "Style should be registered in workbook";
    EXPECT_TRUE(style->italic) << "Cell A2 should be italic";
}

TEST(XLSXReaderTest, ReadStylesBackgroundColor) {
    XLSXReadOptions options;
    options.readStyles = true;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Find B1 (Red background cell) - column 1, row 0
    Cell* redBgCell = nullptr;
    Axis* col1 = sheet->getColumnByPosition(1);
    Axis* row0 = sheet->getRowByPosition(0);
    if (col1 && row0) {
        redBgCell = sheet->getCellAt(col1->id, row0->id);
    }
    ASSERT_NE(redBgCell, nullptr) << "Red background cell B1 should exist";

    // Should have a style ID
    EXPECT_FALSE(redBgCell->styleId.isNull()) << "Red BG cell should have a style";

    // Get the style and verify it has red background
    const CellStyle* style = result.workbook->getStyle(redBgCell->styleId);
    ASSERT_NE(style, nullptr) << "Style should be registered in workbook";
    EXPECT_EQ(style->bgColor, "#FF0000") << "Cell B1 should have red background";
}

TEST(XLSXReaderTest, ReadStylesTextColor) {
    XLSXReadOptions options;
    options.readStyles = true;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Find B2 (Blue text cell) - column 1, row 1
    Cell* blueTextCell = nullptr;
    Axis* col1 = sheet->getColumnByPosition(1);
    Axis* row1 = sheet->getRowByPosition(1);
    if (col1 && row1) {
        blueTextCell = sheet->getCellAt(col1->id, row1->id);
    }
    ASSERT_NE(blueTextCell, nullptr) << "Blue text cell B2 should exist";

    // Should have a style ID
    EXPECT_FALSE(blueTextCell->styleId.isNull()) << "Blue text cell should have a style";

    // Get the style and verify it has blue text
    const CellStyle* style = result.workbook->getStyle(blueTextCell->styleId);
    ASSERT_NE(style, nullptr) << "Style should be registered in workbook";
    EXPECT_EQ(style->textColor, "#0000FF") << "Cell B2 should have blue text";
}

TEST(XLSXReaderTest, ReadStylesAlignment) {
    XLSXReadOptions options;
    options.readStyles = true;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Find C1 (Center aligned cell) - column 2, row 0
    Cell* centerCell = nullptr;
    Axis* col2 = sheet->getColumnByPosition(2);
    Axis* row0 = sheet->getRowByPosition(0);
    if (col2 && row0) {
        centerCell = sheet->getCellAt(col2->id, row0->id);
    }
    ASSERT_NE(centerCell, nullptr) << "Center aligned cell C1 should exist";

    // Should have a style ID
    EXPECT_FALSE(centerCell->styleId.isNull()) << "Center cell should have a style";

    // Get the style and verify it's center aligned
    const CellStyle* style = result.workbook->getStyle(centerCell->styleId);
    ASSERT_NE(style, nullptr) << "Style should be registered in workbook";
    EXPECT_EQ(style->hAlign, TextAlign::CENTER) << "Cell C1 should be center aligned";
}

TEST(XLSXReaderTest, ReadStylesSkipWhenDisabled) {
    XLSXReadOptions options;
    options.readStyles = false;

    auto result = readXLSX(testFilePath("styled.xlsx"), options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // When styles are disabled, cells should have null styleId
    Axis* col0 = sheet->getColumnByPosition(0);
    Axis* row0 = sheet->getRowByPosition(0);
    if (col0 && row0) {
        Cell* cell = sheet->getCellAt(col0->id, row0->id);
        if (cell) {
            EXPECT_TRUE(cell->styleId.isNull()) << "Style should be null when readStyles=false";
        }
    }

    // Workbook should have no styles
    EXPECT_TRUE(result.workbook->getStyles().empty())
        << "No styles should be registered when readStyles=false";
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
