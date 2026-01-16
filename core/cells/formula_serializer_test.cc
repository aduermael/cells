#include "core/cells/formula_serializer.h"

#include <gtest/gtest.h>

#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

class FormulaSerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(ID("testWBId"), "TestWorkbook");
        auto sheetPtr = std::make_unique<Sheet>(ID("testShId"), "Sheet1");
        sheet = sheetPtr.get();
        workbook->addSheet(std::move(sheetPtr));

        // Create a 5x5 grid of cells for testing
        for (uint32_t col = 0; col < 5; ++col) {
            sheet->getOrCreateColumnByPosition(col);
        }
        for (uint32_t row = 0; row < 5; ++row) {
            sheet->getOrCreateRowByPosition(row);
        }
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet{nullptr};
};

TEST_F(FormulaSerializerTest, SerializeLiteral) {
    // Number literal
    FormulaParser parser1("=42");
    auto ast1 = parser1.parse();
    EXPECT_EQ(FormulaSerializer::serialize(ast1.get()), "=42");

    // String literal
    FormulaParser parser2("=\"Hello\"");
    auto ast2 = parser2.parse();
    EXPECT_EQ(FormulaSerializer::serialize(ast2.get()), "=\"Hello\"");

    // Boolean literal
    FormulaParser parser3("=TRUE");
    auto ast3 = parser3.parse();
    EXPECT_EQ(FormulaSerializer::serialize(ast3.get()), "=TRUE");
}

TEST_F(FormulaSerializerTest, SerializeCellRefUnresolved) {
    // Before resolution, should output A1 notation
    FormulaParser parser("=A1");
    auto ast = parser.parse();
    EXPECT_EQ(FormulaSerializer::serialize(ast.get()), "=A1");
}

TEST_F(FormulaSerializerTest, SerializeCellRefResolved) {
    // After resolution, should output UUID format
    FormulaParser parser("=A1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should start with = and contain ~~ (relative ref prefix)
    EXPECT_EQ(serialized[0], '=');
    EXPECT_TRUE(serialized.find("~~") != std::string::npos)
        << "Expected UUID format with ~~ prefix, got: " << serialized;
    // Should NOT contain "A1" anymore
    EXPECT_TRUE(serialized.find("A1") == std::string::npos)
        << "Should not contain A1 notation, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeAbsoluteCellRef) {
    FormulaParser parser("=$A$1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain $$ (both absolute prefix)
    EXPECT_TRUE(serialized.find("$$") != std::string::npos)
        << "Expected $$ prefix for absolute ref, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeMixedAbsoluteCellRef) {
    // $A1 - column absolute, row relative
    FormulaParser parser1("=$A1");
    auto ast1 = parser1.parse();
    FormulaResolver resolver1(*workbook, *sheet);
    resolver1.resolve(ast1.get());
    std::string serialized1 = FormulaSerializer::serialize(ast1.get());
    EXPECT_TRUE(serialized1.find("$~") != std::string::npos)
        << "Expected $~ prefix for col-absolute ref, got: " << serialized1;

    // A$1 - column relative, row absolute
    FormulaParser parser2("=A$1");
    auto ast2 = parser2.parse();
    FormulaResolver resolver2(*workbook, *sheet);
    resolver2.resolve(ast2.get());
    std::string serialized2 = FormulaSerializer::serialize(ast2.get());
    EXPECT_TRUE(serialized2.find("~$") != std::string::npos)
        << "Expected ~$ prefix for row-absolute ref, got: " << serialized2;
}

TEST_F(FormulaSerializerTest, SerializeRangeRef) {
    FormulaParser parser("=A1:B2");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain two ~~ prefixes (for both corners) separated by :
    EXPECT_EQ(serialized[0], '=');
    EXPECT_TRUE(serialized.find(":") != std::string::npos);
    // Count ~~ occurrences
    size_t count = 0;
    size_t pos = 0;
    while ((pos = serialized.find("~~", pos)) != std::string::npos) {
        ++count;
        pos += 2;
    }
    EXPECT_EQ(count, 2u) << "Expected two ~~ prefixes for range corners, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeBinaryOp) {
    FormulaParser parser("=A1+B1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain + operator between two UUID refs
    EXPECT_TRUE(serialized.find("+") != std::string::npos);
    // Count ~~ occurrences (should be 2 for A1 and B1)
    size_t count = 0;
    size_t pos = 0;
    while ((pos = serialized.find("~~", pos)) != std::string::npos) {
        ++count;
        pos += 2;
    }
    EXPECT_EQ(count, 2u) << "Expected two cell refs, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeFunctionCall) {
    FormulaParser parser("=SUM(A1,B1)");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain SUM function name and parentheses
    EXPECT_TRUE(serialized.find("SUM(") != std::string::npos);
    EXPECT_TRUE(serialized.find(",") != std::string::npos);
    EXPECT_TRUE(serialized.find(")") != std::string::npos);
}

TEST_F(FormulaSerializerTest, SerializeComplexFormula) {
    FormulaParser parser("=SUM(A1:B2)+C3*2");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain SUM, +, *, and 2
    EXPECT_TRUE(serialized.find("SUM(") != std::string::npos);
    EXPECT_TRUE(serialized.find("+") != std::string::npos);
    EXPECT_TRUE(serialized.find("*") != std::string::npos);
    EXPECT_TRUE(serialized.find("2") != std::string::npos);
}

TEST_F(FormulaSerializerTest, SerializeUnaryOp) {
    FormulaParser parser("=-A1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should start with =- followed by UUID ref
    EXPECT_EQ(serialized.substr(0, 2), "=-");
}

TEST_F(FormulaSerializerTest, RefPrefixFunction) {
    EXPECT_EQ(FormulaSerializer::refPrefix(true, true), "$$");
    EXPECT_EQ(FormulaSerializer::refPrefix(true, false), "$~");
    EXPECT_EQ(FormulaSerializer::refPrefix(false, true), "~$");
    EXPECT_EQ(FormulaSerializer::refPrefix(false, false), "~~");
}

TEST_F(FormulaSerializerTest, SerializeColumnRef) {
    FormulaParser parser("=A:A");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Column refs use @~ or @$ prefix
    EXPECT_TRUE(serialized.find("@~") != std::string::npos ||
                serialized.find("@$") != std::string::npos)
        << "Expected @~ or @$ prefix for column ref, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeRowRef) {
    FormulaParser parser("=1:1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success);

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Row refs use #~ or #$ prefix
    EXPECT_TRUE(serialized.find("#~") != std::string::npos ||
                serialized.find("#$") != std::string::npos)
        << "Expected #~ or #$ prefix for row ref, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeNamedRef) {
    // Named refs pass through as-is
    FormulaParser parser("=myRange");
    auto ast = parser.parse();

    // Don't resolve - named refs stay as names
    std::string serialized = FormulaSerializer::serialize(ast.get());
    EXPECT_EQ(serialized, "=myRange");
}

TEST_F(FormulaSerializerTest, SerializeStringWithQuotes) {
    FormulaParser parser("=\"Hello \"\"World\"\"\"");
    auto ast = parser.parse();

    std::string serialized = FormulaSerializer::serialize(ast.get());
    // Should preserve escaped quotes
    EXPECT_TRUE(serialized.find("\"\"") != std::string::npos)
        << "Expected escaped quotes, got: " << serialized;
}

TEST_F(FormulaSerializerTest, SerializeNullAst) {
    EXPECT_EQ(FormulaSerializer::serialize(nullptr), "");
}

TEST_F(FormulaSerializerTest, SerializePrecedence) {
    // Test that parentheses are added correctly
    FormulaParser parser("=(A1+B1)*C1");
    auto ast = parser.parse();

    FormulaResolver resolver(*workbook, *sheet);
    resolver.resolve(ast.get());

    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain parentheses
    EXPECT_TRUE(serialized.find("(") != std::string::npos);
    EXPECT_TRUE(serialized.find(")") != std::string::npos);
}

TEST_F(FormulaSerializerTest, SerializeCrossSheetCellRef) {
    // Add a second sheet
    auto sheet2Ptr = std::make_unique<Sheet>(generate_id(), "Sheet2");
    Sheet* sheet2 = sheet2Ptr.get();
    workbook->addSheet(std::move(sheet2Ptr));

    // Create cell B27 on Sheet2
    const Axis* colB = sheet2->getOrCreateColumnByPosition(1);  // B = position 1
    const Axis* row27 = sheet2->getOrCreateRowByPosition(26);   // Row 27 = position 26
    Cell* cellB27 = sheet2->getOrCreateCellAt(colB->id, row27->id);

    // Parse cross-sheet reference from Sheet1's perspective
    FormulaParser parser("=Sheet2!B27");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    // Resolve (should set sheetId and cellId)
    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success) << result.errorMessage;

    // Check the AST has sheetId set
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_EQ(cellRef->sheetId, sheet2->id.toString()) << "sheetId should be Sheet2's ID";
    EXPECT_EQ(cellRef->cellId, cellB27->id.toString()) << "cellId should be B27's ID";

    // Serialize and verify format
    std::string serialized = FormulaSerializer::serialize(ast.get());
    // Should be: =!<8-char-sheet-id>~~<8-char-cell-id>
    EXPECT_TRUE(serialized.find("=!" + sheet2->id.toString()) != std::string::npos)
        << "Should contain sheet ID prefix: " << serialized;
    EXPECT_TRUE(serialized.find(cellB27->id.toString()) != std::string::npos)
        << "Should contain cell ID: " << serialized;

    // Parse the serialized formula back
    FormulaParser parser2(serialized);
    auto ast2 = parser2.parse();
    ASSERT_NE(ast2, nullptr) << "Failed to parse serialized formula: " << serialized;
    ASSERT_FALSE(parser2.hasErrors());

    // The parsed AST should have sheetId set correctly
    auto* cellRef2 = dynamic_cast<CellRefNode*>(ast2.get());
    ASSERT_NE(cellRef2, nullptr);
    EXPECT_EQ(cellRef2->sheetId, sheet2->id.toString()) << "Round-trip should preserve sheetId";
    EXPECT_EQ(cellRef2->cellId, cellB27->id.toString()) << "Round-trip should preserve cellId";
}

TEST_F(FormulaSerializerTest, SerializeCrossSheetRange) {
    // Add a second sheet
    auto sheet2Ptr = std::make_unique<Sheet>(generate_id(), "Sheet2");
    Sheet* sheet2 = sheet2Ptr.get();
    workbook->addSheet(std::move(sheet2Ptr));

    // Create cells A1, A2, A3 on Sheet2
    const Axis* colA = sheet2->getOrCreateColumnByPosition(0);
    const Axis* row1 = sheet2->getOrCreateRowByPosition(0);
    const Axis* row2 = sheet2->getOrCreateRowByPosition(1);
    const Axis* row3 = sheet2->getOrCreateRowByPosition(2);
    Cell* cellA1 = sheet2->getOrCreateCellAt(colA->id, row1->id);
    Cell* cellA3 = sheet2->getOrCreateCellAt(colA->id, row3->id);

    // Parse cross-sheet range from Sheet1's perspective
    FormulaParser parser("=SUM(Sheet2!A1:A3)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    // Resolve
    FormulaResolver resolver(*workbook, *sheet);
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success) << result.errorMessage;

    // Serialize
    std::string serialized = FormulaSerializer::serialize(ast.get());

    // Should contain sheet ID prefix only ONCE for the range (not twice)
    size_t firstSheetPrefix = serialized.find("!" + sheet2->id.toString());
    EXPECT_NE(firstSheetPrefix, std::string::npos)
        << "Should contain sheet ID prefix: " << serialized;

    // Parse the serialized formula back
    FormulaParser parser2(serialized);
    auto ast2 = parser2.parse();
    ASSERT_NE(ast2, nullptr) << "Failed to parse serialized formula: " << serialized;
    ASSERT_FALSE(parser2.hasErrors());

    // Find the range ref inside the SUM function
    auto* func = dynamic_cast<FunctionCallNode*>(ast2.get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->args.size(), 1u);

    auto* rangeRef = dynamic_cast<RangeRefNode*>(func->args[0].get());
    ASSERT_NE(rangeRef, nullptr);

    // Both cells should have sheetId set
    EXPECT_EQ(rangeRef->topLeft->sheetId, sheet2->id.toString())
        << "topLeft should have sheetId after round-trip";
    EXPECT_EQ(rangeRef->bottomRight->sheetId, sheet2->id.toString())
        << "bottomRight should have sheetId after round-trip";
}

}  // namespace
}  // namespace cells
