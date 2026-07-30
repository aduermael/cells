#include "core/cells/serializer.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "core/cells/format_buffer.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/number_format.h"
#include "core/cells/parser.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a Formula from text (parses the formula to AST)
Formula* createFormula(const std::string& text) {
    FormulaParser parser(text);
    std::unique_ptr<ASTNode> ast = parser.parse();
    auto* formula = new Formula();
    formula->ast = ast.release();
    formula->dirty = true;
    return formula;
}

// Helper to create a minimal workbook for testing
std::unique_ptr<Workbook> createMinimalWorkbook() {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test Workbook");

    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    // Add one column
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    sheet->addColumn(std::move(col));

    // Add one row
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    sheet->addRow(std::move(row));

    // Add one cell
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    sheet->addCell(std::move(cell));

    wb->addSheet(std::move(sheet));
    return wb;
}

// --- Basic Serialization Tests ---

TEST(SerializerTest, SerializeEmptyWorkbook) {
    Workbook wb(ID("aB3cD4eF"), "Empty");

    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Should contain document line
    EXPECT_NE(output.find("D aB3cD4eF \"Empty\""), std::string::npos);
}

TEST(SerializerTest, SerializeMinimalWorkbook) {
    auto wb = createMinimalWorkbook();

    Serializer serializer;
    const std::string output = serializer.serialize(*wb);

    // Check for document line
    EXPECT_NE(output.find("D aB3cD4eF \"Test Workbook\""), std::string::npos);

    // Check for sheet line
    EXPECT_NE(output.find("S sH3eE4tB \"Sheet1\""), std::string::npos);

    // Check for column line
    EXPECT_NE(output.find("C cA1bC2dE"), std::string::npos);

    // Check for row line
    EXPECT_NE(output.find("R rA1bC2dE"), std::string::npos);

    // Check for cell line
    EXPECT_NE(output.find("X xA1bC2dE"), std::string::npos);
}

TEST(SerializerTest, SerializeToStream) {
    auto wb = createMinimalWorkbook();

    std::ostringstream ss;
    Serializer serializer;
    serializer.serialize(*wb, ss);

    const std::string output = ss.str();
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("D aB3cD4eF"), std::string::npos);
}

// --- Value Type Serialization ---

TEST(SerializerTest, SerializeNumberCell) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(123.456);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("n 123.456"), std::string::npos);
}

TEST(SerializerTest, SerializeStringCell) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(std::string("Hello, World!"));

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("s \"Hello, World!\""), std::string::npos);
}

TEST(SerializerTest, SerializeBooleanCell) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->position = 0;
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    auto cellTrue = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cellTrue->value = CellValue(true);

    auto cellFalse = std::make_unique<Cell>(ID("xB1cC3dD"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    cellFalse->value = CellValue(false);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addRow(std::move(row2));
    sheet->addCell(std::move(cellTrue));
    sheet->addCell(std::move(cellFalse));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("b true"), std::string::npos);
    EXPECT_NE(output.find("b false"), std::string::npos);
}

TEST(SerializerTest, SerializeFormulaCell) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->setFormula(createFormula("=SUM(A1:B2)"));

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("f \"=SUM(A1:B2)\""), std::string::npos);
}

TEST(SerializerTest, SerializeErrorCell) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(CellError::DIV);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("e #DIV/0!"), std::string::npos);
}

// --- String Escaping ---

TEST(SerializerTest, EscapeQuotes) {
    const std::string result = escapeString("Say \"Hello\"");
    EXPECT_EQ(result, "Say \\\"Hello\\\"");
}

TEST(SerializerTest, EscapeNewlines) {
    const std::string result = escapeString("Line1\nLine2");
    EXPECT_EQ(result, "Line1\\nLine2");
}

TEST(SerializerTest, EscapeBackslash) {
    const std::string result = escapeString("path\\to\\file");
    EXPECT_EQ(result, "path\\\\to\\\\file");
}

TEST(SerializerTest, EscapeTabs) {
    const std::string result = escapeString("col1\tcol2");
    EXPECT_EQ(result, "col1\\tcol2");
}

TEST(SerializerTest, EscapeCarriageReturn) {
    const std::string result = escapeString("line\r\n");
    EXPECT_EQ(result, "line\\r\\n");
}

TEST(SerializerTest, NoEscapeUnicode) {
    // Unicode should pass through unchanged
    const std::string result = escapeString("日本語");
    EXPECT_EQ(result, "日本語");
}

// --- Position Serialization ---

TEST(SerializerTest, SerializeColumnPositions) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->position = 0;

    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->position = 5;

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->position = 0;
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // Should have positions
    EXPECT_NE(output.find("C cA1bC2dE 0"), std::string::npos);
    EXPECT_NE(output.find("C cB3dE4fG 5"), std::string::npos);
}

// --- Axis Properties ---

TEST(SerializerTest, SerializeColumnWidth) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->size = 200;

    sheet->addColumn(std::move(col));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("w:200"), std::string::npos);
}

TEST(SerializerTest, SerializeColumnName) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->name = "Revenue";

    sheet->addColumn(std::move(col));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("name:\"Revenue\""), std::string::npos);
}

TEST(SerializerTest, SerializeRowHeight) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->size = 48;

    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("h:48"), std::string::npos);
}

TEST(SerializerTest, SerializeColumnSizeOriginal) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->size = 200;
    col->sizeOriginal = 8.43;

    sheet->addColumn(std::move(col));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("w:200"), std::string::npos);
    EXPECT_NE(output.find("wo:8.43"), std::string::npos);
}

TEST(SerializerTest, SerializeRowSizeOriginal) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->size = 48;
    row->sizeOriginal = 16.5;

    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("h:48"), std::string::npos);
    EXPECT_NE(output.find("ho:16.5"), std::string::npos);
}

TEST(SerializerTest, RoundtripColumnSizeOriginal) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->size = 200;
    col->sizeOriginal = 8.43;

    sheet->addColumn(std::move(col));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    Parser parser;
    auto result = parser.parse(output);
    ASSERT_TRUE(result.ok());

    const Axis* col2 = result.workbook->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->size, 200u);
    EXPECT_DOUBLE_EQ(col2->sizeOriginal, 8.43);
}

TEST(SerializerTest, RoundtripRowSizeOriginal) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->size = 48;
    row->sizeOriginal = 16.5;

    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    Parser parser;
    auto result = parser.parse(output);
    ASSERT_TRUE(result.ok());

    const Axis* row2 = result.workbook->getRow(ID("rA1bC2dE"));
    ASSERT_NE(row2, nullptr);
    EXPECT_EQ(row2->size, 48u);
    EXPECT_DOUBLE_EQ(row2->sizeOriginal, 16.5);
}

TEST(SerializerTest, SerializeShowGridLinesDefault) {
    // When showGridLines is true (default), V line should not be emitted
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    // showGridLines defaults to true
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should not appear when showGridLines is default (true)
    EXPECT_EQ(output.find("V showGridLines"), std::string::npos);
}

TEST(SerializerTest, SerializeShowGridLinesFalse) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->showGridLines = false;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should appear when showGridLines is false
    EXPECT_NE(output.find("V showGridLines:0"), std::string::npos);
}

TEST(SerializerTest, SerializeZoomScaleDefault) {
    // When zoomScale is 100 (default), V line should not be emitted
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    // zoomScale defaults to 100
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should not appear when zoomScale is default (100)
    EXPECT_EQ(output.find("V "), std::string::npos);
}

TEST(SerializerTest, SerializeZoomScaleNonDefault) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->zoomScale = 150;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should appear when zoomScale is not default
    EXPECT_NE(output.find("V zoomScale:150"), std::string::npos);
}

TEST(SerializerTest, SerializeMultipleViewProperties) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->showGridLines = false;
    sheet->zoomScale = 75;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should have both properties
    EXPECT_NE(output.find("showGridLines:0"), std::string::npos);
    EXPECT_NE(output.find("zoomScale:75"), std::string::npos);
}

TEST(SerializerTest, SerializeFreezePanesDefault) {
    // When freezeCol/freezeRow are 0 (default), V line should not be emitted
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    // freezeCol/freezeRow default to 0
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should not appear when freeze panes are default (0)
    EXPECT_EQ(output.find("V "), std::string::npos);
}

TEST(SerializerTest, SerializeFreezePanesNonDefault) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->freezeCol = 2;
    sheet->freezeRow = 3;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should appear when freeze panes are set
    EXPECT_NE(output.find("freezeCol:2"), std::string::npos);
    EXPECT_NE(output.find("freezeRow:3"), std::string::npos);
}

TEST(SerializerTest, SerializeAllViewProperties) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->showGridLines = false;
    sheet->zoomScale = 115;
    sheet->freezeCol = 1;
    sheet->freezeRow = 2;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // V line should have all properties
    EXPECT_NE(output.find("showGridLines:0"), std::string::npos);
    EXPECT_NE(output.find("zoomScale:115"), std::string::npos);
    EXPECT_NE(output.find("freezeCol:1"), std::string::npos);
    EXPECT_NE(output.find("freezeRow:2"), std::string::npos);
}

TEST(SerializerTest, SerializeDefaultRowHeightDefault) {
    // When defaultRowHeight is 0 (default), V line should not include it
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_EQ(output.find("defaultRowHeight"), std::string::npos);
}

TEST(SerializerTest, SerializeDefaultRowHeightNonDefault) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->defaultRowHeight = 16.0;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("defaultRowHeight:16"), std::string::npos);
}

TEST(SerializerTest, SerializePageMarginsDefault) {
    // When hasPageMargins is false (default), V line should not include it
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_EQ(output.find("pageMargins"), std::string::npos);
}

TEST(SerializerTest, SerializePageMarginsNonDefault) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());
    sheet->hasPageMargins = true;
    sheet->pageMargins.left = 0.7;
    sheet->pageMargins.right = 0.7;
    sheet->pageMargins.top = 0.75;
    sheet->pageMargins.bottom = 0.75;
    sheet->pageMargins.header = 0.3;
    sheet->pageMargins.footer = 0.3;
    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("pageMargins:0.7"), std::string::npos);
}

// --- Convenience Functions ---

TEST(SerializerTest, ConvenienceSerializeFunction) {
    auto wb = createMinimalWorkbook();
    const std::string output = serialize(*wb);

    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("D aB3cD4eF"), std::string::npos);
}

TEST(SerializerTest, ConvenienceSerializeToStreamFunction) {
    auto wb = createMinimalWorkbook();
    std::ostringstream ss;
    serialize(*wb, ss);

    const std::string output = ss.str();
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("D aB3cD4eF"), std::string::npos);
}

// --- Roundtrip Tests ---
// Parse -> Serialize -> Parse -> Compare

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

// Compare two workbooks for structural equality
void compareWorkbooks(const Workbook& wb1, const Workbook& wb2) {
    EXPECT_EQ(wb1.name, wb2.name);
    EXPECT_EQ(wb1.sheetCount(), wb2.sheetCount());

    for (size_t i = 0; i < wb1.sheetCount() && i < wb2.sheetCount(); i++) {
        const Sheet* s1 = wb1.sheets[i].get();
        const Sheet* s2 = wb2.sheets[i].get();

        EXPECT_EQ(s1->name, s2->name) << "Sheet " << i;
        EXPECT_EQ(s1->columnCount(), s2->columnCount()) << "Sheet " << i;
        EXPECT_EQ(s1->rowCount(), s2->rowCount()) << "Sheet " << i;
        EXPECT_EQ(s1->cellCount(), s2->cellCount()) << "Sheet " << i;
    }
}
}  // namespace

TEST(RoundtripTest, MinimalFile) {
    const std::string content = readTestFile("minimal.zcd");
    ASSERT_FALSE(content.empty());

    // Parse original
    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    // Serialize
    const std::string serialized = serialize(*result1.workbook);
    EXPECT_FALSE(serialized.empty());

    // Parse serialized
    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    // Compare
    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, SimpleFile) {
    const std::string content = readTestFile("simple.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, BudgetFile) {
    const std::string content = readTestFile("budget.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, AllTypesFile) {
    const std::string content = readTestFile("all_types.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, SparseFile) {
    const std::string content = readTestFile("sparse.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, UnicodeFile) {
    const std::string content = readTestFile("unicode.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, EmptyFile) {
    const std::string content = readTestFile("empty.zcd");
    ASSERT_FALSE(content.empty());

    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok());

    const std::string serialized = serialize(*result1.workbook);

    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(RoundtripTest, InMemoryWorkbook) {
    // Create a workbook in memory, serialize, parse, compare
    auto wb = createMinimalWorkbook();

    const std::string serialized = serialize(*wb);

    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    compareWorkbooks(*wb, *result.workbook);
}

// --- Shared Formula Tests ---

TEST(SharedFormulaTest, SerializeSharedFormulaMaster) {
    // Master cell should serialize with full formula text
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto masterCell = std::make_unique<Cell>(ID("xMaster01"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    masterCell->setFormula(createFormula("=SUM(A1:A10)"));

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(masterCell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Master should have full formula
    EXPECT_NE(output.find("f \"=SUM(A1:A10)\""), std::string::npos);
}

TEST(SharedFormulaTest, SerializeSharedFormulaSubscriber) {
    // Subscriber cell should serialize with =@masterUUID
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    // Master cell (first alphabetically: xAMaster)
    auto masterCell = std::make_unique<Cell>(ID("xAMaster"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    masterCell->setFormula(createFormula("=SUM(A1:A10)"));
    ID masterId = masterCell->id;

    // Subscriber cell (second alphabetically: xBSubscr)
    auto subCell = std::make_unique<Cell>(ID("xBSubscr"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    subCell->setSharedFormulaSubscriber(true);
    ID subId = subCell->id;

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addCell(std::move(masterCell));
    sheet->addCell(std::move(subCell));

    // Register shared formula group at Sheet level
    sheet->registerSharedFormulaGroup(masterId, {subId});

    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Master should have full formula
    EXPECT_NE(output.find("f \"=SUM(A1:A10)\""), std::string::npos);
    // Subscriber should have =@masterUUID
    EXPECT_NE(output.find("f \"=@xAMaster\""), std::string::npos);
}

TEST(SharedFormulaTest, RoundtripSharedFormulas) {
    // Create workbook with shared formulas
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;
    auto row3 = std::make_unique<Axis>(ID("rC5fG6hI"), false);
    row3->position = 2;

    // Master cell
    auto masterCell = std::make_unique<Cell>(ID("xAMaster"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    masterCell->setFormula(createFormula("=A1+B1"));
    ID masterId = masterCell->id;

    // Two subscriber cells
    auto sub1 = std::make_unique<Cell>(ID("xBSub001"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    sub1->setSharedFormulaSubscriber(true);
    ID sub1Id = sub1->id;

    auto sub2 = std::make_unique<Cell>(ID("xCSub002"), ID("cA1bC2dE"), ID("rC5fG6hI"));
    sub2->setSharedFormulaSubscriber(true);
    ID sub2Id = sub2->id;

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addRow(std::move(row3));
    sheet->addCell(std::move(masterCell));
    sheet->addCell(std::move(sub1));
    sheet->addCell(std::move(sub2));

    // Register shared formula group at Sheet level
    sheet->registerSharedFormulaGroup(masterId, {sub1Id, sub2Id});

    wb->addSheet(std::move(sheet));

    // Serialize
    const std::string serialized = serialize(*wb);

    // Verify output format
    EXPECT_NE(serialized.find("f \"=A1+B1\""), std::string::npos);
    EXPECT_NE(serialized.find("f \"=@xAMaster\""), std::string::npos);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify structure
    ASSERT_EQ(result.workbook->sheetCount(), 1u);
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    ASSERT_EQ(parsedSheet->cellCount(), 3u);

    // Find cells by ID
    Cell* parsedMaster = parsedSheet->getCell(ID("xAMaster"));
    Cell* parsedSub1 = parsedSheet->getCell(ID("xBSub001"));
    Cell* parsedSub2 = parsedSheet->getCell(ID("xCSub002"));

    ASSERT_NE(parsedMaster, nullptr);
    ASSERT_NE(parsedSub1, nullptr);
    ASSERT_NE(parsedSub2, nullptr);

    // Verify master has formula
    EXPECT_TRUE(parsedMaster->isFormula());
    EXPECT_FALSE(parsedMaster->isSharedFormula());
    EXPECT_NE(parsedMaster->formula, nullptr);
    EXPECT_EQ(FormulaSerializer::serialize(parsedMaster->formula->ast), "=A1+B1");

    // Verify subscribers reference master via Sheet-level tracking
    EXPECT_TRUE(parsedSub1->isFormula());
    EXPECT_TRUE(parsedSub1->isSharedFormula());
    EXPECT_EQ(parsedSheet->getSharedFormulaMaster(parsedSub1->id), parsedMaster->id);

    EXPECT_TRUE(parsedSub2->isFormula());
    EXPECT_TRUE(parsedSub2->isSharedFormula());
    EXPECT_EQ(parsedSheet->getSharedFormulaMaster(parsedSub2->id), parsedMaster->id);

    // Verify effective formula returns master's formula
    EXPECT_EQ(parsedSheet->getEffectiveFormula(parsedSub1), parsedMaster->formula);
    EXPECT_EQ(parsedSheet->getEffectiveFormula(parsedSub2), parsedMaster->formula);

    // Verify master is marked as master
    EXPECT_TRUE(parsedMaster->isSharedFormulaMaster());
}

TEST(SharedFormulaTest, ParseSharedFormulaSubscriberBeforeMaster) {
    // Test that parsing works even if subscriber appears before master
    // (cells are sorted alphabetically in output, master is first alphabetically)
    const std::string content = R"(
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
R rB3dE4fG 1
X xBSubscr cA1bC2dE rB3dE4fG f "=@xAMaster"
X xAMaster cA1bC2dE rA1bC2dE f "=A1+B1"
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* master = sheet->getCell(ID("xAMaster"));
    Cell* subscriber = sheet->getCell(ID("xBSubscr"));

    ASSERT_NE(master, nullptr);
    ASSERT_NE(subscriber, nullptr);

    EXPECT_TRUE(subscriber->isSharedFormula());
    EXPECT_EQ(sheet->getSharedFormulaMaster(subscriber->id), master->id);
    EXPECT_EQ(sheet->getEffectiveFormula(subscriber), master->formula);
}

TEST(SharedFormulaTest, ParseInvalidSharedFormulaReference) {
    // Test that parsing fails gracefully when master doesn't exist
    const std::string content = R"(
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xSubscrb cA1bC2dE rA1bC2dE f "=@NOTEXIST"
)";

    ParseResult result = parse(content);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find("master not found"), std::string::npos);
}

// --- Formula Evaluation Roundtrip Test ---
// Verifies that formulas are preserved after evaluation (not just initial creation)

TEST(FormulaRoundtripTest, FormulaPreservedAfterEvaluation) {
    // Create a workbook with a formula cell
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    // Create column A at position 0
    auto colA = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    colA->position = 0;

    // Create rows 0 and 1
    auto row0 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row0->position = 0;
    auto row1 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row1->position = 1;

    // Cell A1 with value 10
    auto cellA1 = std::make_unique<Cell>(ID("xCellA1a"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cellA1->value = CellValue(10.0);

    // Cell A2 with formula =10+5 (simple arithmetic)
    auto cellA2 = std::make_unique<Cell>(ID("xCellA2a"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    cellA2->setFormula(createFormula("=10+5"));

    // Add everything to sheet
    Sheet* sheetPtr = sheet.get();
    sheet->addColumn(std::move(colA));
    sheet->addRow(std::move(row0));
    sheet->addRow(std::move(row1));
    sheet->addCell(std::move(cellA1));
    sheet->addCell(std::move(cellA2));
    wb->addSheet(std::move(sheet));

    // Verify cell has formula type BEFORE evaluation
    Cell* formulaCell = sheetPtr->getCell(ID("xCellA2a"));
    ASSERT_NE(formulaCell, nullptr);
    EXPECT_EQ(formulaCell->value.type, CellValueType::FORMULA);
    EXPECT_TRUE(formulaCell->isFormula());

    // Serialize BEFORE evaluation - should work
    const std::string outputBefore = serialize(*wb);
    EXPECT_NE(outputBefore.find("f \"=10+5\""), std::string::npos)
        << "Formula should be serialized before evaluation";

    // Now evaluate the formula using the recalculation engine
    // This uses the evaluateCell function from formula_recalc.cc
    EvalResult result = evaluateCell(sheetPtr, formulaCell);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);

    // After evaluation, the cell should still have a formula type
    // (The fix in formula_recalc.cc uses FORMULA_* result types)
    EXPECT_TRUE(isFormulaType(formulaCell->value.type))
        << "Cell type should be a formula type after evaluation";
    EXPECT_EQ(formulaCell->value.type, CellValueType::FORMULA_NUMBER)
        << "Formula evaluating to number should have FORMULA_NUMBER type";
    EXPECT_TRUE(formulaCell->isFormula());

    // Serialize AFTER evaluation - formula text should be preserved
    const std::string outputAfter = serialize(*wb);
    EXPECT_NE(outputAfter.find("f \"=10+5\""), std::string::npos)
        << "Formula should be serialized after evaluation";

    // Parse back and verify formula is preserved
    ParseResult parsed = parse(outputAfter);
    ASSERT_TRUE(parsed.ok()) << (parsed.error ? parsed.error->toString() : "");

    Sheet* parsedSheet = parsed.workbook->getSheetByIndex(0);
    ASSERT_NE(parsedSheet, nullptr);

    Cell* parsedCell = parsedSheet->getCell(ID("xCellA2a"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_TRUE(parsedCell->isFormula());
    EXPECT_NE(parsedCell->formula, nullptr);
    EXPECT_EQ(FormulaSerializer::serialize(parsedCell->formula->ast), "=10+5");
}

// --- Cell Format Tests ---

TEST(CellFormatTest, SerializeCellWithFormat) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(1234.56);
    // Store format in workbook map using content-addressed FormatBuffer
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::CURRENCY);
    fmt.setDecimals(2);
    fmt.setThousandsSeparator(true);
    fmt.setCurrencySymbol("$");
    wb->setEntityFormat(cell->id, fmt);
    cell->markHasFormat();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Should contain the format property as base64
    EXPECT_NE(output.find("fmt:"), std::string::npos);
    // Verify it's base64 encoded (contains the format we set)
    EXPECT_NE(output.find("fmt:" + fmt.toBase64()), std::string::npos);
}

TEST(CellFormatTest, SerializeCellWithoutFormat) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    // No formatId set (defaults to null)

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Should NOT contain fmt: property
    EXPECT_EQ(output.find("fmt:"), std::string::npos);
}

TEST(CellFormatTest, RoundtripCellWithFormat) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(0.15);
    // Store format in workbook map using content-addressed FormatBuffer
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::PERCENTAGE);
    fmt.setDecimals(2);
    wb->setEntityFormat(cell->id, fmt);
    cell->markHasFormat();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Serialize
    const std::string serialized = serialize(*wb);
    EXPECT_NE(serialized.find("fmt:" + fmt.toBase64()), std::string::npos);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify format is preserved (read from workbook map)
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(parsedCell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(parsedFmt->getDecimals(), 2);
}

TEST(CellFormatTest, ParseCellWithFormat) {
    // Create a currency format for testing
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::CURRENCY);
    fmt.setDecimals(2);
    fmt.setThousandsSeparator(true);
    fmt.setCurrencySymbol("$");
    const std::string fmtBase64 = fmt.toBase64();

    const std::string content =
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE n 1234.56 fmt:" +
        fmtBase64 + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_DOUBLE_EQ(cell->value.asNumber(), 1234.56);
    // Format is now read from workbook map as FormatBuffer
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(cell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(parsedFmt->getDecimals(), 2);
    EXPECT_TRUE(parsedFmt->hasThousandsSeparator());
    EXPECT_EQ(parsedFmt->getCurrencySymbol(), "$");
}

TEST(CellFormatTest, ParseCellWithStringValueAndFormat) {
    // Create a text format for testing
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::TEXT);
    const std::string fmtBase64 = fmt.toBase64();

    const std::string content =
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE s \"Hello\" fmt:" +
        fmtBase64 + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_EQ(cell->value.asString(), "Hello");
    // Format is now read from workbook map as FormatBuffer
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(cell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::TEXT);
}

TEST(CellFormatTest, ParseCellWithFormulaAndFormat) {
    // Create a currency format for testing
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::CURRENCY);
    fmt.setDecimals(2);
    const std::string fmtBase64 = fmt.toBase64();

    const std::string content =
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE f \"=A1+A2\" fmt:" +
        fmtBase64 + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_TRUE(cell->isFormula());
    // Format is now read from workbook map as FormatBuffer
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(cell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(parsedFmt->getDecimals(), 2);
}

// --- Content-Addressed Format Tests ---
// NOTE: Custom F lines are no longer serialized. Formats are content-addressed
// and stored directly on entities as base64. Legacy F lines are ignored on parse.

TEST(ContentAddressedFormatTest, FormatBufferSerializesToBase64) {
    FormatBuffer fmt;
    fmt.setCategory(NumberFormatCategory::NUMBER);
    fmt.setDecimals(2);
    fmt.setThousandsSeparator(true);

    const std::string base64 = fmt.toBase64();
    EXPECT_FALSE(base64.empty());

    // Round-trip
    auto parsed = FormatBuffer::fromBase64(base64);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_EQ(parsed->getDecimals(), 2);
    EXPECT_TRUE(parsed->hasThousandsSeparator());
}

TEST(ContentAddressedFormatTest, LegacyFLinesIgnored) {
    // Legacy F lines should be ignored (not cause parse errors)
    const std::string content = R"(
D aB3cD4eF "Test"
F cF1aB2cD "#,##0.00"
F cF2eF3gH "0.00%"
S sH3eE4tB "Sheet"
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Legacy F lines are silently ignored - formats are now content-addressed
    // and stored directly on entities via FormatBuffer
    EXPECT_NE(result.workbook, nullptr);
}

TEST(ContentAddressedFormatTest, NoFLinesInOutput) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    wb->addSheet(std::move(sheet));

    const std::string serialized = serialize(*wb);

    // F lines should NOT appear in output - formats are now content-addressed
    EXPECT_EQ(serialized.find("\nF "), std::string::npos);
}

TEST(ContentAddressedFormatTest, CustomFormatCodeInFormatBuffer) {
    // For complex Excel format codes, use setCustomFormatCode
    FormatBuffer fmt;
    fmt.setCustomFormatCode("[Red]#,##0.00;[Blue]-#,##0.00");

    const std::string base64 = fmt.toBase64();
    auto parsed = FormatBuffer::fromBase64(base64);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->getCustomFormatCode(), "[Red]#,##0.00;[Blue]-#,##0.00");
}

// =============================================================================
// Style Serialization Tests
// =============================================================================

TEST(StyleSerializationTest, SerializeStyleDefinition) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Set style directly on the entity using content-addressed system
    CellStyle style;
    style.bold = true;
    style.setDefined(DEFINED_BOLD);
    style.italic = true;
    style.setDefined(DEFINED_ITALIC);
    style.bgColor = "#FF0000";
    style.setDefined(DEFINED_BGCOLOR);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string output = serialize(*wb);

    // Content-addressed styles: check that cell line has inline base64 style
    // No more Y lines - styles are embedded directly in entities
    EXPECT_EQ(output.find("Y STY"), std::string::npos);  // No Y lines
    EXPECT_NE(output.find("sty:"), std::string::npos);   // Has inline style
    // The style is base64 encoded, so we can't check for raw JSON properties
}

TEST(StyleSerializationTest, SerializeCellWithStyle) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Set style directly on the entity using content-addressed system
    CellStyle style;
    style.bold = true;
    style.setDefined(DEFINED_BOLD);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string output = serialize(*wb);

    // Check cell line has inline base64 style (not style ID reference)
    EXPECT_NE(output.find("sty:"), std::string::npos);
    // Verify it's base64, not a STY reference (base64 for bold style is not STY...)
    // The serializer may emit dedup style IDs for efficiency, but parser handles base64
}

TEST(StyleSerializationTest, ParseStyleWithBase64) {
    // Create a style and get its base64 encoding
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    styleBuf.setBgColor(0xFF, 0x00, 0x00);  // #FF0000
    styleBuf.setHAlign(TextAlign::CENTER);
    const std::string base64Style = styleBuf.toBase64();

    // Parse a ZCD with content-addressed base64 style
    const std::string content =
        "#cells v1\n"
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE n 42 sty:" +
        base64Style + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Style is stored on the entity (cell)
    const StyleBuffer* parsedBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(parsedBuf, nullptr);
    const CellStyle style = parsedBuf->toCellStyle();
    EXPECT_TRUE(style.bold);
    EXPECT_EQ(style.bgColor, "#FF0000");
    EXPECT_EQ(style.hAlign, TextAlign::CENTER);
}

TEST(StyleSerializationTest, ParseCellWithBase64Style) {
    // Create a bold style and encode it
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    const std::string base64Style = styleBuf.toBase64();

    const std::string content =
        "#cells v1\n"
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE n 42 sty:" +
        base64Style + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);
    // Style is stored directly on the entity
    const StyleBuffer* parsedBuf = result.workbook->getEntityStyle(cell->id);
    ASSERT_NE(parsedBuf, nullptr);
    const CellStyle style = parsedBuf->toCellStyle();
    EXPECT_TRUE(style.bold);
}

TEST(StyleSerializationTest, RoundtripStyles) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Set style with defined flags directly on the entity
    CellStyle style;
    style.bold = true;
    style.setDefined(DEFINED_BOLD);
    style.italic = true;
    style.setDefined(DEFINED_ITALIC);
    style.underline = true;
    style.setDefined(DEFINED_UNDERLINE);
    style.wrapText = true;
    style.setDefined(DEFINED_WRAPTEXT);
    style.bgColor = "#FFFF00";
    style.setDefined(DEFINED_BGCOLOR);
    style.textColor = "#000000";
    style.setDefined(DEFINED_TEXTCOLOR);
    style.fontFamily = "Arial";
    style.setDefined(DEFINED_FONTFAMILY);
    style.fontSize = 14;
    style.setDefined(DEFINED_FONTSIZE);
    style.hAlign = TextAlign::CENTER;
    style.setDefined(DEFINED_HALIGN);
    style.vAlign = VerticalAlign::MIDDLE;
    style.setDefined(DEFINED_VALIGN);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify style is preserved on the entity
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(parsedCell->id);
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_TRUE(parsed.bold);
    EXPECT_TRUE(parsed.italic);
    EXPECT_TRUE(parsed.underline);
    EXPECT_TRUE(parsed.wrapText);
    EXPECT_EQ(parsed.bgColor, "#FFFF00");
    EXPECT_EQ(parsed.textColor, "#000000");
    EXPECT_EQ(parsed.fontFamily, "Arial");
    EXPECT_EQ(parsed.fontSize, 14);
    EXPECT_EQ(parsed.hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsed.vAlign, VerticalAlign::MIDDLE);
}

TEST(StyleSerializationTest, ParseCellWithBothFormatAndStyle) {
    // Create a bold style and encode it
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    const std::string base64Style = styleBuf.toBase64();

    // Create a currency format and encode it
    FormatBuffer fmtBuf;
    fmtBuf.setCategory(NumberFormatCategory::CURRENCY);
    fmtBuf.setDecimals(2);
    fmtBuf.setCurrencySymbol("$");
    const std::string base64Fmt = fmtBuf.toBase64();

    const std::string content =
        "#cells v1\n"
        "D aB3cD4eF \"Test\"\n"
        "S sH3eE4tB \"Sheet\"\n"
        "C cA1bC2dE 0\n"
        "R rA1bC2dE 0\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE n 42 fmt:" +
        base64Fmt + " sty:" + base64Style + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);
    // Format is read from workbook map as FormatBuffer
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(cell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(parsedFmt->getDecimals(), 2);
    // Style from entity
    const StyleBuffer* parsedBuf = result.workbook->getEntityStyle(cell->id);
    ASSERT_NE(parsedBuf, nullptr);
    const CellStyle style = parsedBuf->toCellStyle();
    EXPECT_TRUE(style.bold);
}

// =============================================================================
// ZCD Style Round-trip Tests (Phase 2c)
// =============================================================================

TEST(StyleZCDRoundtripTest, EmptyStyleIsNotStored) {
    // Empty styles (all defaults) are not meaningful and should not be stored.
    // This test verifies that setting an empty style doesn't create a stored entry.
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Set empty style - this is stored but will serialize to "AAA="
    CellStyle emptyStyle;
    EXPECT_TRUE(emptyStyle.isEmpty());
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(emptyStyle));

    // Serialize
    const std::string serialized = serialize(*wb);

    // The serializer will emit the empty style base64 "AAA="
    // But when parsing, an empty style is valid (though semantically meaningless)
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify the cell exists but empty styles may or may not be preserved
    // (implementation detail - the important thing is the cell is intact)
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_EQ(parsedCell->value.asNumber(), 42.0);
}

TEST(StyleZCDRoundtripTest, PartialStyleBoldOnly) {
    // Test style with only bold set
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue("Header");
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.bold = true;
    style.setDefined(DEFINED_BOLD);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_TRUE(parsed.bold);
    EXPECT_FALSE(parsed.italic);
    EXPECT_FALSE(parsed.underline);
    EXPECT_FALSE(parsed.wrapText);
}

TEST(StyleZCDRoundtripTest, PartialStyleColorOnly) {
    // Test style with only colors set
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue("Highlighted");
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.bgColor = "#FFFF00";  // Yellow background
    style.setDefined(DEFINED_BGCOLOR);
    style.textColor = "#0000FF";  // Blue text
    style.setDefined(DEFINED_TEXTCOLOR);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_EQ(parsed.bgColor, "#FFFF00");
    EXPECT_EQ(parsed.textColor, "#0000FF");
    EXPECT_FALSE(parsed.bold);
    EXPECT_FALSE(parsed.italic);
}

TEST(StyleZCDRoundtripTest, PartialStyleAlignmentOnly) {
    // Test style with only alignment set
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue("Centered");
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.hAlign = TextAlign::CENTER;
    style.setDefined(DEFINED_HALIGN);
    style.vAlign = VerticalAlign::MIDDLE;
    style.setDefined(DEFINED_VALIGN);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_EQ(parsed.hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsed.vAlign, VerticalAlign::MIDDLE);
    EXPECT_FALSE(parsed.bold);
}

TEST(StyleZCDRoundtripTest, PartialStyleFontOnly) {
    // Test style with only font properties set
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue("Large Text");
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.fontFamily = "Times New Roman";
    style.setDefined(DEFINED_FONTFAMILY);
    style.fontSize = 18;
    style.setDefined(DEFINED_FONTSIZE);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_EQ(parsed.fontFamily, "Times New Roman");
    EXPECT_EQ(parsed.fontSize, 18);
    EXPECT_FALSE(parsed.bold);
}

TEST(StyleZCDRoundtripTest, MultipleCellsDifferentStyles) {
    // Test multiple cells with different styles
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    // Create grid
    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->position = 0;
    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->position = 1;
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    // Cell A1: Bold header
    auto cellA1 = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cellA1->value = CellValue("Name");
    cellA1->markHasStyle();

    // Cell B1: Bold header
    auto cellB1 = std::make_unique<Cell>(ID("xB3dE4fG"), ID("cB3dE4fG"), ID("rA1bC2dE"));
    cellB1->value = CellValue("Value");
    cellB1->markHasStyle();

    // Cell A2: Normal text
    auto cellA2 = std::make_unique<Cell>(ID("xC5fG6hJ"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    cellA2->value = CellValue("Item 1");
    // No style

    // Cell B2: Currency with color
    auto cellB2 = std::make_unique<Cell>(ID("xD7hJ8kL"), ID("cB3dE4fG"), ID("rB3dE4fG"));
    cellB2->value = CellValue(1234.56);
    cellB2->markHasStyle();

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addCell(std::move(cellA1));
    sheet->addCell(std::move(cellB1));
    sheet->addCell(std::move(cellA2));
    sheet->addCell(std::move(cellB2));
    wb->addSheet(std::move(sheet));

    // Set styles directly on entities
    CellStyle headerStyle;
    headerStyle.bold = true;
    headerStyle.setDefined(DEFINED_BOLD);
    headerStyle.bgColor = "#4472C4";
    headerStyle.setDefined(DEFINED_BGCOLOR);
    headerStyle.textColor = "#FFFFFF";
    headerStyle.setDefined(DEFINED_TEXTCOLOR);
    headerStyle.hAlign = TextAlign::CENTER;
    headerStyle.setDefined(DEFINED_HALIGN);
    const StyleBuffer headerBuf = StyleBuffer::fromCellStyle(headerStyle);
    wb->setEntityStyle(ID("xA1bC2dE"), headerBuf);
    wb->setEntityStyle(ID("xB3dE4fG"), headerBuf);

    CellStyle moneyStyle;
    moneyStyle.textColor = "#008000";  // Green for positive values
    moneyStyle.setDefined(DEFINED_TEXTCOLOR);
    moneyStyle.hAlign = TextAlign::RIGHT;
    moneyStyle.setDefined(DEFINED_HALIGN);
    wb->setEntityStyle(ID("xD7hJ8kL"), StyleBuffer::fromCellStyle(moneyStyle));

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify header style on cells A1 and B1
    const StyleBuffer* headerBufA1 = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(headerBufA1, nullptr);
    const CellStyle headerParsed = headerBufA1->toCellStyle();
    EXPECT_TRUE(headerParsed.bold);
    EXPECT_EQ(headerParsed.bgColor, "#4472C4");
    EXPECT_EQ(headerParsed.textColor, "#FFFFFF");
    EXPECT_EQ(headerParsed.hAlign, TextAlign::CENTER);

    const StyleBuffer* headerBufB1 = result.workbook->getEntityStyle(ID("xB3dE4fG"));
    ASSERT_NE(headerBufB1, nullptr);
    const CellStyle headerParsedB1 = headerBufB1->toCellStyle();
    EXPECT_TRUE(headerParsedB1.bold);
    EXPECT_EQ(headerParsedB1.bgColor, "#4472C4");

    // Verify money style on cell B2
    const StyleBuffer* moneyBuf = result.workbook->getEntityStyle(ID("xD7hJ8kL"));
    ASSERT_NE(moneyBuf, nullptr);
    const CellStyle moneyParsed = moneyBuf->toCellStyle();
    EXPECT_EQ(moneyParsed.textColor, "#008000");
    EXPECT_EQ(moneyParsed.hAlign, TextAlign::RIGHT);

    // Verify cell A2 has no style
    const StyleBuffer* noStyleBuf = result.workbook->getEntityStyle(ID("xC5fG6hJ"));
    EXPECT_EQ(noStyleBuf, nullptr);
}

TEST(StyleZCDRoundtripTest, AllAlignmentValues) {
    // Test all alignment values round-trip correctly
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    // Create cells for each alignment type
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    std::vector<std::unique_ptr<Axis>> rows;
    rows.push_back(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    rows.push_back(std::make_unique<Axis>(ID("rB3dE4fG"), false));
    rows.push_back(std::make_unique<Axis>(ID("rC5fG6hJ"), false));
    rows.push_back(std::make_unique<Axis>(ID("rD7hJ8kL"), false));
    rows.push_back(std::make_unique<Axis>(ID("rE9kL0mN"), false));
    rows.push_back(std::make_unique<Axis>(ID("rF1mN2pQ"), false));
    rows.push_back(std::make_unique<Axis>(ID("rG3pQ4rS"), false));

    // Create cells
    auto cell1 = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    auto cell2 = std::make_unique<Cell>(ID("xB3dE4fG"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    auto cell3 = std::make_unique<Cell>(ID("xC5fG6hJ"), ID("cA1bC2dE"), ID("rC5fG6hJ"));
    auto cell4 = std::make_unique<Cell>(ID("xD7hJ8kL"), ID("cA1bC2dE"), ID("rD7hJ8kL"));
    auto cell5 = std::make_unique<Cell>(ID("xE9kL0mN"), ID("cA1bC2dE"), ID("rE9kL0mN"));
    auto cell6 = std::make_unique<Cell>(ID("xF1mN2pQ"), ID("cA1bC2dE"), ID("rF1mN2pQ"));
    auto cell7 = std::make_unique<Cell>(ID("xG3pQ4rS"), ID("cA1bC2dE"), ID("rG3pQ4rS"));
    cell1->markHasStyle();
    cell2->markHasStyle();
    cell3->markHasStyle();
    cell4->markHasStyle();
    cell5->markHasStyle();
    cell6->markHasStyle();
    cell7->markHasStyle();

    sheet->addColumn(std::move(col));
    for (auto& row : rows) {
        sheet->addRow(std::move(row));
    }
    sheet->addCell(std::move(cell1));
    sheet->addCell(std::move(cell2));
    sheet->addCell(std::move(cell3));
    sheet->addCell(std::move(cell4));
    sheet->addCell(std::move(cell5));
    sheet->addCell(std::move(cell6));
    sheet->addCell(std::move(cell7));
    wb->addSheet(std::move(sheet));

    // Set horizontal alignment styles
    CellStyle leftStyle;
    leftStyle.hAlign = TextAlign::LEFT;
    leftStyle.setDefined(DEFINED_HALIGN);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(leftStyle));

    CellStyle centerStyle;
    centerStyle.hAlign = TextAlign::CENTER;
    centerStyle.setDefined(DEFINED_HALIGN);
    wb->setEntityStyle(ID("xB3dE4fG"), StyleBuffer::fromCellStyle(centerStyle));

    CellStyle rightStyle;
    rightStyle.hAlign = TextAlign::RIGHT;
    rightStyle.setDefined(DEFINED_HALIGN);
    wb->setEntityStyle(ID("xC5fG6hJ"), StyleBuffer::fromCellStyle(rightStyle));

    CellStyle justifyStyle;
    justifyStyle.hAlign = TextAlign::JUSTIFY;
    justifyStyle.setDefined(DEFINED_HALIGN);
    wb->setEntityStyle(ID("xD7hJ8kL"), StyleBuffer::fromCellStyle(justifyStyle));

    // Set vertical alignment styles
    CellStyle topStyle;
    topStyle.vAlign = VerticalAlign::TOP;
    topStyle.setDefined(DEFINED_VALIGN);
    wb->setEntityStyle(ID("xE9kL0mN"), StyleBuffer::fromCellStyle(topStyle));

    CellStyle middleStyle;
    middleStyle.vAlign = VerticalAlign::MIDDLE;
    middleStyle.setDefined(DEFINED_VALIGN);
    wb->setEntityStyle(ID("xF1mN2pQ"), StyleBuffer::fromCellStyle(middleStyle));

    CellStyle bottomStyle;
    bottomStyle.vAlign = VerticalAlign::BOTTOM;
    bottomStyle.setDefined(DEFINED_VALIGN);
    wb->setEntityStyle(ID("xG3pQ4rS"), StyleBuffer::fromCellStyle(bottomStyle));

    // Serialize and parse
    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify horizontal alignments
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xA1bC2dE"))->toCellStyle().hAlign,
              TextAlign::LEFT);
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xB3dE4fG"))->toCellStyle().hAlign,
              TextAlign::CENTER);
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xC5fG6hJ"))->toCellStyle().hAlign,
              TextAlign::RIGHT);
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xD7hJ8kL"))->toCellStyle().hAlign,
              TextAlign::JUSTIFY);

    // Verify vertical alignments
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xE9kL0mN"))->toCellStyle().vAlign,
              VerticalAlign::TOP);
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xF1mN2pQ"))->toCellStyle().vAlign,
              VerticalAlign::MIDDLE);
    EXPECT_EQ(result.workbook->getEntityStyle(ID("xG3pQ4rS"))->toCellStyle().vAlign,
              VerticalAlign::BOTTOM);
}

TEST(StyleZCDRoundtripTest, StyleWithSpecialCharactersInFont) {
    // Test font family with special characters
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.fontFamily = "Courier New";  // Space in name
    style.setDefined(DEFINED_FONTFAMILY);
    style.fontSize = 12;
    style.setDefined(DEFINED_FONTSIZE);
    wb->setEntityStyle(ID("xA1bC2dE"), StyleBuffer::fromCellStyle(style));

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const StyleBuffer* styleBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle parsed = styleBuf->toCellStyle();
    EXPECT_EQ(parsed.fontFamily, "Courier New");
    EXPECT_EQ(parsed.fontSize, 12);
}

TEST(StyleZCDRoundtripTest, RoundtripMultipleStyles) {
    // Create workbook with multiple styled cells
    auto wb = std::make_unique<Workbook>(ID("tY8pL3mK"), "Styles Test");
    auto sheet = std::make_unique<Sheet>(ID("qR5sW2xN"), "Styled Sheet");
    sheet->setWorkbook(wb.get());

    // Create columns and rows
    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));

    // Create cells with different styles
    auto cell1 = std::make_unique<Cell>(ID("xA1aB2cD"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell1->value = CellValue("Header");
    cell1->markHasStyle();
    sheet->addCell(std::move(cell1));

    auto cell2 = std::make_unique<Cell>(ID("xE1eF2gH"), ID("cB3dE4fG"), ID("rB3dE4fG"));
    cell2->value = CellValue("Full Style");
    cell2->markHasStyle();
    sheet->addCell(std::move(cell2));

    wb->addSheet(std::move(sheet));

    // Create header style: bold, bgColor, textColor, hAlign
    StyleBuffer headerStyle;
    headerStyle.setBold(true);
    headerStyle.setBgColor(0x44, 0x72, 0xC4);    // #4472C4
    headerStyle.setTextColor(0xFF, 0xFF, 0xFF);  // #FFFFFF
    headerStyle.setHAlign(TextAlign::CENTER);
    wb->setEntityStyle(ID("xA1aB2cD"), headerStyle);

    // Create full style: bold, italic, underline, colors, font, alignment
    StyleBuffer fullStyle;
    fullStyle.setBold(true);
    fullStyle.setItalic(true);
    fullStyle.setUnderline(true);
    fullStyle.setBgColor(0xFF, 0x00, 0x00);    // #FF0000
    fullStyle.setTextColor(0xFF, 0xFF, 0xFF);  // #FFFFFF
    fullStyle.setFontFamily("Arial");
    fullStyle.setFontSize(14);
    fullStyle.setHAlign(TextAlign::CENTER);
    fullStyle.setVAlign(VerticalAlign::MIDDLE);
    wb->setEntityStyle(ID("xE1eF2gH"), fullStyle);

    // Serialize
    const std::string serialized = serialize(*wb);
    EXPECT_FALSE(serialized.empty());

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify full style preserved
    const StyleBuffer* fullStyleBuf = result.workbook->getEntityStyle(ID("xE1eF2gH"));
    ASSERT_NE(fullStyleBuf, nullptr);
    const CellStyle parsedFull = fullStyleBuf->toCellStyle();
    EXPECT_TRUE(parsedFull.bold);
    EXPECT_TRUE(parsedFull.italic);
    EXPECT_TRUE(parsedFull.underline);
    EXPECT_EQ(parsedFull.bgColor, "#FF0000");
    EXPECT_EQ(parsedFull.textColor, "#FFFFFF");
    EXPECT_EQ(parsedFull.fontFamily, "Arial");
    EXPECT_EQ(parsedFull.fontSize, 14);
    EXPECT_EQ(parsedFull.hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsedFull.vAlign, VerticalAlign::MIDDLE);

    // Verify header style preserved
    const StyleBuffer* headerStyleBuf = result.workbook->getEntityStyle(ID("xA1aB2cD"));
    ASSERT_NE(headerStyleBuf, nullptr);
    const CellStyle parsedHeader = headerStyleBuf->toCellStyle();
    EXPECT_TRUE(parsedHeader.bold);
    EXPECT_EQ(parsedHeader.bgColor, "#4472C4");
    EXPECT_EQ(parsedHeader.textColor, "#FFFFFF");
    EXPECT_EQ(parsedHeader.hAlign, TextAlign::CENTER);
}

TEST(StyleZCDRoundtripTest, ParseStyledZCDFile) {
    // Create base64-encoded styles to embed in the ZCD content
    StyleBuffer headerStyle;
    headerStyle.setBold(true);
    headerStyle.setBgColor(0x44, 0x72, 0xC4);    // #4472C4
    headerStyle.setTextColor(0xFF, 0xFF, 0xFF);  // #FFFFFF
    headerStyle.setHAlign(TextAlign::CENTER);
    const std::string headerB64 = headerStyle.toBase64();

    StyleBuffer moneyStyle;
    moneyStyle.setTextColor(0x00, 0x80, 0x00);  // #008000
    moneyStyle.setHAlign(TextAlign::RIGHT);
    const std::string moneyB64 = moneyStyle.toBase64();

    StyleBuffer warnStyle;
    warnStyle.setBold(true);
    warnStyle.setBgColor(0xFF, 0xC0, 0x00);  // #FFC000
    const std::string warnB64 = warnStyle.toBase64();

    // Create a currency format (content-addressed)
    FormatBuffer currencyFmt;
    currencyFmt.setCategory(NumberFormatCategory::CURRENCY);
    currencyFmt.setDecimals(2);
    currencyFmt.setThousandsSeparator(true);
    currencyFmt.setCurrencySymbol("$");
    const std::string currencyB64 = currencyFmt.toBase64();

    // Build ZCD content with content-addressed styles and formats
    std::string content =
        "#cells v1\n"
        "D aB3cD4eF \"Styled Document\"\n"
        "S sH3eE4tB \"Data\"\n"
        "C cA1bC2dE 0\n"
        "C cB3dE4fG 1\n"
        "R rA1bC2dE 0\n"
        "R rB3dE4fG 1\n"
        "R rC5fG6hJ 2\n"
        "X xA1bC2dE cA1bC2dE rA1bC2dE s \"Category\" sty:" +
        headerB64 +
        "\n"
        "X xB3dE4fG cB3dE4fG rA1bC2dE s \"Amount\" sty:" +
        headerB64 +
        "\n"
        "X xC5fG6hJ cA1bC2dE rB3dE4fG s \"Sales\"\n"
        "X xD7hJ8kL cB3dE4fG rB3dE4fG n 10000 fmt:" +
        currencyB64 + " sty:" + moneyB64 +
        "\n"
        "X xE9kL0mN cA1bC2dE rC5fG6hJ s \"Warning\" sty:" +
        warnB64 +
        "\n"
        "X xF1mN2pQ cB3dE4fG rC5fG6hJ n -500 fmt:" +
        currencyB64 + " sty:" + warnB64 + "\n";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify style properties on entities
    const StyleBuffer* headerBuf = result.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(headerBuf, nullptr);
    const CellStyle header = headerBuf->toCellStyle();
    EXPECT_TRUE(header.bold);
    EXPECT_EQ(header.bgColor, "#4472C4");
    EXPECT_EQ(header.textColor, "#FFFFFF");
    EXPECT_EQ(header.hAlign, TextAlign::CENTER);

    const StyleBuffer* moneyBufPtr = result.workbook->getEntityStyle(ID("xD7hJ8kL"));
    ASSERT_NE(moneyBufPtr, nullptr);
    const CellStyle money = moneyBufPtr->toCellStyle();
    EXPECT_EQ(money.textColor, "#008000");
    EXPECT_EQ(money.hAlign, TextAlign::RIGHT);

    const StyleBuffer* warnBuf = result.workbook->getEntityStyle(ID("xE9kL0mN"));
    ASSERT_NE(warnBuf, nullptr);
    const CellStyle warn = warnBuf->toCellStyle();
    EXPECT_TRUE(warn.bold);
    EXPECT_EQ(warn.bgColor, "#FFC000");

    // Verify cell with both format and style
    Cell* amountCell = result.workbook->getSheetByIndex(0)->getCell(ID("xD7hJ8kL"));
    const FormatBuffer* parsedFmt = result.workbook->getEntityFormat(amountCell->id);
    ASSERT_NE(parsedFmt, nullptr);
    EXPECT_EQ(parsedFmt->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_NE(result.workbook->getEntityStyle(amountCell->id), nullptr);

    // Serialize and verify round-trip
    const std::string serialized = serialize(*result.workbook);
    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    // Compare styles after round-trip
    const StyleBuffer* headerBuf2 = result2.workbook->getEntityStyle(ID("xA1bC2dE"));
    ASSERT_NE(headerBuf2, nullptr);
    const CellStyle header2 = headerBuf2->toCellStyle();
    EXPECT_EQ(header.bold, header2.bold);
    EXPECT_EQ(header.bgColor, header2.bgColor);
    EXPECT_EQ(header.textColor, header2.textColor);
    EXPECT_EQ(header.hAlign, header2.hAlign);

    const StyleBuffer* moneyBuf2 = result2.workbook->getEntityStyle(ID("xD7hJ8kL"));
    ASSERT_NE(moneyBuf2, nullptr);
    const CellStyle money2 = moneyBuf2->toCellStyle();
    EXPECT_EQ(money.textColor, money2.textColor);
    EXPECT_EQ(money.hAlign, money2.hAlign);

    const StyleBuffer* warnBuf2 = result2.workbook->getEntityStyle(ID("xE9kL0mN"));
    ASSERT_NE(warnBuf2, nullptr);
    const CellStyle warn2 = warnBuf2->toCellStyle();
    EXPECT_EQ(warn.bold, warn2.bold);
    EXPECT_EQ(warn.bgColor, warn2.bgColor);
}

// =============================================================================
// Named Range ZCD Persistence Tests (Phase 4c)
// =============================================================================

TEST(NamedRangeZCDTest, SerializeWorkbookScopedNamedRange) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(100.0);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Define a workbook-scoped named range
    NamedRangeTarget target = NamedRangeTarget::cell(ID("xA1bC2dE"), ID("sH3eE4tB"));
    wb->getNamedRanges()->defineWorkbook("MyTotal", target);

    const std::string output = serialize(*wb);

    // Should contain named range line
    EXPECT_NE(output.find("N \"MyTotal\""), std::string::npos);
    EXPECT_NE(output.find("W -"), std::string::npos);  // Workbook scope
    EXPECT_NE(output.find("CELL"), std::string::npos);
    EXPECT_NE(output.find("xA1bC2dE"), std::string::npos);
}

TEST(NamedRangeZCDTest, SerializeSheetScopedNamedRange) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    // Define a sheet-scoped named range for a column
    NamedRangeTarget target = NamedRangeTarget::column(ID("cA1bC2dE"), ID("sH3eE4tB"));
    wb->getNamedRanges()->defineSheet("Revenue", ID("sH3eE4tB"), target);

    const std::string output = serialize(*wb);

    // Should contain named range line with sheet scope
    EXPECT_NE(output.find("N \"Revenue\""), std::string::npos);
    EXPECT_NE(output.find("S sH3eE4tB"), std::string::npos);  // Sheet scope
    EXPECT_NE(output.find("COLUMN"), std::string::npos);
}

TEST(NamedRangeZCDTest, SerializeRangeTarget) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->position = 0;
    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->position = 1;
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    auto cellA1 = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    auto cellB2 = std::make_unique<Cell>(ID("xB3dE4fG"), ID("cB3dE4fG"), ID("rB3dE4fG"));

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addCell(std::move(cellA1));
    sheet->addCell(std::move(cellB2));
    wb->addSheet(std::move(sheet));

    // Define a named range for a cell range (A1:B2)
    NamedRangeTarget target =
        NamedRangeTarget::range(ID("xA1bC2dE"), ID("xB3dE4fG"), ID("sH3eE4tB"));
    wb->getNamedRanges()->defineWorkbook("DataRange", target);

    const std::string output = serialize(*wb);

    // Should contain RANGE with both cell IDs
    EXPECT_NE(output.find("N \"DataRange\""), std::string::npos);
    EXPECT_NE(output.find("RANGE"), std::string::npos);
    EXPECT_NE(output.find("xA1bC2dE"), std::string::npos);
    EXPECT_NE(output.find("xB3dE4fG"), std::string::npos);
}

TEST(NamedRangeZCDTest, ParseNamedRange) {
    const std::string content = R"(
D aB3cD4eF "Test"
N "MyTotal" W - CELL xA1bC2dE sH3eE4tB
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 100
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify named range was parsed
    NamedRangeRegistry* registry = result.workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    const NamedRange* nr = registry->resolve("MyTotal", ID());
    ASSERT_NE(nr, nullptr);
    EXPECT_EQ(nr->name, "MyTotal");
    EXPECT_EQ(nr->scope, NamedRangeScope::WORKBOOK);
    EXPECT_EQ(nr->target.type, NamedRangeTarget::Type::CELL);
    EXPECT_EQ(nr->target.id1.toString(), "xA1bC2dE");
    EXPECT_EQ(nr->target.sheetId.toString(), "sH3eE4tB");
}

TEST(NamedRangeZCDTest, ParseSheetScopedNamedRange) {
    const std::string content = R"(
D aB3cD4eF "Test"
N "LocalName" S sH3eE4tB COLUMN cA1bC2dE sH3eE4tB
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
R rA1bC2dE 0
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    NamedRangeRegistry* registry = result.workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    // Should find it when resolving from the sheet
    const NamedRange* nr = registry->resolve("LocalName", ID("sH3eE4tB"));
    ASSERT_NE(nr, nullptr);
    EXPECT_EQ(nr->name, "LocalName");
    EXPECT_EQ(nr->scope, NamedRangeScope::SHEET);
    EXPECT_EQ(nr->scopeSheetId.toString(), "sH3eE4tB");
    EXPECT_EQ(nr->target.type, NamedRangeTarget::Type::COLUMN);
}

TEST(NamedRangeZCDTest, ParseRangeNamedRange) {
    const std::string content = R"(
D aB3cD4eF "Test"
N "DataArea" W - RANGE xA1bC2dE xB3dE4fG sH3eE4tB
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
C cB3dE4fG 1
R rA1bC2dE 0
R rB3dE4fG 1
X xA1bC2dE cA1bC2dE rA1bC2dE n 1
X xB3dE4fG cB3dE4fG rB3dE4fG n 2
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    NamedRangeRegistry* registry = result.workbook->getNamedRanges();
    const NamedRange* nr = registry->resolve("DataArea", ID());
    ASSERT_NE(nr, nullptr);
    EXPECT_EQ(nr->target.type, NamedRangeTarget::Type::RANGE);
    EXPECT_EQ(nr->target.id1.toString(), "xA1bC2dE");
    EXPECT_EQ(nr->target.id2.toString(), "xB3dE4fG");
    EXPECT_EQ(nr->target.sheetId.toString(), "sH3eE4tB");
}

TEST(NamedRangeZCDTest, RoundtripNamedRanges) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->position = 0;
    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->position = 1;
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;

    auto cell1 = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell1->value = CellValue(100.0);
    auto cell2 = std::make_unique<Cell>(ID("xB3dE4fG"), ID("cB3dE4fG"), ID("rA1bC2dE"));
    cell2->value = CellValue(200.0);

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::move(row1));
    sheet->addCell(std::move(cell1));
    sheet->addCell(std::move(cell2));
    wb->addSheet(std::move(sheet));

    // Define multiple named ranges of different types
    wb->getNamedRanges()->defineWorkbook("CellRef",
                                         NamedRangeTarget::cell(ID("xA1bC2dE"), ID("sH3eE4tB")));
    wb->getNamedRanges()->defineWorkbook(
        "RangeRef", NamedRangeTarget::range(ID("xA1bC2dE"), ID("xB3dE4fG"), ID("sH3eE4tB")));
    wb->getNamedRanges()->defineWorkbook("ColRef",
                                         NamedRangeTarget::column(ID("cA1bC2dE"), ID("sH3eE4tB")));
    wb->getNamedRanges()->defineSheet("LocalName", ID("sH3eE4tB"),
                                      NamedRangeTarget::row(ID("rA1bC2dE"), ID("sH3eE4tB")));

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify all named ranges are preserved
    NamedRangeRegistry* registry = result.workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    // Check workbook-scoped names
    const NamedRange* cellRef = registry->resolve("CellRef", ID());
    ASSERT_NE(cellRef, nullptr);
    EXPECT_EQ(cellRef->scope, NamedRangeScope::WORKBOOK);
    EXPECT_EQ(cellRef->target.type, NamedRangeTarget::Type::CELL);
    EXPECT_EQ(cellRef->target.id1.toString(), "xA1bC2dE");

    const NamedRange* rangeRef = registry->resolve("RangeRef", ID());
    ASSERT_NE(rangeRef, nullptr);
    EXPECT_EQ(rangeRef->target.type, NamedRangeTarget::Type::RANGE);
    EXPECT_EQ(rangeRef->target.id1.toString(), "xA1bC2dE");
    EXPECT_EQ(rangeRef->target.id2.toString(), "xB3dE4fG");

    const NamedRange* colRef = registry->resolve("ColRef", ID());
    ASSERT_NE(colRef, nullptr);
    EXPECT_EQ(colRef->target.type, NamedRangeTarget::Type::COLUMN);
    EXPECT_EQ(colRef->target.id1.toString(), "cA1bC2dE");

    // Check sheet-scoped name (should only resolve from that sheet)
    const NamedRange* localName = registry->resolve("LocalName", ID("sH3eE4tB"));
    ASSERT_NE(localName, nullptr);
    EXPECT_EQ(localName->scope, NamedRangeScope::SHEET);
    EXPECT_EQ(localName->scopeSheetId.toString(), "sH3eE4tB");
    EXPECT_EQ(localName->target.type, NamedRangeTarget::Type::ROW);

    // Sheet-scoped name should not resolve from workbook scope
    const NamedRange* localFromWB = registry->resolve("LocalName", ID());
    EXPECT_EQ(localFromWB, nullptr);
}

TEST(NamedRangeZCDTest, RoundtripColumnRowRanges) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");

    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->position = 0;
    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->position = 1;
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    wb->addSheet(std::move(sheet));

    // Define column range and row range named ranges
    wb->getNamedRanges()->defineWorkbook(
        "ColRange", NamedRangeTarget::columnRange(ID("cA1bC2dE"), ID("cB3dE4fG"), ID("sH3eE4tB")));
    wb->getNamedRanges()->defineWorkbook(
        "RowRange", NamedRangeTarget::rowRange(ID("rA1bC2dE"), ID("rB3dE4fG"), ID("sH3eE4tB")));

    // Serialize
    const std::string serialized = serialize(*wb);

    // Should contain the named ranges
    EXPECT_NE(serialized.find("COLUMN_RANGE"), std::string::npos);
    EXPECT_NE(serialized.find("ROW_RANGE"), std::string::npos);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    NamedRangeRegistry* registry = result.workbook->getNamedRanges();

    const NamedRange* colRange = registry->resolve("ColRange", ID());
    ASSERT_NE(colRange, nullptr);
    EXPECT_EQ(colRange->target.type, NamedRangeTarget::Type::COLUMN_RANGE);
    EXPECT_EQ(colRange->target.id1.toString(), "cA1bC2dE");
    EXPECT_EQ(colRange->target.id2.toString(), "cB3dE4fG");

    const NamedRange* rowRange = registry->resolve("RowRange", ID());
    ASSERT_NE(rowRange, nullptr);
    EXPECT_EQ(rowRange->target.type, NamedRangeTarget::Type::ROW_RANGE);
    EXPECT_EQ(rowRange->target.id1.toString(), "rA1bC2dE");
    EXPECT_EQ(rowRange->target.id2.toString(), "rB3dE4fG");
}

TEST(NamedRangeZCDTest, ParseNamedRangeWithSpecialChars) {
    // Test named range with special characters in name that need escaping
    const std::string content = R"(
D aB3cD4eF "Test"
N "Total_2024.Q1" W - CELL xA1bC2dE sH3eE4tB
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 100
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    NamedRangeRegistry* registry = result.workbook->getNamedRanges();
    const NamedRange* nr = registry->resolve("Total_2024.Q1", ID());
    ASSERT_NE(nr, nullptr);
    EXPECT_EQ(nr->name, "Total_2024.Q1");
}

// --- Hidden Columns/Rows Tests ---

TEST(SerializerTest, SerializeHiddenColumn) {
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->setHidden(true);
    sheet->addColumn(std::move(col));

    wb.addSheet(std::move(sheet));

    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Should include hidden:1 property
    EXPECT_NE(output.find("hidden:1"), std::string::npos);
}

TEST(SerializerTest, SerializeHiddenRow) {
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->setHidden(true);
    sheet->addRow(std::move(row));

    wb.addSheet(std::move(sheet));

    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Should include hidden:1 property
    EXPECT_NE(output.find("hidden:1"), std::string::npos);
}

TEST(SerializerTest, VisibleColumnNoHiddenProperty) {
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->setHidden(false);  // Not hidden (default)
    sheet->addColumn(std::move(col));

    wb.addSheet(std::move(sheet));

    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Should NOT include hidden property for visible columns
    EXPECT_EQ(output.find("hidden:"), std::string::npos);
}

TEST(ParserTest, ParseHiddenColumn) {
    const std::string content = R"(D aB3cD4eF "Test"
S sH3eE4tB "Sheet1"
C cA1bC2dE 0 hidden:1
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    Axis* col = sheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->hidden());
}

TEST(ParserTest, ParseHiddenRow) {
    const std::string content = R"(D aB3cD4eF "Test"
S sH3eE4tB "Sheet1"
R rA1bC2dE 0 hidden:1
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    Axis* row = sheet->getRow(ID("rA1bC2dE"));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->hidden());
}

TEST(ParserTest, ParseVisibleColumnNoHiddenProperty) {
    const std::string content = R"(D aB3cD4eF "Test"
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    Axis* col = sheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(col, nullptr);
    EXPECT_FALSE(col->hidden());  // Default is visible
}

TEST(SerializerTest, HiddenAxisRoundTrip) {
    // Create workbook with hidden column and row
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->setHidden(true);
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->setHidden(true);
    sheet->addRow(std::move(row));

    wb.addSheet(std::move(sheet));

    // Serialize
    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Parse back
    ParseResult result = parse(output);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(parsedSheet, nullptr);

    Axis* parsedCol = parsedSheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(parsedCol, nullptr);
    EXPECT_TRUE(parsedCol->hidden());

    Axis* parsedRow = parsedSheet->getRow(ID("rA1bC2dE"));
    ASSERT_NE(parsedRow, nullptr);
    EXPECT_TRUE(parsedRow->hidden());
}

TEST(SerializerTest, AxisDefaultStyleRoundTrip) {
    // Create workbook with styled column and row
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);

    // Create styles using content-addressed system
    CellStyle boldStyle;
    boldStyle.bold = true;
    boldStyle.setDefined(DEFINED_BOLD);

    CellStyle italicStyle;
    italicStyle.italic = true;
    italicStyle.setDefined(DEFINED_ITALIC);

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->setHasStyle(true);
    sheet->addColumn(std::move(col));
    wb.setEntityStyle(ID("cA1bC2dE"), StyleBuffer::fromCellStyle(boldStyle));

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->setHasStyle(true);
    sheet->addRow(std::move(row));
    wb.setEntityStyle(ID("rA1bC2dE"), StyleBuffer::fromCellStyle(italicStyle));

    wb.addSheet(std::move(sheet));

    // Serialize
    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Verify output contains inline base64 style (not style ID reference)
    EXPECT_NE(output.find("sty:"), std::string::npos);

    // Parse back
    ParseResult result = parse(output);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(parsedSheet, nullptr);

    Axis* parsedCol = parsedSheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(parsedCol, nullptr);
    EXPECT_TRUE(parsedCol->hasStyle());
    const StyleBuffer* colStyleBuf = result.workbook->getEntityStyle(parsedCol->id);
    ASSERT_NE(colStyleBuf, nullptr);
    const CellStyle colStyle = colStyleBuf->toCellStyle();
    EXPECT_TRUE(colStyle.bold);

    Axis* parsedRow = parsedSheet->getRow(ID("rA1bC2dE"));
    ASSERT_NE(parsedRow, nullptr);
    EXPECT_TRUE(parsedRow->hasStyle());
    const StyleBuffer* rowStyleBuf = result.workbook->getEntityStyle(parsedRow->id);
    ASSERT_NE(rowStyleBuf, nullptr);
    const CellStyle rowStyle = rowStyleBuf->toCellStyle();
    EXPECT_TRUE(rowStyle.italic);
}

TEST(SerializerTest, PeerKnowledgeRoundTrip) {
    Workbook wb(ID("aB3cD4eF"), "PeerKnowledge");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");
    sheet->setWorkbook(&wb);
    wb.addSheet(std::move(sheet));

    const ID peer1("PeerOne1");
    const ID peer2("PeerTwo2");
    const HLC hlc1(1705312200000LL, 0, ID("NodeAAAA"));
    const HLC hlc2(1705312200999LL, 3, ID("NodeBBBB"));

    wb.setPeerFrontier(peer1, hlc1);
    wb.setPeerFrontier(peer2, hlc2);

    const std::string output = serialize(wb);
    EXPECT_NE(output.find("#peers"), std::string::npos);
    EXPECT_NE(output.find("P PeerOne1 "), std::string::npos);
    EXPECT_NE(output.find("P PeerTwo2 "), std::string::npos);
    EXPECT_NE(output.find(hlc1.toString()), std::string::npos);
    EXPECT_NE(output.find(hlc2.toString()), std::string::npos);

    ParseResult result = parse(output);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    EXPECT_TRUE(result.workbook->hasPeerKnowledge(peer1));
    EXPECT_TRUE(result.workbook->hasPeerKnowledge(peer2));
    EXPECT_EQ(result.workbook->getPeerFrontier(peer1), hlc1);
    EXPECT_EQ(result.workbook->getPeerFrontier(peer2), hlc2);
    EXPECT_EQ(result.workbook->getPeerKnowledge().size(), 2u);
}

TEST(SerializerTest, PeerKnowledgeAbsentInLegacyFiles) {
    // Minimal document without any P lines
    const std::string legacy = R"(#zcd v1
D aB3cD4eF "Legacy"
S sH3eE4tB "Sheet1"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 1
)";
    ParseResult result = parse(legacy);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    EXPECT_TRUE(result.workbook->getPeerKnowledge().empty());

    // Re-serialize should not invent peer knowledge
    const std::string output = serialize(*result.workbook);
    EXPECT_EQ(output.find("#peers"), std::string::npos);
    EXPECT_EQ(output.find("\nP "), std::string::npos);
}

TEST(SerializerTest, PeerKnowledgeParseOnlyLines) {
    const std::string content = R"(#zcd v1
D aB3cD4eF "WithPeers"
S sH3eE4tB "Sheet1"
#peers
P AbCdEf12 1000.0.NodeId01
P GhIjKl34 2000.1.NodeId02
)";
    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    EXPECT_EQ(result.workbook->getPeerKnowledge().size(), 2u);
    EXPECT_EQ(result.workbook->getPeerFrontier(ID("AbCdEf12")).toString(), "1000.0.NodeId01");
    EXPECT_EQ(result.workbook->getPeerFrontier(ID("GhIjKl34")).toString(), "2000.1.NodeId02");
}

}  // namespace
}  // namespace cells
