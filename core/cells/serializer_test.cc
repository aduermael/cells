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
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row1 = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    row1->position = 0;
    auto row2 = std::make_unique<Axis>(ID("rB3dE4fG"), false);
    row2->position = 1;

    // Master cell (first alphabetically: xAMaster)
    auto masterCell = std::make_unique<Cell>(ID("xAMaster"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    masterCell->setFormula(createFormula("=SUM(A1:A10)"));
    Cell* masterPtr = masterCell.get();

    // Subscriber cell (second alphabetically: xBSubscr)
    auto subCell = std::make_unique<Cell>(ID("xBSubscr"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    subCell->setSharedFormulaRef(masterPtr);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addCell(std::move(masterCell));
    sheet->addCell(std::move(subCell));
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
    Cell* masterPtr = masterCell.get();

    // Two subscriber cells
    auto sub1 = std::make_unique<Cell>(ID("xBSub001"), ID("cA1bC2dE"), ID("rB3dE4fG"));
    sub1->setSharedFormulaRef(masterPtr);

    auto sub2 = std::make_unique<Cell>(ID("xCSub002"), ID("cA1bC2dE"), ID("rC5fG6hI"));
    sub2->setSharedFormulaRef(masterPtr);

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row1));
    sheet->addRow(std::move(row2));
    sheet->addRow(std::move(row3));
    sheet->addCell(std::move(masterCell));
    sheet->addCell(std::move(sub1));
    sheet->addCell(std::move(sub2));
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

    // Verify subscribers reference master
    EXPECT_TRUE(parsedSub1->isFormula());
    EXPECT_TRUE(parsedSub1->isSharedFormula());
    EXPECT_EQ(parsedSub1->sharedFormulaRef, parsedMaster);

    EXPECT_TRUE(parsedSub2->isFormula());
    EXPECT_TRUE(parsedSub2->isSharedFormula());
    EXPECT_EQ(parsedSub2->sharedFormulaRef, parsedMaster);

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
    EXPECT_EQ(subscriber->sharedFormulaRef, master);
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
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(1234.56);
    cell->formatId = ID("FMT_C002");  // Currency format

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
    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);

    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(0.15);
    cell->formatId = ID("FMT_P002");  // Percentage format

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

    // Verify format is preserved
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_EQ(parsedCell->formatId, ID("FMT_P002"));
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
    EXPECT_EQ(cell->formatId, ID("FMT_C002"));
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
    EXPECT_EQ(cell->formatId, ID("FMT_TEXT"));
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
    EXPECT_EQ(cell->formatId, ID("FMT_C002"));
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

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    cell->styleId = ID("STYbold1");

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
    EXPECT_EQ(cell->styleId.toString(), "STYbold1");
}

TEST(StyleSerializationTest, RoundtripStyles) {
    auto wb = std::make_unique<Workbook>(ID("aB3cD4eF"), "Test");
    auto sheet = std::make_unique<Sheet>(ID("sH3eE4tB"), "Sheet");

    auto col = std::make_unique<Axis>(ID("cA1bC2dE"), true);
    auto row = std::make_unique<Axis>(ID("rA1bC2dE"), false);
    auto cell = std::make_unique<Cell>(ID("xA1bC2dE"), ID("cA1bC2dE"), ID("rA1bC2dE"));
    cell->value = CellValue(42.0);
    cell->styleId = ID("STYbold1");

    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    sheet->addCell(std::move(cell));
    wb->addSheet(std::move(sheet));

    // Register style
    CellStyle style;
    style.bold = true;
    style.italic = true;
    style.underline = true;
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
    EXPECT_EQ(parsed->bgColor, "#FFFF00");
    EXPECT_EQ(parsed->textColor, "#000000");
    EXPECT_EQ(parsed->fontFamily, "Arial");
    EXPECT_EQ(parsed->fontSize, 14);
    EXPECT_EQ(parsed->hAlign, TextAlign::CENTER);
    EXPECT_EQ(parsed->vAlign, VerticalAlign::MIDDLE);

    // Verify cell style ID is preserved
    Sheet* parsedSheet = result.workbook->getSheetByIndex(0);
    Cell* parsedCell = parsedSheet->getCell(ID("xA1bC2dE"));
    ASSERT_NE(parsedCell, nullptr);
    EXPECT_EQ(parsedCell->styleId.toString(), "STYbold1");
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
    EXPECT_EQ(cell->formatId.toString(), "FMT_C002");
    EXPECT_EQ(cell->styleId.toString(), "STYbold1");
}

}  // namespace
}  // namespace cells
