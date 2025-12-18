#include "core/cells/parser.h"

#include <fstream>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace cells {
namespace {

// Parser behavior tests - parser is permissive by design
TEST(ParserTest, ParseEmptyStringReturnsEmptyWorkbook) {
    Parser parser;
    ParseResult result = parser.parse(std::string(""));
    // Parser is permissive - returns empty workbook for empty input
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->sheetCount(), 0u);
}

TEST(ParserTest, ParseUnknownLinesIgnored) {
    Parser parser;
    // Unknown line types are silently ignored
    ParseResult result = parser.parse(std::string("not a valid cells file\n"));
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.workbook, nullptr);
}

TEST(ParserTest, ParseCommentLines) {
    Parser parser;
    // Lines starting with # are treated as comments
    ParseResult result = parser.parse(std::string("#cells v1\n#anything here\n"));
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.workbook, nullptr);
}

TEST(ParserTest, ParseOnlyHeaderReturnsEmptyWorkbook) {
    Parser parser;
    ParseResult result = parser.parse(std::string("#cells v1\n"));
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->sheetCount(), 0u);
}

// Minimal valid file
TEST(ParserTest, ParseMinimalFileSucceeds) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"

S gH5jK6mN "Sheet1"

#cols
C pQ7rS8tW ~ ~

#rows
R xY9zA1bC ~ ~

#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC n 42
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");
    EXPECT_NE(result.workbook, nullptr);

    if (result.workbook) {
        EXPECT_EQ(result.workbook->name, "Test");
        EXPECT_EQ(result.workbook->sheetCount(), 1u);

        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->name, "Sheet1");
        EXPECT_EQ(sheet->columnCount(), 1u);
        EXPECT_EQ(sheet->rowCount(), 1u);
        EXPECT_EQ(sheet->cellCount(), 1u);
    }
}

// Empty file (no cells)
TEST(ParserTest, ParseEmptyFileSucceeds) {
    const std::string content = R"(#cells v1
D eM1pT2yF "Empty"

S sH3eE4tB "Empty Sheet"

#cols

#rows

#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        EXPECT_EQ(result.workbook->name, "Empty");
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->cellCount(), 0u);
    }
}

// Value type parsing
TEST(ParserTest, ParseNumberValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC n 123.456
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(cell->value.asNumber(), 123.456);
    }
}

TEST(ParserTest, ParseStringValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC s "Hello, World!"
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::STRING);
        EXPECT_EQ(cell->value.asString(), "Hello, World!");
    }
}

TEST(ParserTest, ParseBooleanValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
R yY9zA1bD xY9zA1bC ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC b true
X dE2fG3hK pQ7rS8tW yY9zA1bD b false
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);

        Cell* cellTrue = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cellTrue, nullptr);
        EXPECT_EQ(cellTrue->value.type, CellValueType::BOOLEAN);
        EXPECT_TRUE(cellTrue->value.asBoolean());

        Cell* cellFalse = sheet->getCell(ID("dE2fG3hK"));
        ASSERT_NE(cellFalse, nullptr);
        EXPECT_EQ(cellFalse->value.type, CellValueType::BOOLEAN);
        EXPECT_FALSE(cellFalse->value.asBoolean());
    }
}

TEST(ParserTest, ParseFormulaValue) {
    const std::string content = R"cells(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC f "=SUM(A1:B2)"
)cells";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_TRUE(cell->isFormula());
        ASSERT_NE(cell->formula, nullptr);
        EXPECT_STREQ(cell->formula->text, "=SUM(A1:B2)");
    }
}

TEST(ParserTest, ParseErrorValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC e #DIV/0!
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::ERROR);
        EXPECT_EQ(cell->value.error, CellError::DIV);
    }
}

TEST(ParserTest, ParseDateValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC d 2024-01-15
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::DATE);
        EXPECT_EQ(cell->value.raw, "2024-01-15");
    }
}

TEST(ParserTest, ParseDateTimeValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
X dE2fG3hJ pQ7rS8tW xY9zA1bC t 2024-01-15T10:30:00Z
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("dE2fG3hJ"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::DATE_TIME);
        EXPECT_EQ(cell->value.raw, "2024-01-15T10:30:00Z");
    }
}

// Linked list parsing
TEST(ParserTest, ParseAxisLinks) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE ~ cB3dE4fG
C cB3dE4fG cA1bC2dE cC5fG6hJ
C cC5fG6hJ cB3dE4fG ~
#rows
R rA1bC2dE ~ ~
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        EXPECT_EQ(sheet->columnCount(), 3u);

        Axis* col1 = sheet->getColumn(ID("cA1bC2dE"));
        ASSERT_NE(col1, nullptr);
        EXPECT_TRUE(col1->prevId.isNull());
        EXPECT_EQ(col1->nextId.toString(), "cB3dE4fG");

        Axis* col2 = sheet->getColumn(ID("cB3dE4fG"));
        ASSERT_NE(col2, nullptr);
        EXPECT_EQ(col2->prevId.toString(), "cA1bC2dE");
        EXPECT_EQ(col2->nextId.toString(), "cC5fG6hJ");

        Axis* col3 = sheet->getColumn(ID("cC5fG6hJ"));
        ASSERT_NE(col3, nullptr);
        EXPECT_EQ(col3->prevId.toString(), "cB3dE4fG");
        EXPECT_TRUE(col3->nextId.isNull());
    }
}

TEST(ParserTest, ParseAxisGaps) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE ~ cB3dE4fG:3
C cB3dE4fG cA1bC2dE:3 ~
#rows
R rA1bC2dE ~ ~
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);

        Axis* col1 = sheet->getColumn(ID("cA1bC2dE"));
        ASSERT_NE(col1, nullptr);
        EXPECT_EQ(col1->gapAfter, 3u);

        Axis* col2 = sheet->getColumn(ID("cB3dE4fG"));
        ASSERT_NE(col2, nullptr);
        EXPECT_EQ(col2->gapBefore, 3u);
    }
}

// Axis properties
TEST(ParserTest, ParseAxisWidth) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE ~ ~ w:200
#rows
R rA1bC2dE ~ ~
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Axis* col = sheet->getColumn(ID("cA1bC2dE"));
        ASSERT_NE(col, nullptr);
        EXPECT_EQ(col->size, 200u);
    }
}

TEST(ParserTest, ParseAxisName) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE ~ ~ name:"Revenue"
#rows
R rA1bC2dE ~ ~
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Axis* col = sheet->getColumn(ID("cA1bC2dE"));
        ASSERT_NE(col, nullptr);
        EXPECT_EQ(col->name, "Revenue");
    }
}

// Error reporting
TEST(ParserTest, ErrorContainsLineNumber) {
    // Column before any sheet is defined - triggers "Column outside of sheet" error
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
C pQ7rS8tW ~ ~
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_GT(result.error->line, 0);
}

TEST(ParserTest, ErrorMissingColumnTokens) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(ParserTest, ErrorInvalidDocumentName) {
    // Document line without proper quoted name
    const std::string content = "D aB3cD4eF unquoted\n";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

// --- Malformed File Tests ---
// Comprehensive tests for various parser error conditions

TEST(MalformedFileTest, RowOutsideSheet) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
R rA1bC2dE ~ ~
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find("outside"), std::string::npos);
}

TEST(MalformedFileTest, CellOutsideSheet) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
X xA1bC2dE cA1bC2dE rA1bC2dE n 42
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, ColumnMissingPrev) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, ColumnMissingNext) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE ~
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, RowMissingTokens) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#rows
R rA1bC2dE
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, CellMissingValueAccepted) {
    // Parser is permissive - a number without value defaults to 0
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE ~ ~
#rows
R rA1bC2dE ~ ~
#cells
X xA1bC2dE cA1bC2dE rA1bC2dE n
)";

    ParseResult result = parse(content);
    // Parser accepts this - number defaults to 0
    EXPECT_TRUE(result.ok());
    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        Cell* cell = sheet->getCell(ID("xA1bC2dE"));
        ASSERT_NE(cell, nullptr);
        EXPECT_EQ(cell->value.type, CellValueType::NUMBER);
    }
}

TEST(MalformedFileTest, CellMissingType) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE ~ ~
#rows
R rA1bC2dE ~ ~
#cells
X xA1bC2dE cA1bC2dE rA1bC2dE
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, InvalidGapValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE ~ cB3dE4fG:abc
C cB3dE4fG cA1bC2dE:abc ~
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, SheetMissingName) {
    const std::string content = "S sH3eE4tB\n";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, SheetUnterminatedQuote) {
    const std::string content = "S sH3eE4tB \"Unterminated\n";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, CellStringUnterminatedQuote) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE ~ ~
#rows
R rA1bC2dE ~ ~
#cells
X xA1bC2dE cA1bC2dE rA1bC2dE s "unterminated
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, DocumentMissingId) {
    const std::string content = "D \"Test\"\n";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
}

TEST(MalformedFileTest, ErrorMessageHasLineNumber) {
    // Verify that error messages include line numbers for debugging
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"

S sH3eE4tB "Sheet"
C cA1bC2dE
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_GT(result.error->line, 0);
    // Error should be on line 5 (the malformed column line)
    EXPECT_EQ(result.error->line, 5);
}

// Convenience function
TEST(ParserTest, ParseConvenienceFunction) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW ~ ~
#rows
R xY9zA1bC ~ ~
#cells
)";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok());
}

// --- Sample File Tests ---
// These tests parse the sample .cells files from core/testdata/

namespace {
std::string readTestFile(const std::string& filename) {
    const std::string path = "core/testdata/" + filename;
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
}  // namespace

TEST(SampleFileTest, ParseMinimalCells) {
    const std::string content = readTestFile("minimal.cells");
    ASSERT_FALSE(content.empty()) << "Could not read minimal.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Minimal");
    EXPECT_EQ(result.workbook->sheetCount(), 1u);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->name, "Sheet1");
    EXPECT_EQ(sheet->columnCount(), 1u);
    EXPECT_EQ(sheet->rowCount(), 1u);
    EXPECT_EQ(sheet->cellCount(), 1u);
}

TEST(SampleFileTest, ParseSimpleCells) {
    const std::string content = readTestFile("simple.cells");
    ASSERT_FALSE(content.empty()) << "Could not read simple.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Untitled");
    EXPECT_EQ(result.workbook->sheetCount(), 1u);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->columnCount(), 2u);
    EXPECT_EQ(sheet->rowCount(), 3u);
    EXPECT_EQ(sheet->cellCount(), 3u);
}

TEST(SampleFileTest, ParseBudgetCells) {
    const std::string content = readTestFile("budget.cells");
    ASSERT_FALSE(content.empty()) << "Could not read budget.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Budget 2024");
    EXPECT_EQ(result.workbook->sheetCount(), 1u);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->name, "Q1 Expenses");
    EXPECT_EQ(sheet->columnCount(), 5u);
    EXPECT_EQ(sheet->rowCount(), 4u);
    EXPECT_EQ(sheet->cellCount(), 20u);
}

TEST(SampleFileTest, ParseAllTypesCells) {
    const std::string content = readTestFile("all_types.cells");
    ASSERT_FALSE(content.empty()) << "Could not read all_types.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "All Types Test");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->columnCount(), 2u);
    EXPECT_EQ(sheet->rowCount(), 11u);
    EXPECT_EQ(sheet->cellCount(), 22u);
}

TEST(SampleFileTest, ParseGapsCells) {
    const std::string content = readTestFile("gaps.cells");
    ASSERT_FALSE(content.empty()) << "Could not read gaps.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Gaps Test");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 4u);
    EXPECT_EQ(sheet->cellCount(), 11u);

    // Verify gap values on columns
    Axis* col1 = sheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->gapAfter, 3u);

    Axis* col2 = sheet->getColumn(ID("cE5fG6hJ"));
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->gapBefore, 3u);
    EXPECT_EQ(col2->gapAfter, 5u);
}

TEST(SampleFileTest, ParseUnicodeCells) {
    const std::string content = readTestFile("unicode.cells");
    ASSERT_FALSE(content.empty()) << "Could not read unicode.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    // Document name is Japanese
    EXPECT_EQ(result.workbook->name, "日本語テスト");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    // Sheet name is Japanese
    EXPECT_EQ(sheet->name, "表計算");
    EXPECT_EQ(sheet->cellCount(), 14u);
}

TEST(SampleFileTest, ParseEmptyCells) {
    const std::string content = readTestFile("empty.cells");
    ASSERT_FALSE(content.empty()) << "Could not read empty.cells";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Empty Workbook");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->name, "Empty Sheet");
    EXPECT_EQ(sheet->columnCount(), 0u);
    EXPECT_EQ(sheet->rowCount(), 0u);
    EXPECT_EQ(sheet->cellCount(), 0u);
}

}  // namespace
}  // namespace cells
