#include "core/cells/serializer.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
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

TEST(SerializerTest, SerializeShowGridLinesDefault) {
    // When showGridLines is true (default), V line should not be emitted
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
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
    // Store format in workbook map
    wb->setCellFormatId(cell->id, ID("FMT_C002"));
    cell->markHasFormat();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Should contain the format property
    EXPECT_NE(output.find("fmt:FMT_C002"), std::string::npos);
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
    // Store format in workbook map
    wb->setCellFormatId(cell->id, ID("FMT_P002"));
    cell->markHasFormat();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Serialize
    const std::string serialized = serialize(*wb);
    EXPECT_NE(serialized.find("fmt:FMT_P002"), std::string::npos);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify format is preserved (read from workbook map)
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_EQ(result.workbook->getCellFormatId(parsedCell->id), ID("FMT_P002"));
}

TEST(CellFormatTest, ParseCellWithFormat) {
    const std::string content = R"(
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 1234.56 fmt:FMT_C002
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_DOUBLE_EQ(cell->value.asNumber(), 1234.56);
    // Format is now read from workbook map
    EXPECT_EQ(result.workbook->getCellFormatId(cell->id), ID("FMT_C002"));
}

TEST(CellFormatTest, ParseCellWithStringValueAndFormat) {
    const std::string content = R"(
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE s "Hello" fmt:FMT_TEXT
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_EQ(cell->value.asString(), "Hello");
    // Format is now read from workbook map
    EXPECT_EQ(result.workbook->getCellFormatId(cell->id), ID("FMT_TEXT"));
}

TEST(CellFormatTest, ParseCellWithFormulaAndFormat) {
    const std::string content = R"(
D aB3cD4eF "Test"
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE f "=A1+A2" fmt:FMT_C002
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);

    EXPECT_TRUE(cell->isFormula());
    // Format is now read from workbook map
    EXPECT_EQ(result.workbook->getCellFormatId(cell->id), ID("FMT_C002"));
}

// --- Custom Format Tests ---

TEST(CustomFormatTest, SerializeCustomFormats) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");

    // Register some custom formats
    wb->registerCustomFormat(ID("cF1aB2cD"), "#,##0.00");
    wb->registerCustomFormat(ID("cF2eF3gH"), "0.00%");
    wb->registerCustomFormat(ID("cF3iJ4kL"), "$#,##0.00");

    const std::string serialized = serialize(*wb);

    // Check that format lines are in the output
    EXPECT_NE(serialized.find("F cF1aB2cD \"#,##0.00\""), std::string::npos);
    EXPECT_NE(serialized.find("F cF2eF3gH \"0.00%\""), std::string::npos);
    EXPECT_NE(serialized.find("F cF3iJ4kL \"$#,##0.00\""), std::string::npos);
}

TEST(CustomFormatTest, ParseCustomFormats) {
    const std::string content = R"(
D aB3cD4eF "Test"
F cF1aB2cD "#,##0.00"
F cF2eF3gH "0.00%"
S sH3eE4tB "Sheet"
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify custom formats were parsed
    EXPECT_TRUE(result.workbook->hasCustomFormat(ID("cF1aB2cD")));
    EXPECT_TRUE(result.workbook->hasCustomFormat(ID("cF2eF3gH")));
    EXPECT_EQ(result.workbook->getCustomFormatCode(ID("cF1aB2cD")), "#,##0.00");
    EXPECT_EQ(result.workbook->getCustomFormatCode(ID("cF2eF3gH")), "0.00%");
}

TEST(CustomFormatTest, RoundtripCustomFormats) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    wb->addSheet(std::move(sheet));

    // Register custom formats
    wb->registerCustomFormat(ID("cF1aB2cD"), "#,##0.00");
    wb->registerCustomFormat(ID("cF2eF3gH"), "0.00%");

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify custom formats are preserved
    EXPECT_TRUE(result.workbook->hasCustomFormat(ID("cF1aB2cD")));
    EXPECT_TRUE(result.workbook->hasCustomFormat(ID("cF2eF3gH")));
    EXPECT_EQ(result.workbook->getCustomFormatCode(ID("cF1aB2cD")), "#,##0.00");
    EXPECT_EQ(result.workbook->getCustomFormatCode(ID("cF2eF3gH")), "0.00%");

    // Verify formats can be retrieved
    const auto& formats = result.workbook->getCustomFormats();
    EXPECT_EQ(formats.size(), 2);
}

TEST(CustomFormatTest, ParseFormatWithSpecialChars) {
    // Format codes can contain special characters that need escaping
    const std::string content = R"(
D aB3cD4eF "Test"
F cF1aB2cD "[Red]#,##0.00;[Blue]-#,##0.00"
S sH3eE4tB "Sheet"
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    EXPECT_TRUE(result.workbook->hasCustomFormat(ID("cF1aB2cD")));
    EXPECT_EQ(result.workbook->getCustomFormatCode(ID("cF1aB2cD")),
              "[Red]#,##0.00;[Blue]-#,##0.00");
}

// =============================================================================
// Style Serialization Tests
// =============================================================================

TEST(StyleSerializationTest, SerializeStyleDefinition) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    wb->addSheet(std::move(sheet));

    // Register a style
    CellStyle style;
    style.bold = true;
    style.italic = true;
    style.bgColor = "#FF0000";
    wb->registerStyle(ID("STYbold1"), style);

    const std::string output = serialize(*wb);

    // Check style line is present
    EXPECT_NE(output.find("Y STYbold1"), std::string::npos);
    EXPECT_NE(output.find("\"bold\":true"), std::string::npos);
    EXPECT_NE(output.find("\"italic\":true"), std::string::npos);
    EXPECT_NE(output.find("\"bgColor\":\"#FF0000\""), std::string::npos);
}

TEST(StyleSerializationTest, SerializeCellWithStyle) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYbold1"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    const std::string output = serialize(*wb);

    // Check cell line has style property
    EXPECT_NE(output.find("sty:STYbold1"), std::string::npos);
}

TEST(StyleSerializationTest, ParseStyleDefinition) {
    const std::string content = R"(
D aB3cD4eF "Test"
Y STYbold1 {"bold":true,"italic":false,"bgColor":"#FF0000","hAlign":"center"}
S sH3eE4tB "Sheet"
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    EXPECT_TRUE(result.workbook->hasStyle(ID("STYbold1")));
    const CellStyle* style = result.workbook->getStyle(ID("STYbold1"));
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->bold);
    EXPECT_FALSE(style->italic);
    EXPECT_EQ(style->bgColor, "#FF0000");
    EXPECT_EQ(style->hAlign, TextAlign::CENTER);
}

TEST(StyleSerializationTest, ParseCellWithStyle) {
    const std::string content = R"(
D aB3cD4eF "Test"
Y STYbold1 {"bold":true}
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 42 sty:STYbold1
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);
    // Style is now read from workbook map
    EXPECT_EQ(result.workbook->getCellStyleId(cell->id).toString(), "STYbold1");
}

TEST(StyleSerializationTest, RoundtripStyles) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYbold1"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Register style
    CellStyle style;
    style.bold = true;
    style.italic = true;
    style.underline = true;
    style.wrapText = true;
    style.bgColor = "#FFFF00";
    style.textColor = "#000000";
    style.fontFamily = "Arial";
    style.fontSize = 14;
    style.hAlign = TextAlign::CENTER;
    style.vAlign = VerticalAlign::MIDDLE;
    wb->registerStyle(ID("STYbold1"), style);

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify style is preserved
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYbold1")));
    const CellStyle* parsed = result.workbook->getStyle(ID("STYbold1"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_TRUE(parsed->bold);
    EXPECT_TRUE(parsed->italic);
    EXPECT_TRUE(parsed->underline);
    EXPECT_TRUE(parsed->wrapText);
    EXPECT_EQ(parsed->bgColor, "#FFFF00");
    EXPECT_EQ(parsed->textColor, "#000000");
    EXPECT_EQ(parsed->fontFamily, "Arial");
    EXPECT_EQ(parsed->fontSize, 14);
    EXPECT_EQ(parsed->hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsed->vAlign, VerticalAlign::MIDDLE);

    // Verify cell style ID is preserved (read from workbook map)
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_EQ(result.workbook->getCellStyleId(parsedCell->id).toString(), "STYbold1");
}

TEST(StyleSerializationTest, ParseCellWithBothFormatAndStyle) {
    const std::string content = R"(
D aB3cD4eF "Test"
F FMT_C002 "$#,##0.00"
Y STYbold1 {"bold":true}
S sH3eE4tB "Sheet"
C cA1bC2dE 0
R rA1bC2dE 0
X xA1bC2dE cA1bC2dE rA1bC2dE n 42 fmt:FMT_C002 sty:STYbold1
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(cell, nullptr);
    // Format and style are now read from workbook map
    EXPECT_EQ(result.workbook->getCellFormatId(cell->id).toString(), "FMT_C002");
    EXPECT_EQ(result.workbook->getCellStyleId(cell->id).toString(), "STYbold1");
}

// =============================================================================
// ZCD Style Round-trip Tests (Phase 2c)
// =============================================================================

TEST(StyleZCDRoundtripTest, EmptyStyleRoundtrip) {
    // Test that an empty style (all defaults) serializes and deserializes correctly
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYempty"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Register empty style (all defaults)
    CellStyle emptyStyle;
    EXPECT_TRUE(emptyStyle.isEmpty());
    wb->registerStyle(ID("STYempty"), emptyStyle);

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify empty style is preserved
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYempty")));
    const CellStyle* parsed = result.workbook->getStyle(ID("STYempty"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_TRUE(parsed->isEmpty());
    EXPECT_FALSE(parsed->bold);
    EXPECT_FALSE(parsed->italic);
    EXPECT_FALSE(parsed->underline);
    EXPECT_FALSE(parsed->wrapText);
    EXPECT_TRUE(parsed->bgColor.empty());
    EXPECT_TRUE(parsed->textColor.empty());
    EXPECT_TRUE(parsed->fontFamily.empty());
    EXPECT_EQ(parsed->fontSize, 0);
    // Default hAlign is GENERAL (content-type-based alignment)
    EXPECT_EQ(parsed->hAlign, TextAlign::GENERAL);
    EXPECT_EQ(parsed->vAlign, VerticalAlign::BOTTOM);
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
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYbold0"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.bold = true;
    wb->registerStyle(ID("STYbold0"), style);

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const CellStyle* parsed = result.workbook->getStyle(ID("STYbold0"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_TRUE(parsed->bold);
    EXPECT_FALSE(parsed->italic);
    EXPECT_FALSE(parsed->underline);
    EXPECT_FALSE(parsed->wrapText);
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
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYcolor"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.bgColor = "#FFFF00";    // Yellow background
    style.textColor = "#0000FF";  // Blue text
    wb->registerStyle(ID("STYcolor"), style);

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const CellStyle* parsed = result.workbook->getStyle(ID("STYcolor"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->bgColor, "#FFFF00");
    EXPECT_EQ(parsed->textColor, "#0000FF");
    EXPECT_FALSE(parsed->bold);
    EXPECT_FALSE(parsed->italic);
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
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYalign"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.hAlign = TextAlign::CENTER;
    style.vAlign = VerticalAlign::MIDDLE;
    wb->registerStyle(ID("STYalign"), style);

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const CellStyle* parsed = result.workbook->getStyle(ID("STYalign"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsed->vAlign, VerticalAlign::MIDDLE);
    EXPECT_FALSE(parsed->bold);
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
    // Store style in workbook map
    wb->setCellStyleId(cell->id, ID("STYfont0"));
    cell->markHasStyle();

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.fontFamily = "Times New Roman";
    style.fontSize = 18;
    wb->registerStyle(ID("STYfont0"), style);

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const CellStyle* parsed = result.workbook->getStyle(ID("STYfont0"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->fontFamily, "Times New Roman");
    EXPECT_EQ(parsed->fontSize, 18);
    EXPECT_FALSE(parsed->bold);
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
    // Store style in workbook map
    wb->setCellStyleId(cellA1->id, ID("STYhead0"));
    cellA1->markHasStyle();

    // Cell B1: Bold header
    auto cellB1 = std::make_unique<Cell>(ID("xB3dE4fG"), ID("cB3dE4fG"), ID("rA1bC2dE"));
    cellB1->value = CellValue("Value");
    // Store style in workbook map
    wb->setCellStyleId(cellB1->id, ID("STYhead0"));
    cellB1->markHasStyle();

    // Cell A2: Normal text
    auto cellA2 = std::make_unique<Cell>(ID("xC5fG6hJ"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    cellA2->value = CellValue("Item 1");
    // No style

    // Cell B2: Currency with color
    auto cellB2 = std::make_unique<Cell>(ID("xD7hJ8kL"), ID("cB3dE4fG"), ID("rB3dE4fG"));
    cellB2->value = CellValue(1234.56);
    // Store style in workbook map
    wb->setCellStyleId(cellB2->id, ID("STYmoney"));
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

    // Register styles
    CellStyle headerStyle;
    headerStyle.bold = true;
    headerStyle.bgColor = "#4472C4";
    headerStyle.textColor = "#FFFFFF";
    headerStyle.hAlign = TextAlign::CENTER;
    wb->registerStyle(ID("STYhead0"), headerStyle);

    CellStyle moneyStyle;
    moneyStyle.textColor = "#008000";  // Green for positive values
    moneyStyle.hAlign = TextAlign::RIGHT;
    wb->registerStyle(ID("STYmoney"), moneyStyle);

    // Serialize
    const std::string serialized = serialize(*wb);

    // Parse back
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify styles
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYhead0")));
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYmoney")));

    const CellStyle* header = result.workbook->getStyle(ID("STYhead0"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->bold);
    EXPECT_EQ(header->bgColor, "#4472C4");
    EXPECT_EQ(header->textColor, "#FFFFFF");
    EXPECT_EQ(header->hAlign, TextAlign::CENTER);

    const CellStyle* money = result.workbook->getStyle(ID("STYmoney"));
    ASSERT_NE(money, nullptr);
    EXPECT_EQ(money->textColor, "#008000");
    EXPECT_EQ(money->hAlign, TextAlign::RIGHT);

    // Verify cells (read style from workbook map)
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(result.workbook->getCellStyleId(parsedSheet->getCell(ID("xA1bC2dE"))->id).toString(),
              "STYhead0");
    EXPECT_EQ(result.workbook->getCellStyleId(parsedSheet->getCell(ID("xB3dE4fG"))->id).toString(),
              "STYhead0");
    EXPECT_TRUE(result.workbook->getCellStyleId(parsedSheet->getCell(ID("xC5fG6hJ"))->id).isNull());
    EXPECT_EQ(result.workbook->getCellStyleId(parsedSheet->getCell(ID("xD7hJ8kL"))->id).toString(),
              "STYmoney");
}

TEST(StyleZCDRoundtripTest, AllAlignmentValues) {
    // Test all alignment values round-trip correctly
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    wb->addSheet(std::move(sheet));

    // Test all horizontal alignment values
    CellStyle leftStyle;
    leftStyle.hAlign = TextAlign::LEFT;
    wb->registerStyle(ID("STYleft0"), leftStyle);

    CellStyle centerStyle;
    centerStyle.hAlign = TextAlign::CENTER;
    wb->registerStyle(ID("STYcentr"), centerStyle);

    CellStyle rightStyle;
    rightStyle.hAlign = TextAlign::RIGHT;
    wb->registerStyle(ID("STYright"), rightStyle);

    CellStyle justifyStyle;
    justifyStyle.hAlign = TextAlign::JUSTIFY;
    wb->registerStyle(ID("STYjstfy"), justifyStyle);

    // Test all vertical alignment values
    CellStyle topStyle;
    topStyle.vAlign = VerticalAlign::TOP;
    wb->registerStyle(ID("STYtop00"), topStyle);

    CellStyle middleStyle;
    middleStyle.vAlign = VerticalAlign::MIDDLE;
    wb->registerStyle(ID("STYmidl0"), middleStyle);

    CellStyle bottomStyle;
    bottomStyle.vAlign = VerticalAlign::BOTTOM;
    wb->registerStyle(ID("STYbotm0"), bottomStyle);

    // Serialize and parse
    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify horizontal alignments
    EXPECT_EQ(result.workbook->getStyle(ID("STYleft0"))->hAlign, TextAlign::LEFT);
    EXPECT_EQ(result.workbook->getStyle(ID("STYcentr"))->hAlign, TextAlign::CENTER);
    EXPECT_EQ(result.workbook->getStyle(ID("STYright"))->hAlign, TextAlign::RIGHT);
    EXPECT_EQ(result.workbook->getStyle(ID("STYjstfy"))->hAlign, TextAlign::JUSTIFY);

    // Verify vertical alignments
    EXPECT_EQ(result.workbook->getStyle(ID("STYtop00"))->vAlign, VerticalAlign::TOP);
    EXPECT_EQ(result.workbook->getStyle(ID("STYmidl0"))->vAlign, VerticalAlign::MIDDLE);
    EXPECT_EQ(result.workbook->getStyle(ID("STYbotm0"))->vAlign, VerticalAlign::BOTTOM);
}

TEST(StyleZCDRoundtripTest, StyleWithSpecialCharactersInFont) {
    // Test font family with special characters
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");
    wb->addSheet(std::move(sheet));

    CellStyle style;
    style.fontFamily = "Courier New";  // Space in name
    style.fontSize = 12;
    wb->registerStyle(ID("STYcourn"), style);

    const std::string serialized = serialize(*wb);
    ParseResult result = parse(serialized);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    const CellStyle* parsed = result.workbook->getStyle(ID("STYcourn"));
    ASSERT_NE(parsed, nullptr);
    EXPECT_EQ(parsed->fontFamily, "Courier New");
    EXPECT_EQ(parsed->fontSize, 12);
}

TEST(StyleZCDRoundtripTest, RoundtripStylesFromTestdataFile) {
    // Load styles.zcd from testdata directory and verify round-trip
    const std::string content = readTestFile("styles.zcd");
    ASSERT_FALSE(content.empty()) << "Failed to load testdata/styles.zcd";

    // Parse original
    ParseResult result1 = parse(content);
    ASSERT_TRUE(result1.ok()) << (result1.error ? result1.error->toString() : "");

    // Verify styles loaded
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYhead0")));
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYmoney")));
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYwarn0")));
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYitalc")));
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYunder")));
    EXPECT_TRUE(result1.workbook->hasStyle(ID("STYfull0")));

    // Verify full style has all properties
    const CellStyle* fullStyle = result1.workbook->getStyle(ID("STYfull0"));
    ASSERT_NE(fullStyle, nullptr);
    EXPECT_TRUE(fullStyle->bold);
    EXPECT_TRUE(fullStyle->italic);
    EXPECT_TRUE(fullStyle->underline);
    EXPECT_EQ(fullStyle->bgColor, "#FF0000");
    EXPECT_EQ(fullStyle->textColor, "#FFFFFF");
    EXPECT_EQ(fullStyle->fontFamily, "Arial");
    EXPECT_EQ(fullStyle->fontSize, 14);
    EXPECT_EQ(fullStyle->hAlign, TextAlign::CENTER);
    EXPECT_EQ(fullStyle->vAlign, VerticalAlign::MIDDLE);

    // Serialize
    const std::string serialized = serialize(*result1.workbook);
    EXPECT_FALSE(serialized.empty());

    // Parse again
    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    // Verify all styles match after round-trip
    EXPECT_EQ(*result1.workbook->getStyle(ID("STYhead0")),
              *result2.workbook->getStyle(ID("STYhead0")));
    EXPECT_EQ(*result1.workbook->getStyle(ID("STYmoney")),
              *result2.workbook->getStyle(ID("STYmoney")));
    EXPECT_EQ(*result1.workbook->getStyle(ID("STYwarn0")),
              *result2.workbook->getStyle(ID("STYwarn0")));
    EXPECT_EQ(*result1.workbook->getStyle(ID("STYfull0")),
              *result2.workbook->getStyle(ID("STYfull0")));

    // Verify cell count matches
    compareWorkbooks(*result1.workbook, *result2.workbook);
}

TEST(StyleZCDRoundtripTest, ParseStyledZCDFile) {
    // Parse a ZCD file with styles - simulating reading from disk
    const std::string content = R"(#cells v1
D aB3cD4eF "Styled Document"
Y STYhead0 {"bold":true,"bgColor":"#4472C4","textColor":"#FFFFFF","hAlign":"center"}
Y STYmoney {"textColor":"#008000","hAlign":"right"}
Y STYwarn0 {"bgColor":"#FFC000","bold":true}
F FMT_C002 "$#,##0.00"
S sH3eE4tB "Data"
C cA1bC2dE 0
C cB3dE4fG 1
R rA1bC2dE 0
R rB3dE4fG 1
R rC5fG6hJ 2
X xA1bC2dE cA1bC2dE rA1bC2dE s "Category" sty:STYhead0
X xB3dE4fG cB3dE4fG rA1bC2dE s "Amount" sty:STYhead0
X xC5fG6hJ cA1bC2dE rB3dE4fG s "Sales"
X xD7hJ8kL cB3dE4fG rB3dE4fG n 10000 fmt:FMT_C002 sty:STYmoney
X xE9kL0mN cA1bC2dE rC5fG6hJ s "Warning" sty:STYwarn0
X xF1mN2pQ cB3dE4fG rC5fG6hJ n -500 fmt:FMT_C002 sty:STYwarn0
)";

    ParseResult result = parse(content);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Verify all styles loaded
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYhead0")));
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYmoney")));
    EXPECT_TRUE(result.workbook->hasStyle(ID("STYwarn0")));

    // Verify style properties
    const CellStyle* header = result.workbook->getStyle(ID("STYhead0"));
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->bold);
    EXPECT_EQ(header->bgColor, "#4472C4");
    EXPECT_EQ(header->textColor, "#FFFFFF");
    EXPECT_EQ(header->hAlign, TextAlign::CENTER);

    const CellStyle* money = result.workbook->getStyle(ID("STYmoney"));
    ASSERT_NE(money, nullptr);
    EXPECT_EQ(money->textColor, "#008000");
    EXPECT_EQ(money->hAlign, TextAlign::RIGHT);

    const CellStyle* warn = result.workbook->getStyle(ID("STYwarn0"));
    ASSERT_NE(warn, nullptr);
    EXPECT_TRUE(warn->bold);
    EXPECT_EQ(warn->bgColor, "#FFC000");

    // Verify cells have correct styles (read from workbook map)
    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(result.workbook->getCellStyleId(sheet->getCell(ID("xA1bC2dE"))->id).toString(),
              "STYhead0");
    EXPECT_EQ(result.workbook->getCellStyleId(sheet->getCell(ID("xD7hJ8kL"))->id).toString(),
              "STYmoney");
    EXPECT_EQ(result.workbook->getCellStyleId(sheet->getCell(ID("xE9kL0mN"))->id).toString(),
              "STYwarn0");

    // Verify cell with both format and style (read from workbook map)
    Cell* amountCell = sheet->getCell(ID("xD7hJ8kL"));
    EXPECT_EQ(result.workbook->getCellFormatId(amountCell->id).toString(), "FMT_C002");
    EXPECT_EQ(result.workbook->getCellStyleId(amountCell->id).toString(), "STYmoney");

    // Serialize and verify round-trip
    const std::string serialized = serialize(*result.workbook);
    ParseResult result2 = parse(serialized);
    ASSERT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");

    // Compare styles after round-trip
    const CellStyle* header2 = result2.workbook->getStyle(ID("STYhead0"));
    EXPECT_EQ(*header, *header2);

    const CellStyle* money2 = result2.workbook->getStyle(ID("STYmoney"));
    EXPECT_EQ(*money, *money2);

    const CellStyle* warn2 = result2.workbook->getStyle(ID("STYwarn0"));
    EXPECT_EQ(*warn, *warn2);
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

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->hidden = true;
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

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->hidden = true;
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

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->hidden = false;  // Not hidden (default)
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
    EXPECT_TRUE(col->hidden);
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
    EXPECT_TRUE(row->hidden);
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
    EXPECT_FALSE(col->hidden);  // Default is visible
}

TEST(SerializerTest, HiddenAxisRoundTrip) {
    // Create workbook with hidden column and row
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->hidden = true;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->hidden = true;
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
    EXPECT_TRUE(parsedCol->hidden);

    Axis* parsedRow = parsedSheet->getRow(ID("rA1bC2dE"));
    ASSERT_NE(parsedRow, nullptr);
    EXPECT_TRUE(parsedRow->hidden);
}

TEST(SerializerTest, AxisDefaultStyleRoundTrip) {
    // Create workbook with styled column and row
    Workbook wb(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet1");

    // Register styles in workbook
    CellStyle boldStyle;
    boldStyle.bold = true;
    wb.registerStyle(ID("sT1yL2eA"), boldStyle);

    CellStyle italicStyle;
    italicStyle.italic = true;
    wb.registerStyle(ID("sT1yL2eB"), italicStyle);

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    col->defaultStyleId = ID("sT1yL2eA");
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row->defaultStyleId = ID("sT1yL2eB");
    sheet->addRow(std::move(row));

    wb.addSheet(std::move(sheet));

    // Serialize
    Serializer serializer;
    const std::string output = serializer.serialize(wb);

    // Verify output contains style property
    EXPECT_NE(output.find("sty:sT1yL2eA"), std::string::npos);
    EXPECT_NE(output.find("sty:sT1yL2eB"), std::string::npos);

    // Parse back
    ParseResult result = parse(output);
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(parsedSheet, nullptr);

    Axis* parsedCol = parsedSheet->getColumn(ID("cA1bC2dE"));
    ASSERT_NE(parsedCol, nullptr);
    EXPECT_EQ(parsedCol->defaultStyleId.toString(), "sT1yL2eA");

    Axis* parsedRow = parsedSheet->getRow(ID("rA1bC2dE"));
    ASSERT_NE(parsedRow, nullptr);
    EXPECT_EQ(parsedRow->defaultStyleId.toString(), "sT1yL2eB");
}

}  // namespace
}  // namespace cells
