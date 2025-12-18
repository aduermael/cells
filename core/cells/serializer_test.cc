#include "core/cells/serializer.h"

#include <sstream>
#include <string>

#include "core/cells/id.h"
#include "core/cells/model.h"
#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a minimal workbook for testing
std::unique_ptr<Workbook> createMinimalWorkbook() {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test Workbook");

    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");

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
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->prevId = ID("rA1bC2dE");
    row->nextId = ID("rB3dE4fG");

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
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->setFormula(new Formula("=SUM(A1:B2)"));

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

// --- Gap Notation ---

TEST(SerializerTest, SerializeColumnGaps) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");

    auto col1 = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col1->nextId = ID("cB3dE4fG");
    col1->gapAfter = 3;

    auto col2 = std::make_unique<Axis>(ID("cB3dE4fG"), true);
    col2->prevId = ID("cA1bC2dE");
    col2->gapBefore = 3;

    sheet->addColumn(std::move(col1));
    sheet->addColumn(std::move(col2));
    sheet->addRow(std::make_unique<Axis>(ID("rA1bC2dE"), false));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    // Should have gap notation like "cB3dE4fG:3"
    EXPECT_NE(output.find(":3"), std::string::npos);
}

// --- Axis Properties ---

TEST(SerializerTest, SerializeColumnWidth) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");

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

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->size = 48;

    sheet->addColumn(std::make_unique<Axis>(ID("cA1bC2dE"), true));
    sheet->addRow(std::move(row));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);
    EXPECT_NE(output.find("h:48"), std::string::npos);
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

}  // namespace
}  // namespace cells
