#include "core/cells/parser.h"

#include <fstream>
#include <sstream>
#include <string>

#include "core/cells/formula_serializer.h"

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
C pQ7rS8tW 0

#rows
R xY9zA1bC 0

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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
R yY9zA1bD 1
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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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
        EXPECT_EQ(FormulaSerializer::serialize(cell->formula->ast), "=SUM(A1:B2)");
    }
}

TEST(ParserTest, ParseErrorValue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
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

// Position-based axis parsing
TEST(ParserTest, ParseAxisPositions) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE 0
C cB3dE4fG 1
C cC5fG6hJ 2
#rows
R rA1bC2dE 0
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
        EXPECT_EQ(col1->position, 0u);

        Axis* col2 = sheet->getColumn(ID("cB3dE4fG"));
        ASSERT_NE(col2, nullptr);
        EXPECT_EQ(col2->position, 1u);

        Axis* col3 = sheet->getColumn(ID("cC5fG6hJ"));
        ASSERT_NE(col3, nullptr);
        EXPECT_EQ(col3->position, 2u);
    }
}

TEST(ParserTest, ParseSparsePositions) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE 0
C cB3dE4fG 5
#rows
R rA1bC2dE 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);

        Axis* col1 = sheet->getColumn(ID("cA1bC2dE"));
        ASSERT_NE(col1, nullptr);
        EXPECT_EQ(col1->position, 0u);

        Axis* col2 = sheet->getColumn(ID("cB3dE4fG"));
        ASSERT_NE(col2, nullptr);
        EXPECT_EQ(col2->position, 5u);
    }
}

// Axis properties
TEST(ParserTest, ParseAxisWidth) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C cA1bC2dE 0 w:200
#rows
R rA1bC2dE 0
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
C cA1bC2dE 0 name:"Revenue"
#rows
R rA1bC2dE 0
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
C pQ7rS8tW 0
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_GT(result.error->line, 0);
}

TEST(ParserTest, ErrorMissingColumnPosition) {
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
R rA1bC2dE 0
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

TEST(MalformedFileTest, ColumnMissingPosition) {
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

TEST(MalformedFileTest, RowMissingPosition) {
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

TEST(MalformedFileTest, InvalidColumnPosition) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
#cols
C cA1bC2dE abc
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
C cA1bC2dE 0
#rows
R rA1bC2dE 0
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
C cA1bC2dE 0
#rows
R rA1bC2dE 0
#cells
X xA1bC2dE cA1bC2dE rA1bC2dE
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
C cA1bC2dE 0
#rows
R rA1bC2dE 0
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
    // Error should be on line 5 (the malformed column line - missing position)
    EXPECT_EQ(result.error->line, 5);
}

// Sheet view properties (V line)
TEST(ParserTest, ParseSheetViewShowGridLinesDefault) {
    // Default showGridLines is true when not specified
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_TRUE(sheet->showGridLines);  // Default is true
    }
}

TEST(ParserTest, ParseSheetViewShowGridLinesFalse) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V showGridLines:0
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_FALSE(sheet->showGridLines);
    }
}

TEST(ParserTest, ParseSheetViewShowGridLinesTrue) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V showGridLines:1
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_TRUE(sheet->showGridLines);
    }
}

TEST(ParserTest, ParseSheetViewZoomScaleDefault) {
    // Default zoomScale is 100 when not specified
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->zoomScale, 100);  // Default is 100
    }
}

TEST(ParserTest, ParseSheetViewZoomScale150) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V zoomScale:150
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->zoomScale, 150);
    }
}

TEST(ParserTest, ParseSheetViewMultipleProperties) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V showGridLines:0 zoomScale:75
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_FALSE(sheet->showGridLines);
        EXPECT_EQ(sheet->zoomScale, 75);
    }
}

TEST(ParserTest, ParseSheetViewFreezePanesDefault) {
    // Default freezeCol/freezeRow are 0 when not specified
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok());

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->freezeCol, 0);  // Default is 0
        EXPECT_EQ(sheet->freezeRow, 0);  // Default is 0
    }
}

TEST(ParserTest, ParseSheetViewFreezePanes) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V freezeCol:2 freezeRow:3
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_EQ(sheet->freezeCol, 2);
        EXPECT_EQ(sheet->freezeRow, 3);
    }
}

TEST(ParserTest, ParseSheetViewAllProperties) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V showGridLines:0 zoomScale:115 freezeCol:1 freezeRow:2
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_FALSE(sheet->showGridLines);
        EXPECT_EQ(sheet->zoomScale, 115);
        EXPECT_EQ(sheet->freezeCol, 1);
        EXPECT_EQ(sheet->freezeRow, 2);
    }
}

TEST(ParserTest, ParseDefaultRowHeight) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V defaultRowHeight:16
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_DOUBLE_EQ(sheet->defaultRowHeight, 16.0);
    }
}

TEST(ParserTest, ParsePageMargins) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V pageMargins:0.7,0.7,0.75,0.75,0.3,0.3
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_TRUE(sheet->hasPageMargins);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.left, 0.7);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.right, 0.7);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.top, 0.75);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.bottom, 0.75);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.header, 0.3);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.footer, 0.3);
    }
}

TEST(ParserTest, ParseAllSheetProperties) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
V showGridLines:0 zoomScale:115 freezeCol:1 freezeRow:2 defaultRowHeight:16 pageMargins:0.7,0.7,0.75,0.75,0.3,0.3
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    Parser parser;
    ParseResult result = parser.parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "no error");

    if (result.workbook) {
        Sheet* sheet = result.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet, nullptr);
        EXPECT_FALSE(sheet->showGridLines);
        EXPECT_EQ(sheet->zoomScale, 115);
        EXPECT_EQ(sheet->freezeCol, 1);
        EXPECT_EQ(sheet->freezeRow, 2);
        EXPECT_DOUBLE_EQ(sheet->defaultRowHeight, 16.0);
        EXPECT_TRUE(sheet->hasPageMargins);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.left, 0.7);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.right, 0.7);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.top, 0.75);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.bottom, 0.75);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.header, 0.3);
        EXPECT_DOUBLE_EQ(sheet->pageMargins.footer, 0.3);
    }
}

// Convenience function
TEST(ParserTest, ParseConvenienceFunction) {
    const std::string content = R"(#cells v1
D aB3cD4eF "Test"
S gH5jK6mN "Sheet1"
#cols
C pQ7rS8tW 0
#rows
R xY9zA1bC 0
#cells
)";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok());
}

// --- Sample File Tests ---
// These tests parse the sample .zcd files from testdata/

namespace {
std::string readTestFile(const std::string& filename) {
    const std::string path = "testdata/" + filename;
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
    const std::string content = readTestFile("minimal.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read minimal.zcd";

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
    const std::string content = readTestFile("simple.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read simple.zcd";

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
    const std::string content = readTestFile("budget.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read budget.zcd";

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
    const std::string content = readTestFile("all_types.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read all_types.zcd";

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

TEST(SampleFileTest, ParseSparsePositionsCells) {
    const std::string content = readTestFile("sparse.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read sparse.zcd";

    ParseResult result = parse(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->name, "Sparse Positions Test");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 4u);
    EXPECT_EQ(sheet->cellCount(), 11u);

    // Verify sparse positions on columns (A=0, E=4, K=10)
    Axis* col1 = sheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->position, 0u);

    Axis* col2 = sheet->getColumn(ID("cE5fG6hJ"));
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->position, 4u);

    Axis* col3 = sheet->getColumn(ID("cK9mN0pQ"));
    ASSERT_NE(col3, nullptr);
    EXPECT_EQ(col3->position, 10u);
}

TEST(SampleFileTest, ParseUnicodeCells) {
    const std::string content = readTestFile("unicode.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read unicode.zcd";

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
    const std::string content = readTestFile("empty.zcd");
    ASSERT_FALSE(content.empty()) << "Could not read empty.zcd";

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
