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

}  // namespace
}  // namespace cells
